//-----------------------------------------
//REMARKS: This program reads FAT32 file system through three command info,list and get
//-----------------------------------------
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <libgen.h>
#include <ctype.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <assert.h>
#include <inttypes.h>
#ifndef FAT32_H
#define FAT32_H
#include <inttypes.h>

//variables
char size_buffer[1024];
int fs_descriptor; //file desceptor
char *get_file_path;//path name
char *file_name;//file name for download

off_t location_fat;//fat adddress
off_t size_of_clstr;
uint32_t size_of_entry;
off_t search_cluster;//finding proper cluster
off_t search_add;//proper address

/* boot sector constants */
#define BS_OEMName_LENGTH 8
#define BS_VolLab_LENGTH 11
#define BS_FilSysType_LENGTH 8
#define _FILE_OFFSET_BITS 64
#pragma pack(push)
#pragma pack(1)
typedef struct fat32BS_struct {
    char BS_jmpBoot[3];
    char BS_OEMName[BS_OEMName_LENGTH];
    uint16_t BPB_BytesPerSec;
    uint8_t BPB_SecPerClus;
    uint16_t BPB_RsvdSecCnt;
    uint8_t BPB_NumFATs;
    uint16_t BPB_RootEntCnt;
    uint16_t BPB_TotSec16;
    uint8_t BPB_Media;
    uint16_t BPB_FATSz16;
    uint16_t BPB_SecPerTrk;
    uint16_t BPB_NumHeads;
    uint32_t BPB_HiddSec;
    uint32_t BPB_TotSec32;
    uint32_t BPB_FATSz32;
    uint16_t BPB_ExtFlags;
    uint8_t BPB_FSVerLow;
    uint8_t BPB_FSVerHigh;
    uint32_t BPB_RootClus;
    uint16_t BPB_FSInfo;
    uint16_t BPB_BkBootSec;
    char BPB_reserved[12];
    uint8_t BS_DrvNum;
    uint8_t BS_Reserved1;
    uint8_t BS_BootSig;
    uint32_t BS_VolID;
    char BS_VolLab[BS_VolLab_LENGTH];
    char BS_FilSysType[BS_FilSysType_LENGTH];
    char BS_CodeReserved[420];
    uint8_t BS_SigA;
    uint8_t BS_SigB;
}fatBS;
#pragma pack(pop)

#pragma pack(push)
#pragma pack(1)
typedef struct FSInfo{
    uint32_t lead_sig;
    uint8_t reserved1[480];
    uint32_t signature;
    uint32_t free_count;
    uint32_t next_free;
    uint8_t reserved2[12];
    uint32_t trail_signature;
}fatFS;

typedef struct DirInfo {
    char dir_name[11];
    uint8_t dir_attr;
    uint8_t dir_ntres;
    uint8_t dir_crt_time_tenth;
    uint16_t dir_crt_time;
    uint16_t dir_crt_date;
    uint16_t dir_last_access_time;
    uint16_t dir_first_cluster_hi;
    uint16_t dir_wrt_time;
    uint16_t dir_wrt_date;
    uint16_t dir_first_cluster_lo;
    uint32_t dir_file_size;
}fatDir;

#pragma pack(pop)

fatBS* fatSysBoot;//fat BOOT sector
fatDir* fat_dir; //fat directory
#define UP 1
#define EOC 0x0FFFFFFF  // page 18
#define BAD_CLUSTER 0x0FFFFFF7
#define NULL_CHR 0x00
#define DEL_DIR 0xE5
#define ENTRYSIZE 11
#define ENDCLSTR 0x0FFFFFF8
#define NULL_BYT 0x00

#define ATTR_READ_ONLY 0x01
#define ATTR_HIDDEN 0x02
#define ATTR_SYSTEM 0x04
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE 0x20
#define ATTR_LONG_NAME ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID

#endif

