#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bf.h"
#include "hp_file.h"
#include "record.h"

#define CALL_BF(call)       \
{                           \
  BF_ErrorCode code = call; \
  if (code != BF_OK) {      \
    BF_PrintError(code);    \
    return HP_ERROR;        \
  }                         \
}
#define NEXT    BF_BLOCK_SIZE-sizeof(HP_block_info)
#define MAX_REC (BF_BLOCK_SIZE-sizeof(HP_block_info))/sizeof(Record)


int HP_CreateFile(char *fileName){

    /*Create file in block - level*/
    if (BF_CreateFile(fileName) == BF_ERROR) {      
      return -1;
    }
    
    /*Open the created file in block - level and get the file identifier*/
    int fileDesc;
    if (BF_OpenFile(fileName, &fileDesc) == BF_ERROR) {       
        return -1;
    }
    
    /*Allocate the header block that keeps the HP_info*/
    BF_Block *block;
    BF_Block_Init(&block);
    if (BF_AllocateBlock(fileDesc, block) == BF_ERROR) {
        return -1;
    }


    void *data = BF_Block_GetData(block);
    
    /* Store hp_info at the beginning of first block */
    HP_info *hp_info = data;
    
    hp_info->fileDesc=fileDesc;
    hp_info->last=0;
    hp_info->first_block=block;

    /* hp block info */
    /* Store hp_block_info at the end of first block */
    data = data + NEXT;
    
    HP_block_info *hp_block_info = data;
    hp_block_info->rec_count = 0;
    hp_block_info->next_block=NULL;


    /* Because we changed the (initially empty) data of the first block */
    BF_Block_SetDirty(block); 
   
    if (BF_UnpinBlock(block)== BF_ERROR){
        return -1;
    }
    
    BF_Block_Destroy(&block);
    BF_CloseFile(fileDesc);

    return 0;
}


HP_info* HP_OpenFile(char *fileName){
    /* Get the file identifier with BF_OpenFile */
    int fileDesc;
    if (BF_OpenFile(fileName, &fileDesc) == BF_ERROR) {
        return NULL;
    }

    /*Read the header_block*/
    BF_Block *block;
    BF_Block_Init(&block);
    if (BF_AllocateBlock(fileDesc, block) == BF_ERROR) {
        return NULL;
    }


    if (BF_GetBlock(fileDesc,0, block) == BF_ERROR){
        return NULL;
    }


    char *data = BF_Block_GetData(block);

    /*Allocate HP_info struct*/
    HP_info *info = (HP_info *)malloc(sizeof(HP_info));
    HP_info *header_info = (HP_info *)data;

    memcpy(info, header_info, sizeof(HP_info));

    return info ;
}


int HP_CloseFile( HP_info* hp_info ){

    if (BF_CloseFile(hp_info->fileDesc) == BF_ERROR) {
        return -1;
    }

    free(hp_info);
    return 0;
}

int HP_InsertEntry(HP_info* hp_info, Record record){
    /* 1st block does not contain any records */
    /* Check if my record can be in last block or else create new one */
    
    BF_Block *last_block;
    BF_Block_Init(&last_block);
    
    if (BF_GetBlock(hp_info->fileDesc,hp_info->last, last_block) == BF_ERROR) {
        return -1;
    }
    

    void *data = BF_Block_GetData(last_block);
    HP_block_info *last_block_info = data+ NEXT;
   

    if (hp_info->last != 0){

        if(last_block_info->rec_count<MAX_REC){
            /* insert the record inside last block, enough space */
            memcpy(data+sizeof(Record)*(last_block_info->rec_count), &record, sizeof(Record));
            last_block_info->rec_count++;
            return hp_info->last;
        }
    }
        
    /* if the last block==first block or last block is full we create a new one */
    /* Create a new block and insert this first record */
    BF_Block *new_block;
    BF_Block_Init(&new_block);
    if (BF_AllocateBlock(hp_info->fileDesc, new_block) == BF_ERROR){
        return -1;
    }
    
    
    /* Add one more block */
    hp_info->last++;
    if (BF_GetBlock(hp_info->fileDesc,hp_info->last, new_block) == BF_ERROR){
        return -1;
    }

    
    /* Create block info for my new block */
    data = BF_Block_GetData(new_block);
    HP_block_info *new_block_info = data+ NEXT;
    new_block_info->rec_count=0;
    new_block_info->next_block=NULL;


    /* Point my new block in the previous one */
    last_block_info->next_block=new_block;
    BF_Block* test=last_block_info->next_block;
    data = BF_Block_GetData(test);
    HP_block_info *tester = data+ NEXT;
    

    /* Insert the record in the block */
    memcpy(data+sizeof(Record)*(new_block_info->rec_count), &record, sizeof(Record));
    new_block_info->rec_count++;
   

    /* New block */
    /* Because we changed the (initially empty) data of the first block */
    BF_Block_SetDirty(new_block); 
    if (BF_UnpinBlock(new_block)== BF_ERROR){
        return -1;
    }
    
    
    /* Because we changed the (initially empty) data of the first block */
    if(hp_info->last!=0){
        BF_Block_SetDirty(last_block);
        if (BF_UnpinBlock(last_block)== BF_ERROR){
            return -1;
        }
    }

    return hp_info->last;
}

int HP_GetAllEntries(HP_info* hp_info, int value){
    int count = 0;
    int blockID = 0;
    int block_counter = 0;
        

    /* Get the block ID of first block of records */
    BF_Block *current_block = NULL;
    
    BF_Block_Init(&current_block);
    if (BF_GetBlock(hp_info->fileDesc,0, current_block) == BF_ERROR){  
        return -1;
    }

    
    /* Create block info for my new block */ 
    void *data = BF_Block_GetData(current_block);
    data = data ;
    HP_block_info *current_block_info = data+NEXT;

    blockID=0;
    
    int  blocks_num;
    BF_GetBlockCounter(hp_info->fileDesc ,  &blocks_num); 
    
    /* Find blockID*/
    while(blockID < hp_info->last){
        blockID++;
        current_block=current_block_info->next_block;

        if (BF_GetBlock(hp_info->fileDesc,blockID, current_block) == BF_ERROR){
            return -1;
        }
        
        data = BF_Block_GetData(current_block);
       
        current_block_info = data + NEXT;

        /* Go through the records of each block */
        for(int j=0; j<=current_block_info->rec_count; j++){
            Record *current_rec = data + j*sizeof(Record);
            if(current_rec->id==value){
                printRecord(*current_rec);
                /* Because we changed the (initially empty) data of the first block */
                // BF_Block_SetDirty(current_block); 

                return block_counter;
            }
        }
        
        block_counter++;
               
        BF_UnpinBlock(current_block);
                        
                 
    }

    return -1;
}
