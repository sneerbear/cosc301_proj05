#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include "bootsect.h"
#include "bpb.h"
#include "direntry.h"
#include "fat.h"
#include "dos.h"


void print_indent(int indent) {
    int i;
    for (i = 0; i < indent*4; i++)
	printf(" ");
}


uint16_t print_dirent(struct direntry *dirent, int indent) {
    uint16_t followclust = 0;

    int i;
    char name[9];
    char extension[4];
    uint32_t size;
    uint16_t file_cluster;
    name[8] = ' ';
    extension[3] = ' ';
    memcpy(name, &(dirent->deName[0]), 8);
    memcpy(extension, dirent->deExtension, 3);
    if (name[0] == SLOT_EMPTY) {
		return followclust;
    }

    /* skip over deleted entries */
    if (((uint8_t)name[0]) == SLOT_DELETED) {
	return followclust;
    }

    if (((uint8_t)name[0]) == 0x2E) {
	// dot entry ("." or "..")
	// skip it
        return followclust;
    }
    else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) {
		if ((dirent->deAttributes & ATTR_HIDDEN) != ATTR_HIDDEN) {
            file_cluster = getushort(dirent->deStartCluster);
            followclust = file_cluster;
        }
    }
    return followclust;
}

void printfile(struct direntry *dirent) {
    char tempn[10];
    char tempext[5];
    char name[10] = {'\0'};
    char ext[5] = {'\0'};

    tempn[8] = ' ';
    tempext[3] = ' ';
    memcpy(tempn, &(dirent->deName[0]), 8);
    memcpy(tempext, dirent->deExtension, 3);
    
    /* names are space padded - remove the spaces */
    for (int i = 8; i > 0; i--)  {
        if (tempn[i] == ' ') 
            tempn[i] = '\0';
        else 
            break;
    }

    /* remove the spaces from extensions */
    for (int i = 3; i > 0; i--) {
        if (tempext[i] == ' ') 
            tempext[i] = '\0';
        else 
            break;
    }
    
    strcat(name,tempn);
    strcat(ext,tempext);

    printf("Filename: %s.%s\n", name,ext);
    printf("Difference: %d\n",difference);
    uint16_t head_cluster = get_fat_entry(getushort(dirent->deStartCluster), image_buf, bpb);
    printf("Start Cluster: %u\n", getushort(dirent->deStartCluster));
    printf("Head: %u\n",head_cluster);
}

void trace(struct direntry *dirent, uint8_t *image_buf, struct bpb33* bpb, uint8_t *BFA){
	uint16_t cluster = getushort(dirent->deStartCluster);
    uint16_t oldCluster = NULL;
	int size = 0;
	while (!is_end_of_file(cluster)){
        if(cluster == CLUST_BAD & FAT12_MASK) {
            if(oldCluster != NULL) {
                set_fat_entry(oldCluster,CLUST_EOFS & FAT12_MASK,image_buf,bpb);
            }
            break;
        }
        size += 512; //Mark cluster as visited
        BFA[(int)cluster] = 1;

        oldCluster = cluster;
        cluster = get_fat_entry(cluster, image_buf, bpb);

        if(size > getushort(dirent->deFileSize)) {
            set_fat_entry(oldCluster,CLUST_EOFS & FAT12_MASK,image_buf,bpb);
            BFA[cluster] = 2;
            break;
        }
	}
    int difference = size - (int)getulong(dirent->deFileSize);

    if(difference > 0) {

    }
}

void difftoolarge(uint8_t start_cluster,uint8_t *image_buf, struct bpb33 *bpb, uint8_t *BFA) {
    uint8_t cluster = start_cluster;
    uint8_t oldCluster = cluster;
    while(!is_end_of_file(start_cluster)) {
        cluster = get_fat_entry(cluster,image_buf,bpb);
        set_fat_entry(oldCluster,CLUST_FREE&FAT12_MASK,image_buf,bpb);
        BFA[oldCluster] = 1;
        oldCluster = cluster;
    }
}

void difftoosmall(int size, uint8_t *image_buf, struct bpb33 *bpb,struct direntry *dirent) {
   
    dirent->deFileSize = (uint8_t *)size;
}

void follow_dir(uint16_t cluster, int indent, uint8_t *image_buf, struct bpb33* bpb, uint8_t *BFA) {
    
	while (is_valid_cluster(cluster, bpb)) {
        struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);
        int numDirEntries = (bpb->bpbBytesPerSec * bpb->bpbSecPerClust) / sizeof(struct direntry);
        int i = 0;
		for ( ; i < numDirEntries; i++){
            uint16_t followclust = print_dirent(dirent, indent);
			if(getushort(dirent->deStartCluster) != 0){
				trace(dirent, image_buf, bpb, BFA);                
			}
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

void freeclusters(uint8_t *BFA, uint8_t *image_buf, struct bpb33 *bpb) {
    for(uint16_t i=CLUST_FIRST; !is_end_of_file(i); i++) {
            //set_fat_entry(i,CLUST_FREE,image_buf,bpb);
        if (BFA[i]==2) { 
            difftoolarge(i,image_buf,bpb,BFA);
        }
    }
}


int main(int argc, char** argv) {
    uint8_t *image_buf;
    int fd;
    struct bpb33* bpb;
    //if (argc < 2) {
		//usage(argv[0]);
    //}
    
    uint8_t *BFA = malloc(sizeof(uint8_t)*(CLUST_LAST & FAT12_MASK));

    for(int i=0; i< (CLUST_LAST & FAT12_MASK);i++) {
        BFA[i] = 0;
    }

    image_buf = mmap_file("goodimage.img", &fd);
    bpb = check_bootsector(image_buf);

    // your code should start here...
	uint16_t cluster = 0;
	struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);
	for(int i = 0; i < bpb->bpbRootDirEnts; i++){
		int16_t followclust = print_dirent(dirent, 0);
		if (is_valid_cluster(followclust, bpb)){
			follow_dir(followclust, 1, image_buf, bpb, BFA);
			//printf("VALID\n");
		}
		dirent++;
	}

    freeclusters(BFA,image_buf,bpb);
	
    unmmap_file(image_buf, &fd);

    // printf("%u\n", sizeof(int)*(bpb->bpbFATsecs));
   // printf("BIG FUCKING ARRAY SIZE %u\n", CLUST_LAST & FAT12_MASK);
    return 0;
}