//determining the name for directories and files
void get_name(int size,char nameEnt[BS_VolLab_LENGTH+1],char nameFile[BS_VolLab_LENGTH+2],bool track){
    int pos=0;
    char nmFL[BS_VolLab_LENGTH+2];
    if(track){
        size=BS_OEMName_LENGTH;
    }
    else{
        size=ENTRYSIZE;
    }
    
    int i = 0;
    while (i<size) {//for name of the file or directory
        if (nameEnt[i]!=' ') {
            nameFile[pos]=nameEnt[i];
            pos=pos+1;
        }
        i++;
    }
    
    if (track==true) {//for name extension of file or the directory
        nameFile[pos]='.';
        pos=pos+1;
        for (int j = BS_OEMName_LENGTH; j<ENTRYSIZE; j++) {
            nameFile[pos]=nameEnt[j]!=' ' ? nameEnt[j] : '\0';
            pos += (nameEnt[j] != ' ');
        }
    }
    //nameFile[pos]='\0';
    
    if(nameFile!=NULL){//getting full name
        for(int j=0;j<BS_VolLab_LENGTH+2;j++){
            nmFL[i]=nameFile[i];
        }
    }
    nmFL[pos]='\0';
    nameFile[pos]='\0';
    
}//get_name

void print_drive_list(off_t rootClus_add,int step){
    int counter=0;
    off_t next_clstr;//next cluster
    off_t curr_cltsr;//current running cluster
    char dir_entry_name[BS_VolLab_LENGTH+1];
    char dir_file_name[BS_VolLab_LENGTH+2];
    uint32_t first_dir_char;//first character of directory

    //calculating memory location
    off_t memory_position=(((rootClus_add-2)*fatSysBoot->BPB_SecPerClus)+(fatSysBoot->BPB_RsvdSecCnt+(fatSysBoot->BPB_NumFATs*fatSysBoot->BPB_FATSz32)))*fatSysBoot->BPB_BytesPerSec;
    //finding cluster number
    off_t cluster_number=size_of_clstr/size_of_entry;
   
    while(counter<cluster_number){
        lseek(fs_descriptor,(memory_position+size_of_entry*counter),SEEK_SET);//going correct position in disk
        read(fs_descriptor,size_buffer,sizeof(fatDir));//reading disk
        memcpy(fat_dir,size_buffer,sizeof(fatDir));//copying drive into memory
        
        first_dir_char=(int)fat_dir->dir_name[0]&EOC;
        if(first_dir_char!=DEL_DIR&&first_dir_char!=NULL_CHR){
            strncpy(dir_entry_name,fat_dir->dir_name,ENTRYSIZE);//copying entry names
            dir_entry_name[ENTRYSIZE]='\0';
            
            if((ATTR_LONG_NAME)==fat_dir->dir_attr|dir_entry_name[0]=='.'|ATTR_HIDDEN==(fat_dir->dir_attr&ATTR_HIDDEN)|ATTR_SYSTEM==(fat_dir->dir_attr&ATTR_SYSTEM)|(fat_dir->dir_attr&ATTR_VOLUME_ID)==ATTR_VOLUME_ID){
                //do nothhing.skip all those entries(e.g. hidden,volume,system etc)
            }
            else if(ATTR_DIRECTORY==(fat_dir->dir_attr&ATTR_DIRECTORY)){//getting directory name
                for(int i=0;i<step;i++){
                    printf("-");
                }
                curr_cltsr=(fat_dir->dir_first_cluster_hi<<16)+fat_dir->dir_first_cluster_lo;//calculating cluster address
                get_name(0,dir_entry_name,dir_file_name,false);
                printf("directory %s\n",dir_file_name);
                print_drive_list(curr_cltsr,step+UP);
            }
            else{//getting file name
                for(int i=0;i<step;i++){
                    printf("-");
                }
                get_name(0,dir_entry_name,dir_file_name,true);
                printf("file %s\n",dir_file_name);
            }
        }
        counter=counter+UP;
    }//while
    
    if(counter==cluster_number){//when look up complete for all cluster, sets counter 0
        counter=0;
    }
    
    //determining anyother cluster exist in this dir
    lseek(fs_descriptor,rootClus_add
          *sizeof(uint32_t)+location_fat,SEEK_SET);
    read(fs_descriptor,&next_clstr,sizeof(uint32_t));//reading into next cluster
    
    if(next_clstr<ENDCLSTR){//grab clusters before the end of cluster
        print_drive_list(next_clstr,step+UP);
    }
}//print_drive_list

