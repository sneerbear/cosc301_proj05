#include <stdio.h>
#include <stdlib.h>
#include "list.h"

/* your list function definitions */

//creates a new context and adds it to the end of the list
void addctx(node** head, node** tail, ucontext_t* returnctx){
	
	node* thread = malloc(sizeof(node));
	thread->next = NULL;
	
	if(*head == NULL){
		*head = thread;
		*tail = thread;
	}
	
	else{
		(**tail).next = thread;
		*tail = thread;
	}
	
	#define STACKSIZE 128000
	unsigned char *stack = (unsigned char *)malloc(STACKSIZE);
	assert(stack);

	getcontext(&thread->ctx);
	thread->ctx.uc_stack.ss_sp   = stack;
	thread->ctx.uc_stack.ss_size = STACKSIZE;
	thread->ctx.uc_link          = returnctx;
	
}

//removes the head of the list and frees it
void headdestroy(node **head){
	
	node* tmp = *head;
	*head = (**head).next;
	free(tmp->ctx.uc_stack.ss_sp);
	free(tmp);
}

// Removes and returns the ucontext_t value in the top node of the queue
// Should never be called if list is empty
ucontext_t *listremove(node **head) {
	
	node *temp = *head;
	head = &(temp -> next);
	ucontext_t *ret = malloc(sizeof(ucontext_t));
	ret = &(temp -> ctx);
	free(temp);
	return ret;
}

//frees everything in the list
void listdestroy(node *list) {

    while (list != NULL) {
        node *tmp = list;
        list = list->next;
		unsigned char *stack = tmp->ctx.uc_stack.ss_sp;
		free(stack);
        free(tmp);
    }
}

//prints the list
void listprint(node *list) {
	
	printf("*** List Contents Begin ***\n");
	int i = 0;
    while (list != NULL) {
		printf("Thread %d\n",i++);
        list = list->next;
    }
	printf("*** List Contents End ***\n");
}

//pops and appends the head
void nextthread(node **head, node **tail){
	
	if(*head == NULL){
		return;
	}
	
	if(*tail == NULL){
		*tail = *head;
		*head = (**head).next;
		(**tail).next = NULL;
		return;
	}
	
	(**tail).next = *head;
	*tail = *head;
	*head = (**head).next;
	(**tail).next = NULL;
}






