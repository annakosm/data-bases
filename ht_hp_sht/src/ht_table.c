#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bf.h"
#include "ht_table.h"
#include "record.h"

#define NEXT_BUCKET BB_BLOCK_SIZE-sizeof(int)
#define NEXT    BF_BLOCK_SIZE-sizeof(HT_block_info)
#define MAX_REC (BF_BLOCK_SIZE-sizeof(HT_block_info))/sizeof(Record)

#define CALL_OR_DIE(call)     \
  {                           \
    BF_ErrorCode code = call; \
    if (code != BF_OK) {      \
      BF_PrintError(code);    \
      exit(code);             \
    }                         \
  }


int HT_CreateFile(char *fileName,  int buckets) {

    /* Create a file with name filename */ 
    if (BF_CreateFile(fileName) == BF_ERROR) {
        return -1;
    }
 

    /* Open the file with name filename */
    int fileDesc;
    if (BF_OpenFile(fileName, &fileDesc) == BF_ERROR) {
        return -1;
    }
    

    /* Allocate the header block that keeps the HP_info */
    BF_Block *block;
    BF_Block_Init(&block);
    if (BF_AllocateBlock(fileDesc, block) == BF_ERROR){
        return -1;
    }

    if (BF_GetBlock(fileDesc, 0, block) == BF_ERROR){
        return -1;
    }


    /* Read the header block and take the address */
    void *data = BF_Block_GetData(block);

    /* Store ht_info at the beginning of first block */ 
    HT_info *ht_info = data;
    ht_info->fileDesc = fileDesc;
    ht_info->first_block = block;
    ht_info->numBuckets = buckets;
   

    void* data_table = data + sizeof(HT_info);
    HT_table * table = data_table;
    for(int i=0; i<buckets; i++) {
        table = data_table + i*sizeof(HT_table);
        /* Pointing that at first place our bucket is empty */
        table->first = -1;
        table->last = -1;
    }

    /* Store ht_block_info at the end of first block */
    data = data + NEXT;
    HT_block_info *ht_block_info = data;
    ht_block_info->recordsCounter = 0;
    ht_block_info->prev_block=NULL;
    ht_block_info->next_block=NULL;
    ht_block_info->next=-1;

    /* Because we changed the (initially empty) data of the first block */
    BF_Block_SetDirty(block); 
    if (BF_UnpinBlock(block)== BF_ERROR) {
        return -1;
    }
      
    BF_Block_Destroy(&block);
    BF_CloseFile(fileDesc);

    return 0;
}


HT_info* HT_OpenFile(char *fileName){

    /* Get the file identifier with BF_OpenFile */
    int fileDesc;
    if (BF_OpenFile(fileName, &fileDesc)== BF_ERROR) {
        return NULL;
    }
   

    /*Read the header_block*/
    BF_Block *block;
    BF_Block_Init(&block);
    if (BF_AllocateBlock(fileDesc, block) == BF_ERROR){
        return NULL;
    }
    

    if (BF_GetBlock(fileDesc,0, block) == BF_ERROR){
        return NULL;
    }


    char *data = BF_Block_GetData(block);
    
    HT_info *info = (HT_info *)malloc(sizeof(HT_info));
    HT_info *header_info = (HT_info *)data;
    
    memcpy(info, header_info, sizeof(HT_info));


    return info ;  
}


int HT_CloseFile( HT_info* HT_info ){
   
    BF_Block_Destroy(&HT_info->first_block);
    if (BF_CloseFile(HT_info->fileDesc)== BF_ERROR){
        BF_PrintError(BF_CloseFile(HT_info->fileDesc));
        return -1;
    }
    free(HT_info);
    return 0;
}

int hash(long int nbuckets, int key){
    return key%nbuckets;
}

