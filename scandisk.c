#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>

#include "bootsect.h"
#include "bpb.h"
#include "direntry.h"
#include "fat.h"
#include "dos.h"

/* Jack Sneeringer and Mike Lese */

void get_name(char *fullname, struct direntry *dirent) 
{
    char name[9];
    char extension[4];
    int i;

    name[8] = ' ';
    extension[3] = ' ';
    memcpy(name, &(dirent->deName[0]), 8);
    memcpy(extension, dirent->deExtension, 3);

    /* names are space padded - remove the padding */
    for (i = 8; i > 0; i--) {
    	if (name[i] == ' ') 
        	name[i] = '\0';
    	else 
        	break;
    }

    /* extensions aren't normally space padded - but remove the
       padding anyway if it's there */
    for (i = 3; i > 0; i--) 
    {
    if (extension[i] == ' ') 
        extension[i] = '\0';
    else 
        break;
    }
    fullname[0]='\0';
    strcat(fullname, name);

    /* append the extension if it's not a directory */
    if ((dirent->deAttributes & ATTR_DIRECTORY) == 0) 
    {
    strcat(fullname, ".");
    strcat(fullname, extension);
    }
}

void trace(struct direntry *dirent, uint8_t *image_buf, struct bpb33* bpb, uint8_t *BFA){
	uint16_t cluster = getushort(dirent->deStartCluster);
    uint16_t oldCluster = 0;
	int size = 0; //There is no cluster 0?
	int filesize = (int)getulong(dirent->deFileSize);
	
	while (!is_end_of_file(cluster)){
		
		if(size != -1){
        	size += 512; 
		}
		else if(size > filesize){
			set_fat_entry(oldCluster, CLUST_FREE & FAT12_MASK, image_buf, bpb);
		}
		
		if(size - 512 > filesize && size != -1){
			set_fat_entry(oldCluster, CLUST_EOFS & FAT12_MASK, image_buf, bpb);
			size = -1;
		}

        oldCluster = cluster;
        cluster = get_fat_entry(cluster, image_buf, bpb);
		
		if(cluster == (CLUST_BAD & FAT12_MASK)){
			putushort(dirent->deFileSize, (uint16_t)size);
			size = -2;
			BFA[oldCluster]++;
			break;
		}
		
        if(BFA[oldCluster]> 0) {
            printf("Multiple impressions on one cluster: %u\n", oldCluster);
			break;
        } 
		else {
			BFA[oldCluster] = 1;
        }
	}

	if(size == -1){
		set_fat_entry(oldCluster, CLUST_FREE & FAT12_MASK, image_buf, bpb);
		set_fat_entry(cluster, CLUST_FREE & FAT12_MASK, image_buf, bpb);
		char name[14];
		get_name(name,dirent);
        printf("%s is too large.\n", name);
	}  
	else if(size == -2){
		set_fat_entry(oldCluster, CLUST_EOFS & FAT12_MASK, image_buf, bpb);
		char name[14];
		get_name(name,dirent);
        printf("%s has a bad cluster.\n", name);
	}  
    else if(size - filesize < 0) {
		char name[14];
		get_name(name,dirent);
        printf("%s is too small.\n", name);
		uint16_t sz = (uint16_t)size;
		putushort(dirent->deFileSize, sz);
	}
}

uint16_t getfollowclust(struct direntry *dirent, int indent,uint8_t *image_buf, struct bpb33* bpb, uint8_t *BFA) {     
    
	
	uint16_t followclust = 0;
	uint16_t file_cluster;
    char name[9];
    name[8] = ' ';
    memcpy(name, &(dirent->deName[0]), 8);
	
    if (name[0] == SLOT_EMPTY) {
		return followclust;
    }

    /* skip over deleted entries */
    if (((uint8_t)name[0]) == SLOT_DELETED) {
		return followclust;
    }

    if (((uint8_t)name[0]) == 0x2E) {
        return followclust;
    }

    if ((dirent->deAttributes & ATTR_WIN95LFN) == ATTR_WIN95LFN){}
    else if ((dirent->deAttributes & ATTR_VOLUME) != 0) {}
    else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) {
		if ((dirent->deAttributes & ATTR_HIDDEN) != ATTR_HIDDEN) {
        	file_cluster = getushort(dirent->deStartCluster);
            followclust = file_cluster;
			BFA[getushort(dirent->deStartCluster)] = 1;
        }
    }
    else {
		if(getushort(dirent->deStartCluster) != 0){
			trace(dirent, image_buf, bpb, BFA);                
		}
		BFA[getushort(dirent->deStartCluster)] = 1;
	}
	
	return followclust;
}

void follow_dir(uint16_t cluster, int indent, uint8_t *image_buf, struct bpb33* bpb, uint8_t *BFA) {    

	while (is_valid_cluster(cluster, bpb)) {

        struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);
        int numDirEntries = (bpb->bpbBytesPerSec * bpb->bpbSecPerClust) / sizeof(struct direntry);
		for (int i = 0; i < numDirEntries; i++){
            uint16_t followclust = getfollowclust(dirent, indent, image_buf, bpb, BFA);
            if (followclust)
                follow_dir(followclust, indent+1, image_buf, bpb, BFA);
            dirent++;
		}
		cluster = get_fat_entry(cluster, image_buf, bpb);
    }
}


void usage(char *progname) {
    fprintf(stderr, "usage: %s <imagename>\n", progname);
    exit(1);
}

