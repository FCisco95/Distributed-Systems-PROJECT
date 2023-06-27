#ifndef _TREE_PRIVATE_H
#define _TREE_PRIVATE_H

#include "entry.h"
#include "tree.h"

struct node_t {
	struct entry_t *entry;
	struct node_t *left;
	struct node_t *right;
	struct node_t *parent;
};

struct tree_t {
	int numNodes;
	struct node_t *root;
};

#endif