int HT_InsertEntry(HT_info* ht_info, Record record){
    
    /* The returning value initialize as -1 in case the record does not entry */
    int block_counter=-1;

    /* Make the hash value */
    int index = hash(ht_info->numBuckets, record.id);

    /* Get the first block that contains the hash table */
    BF_Block *first_block;
    BF_Block_Init(&first_block);
    if (BF_GetBlock(ht_info->fileDesc, 0, first_block) == BF_ERROR){
        return -1;
    }


    void *data = BF_Block_GetData(first_block);
    void* data_table = data + sizeof(HT_info) + index*sizeof(HT_table);

    /* Go to the memory space that hash table is saved */
    HT_table *table_index = data_table;

    /* This variable is in order to check if we have insert the element in the last block or not */
    int full_or_first=0;

    /* Check if this index has  any block */
    if (table_index->last != -1) {
        /* So if it does we go in the last one and check if there is any space to insert our record */
        BF_Block *last_block;
        BF_Block_Init(&last_block);
        if (BF_GetBlock(ht_info->fileDesc, table_index->last , last_block) == BF_ERROR){
            return -1;
        }

        void *data = BF_Block_GetData(last_block);
        HT_block_info *last_block_info = data+ NEXT;
    
    
        /* Check if there is enough space */
        if(last_block_info->recordsCounter!=MAX_REC){

            /* Insert the record inside last block, enough space */
            memcpy(data+sizeof(Record)*(last_block_info->recordsCounter), &record, sizeof(Record));
            Record *rec = data+sizeof(Record)*(last_block_info->recordsCounter) ;
            full_or_first=1;
          
            last_block_info->recordsCounter++;
            block_counter= table_index->last;
            
            BF_Block_SetDirty(last_block);
            if (BF_UnpinBlock(last_block)== BF_ERROR){
                return -1;
            }
        }
    }
    
    if(full_or_first==0){
        /* Create a new block and insert this first record */
        BF_Block *new_block;
        BF_Block_Init(&new_block);
        if (BF_AllocateBlock(ht_info->fileDesc, new_block) == BF_ERROR){
            return -1;
        }

        /* Change the pointer of the last block */
        if (table_index->last == -1) {
            /* In oder to avoid having the same block in different indexes */
            /* Index+1 while block0 is the header block */
            table_index->last = index + 1;
            table_index->first = index + 1;
        }
        else {
            /* It depends on the num of buckets to which will be the next block */
            table_index->last += ht_info->numBuckets ;
        }


        if (BF_GetBlock(ht_info->fileDesc, table_index->last, new_block) == BF_ERROR){
            return -1;
        }

        /* Create block info for my new block */
        data = BF_Block_GetData(new_block);
        HT_block_info *new_block_info = data+ NEXT;
        new_block_info->recordsCounter=0;

        /* Insert the record in the block */
        memcpy(data+sizeof(Record)*(new_block_info->recordsCounter), &record, sizeof(Record));
        new_block_info->recordsCounter++;
        Record *rec = data+sizeof(Record)*(new_block_info->recordsCounter);
        

        /* Because we changed the (initially empty) data of the first block */
        BF_Block_SetDirty(new_block); 
        if (BF_UnpinBlock(new_block)== BF_ERROR){
            return -1;
        }
    }
 
    block_counter= table_index->last;
    return block_counter;
}

int HT_GetAllEntries(HT_info* ht_info, void *value ){
  
    /* Get from the hash function the right pos our index in order to find the right id */
    int new_value= *(int*)value;
    int index = hash(ht_info->numBuckets, new_value);
 
    int count = 0;
    int blockID = 0;
    /* In case the id does not exist */
    int block_counter = -1;
    
    /* Get the block ID of first block of records */
    BF_Block *current_block ;
    BF_Block_Init(&current_block);
    if (BF_GetBlock(ht_info->fileDesc, 0, current_block) == BF_ERROR){
        return -1;
    }

    /* Go to the part of block that is stored our hash table */
    void *data = BF_Block_GetData(current_block);
    void* data_table = data + sizeof(HT_info) + index*sizeof(HT_table);
    HT_table *table_index = data_table;
    HT_block_info *current_block_info;
    blockID = table_index->first;
   
    /* Go through the blocks of our hash index position */
    while(blockID <=table_index->last) {
        /* Count the number of blocks */
        count++;

        if (BF_GetBlock(ht_info->fileDesc, blockID, current_block) == BF_ERROR){
            return -1;
        }
        
        data = BF_Block_GetData(current_block);
        current_block_info = data + NEXT;

        /* Go through the records of each block */
        for(int j=0; j<current_block_info->recordsCounter; j++){
            Record *current_rec = data + j*sizeof(Record);
            if(current_rec->id == new_value){
                printRecord(*current_rec);
                block_counter=count;
                break;
            }
        }

        /* If we found the id stop while */
        if( block_counter==blockID){
            break;
        }

        /* Go to the next block if this index */
        blockID += ht_info->numBuckets ;
    }
    
    BF_Block_SetDirty(current_block); 
    if (BF_UnpinBlock(current_block)== BF_ERROR){
        return -1;
    }

    return block_counter;
}

