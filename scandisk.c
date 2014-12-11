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

void plsincrement(uint16_t clust,uint8_t *BFA) {
    if(BFA[clust] > 1) { 
        printf("Problem, cluster %u has been touched twice.\n", clust); 
    } else { 
        BFA[clust]++; 
    }
} 

void print_indent(int indent) {
    int i;
    for (i = 0; i < indent*4; i++)
	printf(" ");
}


uint16_t getfollowclust(struct direntry *dirent, int indent) {
    uint16_t followclust = 0;

    char name[9];
    char extension[4];
    //uint32_t size;
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

void printfile(struct direntry *dirent, uint8_t *image_buf, struct bpb33 *bpb) {
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
    //printf("Difference: %d\n",difference);
    uint16_t head_cluster = get_fat_entry(getushort(dirent->deStartCluster), image_buf, bpb);
    printf("Start Cluster: %u\n", getushort(dirent->deStartCluster));
    printf("Head: %u\n",head_cluster);
}

void trace(struct direntry *dirent, uint8_t *image_buf, struct bpb33* bpb, uint8_t *BFA){
	uint16_t cluster = getushort(dirent->deStartCluster);
    uint16_t oldCluster = 0;
	int size = 0; //There is no cluster 0?
	while (!is_end_of_file(cluster)){
        if(cluster == (CLUST_BAD & FAT12_MASK)) {
            if(oldCluster != 0) {
                set_fat_entry(oldCluster,CLUST_EOFS & FAT12_MASK,image_buf,bpb);
            }
            break;
        }
        size += 512; 



        plsincrement(cluster,BFA); //Mark cluster as visited

        oldCluster = cluster;
        cluster = get_fat_entry(cluster, image_buf, bpb);

        if(size > getushort(dirent->deFileSize)) {  

            printfile(dirent,image_buf,bpb);
            printf("too small\n");

            //Solution to case where FATS
            // while(!is_end_of_file(cluster)) { 
            //     plsincrement(cluster,BFA); //Set BFA value to 1

            //     set_fat_entry(oldCluster,CLUST_EOFS & FAT12_MASK,image_buf,bpb);
            //     BFA[oldCluster] = 1;
            //     oldCluster = cluster;
            //     get_fat_entry(cluster, image_buf,bpb);                
            // }
            // set_fat_entry(cluster,CLUST_FREE & FAT12_MASK, image_buf,bpb);
            //BFA[cluster] = 2;
            //Fix things here! 
            break;  // THIS IS REALLY STUPID CIRCULAR CODE STRUCTURE, 
                    // SHOULD PROBABLY REASSESS LATER 
        }
	}
    int difference = size - (int)getulong(dirent->deFileSize);

    //Solution to case where file in directory is too large, reset to FAT table size
    if(difference < 0) {
        //Change size
        printfile(dirent,image_buf,bpb);
        printf("Difference: %d\n",difference);
        //putushort(dirent->deFileSize, size); //look at this
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

void follow_dir(uint16_t cluster, int indent, uint8_t *image_buf, struct bpb33* bpb, uint8_t *BFA) {
    
	while (is_valid_cluster(cluster, bpb)) {
        struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);
        int numDirEntries = (bpb->bpbBytesPerSec * bpb->bpbSecPerClust) / sizeof(struct direntry);
        int i = 0;
		for ( ; i < numDirEntries; i++){
            uint16_t followclust = getfollowclust(dirent, indent);
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
    for (i = 0; i < strlen(filename); i++) 
    {
    if (p2[i] == '/' || p2[i] == '\\') 
    {
        uppername = p2+i+1;
    }
    }

    /* convert filename to upper case */
    for (i = 0; i < strlen(uppername); i++) 
    {
    uppername[i] = toupper(uppername[i]);
    }

    /* set the file name and extension */
    memset(dirent->deName, ' ', 8);
    p = strchr(uppername, '.');
    memcpy(dirent->deExtension, "___", 3);
    if (p == NULL) 
    {
    fprintf(stderr, "No filename extension given - defaulting to .___\n");
    }
    else 
    {
    *p = '\0';
    p++;
    len = strlen(p);
    if (len > 3) len = 3;
    memcpy(dirent->deExtension, p, len);
    }

    if (strlen(uppername)>8) 
    {
    uppername[8]='\0';
    }
    memcpy(dirent->deName, uppername, strlen(uppername));
    free(p2);

    /* set the attributes and file size */
    dirent->deAttributes = ATTR_NORMAL;
    putushort(dirent->deStartCluster, start_cluster);
    putulong(dirent->deFileSize, size);

    /* could also set time and date here if we really
       cared... */
}


/* create_dirent finds a free slot in the directory, and write the
   directory entry */

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

void handleorphans(uint8_t *BFA, uint8_t *image_buf, struct bpb33 *bpb) {
    int numOrphans = 0;
    for(uint16_t i=CLUST_FIRST; !is_end_of_file(i); i++) {
        if(BFA[i]==0) {
            if(get_fat_entry(i,image_buf,bpb) != (CLUST_FREE & FAT12_MASK)) {
                struct direntry *dir = (void *)1;
                char *num = malloc(sizeof(char)*4);
                snprintf(num, sizeof(num), "%d", numOrphans);
                char *filename = malloc(5+4+sizeof(num));
                strcpy(filename,"foundm");
                strcat(filename,num);
                strcat(filename,".dat");

                create_dirent(dir, filename, i, 512,image_buf, bpb);
                numOrphans++;
            }
        }
        // } else if (BFA[i]==2) { 
        //     difftoolarge(i,image_buf,bpb,BFA);
        // }
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

    image_buf = mmap_file("badimage2.img", &fd);
    bpb = check_bootsector(image_buf);

    // your code should start here...
	uint16_t cluster = 0;
	struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);
	for(int i = 0; i < bpb->bpbRootDirEnts; i++){
		int16_t followclust = getfollowclust(dirent, 0);
		if (is_valid_cluster(followclust, bpb)){
			follow_dir(followclust, 1, image_buf, bpb, BFA);
			//printf("VALID\n");
		}
		dirent++;
	}

   // handleorphans(BFA,image_buf,bpb);
	
    unmmap_file(image_buf, &fd);

    // printf("%u\n", sizeof(int)*(bpb->bpbFATsecs));
   // printf("BIG FUCKING ARRAY SIZE %u\n", CLUST_LAST & FAT12_MASK);
    return 0;
}