//getting a file
void get_file(char *file,off_t *search_clstr,off_t clstr){
    int counter=0;
    off_t next_clstr;
    off_t curr_cltsr;// current running cluster
    char dir_entry_name[BS_VolLab_LENGTH+1];
    char dir_file_name[BS_VolLab_LENGTH+2];
    uint32_t first_dir_char;//first directory character
   
    off_t cluster_number=size_of_clstr/size_of_entry;//cluster number
    off_t memory_position=(((clstr-2)*fatSysBoot->BPB_SecPerClus)+(fatSysBoot->BPB_RsvdSecCnt+(fatSysBoot->BPB_NumFATs*fatSysBoot->BPB_FATSz32)))*fatSysBoot->BPB_BytesPerSec;//calculating memory location
    while(counter<cluster_number){
        lseek(fs_descriptor,(memory_position+size_of_entry*counter),SEEK_SET);//going correct position in disk
        read(fs_descriptor,size_buffer,sizeof(fatDir));//reading disk
        memcpy(fat_dir,size_buffer,sizeof(fatDir));//copying directory in memory
        
        first_dir_char=(int)fat_dir->dir_name[0]&EOC;
        if(first_dir_char!=DEL_DIR&&first_dir_char!=NULL_CHR){
            strncpy(dir_entry_name,fat_dir->dir_name,ENTRYSIZE);//copying entry names
            dir_entry_name[ENTRYSIZE]='\0';
            
            if(fat_dir->dir_attr==(ATTR_LONG_NAME)|dir_entry_name[0]=='.'|(ATTR_VOLUME_ID==(fat_dir->dir_attr&ATTR_VOLUME_ID))){//ignore all entries such as volume, long names etc
                //nothing
            }
            else if(ATTR_DIRECTORY==(fat_dir->dir_attr&ATTR_DIRECTORY)){//search entry for directory
                curr_cltsr=((fat_dir->dir_first_cluster_hi)<<16)+fat_dir->dir_first_cluster_lo;//calculating running ccluster
                get_file(file,search_clstr,curr_cltsr);
                
                if(*search_clstr!=NULL_BYT){//if cluster is not null
                    return;
                }
            }
            else{//search entry for file
                get_name(0,dir_entry_name,dir_file_name,true);
                if(!strcmp(dir_file_name,file)){
                    *(search_clstr)=(fat_dir->dir_first_cluster_hi<<16)+fat_dir->dir_first_cluster_lo;
                    return;
                }
            }
        }
        counter=counter+UP;
    }//while
    
    if(counter==cluster_number){//when look up complete for all cluster, sets counter 0
        counter=0;
    }
    //determining anyother cluster exist in this dir
    lseek(fs_descriptor,clstr
          *sizeof(uint32_t)+location_fat,SEEK_SET);
    read(fs_descriptor,&next_clstr,sizeof(uint32_t));//reading into next cluster
    
    if(next_clstr>=ENDCLSTR){
        //nothing at the end of cluster
    }
    else{//not at the end of cluster
        get_file(file,search_clstr,next_clstr);
        if(*(search_clstr)!=NULL_BYT){
            return;
        }
    }
}//get file

//getting contents from the file
void get_file_content(char *file){
    int length=size_of_clstr/512;
    char file_buffer[512];
    int file_fd;
    
    if(search_cluster==NULL_BYT){
        //nothing
    }
    else if(search_cluster!=NULL_BYT){
        file_fd = open(file, O_WRONLY|O_CREAT,0777);//open a file to write
        
        while(search_cluster<ENDCLSTR){//search content and erite until reach end of the cluster
            search_add=(((search_cluster-2)*fatSysBoot->BPB_SecPerClus)+(fatSysBoot->BPB_RsvdSecCnt+(fatSysBoot->BPB_NumFATs*fatSysBoot->BPB_FATSz32)))*fatSysBoot->BPB_BytesPerSec;//finding adress of the content
            lseek(fs_descriptor,search_add,SEEK_SET);//seek that address
    
            for(int i=0;i<length;i++){//writing to file
                read(fs_descriptor,&file_buffer,512);
                write(file_fd,file_buffer,512);
            }
            lseek(fs_descriptor,search_cluster*sizeof(uint32_t)+location_fat,SEEK_SET);//another cluster in fat location
            read(fs_descriptor,&search_cluster,sizeof(uint32_t));
        }
    }
    else{
        //nothing
    }
}//get_file_contents