int HashStatistics(char* fileName){

	/* Open the file with name filename */
    int fileDesc;
    if (BF_OpenFile(fileName, &fileDesc) == BF_ERROR) {
        return -1;
    }


	/* Allocate the header block that keeps the HP_info */
    BF_Block *first_block;
    BF_Block_Init(&first_block);
    if (BF_AllocateBlock(fileDesc, first_block) == BF_ERROR){
        return -1;
    }


	if (BF_GetBlock(fileDesc, 0, first_block) == BF_ERROR){
        return -1;
    }


	/* Read the header block and take the address */
    void *data = BF_Block_GetData(first_block);


    /* Store ht_info at the beginning of first block */ 
    HT_info *ht_info = data;
    

    void* index = data + sizeof(HT_info);
    HT_table * table = index;
    int  blockSum = 1;

    BF_Block *current_block ;
    BF_Block_Init(&current_block);
    HT_block_info *current_block_info;
    int* rec_count = malloc(ht_info->numBuckets*sizeof(int));

    int max = 0;
    int min = 10000;
    double avg;
    int rec_sum = 0;

    int block_overflow=0;
    int* overflow = malloc(ht_info->numBuckets*sizeof(int));
    for(int i=0; i< ht_info->numBuckets; i++) {
        
        table = index + i*sizeof(HT_table);
        int first = table->first;
        int last = table->last;
        
        overflow[i] = 0;

        if(first != -1){
            
            if(first == last){
                blockSum ++;

                rec_count[i] = 0;
            }
            else{
        
                int bucket_blocks = (last-first)/ht_info->numBuckets+1;
                blockSum += bucket_blocks;

                rec_count[i] = (bucket_blocks-1)*MAX_REC ;

                block_overflow ++;
                overflow[i] = bucket_blocks-1;
            }

            if (BF_GetBlock(ht_info->fileDesc, last, current_block) == BF_ERROR){
                return -1;
            }

            data = BF_Block_GetData(current_block);
            current_block_info = data + NEXT ;
            rec_count[i] += current_block_info->recordsCounter;
        }
        else{
            rec_count[i] = 0;
        }

        if(rec_count[i] < min){
            min = rec_count[i];
        }  
        
        if(rec_count[i] > max){
            max = rec_count[i];
        }        
        
        rec_sum += rec_count[i]; 
    }
    printf("-----------------------------------------------------------------\n");
    printf("                        Hash Table Statistics                      \n");
    printf("                \n");
    printf("MAX REC %ld",MAX_REC);
    printf("the number of blocks in this file is : %d\n",blockSum);
    printf("the average number of blocks in every bucket : %d\n",blockSum/ht_info->numBuckets);
    printf("max record sum = %d\n", max);
    printf("min record sum = %d\n", min);
    printf("avg record sum = %d\n", rec_sum/ht_info->numBuckets);
    printf("%d blocks overflow\n",block_overflow);

    for(int i=0; i< ht_info->numBuckets; i++){
        printf("bucket[%d] has %d overflow blocks\n", i, overflow[i]);
    }

    printf("-----------------------------------------------------------------\n");
    

    /* Because we changed the (initially empty) data of the first block */
    BF_Block_SetDirty(first_block); 
	if (BF_UnpinBlock(first_block)== BF_ERROR) {
		return -1;
	}
	BF_Block_Destroy(&first_block);
    BF_CloseFile(fileDesc);
		
	return 0;
}