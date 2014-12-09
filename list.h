#ifndef __LIST_H__
#define __LIST_H__

// Define node
typedef struct node {
	uint16_t file_cluster;
	uint32_t size;
	struct node *next;
} node;

// Our node class has 3 functions, an add that places an item in
// the correct location to keep list sorted, a list destroy, and
// a list print
void listadd(node** head, node** tail, uint16_t file_cluster, uint32_t size);
void headdestroy(node **head);
node *listremove(node** head);
void listdestroy(node *head);
void headdestroy(node **head);
void listprint(node *head);


#endif // __LIST_H__
