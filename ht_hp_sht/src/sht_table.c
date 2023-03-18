#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bf.h"
#include "sht_table.h"
#include "ht_table.h"
#include "record.h"

#define CALL_OR_DIE(call)     \
  {                           \
    BF_ErrorCode code = call; \
    if (code != BF_OK) {      \
      BF_PrintError(code);    \
      exit(code);             \
    }                         \
  }


#define BASE (256)
#define NEXT    BF_BLOCK_SIZE-sizeof(SHT_block_info)
#define NEXT_HT    BF_BLOCK_SIZE-sizeof(HT_block_info)
#define MAX_SREC (BF_BLOCK_SIZE-sizeof(SHT_block_info))/sizeof(SHT_record_info)
#define MAX_REC (BF_BLOCK_SIZE-sizeof(HT_block_info))/sizeof(Record)


int SHT_CreateSecondaryIndex(char *sfileName,  int buckets, char* fileName){
	
	/* Create a file with name filename */ 
    if (BF_CreateFile(sfileName) == BF_ERROR) {
        return -1;
    }


	/* Open the file with name filename */
    int sfileDesc;
    if (BF_OpenFile(sfileName, &sfileDesc) == BF_ERROR) {
        return -1;
    }


	/* Allocate the header block that keeps the HP_info */
    BF_Block *first_block;
    BF_Block_Init(&first_block);
    if (BF_AllocateBlock(sfileDesc, first_block) == BF_ERROR){
        return -1;
    }


	if (BF_GetBlock(sfileDesc, 0, first_block) == BF_ERROR){
        return -1;
    }
	
    
    /* Read the header block and take the address */
    void *data = BF_Block_GetData(first_block);

    /* Store ht_info at the beginning of first block */ 
    SHT_info *sht_info = data;
    sht_info->first_block = first_block;
    sht_info->numBuckets = buckets;
    sht_info->fileName = malloc(sizeof(char) * strlen(fileName));
	sht_info->fileDesc = sfileDesc;
	strcpy(sht_info->fileName, fileName);
    

    void* index = data + sizeof(SHT_info);
    SHT_table * table = index;
    for(int i=0; i<buckets; i++) {
        table = index + i*sizeof(SHT_table);
        table->first = -1;
        table->last = -1;
    }
    

    /* Because we changed the (initially empty) data of the first block */
    BF_Block_SetDirty(first_block); 
	if (BF_UnpinBlock(first_block)== BF_ERROR) {
		return -1;
	}
	BF_Block_Destroy(&first_block);
    BF_CloseFile(sfileDesc);


	return 0;
}

