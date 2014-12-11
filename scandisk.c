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

    /* names are space padded - remove the spaces */
    for (i = 8; i > 0; i--)  {
		if (name[i] == ' ') 
	    	name[i] = '\0';
		else 
	    	break;
    }

    /* remove the spaces from extensions */
    for (i = 3; i > 0; i--) {
		if (extension[i] == ' ') 
	    	extension[i] = '\0';
		else 
	    	break;
    }

    if ((dirent->deAttributes & ATTR_WIN95LFN) == ATTR_WIN95LFN) {
	// ignore any long file name extension entries
	//
	// printf("Win95 long-filename entry seq 0x%0x\n", dirent->deName[0]);
    }
    else if ((dirent->deAttributes & ATTR_VOLUME) != 0) {
		//printf("Volume: %s\n", name);
    } 
    else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) {
        // don't deal with hidden directories; MacOS makes these
        // for trash directories and such; just ignore them.
		if ((dirent->deAttributes & ATTR_HIDDEN) != ATTR_HIDDEN) {
	    	//print_indent(indent);
    	   // printf("%s/ (directory)\n", name);
            file_cluster = getushort(dirent->deStartCluster);
            followclust = file_cluster;
        }
    }
    else 
    {
        /*
         * a "regular" file entry
         * print attributes, size, starting cluster, etc.
         */
		int ro = (dirent->deAttributes & ATTR_READONLY) == ATTR_READONLY;
		int hidden = (dirent->deAttributes & ATTR_HIDDEN) == ATTR_HIDDEN;
		int sys = (dirent->deAttributes & ATTR_SYSTEM) == ATTR_SYSTEM;
		int arch = (dirent->deAttributes & ATTR_ARCHIVE) == ATTR_ARCHIVE;

		size = getulong(dirent->deFileSize);
		//print_indent(indent);
		/*printf("%s.%s (%u bytes) (starting cluster %d) %c%c%c%c\n", 
	       name, extension, size, getushort(dirent->deStartCluster),
	       ro?'r':' ', 
           hidden?'h':' ', 
           sys?'s':' ', 
           arch?'a':' '); */
		
    }

    return followclust;
}

int trace(struct direntry *dirent, uint8_t *image_buf, struct bpb33* bpb, uint8_t BFA[]){
	uint16_t cluster = getushort(dirent->deStartCluster);
	int size = 0;
	while (!is_end_of_file(cluster)){
		size += 512;
		cluster = get_fat_entry(cluster, image_buf, bpb);
        BFA[(int)cluster] = 1; //Mark cluster as visited
		//printf("Cluster: %u\n",cluster);
	}
	return size;
}


void follow_dir(uint16_t cluster, int indent, uint8_t *image_buf, struct bpb33* bpb, uint8_t BFA[]) {
    
	while (is_valid_cluster(cluster, bpb)) {
        struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);
        int numDirEntries = (bpb->bpbBytesPerSec * bpb->bpbSecPerClust) / sizeof(struct direntry);
        int i = 0;
		for ( ; i < numDirEntries; i++){
            uint16_t followclust = print_dirent(dirent, indent);
			if(getushort(dirent->deStartCluster) != 0){
				int size = trace(dirent, image_buf, bpb, BFA);
				int difference = size - (int)getulong(dirent->deFileSize);
                if(difference > 512 || difference < 0) {
                    
                    char tempn[9];
                    char tempext[4];
					char name[9] = {'\0'};
					char ext[4] = {'\0'};

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

void freeclusters(uint8_t BFA[], uint8_t *image_buf, struct bpb33 *bpb) {
    for(uint16_t i=CLUST_FIRST; !is_end_of_file(i); i++) {
        if(BFA[i]==0) {
            set_fat_entry(i,CLUST_FREE,image_buf,bpb);
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
    
    uint8_t BFA[CLUST_LAST & FAT12_MASK] = {0};

    image_buf = mmap_file("badimage1.img", &fd);
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



