int main(int argc, char *argv[]){
    if(argc<3){
        printf("Need more arguments\n");
        return EXIT_SUCCESS;
    }
    char* fileName=argv[1];//image file name
    char* cmdName=argv[2];//command name
    //fat variables
   fatSysBoot=(fatBS*) malloc(sizeof(fatBS));//fat Boot
   fatFS* fileInfo=(fatFS*) malloc(sizeof(fatFS));//file info
   fat_dir=(fatDir*) malloc(sizeof(fatDir));//file directory
    fs_descriptor=open(fileName,O_RDONLY);//opening file for reading
    read(fs_descriptor,size_buffer,sizeof(fatBS));//reading the fat boot
    memcpy(fatSysBoot,size_buffer,sizeof(fatBS));
    read(fs_descriptor,size_buffer,sizeof(fatFS));//reading the fat info
    memcpy(fileInfo,size_buffer,sizeof(fatFS));
    
    int available_space;//free space
    int usable_space_free;
    float size_cluster;
    search_cluster=0;//cluster for get
    search_add=0;//address for get

    switch (strcmp(cmdName, "info")) {
        case 0:
            available_space=fileInfo->free_count*fatSysBoot->BPB_SecPerClus*fatSysBoot->BPB_FATSz32;//free sapce on drive
            usable_space_free=(fatSysBoot->BPB_TotSec32*fatSysBoot->BPB_BytesPerSec)-
            ((fatSysBoot->BPB_FATSz32*fatSysBoot->BPB_NumFATs+fatSysBoot->BPB_RsvdSecCnt)
             +fatSysBoot->BPB_BytesPerSec);// usable space
             size_cluster=fatSysBoot->BPB_SecPerClus*fatSysBoot->BPB_BytesPerSec; //cluster size
             
            //printing info about the drive
             printf("Drive name: %s\n",fatSysBoot->BS_OEMName);
             printf("Volume Label: %.11s\n", fatSysBoot->BS_VolLab);
             printf("Free space on drive: %dKB\n",available_space/1024);
             printf("usable space on drive: %dKB\n", usable_space_free/1024);
             printf("cluster size in number of sectors: %fKB\n",size_cluster/1024.0);
             
            break;
        default:
            switch (strcmp(cmdName, "list")) {
                case 0:
                    //finding proper address of fat
                    location_fat=fatSysBoot->BPB_RsvdSecCnt*fatSysBoot->BPB_BytesPerSec;
                    size_of_clstr=fatSysBoot->BPB_BytesPerSec*fatSysBoot->BPB_SecPerClus;//cluster size
                    size_of_entry=sizeof(fatDir);
                    print_drive_list(fatSysBoot->BPB_RootClus,1);
                    
                    break;
                default:
                    switch (strcmp(cmdName, "get")) {
                        case 0:
                            if(argc>=4){
                                get_file_path=argv[3];
                                file_name=strrchr(get_file_path,'/');//getting file name from last / occurance
                                
                                if(file_name==NULL){
                                    file_name=get_file_path;
                                }
                                else{//get past last /(e.g. /pandp.txt->pandp.txt)
                                    file_name++;
                                }
                                printf("Getting File: %s...\n",file_name);
                            }
                            location_fat=fatSysBoot->BPB_RsvdSecCnt*fatSysBoot->BPB_BytesPerSec;//finding fat location
                            size_of_clstr=fatSysBoot->BPB_SecPerClus*fatSysBoot->BPB_BytesPerSec;//cluster size
                            size_of_entry=sizeof(fatDir);
                            
                            get_file(file_name,&search_cluster,fatSysBoot->BPB_RootClus);//look up into diretories from root
                            get_file_content(file_name);//getting file content
                            printf("Download Complete\n");
                            break;
                        default:
                            //Code block executed if commandName is none of the specified values
                            break;
                    }
                    break;
            }
            break;
    }
    return 0;
}//main