SHT_info* SHT_OpenSecondaryIndex(char *indexName){

    /* Get the file identifier with BF_OpenFile */
    int fileDesc;
    if (BF_OpenFile(indexName, &fileDesc)== BF_ERROR){
       
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


    /*Allocate HP_info struct*/
    SHT_info *info = (SHT_info *)malloc(sizeof(SHT_info));
    SHT_info *header_info = (SHT_info *)data;

    memcpy(info, header_info, sizeof(SHT_info));


    return info ;
}


int SHT_CloseSecondaryIndex( SHT_info* SHT_info ){

    BF_Block_Destroy(&SHT_info->first_block);
    if (BF_CloseFile(SHT_info->fileDesc)== BF_ERROR){
        BF_PrintError(BF_CloseFile(SHT_info->fileDesc));
        return -1;
    }
    free(SHT_info);

    return 0;
}


int shash(long int nbuckets, char *key) {

	unsigned long h;
    unsigned const char *k;

	/* Cast s to unsigned const char * */
    /* This ensures that elements of s will be treated as having values >= 0 */
    k = (unsigned const char *)key;
	
    /* Treat strings as base-256 integers with digits in the range 1 to 255 */
    h = 0;
    while (*k != '\0'){
        h = (h * BASE + *k) % nbuckets;
        k++;
    }

    return h % nbuckets;
}


int SHT_SecondaryInsertEntry(SHT_info* sht_info, Record record, int block_id) {
    
    /* The returning value initialize as -1 in case the record does not entry */
    int block_counter = -1;
    int index = shash(sht_info->numBuckets, record.name);

    /* Make the sctruct that consists of the name and the block */
    SHT_record_info srecord;
    srecord.block = block_id;
    strcpy(srecord.name, record.name);

    BF_Block *first_block;
    BF_Block_Init(&first_block);
    if (BF_GetBlock(sht_info->fileDesc, 0, first_block) == BF_ERROR){
        return -1;
    }

    void *data = BF_Block_GetData(first_block);
    void* index_table = data + sizeof(SHT_info) + index*sizeof(SHT_table);

    /* Go to the memory space that hash table is saved */
    SHT_table *table_index = index_table;

    /* This variable is in order to check if we have insert the element in the last block or not */
    int full_or_first=0;

    /* Check if this index has  any block */
    if (table_index->last != -1) {
        /* So if it does we go in the last one and check if there is any space to insert our record */
        BF_Block *last_block;
        BF_Block_Init(&last_block);
        if (BF_GetBlock(sht_info->fileDesc, table_index->last , last_block) == BF_ERROR){
            return -1;
        }

        void *data = BF_Block_GetData(last_block);
        SHT_block_info *last_block_info = data+ NEXT;

        /* Check if there is enough space */
        if(last_block_info->recordsCounter != MAX_SREC){

            /* Insert the record inside last block ,enough space */
            memcpy(data+sizeof(SHT_record_info)*(last_block_info->recordsCounter), &srecord, sizeof(SHT_record_info));
          
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
        if (BF_AllocateBlock(sht_info->fileDesc, new_block) == BF_ERROR){
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
            table_index->last += sht_info->numBuckets ;
        }


        if (BF_GetBlock(sht_info->fileDesc, table_index->last, new_block) == BF_ERROR){
            return -1;
        }

        /* Create block info for my new block */
        data = BF_Block_GetData(new_block);
        SHT_block_info *new_block_info = data+ NEXT;
        new_block_info->recordsCounter=0;

        /* Insert the record in the block */
        memcpy(data+sizeof(SHT_record_info)*(new_block_info->recordsCounter), &srecord, sizeof(SHT_record_info));
        new_block_info->recordsCounter++;

        BF_Block_SetDirty(new_block); 
        if (BF_UnpinBlock(new_block)== BF_ERROR){
            return -1;
        }
    }

    return 0;
}

int SHT_SecondaryGetAllEntries(HT_info* ht_info, SHT_info* sht_info, char* name){
   
    /* Get from the hash function the right pos our index in order to find the right id */ 
    int index = shash(sht_info->numBuckets, name);

    int count = 0;
    int blockID = 0;

    /* In case the id does not exist */
    int block_counter = -1;
    
    /* Get the block ID of first block of records */
    BF_Block *current_block ;
    BF_Block_Init(&current_block);
    if (BF_GetBlock(sht_info->fileDesc, 0, current_block) == BF_ERROR){
        return -1;
    }

    /* Go to the part of block that is stored our hash table */
    void *data = BF_Block_GetData(current_block);
    void* new_data = data + sizeof(SHT_info) + index*sizeof(SHT_table);
    SHT_table *table_index = new_data;
   
    SHT_block_info *current_block_info;
    blockID = table_index->first;

    /* Go through the blocks of our hash index position */
    while(blockID <=table_index->last) {
        /* Count the number of blocks */
        count++;

        if (BF_GetBlock(sht_info->fileDesc, blockID, current_block) == BF_ERROR){
            return -1;
        }

        
        data = BF_Block_GetData(current_block);
        current_block_info = data + NEXT;

        
        /* Go through the records of each block */
        for(int j=0; j<current_block_info->recordsCounter; j++){
            SHT_record_info *current_srec = data + j*sizeof(SHT_record_info);
            
            /* If we find the right name then we go in HT info and search through the block */
            if(strcmp(current_srec->name , name) == 0){
                
                /* Get the block ID of this name srecord */
                BF_Block *record_block ;
                BF_Block_Init(&record_block);
                if (BF_GetBlock(ht_info->fileDesc, current_srec->block, record_block) == BF_ERROR){
                    return -1;
                }

                /* Get out ht records block info */
                void *data = BF_Block_GetData(record_block);
                void *record_data = data + NEXT_HT;
                HT_block_info *record_block_info = record_data;

                /* Search for the name inside the block */
                for(int z=0; z<record_block_info->recordsCounter; z++){
                    Record *rec = data+sizeof(Record)*z;
                    if(strcmp(rec->name , name) == 0){
                        printRecord(*rec);
                    }
                }
                block_counter=count;
            }
        }

        /* Go to the next block if this index */
        blockID += sht_info->numBuckets ;
    }

    BF_Block_SetDirty(current_block); 
    if (BF_UnpinBlock(current_block)== BF_ERROR){
        return -1;
    }
    
    return count;
}

int SHashStatistics(char* sfileName){
    
	/* Open the file with name filename */
    int sfileDesc;
    if (BF_OpenFile(sfileName, &sfileDesc) == BF_ERROR) {
        return -1;
    }
   

	/* Allocate the header block that keeps the HP_info */
    BF_Block *first_block;
    BF_Block_Init(&first_block);
    if (BF_AllocateBlock(sfileDesc, first_block) == BF_ERROR){
        return -1;
    }


	if (BF_GetBlock(sfileDesc, 0, first_block) == BF_ERROR){
        return -1;
    }

	/* Read the header block and take the address */
    void *data = BF_Block_GetData(first_block);


    /* Store ht_info at the beginning of first block */ 
    SHT_info *sht_info = data;
    
    void* index = data + sizeof(SHT_info);
    SHT_table * table = index;
    int  blockSum = 1;

    BF_Block *current_block ;
    BF_Block_Init(&current_block);
    SHT_block_info *current_block_info;
    int* rec_count = malloc(sht_info->numBuckets*sizeof(int));

    int max = 0;
    int min = 10000;
    double avg;
    int rec_sum = 0;

    int block_overflow=0;
    int* overflow = malloc(sht_info->numBuckets*sizeof(int));

    for(int i=0; i< sht_info->numBuckets; i++) {
        
        table = index + i*sizeof(SHT_table);
        int first = table->first;
        int last = table->last;
        
        overflow[i] = 0;

        if(first != -1){
            
            if(first == last){
                blockSum ++;
                rec_count[i] = 0;
            }
            else{
                int bucket_blocks = (last-first)/sht_info->numBuckets+1;
                blockSum += bucket_blocks;

                rec_count[i] = (bucket_blocks-1)*MAX_SREC ;

                block_overflow ++;
                overflow[i] = bucket_blocks-1;
            }

            if (BF_GetBlock(sht_info->fileDesc, last, current_block) == BF_ERROR){
                return -1;
            }

            data = BF_Block_GetData(current_block);
            current_block_info = data + NEXT ;
            rec_count[i] += current_block_info->recordsCounter;
        }
        else {
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
    printf("                     Secondary Hash Table Statistics                      \n");
    printf("                \n");

    printf("MAX REC %ld",MAX_SREC);
    printf("the number of blocks in this file is : %d\n",blockSum);
    printf("the average number of blocks in every bucket : %d\n",blockSum/sht_info->numBuckets);
    printf("max record sum = %d\n", max);
    printf("min record sum = %d\n", min);
    printf("avg record sum = %d\n", rec_sum/sht_info->numBuckets);
    printf("%d blocks overflow\n",block_overflow);

    for(int i=0; i< sht_info->numBuckets; i++){
        printf("bucket[%d] has %d overflow blocks\n", i, overflow[i]);
    }
    printf("-----------------------------------------------------------------\n");

    
    /* Because we changed the (initially empty) data of the first block */
    BF_Block_SetDirty(first_block); 
	if (BF_UnpinBlock(first_block)== BF_ERROR) {
		return -1;
	}
	BF_Block_Destroy(&first_block);
    BF_CloseFile(sfileDesc);
		
	return 0;
}
