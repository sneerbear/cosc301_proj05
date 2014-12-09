#ifndef __LIST_H__
#define __LIST_H__
#include <ucontext.h>

// Define node
typedef struct node {
	ucontext_t ctx;
	struct node *next;
} node;

// Our node class has 3 functions, an add that places an item in
// the correct location to keep list sorted, a list destroy, and
// a list print
void addctx(node** head, node** tail, ucontext_t* returnctx);
void headdestroy(node **head);
ucontext_t* listremove(node** head);
void listdestroy(node *head);
void headdestroy(node **head);
void listprint(node *head);
void nextthread(node **head, node **tail);


#endif // __LIST_H__
