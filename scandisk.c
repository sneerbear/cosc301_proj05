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
    if(BFA[clust] == 1) { 
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
                set_fat_entry(cluster,CLUST_EOFS & FAT12_MASK,image_buf,bpb);
            }
            break;
        }

        size += 512; 
        printf("1\n");
        plsincrement(cluster,BFA); //Mark cluster as visited

        oldCluster = cluster;
        cluster = get_fat_entry(cluster, image_buf, bpb);

        if(size > getushort(dirent->deFileSize)) {  
            printf("%u > %u\n",size, getushort(dirent->deFileSize));
            printfile(dirent,image_buf,bpb);
            set_fat_entry(oldCluster,CLUST_EOFS & FAT12_MASK,image_buf,bpb);

            while(1) {
                printf("2\n");
                plsincrement(cluster,BFA); 
                oldCluster = cluster;
                cluster = get_fat_entry(cluster, image_buf,bpb);  
                set_fat_entry(oldCluster,CLUST_FREE & FAT12_MASK, image_buf,bpb);

                if(is_end_of_file(cluster)) {
                    set_fat_entry(cluster,CLUST_FREE & FAT12_MASK, image_buf,bpb);
                    printf("3\n");
                    plsincrement(cluster,BFA);
                    break;
                }              
            }
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
    for (i = 8; i > 0; i--) 
    {
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


#define FIND_FILE 0
#define FIND_DIR 1

struct direntry* find_file(char *infilename, uint16_t cluster,
               int find_mode,
               uint8_t *image_buf, struct bpb33* bpb)
{
    char buf[MAXPATHLEN];
    char *seek_name, *next_name;
    int d;
    struct direntry *dirent;
    uint16_t dir_cluster;
    char fullname[13];

    /* find the first dirent in this directory */
    dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);

    /* first we need to split the file name we're looking for into the
       first part of the path, and the remainder.  We hunt through the
       current directory for the first part.  If there's a remainder,
       and what we find is a directory, then we recurse, and search
       that directory for the remainder */

    strncpy(buf, infilename, MAXPATHLEN);
    seek_name = buf;

    /* trim leading slashes */
    while (*seek_name == '/' || *seek_name == '\\') 
    {
    seek_name++;
    }

    /* search for any more slashes - if so, it's a dirname */
    next_name = seek_name;
    while (1) 
    {
    if (*next_name == '/' || *next_name == '\\') 
    {
        *next_name = '\0';
        next_name ++;
        break;
    }
    if (*next_name == '\0') 
    {
        /* end of name - no slashes found */
        next_name = NULL;
        if (find_mode == FIND_DIR) 
        {
        return dirent;
        }
        break;
    }
    next_name++;
    }

    while (1) 
    {
    /* hunt a cluster for the relevant dirent.  If we reach the
       end of the cluster, we'll need to go to the next cluster
       for this directory */
    for (d = 0; 
         d < bpb->bpbBytesPerSec * bpb->bpbSecPerClust; 
         d += sizeof(struct direntry)) 
    {
        if (dirent->deName[0] == SLOT_EMPTY) 
        {
        /* we failed to find the file */
        return NULL;
        }

        if (dirent->deName[0] == SLOT_DELETED) 
        {
        /* skip over a deleted file */
        dirent++;
        continue;
        }

        get_name(fullname, dirent);
        if (strcmp(fullname, seek_name)==0) 
        {
        /* found it! */
        if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) 
        {
            /* it's a directory */
            if (next_name == NULL) 
            {
            fprintf(stderr, "Cannot copy out a directory\n");
            exit(1);
            }
            dir_cluster = getushort(dirent->deStartCluster);
            return find_file(next_name, dir_cluster, 
                     find_mode, image_buf, bpb);
        } 
        else if ((dirent->deAttributes & ATTR_VOLUME) != 0) 
        {
            /* it's a volume */
            fprintf(stderr, "Cannot copy out a volume\n");
            exit(1);
        } 
        else 
        {
            /* assume it's a file */
            return dirent;
        }
        }
        dirent++;
    }

    /* we've reached the end of the cluster for this directory.
       Where's the next cluster? */
    if (cluster == 0) 
    {
        // root dir is special
        dirent++;
    } 
    else 
    {
        cluster = get_fat_entry(cluster, image_buf, bpb);
        dirent = (struct direntry*)cluster_to_addr(cluster, 
                               image_buf, bpb);
    }
    }
}


void handleorphans(uint8_t *BFA, uint8_t *image_buf, struct bpb33 *bpb) {
    int numOrphans = 0;
    for(uint16_t i=CLUST_FIRST; i < sizeof(BFA)/sizeof(uint16_t); i++) {
        if(BFA[i]==0) {
            if(get_fat_entry(i,image_buf,bpb) != (CLUST_FREE & FAT12_MASK)) {
                struct direntry *dirent = (void *)1;
                char *num = malloc(sizeof(char)*4);
                snprintf(num, sizeof(num), "%d", numOrphans);
                char *filename = malloc(5+4+sizeof(num));
                strcpy(filename,"found");
                strcat(filename,num);
                strcat(filename,".dat");

                dirent = find_file(filename, 0xe5, FIND_FILE, image_buf, bpb);
                if (dirent == NULL) 
                {
                    fprintf(stderr, "Directory does not exists in the disk image\n");
                    exit(1);
                }

                write_dirent(dirent,filename,i,512);
                create_dirent(dirent, filename, i, 512,image_buf, bpb);
                numOrphans++;
                BFA[i]=1;
            }
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
		int16_t followclust = getfollowclust(dirent, 0);
		if (is_valid_cluster(followclust, bpb)){
			follow_dir(followclust, 1, image_buf, bpb, BFA);
			//printf("VALID\n");
		}
		dirent++;
	}

    handleorphans(BFA,image_buf,bpb);
	
    unmmap_file(image_buf, &fd);
    free(BFA);


    // printf("%u\n", sizeof(int)*(bpb->bpbFATsecs));
   // printf("BIG FUCKING ARRAY SIZE %u\n", CLUST_LAST & FAT12_MASK);
    return 0;
}
