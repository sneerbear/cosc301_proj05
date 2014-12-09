#include <stdio.h>
#include <stdlib.h>
#include "list.h"

/* your list function definitions */

//creates a new context and adds it to the end of the list
void listadd(node** head, node** tail, uint16_t file_cluster, uint32_t size){
	
	node* added = malloc(sizeof(node));
	added->next = NULL;
	
	if(*head == NULL){
		*head = added;
		*tail = added;
	}
	
	else{
		(**tail).next = added;
		*tail = added;
	}
	added -> file_cluster = file_cluster;
	added -> size = size;
}

//removes the head of the list and frees it
void headdestroy(node **head){
	
	node* tmp = *head;
	*head = (**head).next;
	//free(tmp->ctx.uc_stack.ss_sp);
	//free(tmp);
}

// Removes and returns the node value in the top node of the queue
// Should never be called if list is empty
node *listremove(node **head) {
	
	node *temp = *head;
	head = &(temp -> next);
	return temp;
}

//frees everything in the list
void listdestroy(node *list) {

    while (list != NULL) {
        node *tmp = list;
        list = list->next;
        free(tmp);
    }
}

//prints the list
void listprint(node *list) {
	
	printf("*** List Contents Begin ***\n");
	int i = 0;
    while (list != NULL) {
    	printf("size: %d cluster: %d\n", list->size, list->file_cluster);
        list = list->next;
    }
	printf("*** List Contents End ***\n");
}