/* write the values into a directory entry */
void write_dirent(struct direntry *dirent, char *filename, 
          uint16_t start_cluster, uint32_t size)
{
    char *p, *p2;
    char *uppername;
    int len, i;

    /* clean out anything old that used to be here */
    memset(dirent, 0, sizeof(struct direntry));

    /* extract just the filename part */
    uppername = strdup(filename);
    p2 = uppername;
    for (i = 0; i < strlen(filename); i++) {
    	if (p2[i] == '/' || p2[i] == '\\') {
        	uppername = p2+i+1;
    	}
    }

    /* convert filename to upper case */
    for (i = 0; i < strlen(uppername); i++) {
    	uppername[i] = toupper(uppername[i]);
    }

    /* set the file name and extension */
    memset(dirent->deName, ' ', 8);
    p = strchr(uppername, '.');
    memcpy(dirent->deExtension, "___", 3);
    if (p == NULL) {
    	fprintf(stderr, "No filename extension given - defaulting to .___\n");
    }
    else {
    	*p = '\0';
    	p++;
    	len = strlen(p);
    	if (len > 3) len = 3;
    		memcpy(dirent->deExtension, p, len);
    }

    if (strlen(uppername)>8) {
    	uppername[8]='\0';
    }
    memcpy(dirent->deName, uppername, strlen(uppername));
    free(p2);

    /* set the attributes and file size */
    dirent->deAttributes = ATTR_NORMAL;
    putushort(dirent->deStartCluster, start_cluster);
    putulong(dirent->deFileSize, size);

}

void create_dirent(struct direntry *dirent, char *filename, 
           uint16_t start_cluster, uint32_t size,
           uint8_t *image_buf, struct bpb33* bpb)
{
    while (1) 
    {
    if (dirent->deName[0] == SLOT_EMPTY) 
    {
        /* we found an empty slot at the end of the directory */
        write_dirent(dirent, filename, start_cluster, size);
        dirent++;

        /* make sure the next dirent is set to be empty, just in
           case it wasn't before */
        memset((uint8_t*)dirent, 0, sizeof(struct direntry));
        dirent->deName[0] = SLOT_EMPTY;
        return;
    }

    if (dirent->deName[0] == SLOT_DELETED) 
    {
        /* we found a deleted entry - we can just overwrite it */
        write_dirent(dirent, filename, start_cluster, size);
        return;
    }
    dirent++;
    }
}

void orphanage(uint8_t *BFA, uint8_t *image_buf, struct bpb33 *bpb) {
	int endfat = bpb->bpbSectors - 33;
    for(int i=(int)CLUST_FIRST; i < endfat; i++) {
        if(BFA[i] == 0) {
			uint16_t newclust = get_fat_entry(i,image_buf,bpb);
            if(newclust != (CLUST_FREE & FAT12_MASK) && newclust != (CLUST_BAD & FAT12_MASK)) {
				
				BFA[i] = 8; //Signals head of orphan
				uint16_t cluster = (uint16_t)i;
				cluster = get_fat_entry(cluster, image_buf, bpb);
				while (!is_end_of_file(cluster)){

			        if(BFA[cluster] > 0 && BFA[cluster] < 7) {
						printf("Multiple impressions on one cluster: %u\n", cluster);
						BFA[i] = 1;
						break;
			        } 
					if(BFA[cluster] == 8) {
						printf("here\n");
						BFA[cluster] = 1;
                        break;
			        }
					
					BFA[cluster] = 1;
					
					cluster = get_fat_entry(cluster, image_buf, bpb);	
				}
			}
    	}
	}
}

void adoptioncenter(uint8_t *BFA, uint8_t *image_buf, struct bpb33 *bpb) {
    int numOrphans=0;
	int endfat = bpb->bpbSectors - 33;
    for(int i=CLUST_FIRST & FAT12_MASK; i < endfat; i++) {
        if(BFA[i]==8) {
			int size = 0;
			uint16_t cluster= (uint16_t)i;
			while (!is_end_of_file(cluster)){
				cluster = get_fat_entry(cluster, image_buf, bpb);
				size += 512;	
			}
            numOrphans++;
            struct direntry *dirent = (struct direntry*)cluster_to_addr(0, image_buf, bpb);
            char *num = malloc(sizeof(char)*4);
            snprintf(num, sizeof(num), "%d", numOrphans);
            char *filename = malloc(5+4+sizeof(num)+1);
            strcpy(filename,"found");
            strcat(filename,num);
            strcat(filename,".dat");
			printf("Orphan at: %d\n",i);
            create_dirent(dirent,filename,(uint16_t)i,(uint16_t)size,image_buf,bpb);
            BFA[i]=1;
            free(filename);
            free(num);
        }
    }
} 


int main(int argc, char** argv) {
    uint8_t *image_buf;
    int fd;
    struct bpb33* bpb;

    // Big fscking array
    uint8_t *BFA = malloc(sizeof(uint8_t)*(CLUST_LAST & FAT12_MASK));

    for(int i=0; i< (CLUST_LAST & FAT12_MASK);i++) {
        BFA[i] = 0;
    }

    if (argc < 2) {
        usage(argv[0]);
    }


    image_buf = mmap_file(argv[1], &fd);
    bpb = check_bootsector(image_buf);

	uint16_t cluster = 0;
	struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);
	for(int i = 0; i < bpb->bpbRootDirEnts; i++){
		int16_t followclust = getfollowclust(dirent, 0, image_buf, bpb, BFA);
		if (is_valid_cluster(followclust, bpb)){
			follow_dir(followclust, 1, image_buf, bpb, BFA);
		}
		dirent++;
	}
    orphanage(BFA,image_buf,bpb);
	adoptioncenter(BFA,image_buf,bpb);
    unmmap_file(image_buf, &fd);
    free(BFA);
	free(bpb);
    return 0;
}











