#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "entry.h"
#include "tree.h"
#include "tree-private.h"

struct node_t *node_create(struct entry_t *entry) {
    struct node_t *node = malloc(sizeof(struct node_t));
    node->entry = entry;
    node->left = NULL;
    node->right = NULL;
    node->parent = NULL;
    return node;
}

void node_destroy(struct node_t *node) {
    entry_destroy(node->entry);
    free(node);
}

void node_destroy_subtree(struct node_t *node) {
    if (node->left == NULL && node->right == NULL) {
        node_destroy(node);
        return;
    }
    if (node->left != NULL) node_destroy(node->left);
    if (node->right != NULL) node_destroy(node->right);
}

/**
 * insertResult = 0 if replacement
 * insertResult = 1 if insertion
 */
struct node_t *node_insert(struct node_t *node, struct entry_t *entry, int *insertResult) {
    if (node == NULL) {
        *insertResult = 1;
        return node_create(entry);
    }
    int compare = entry_compare(node->entry, entry);
    if (compare == 0) {
        entry_destroy(node->entry);
        node->entry = entry;
        *insertResult = 0;
        return node;
    }
    else if (compare > 0) {
        node->left = node_insert(node->left, entry, insertResult);
        node->left->parent = node;
    } else {
        node->right = node_insert(node->right, entry, insertResult);
        node->right->parent = node;
    }
    return node;
}

struct node_t *node_find(struct node_t *node, struct entry_t *entryKey) {
    if (node == NULL) return NULL;
    int compare = entry_compare(node->entry, entryKey);
    if (compare == 0) return node;
    if (compare < 0) return node_find(node->right, entryKey);
    return node_find(node->left, entryKey);
}

int node_tree_height(struct node_t *node, int height) {
    if (node == NULL) return height;
    if (node->left == NULL && node->right == NULL) return height;
    int leftHeight = 0;
    int rightHeight = 0;
    if (node->left != NULL) leftHeight = node_tree_height(node->left, 1);
    if (node->right != NULL) rightHeight = node_tree_height(node->right, 1);
    int max = leftHeight;
    if (max < rightHeight) max = rightHeight;
    return height + max;
}

/**
 * nodeArray MUST have enough positions!
 */
int node_array_subtree(struct node_t *node, struct node_t **nodeArray) {
    int count = 0;
    if (node == NULL) return 0;
    nodeArray[0] = node;
    count++;
    int i = 0;
    while (i < count) {
        if (nodeArray[i]->left != NULL) {
            nodeArray[count] = nodeArray[i]->left;
            count++;
        }
        if (nodeArray[i]->right != NULL) {
            nodeArray[count] = nodeArray[i]->right;
            count++;
        }
        i++;
    }
    return count;
}

/*
From wikipedia:
    BST-Minimum(x)
        while x.left ≠ NIL then
            x := x.left
        repeat
        return x
*/
struct node_t *node_bst_minimum(struct node_t *node) {
    struct node_t *nodeTmp = node;
    while (nodeTmp->left != NULL) {
        nodeTmp = nodeTmp->left;
    }
    return nodeTmp;
}


/*
From wikipedia:
    BST-Maximum(x)
        while x.right ≠ NIL then
            x := x.right
        repeat
        return x
*/
struct node_t *node_bst_maximum(struct node_t *node) {
    struct node_t *nodeTmp = node;
    while (nodeTmp->right != NULL) {
        nodeTmp = nodeTmp->right;
    }
    return nodeTmp;
}

/*
From wikipedia:
    Shift-Nodes(T, u, v)
2      if u.parent = NIL then
3        T.root := v
4      else if u = u.parent.left then
5        u.parent.left := v
5      else
6        u.parent.right := v
7      end if
8      if v ≠ NIL then
9        v.parent := u.parent
10     end if
*/
void shift_nodes(struct node_t **root, struct node_t *u, struct node_t *v) {
    if (u->parent == NULL) {
        *root = v;
    } else if (u == u->parent->left) {
        u->parent->left = v;
    } else {
        u->parent->right = v;
    }
    if (v != NULL) {
        v->parent = u->parent;
    }
}


/*
From wikipedia:
    Assuming all the keys of the BST are distinct, the successor of a node x 
    in BST is the node with the smallest key greater than x's key. 

    BST-Successor(x)
   if x.right ≠ NIL then
     return BST-Minimum(x.right)
   end if
   y := x.parent
   while y ≠ NIL and x = y.right then
     x := y
     y := y.parent
   repeat
   return y
*/
struct node_t *node_successor(struct node_t *node) {
    struct node_t *x = node;
    if (x->right != NULL) {
        return node_bst_minimum(x->right);
    }
    struct node_t *y = x->parent;
    while (y != NULL && x == y->right) {
        x = y;
        y = y->parent;
    }
    return y;
}


/*****************************************************************************/

/* Função para criar uma nova árvore tree vazia.
 * Em caso de erro retorna NULL.
 */
struct tree_t *tree_create() {
    struct tree_t *tree = malloc(sizeof(struct tree_t));
    tree->numNodes = 0;
    tree->root = NULL;
    return tree;
}


/* Função para libertar toda a memória ocupada por uma árvore.
 */
void tree_destroy(struct tree_t *tree) {
    if (tree->numNodes == 0) return;
    node_destroy_subtree(tree->root);
    tree->root = NULL;
    tree->numNodes = 0;
}

/* Função para adicionar um par chave-valor à árvore.
 * Os dados de entrada desta função deverão ser copiados, ou seja, a
 * função vai *COPIAR* a key (string) e os dados para um novo espaço de
 * memória que tem de ser reservado. Se a key já existir na árvore,
 * a função tem de substituir a entrada existente pela nova, fazendo
 * a necessária gestão da memória para armazenar os novos dados.
 * Retorna 0 (ok) ou -1 em caso de erro.
 */
int tree_put(struct tree_t *tree, char *key, struct data_t *value) {
    if (tree == NULL || key == NULL || value == NULL) return -1;
    struct entry_t *entry = entry_create(key, value);
    struct entry_t *entry_copy = entry_dup(entry);
    free(entry);
    int insertResult = 0;
    tree->root = node_insert(tree->root, entry_copy, &insertResult);
    tree->numNodes += insertResult;
    return 0;
}

/* Função para obter da árvore o valor associado à chave key.
 * A função deve devolver uma cópia dos dados que terão de ser
 * libertados no contexto da função que chamou tree_get, ou seja, a
 * função aloca memória para armazenar uma *CÓPIA* dos dados da árvore,
 * retorna o endereço desta memória com a cópia dos dados, assumindo-se
 * que esta memória será depois libertada pelo programa que chamou
 * a função. Devolve NULL em caso de erro.
 */
struct data_t *tree_get(struct tree_t *tree, char *key) {
    struct entry_t entryKey;
    entryKey.key = key;
    struct node_t *found = node_find(tree->root, &entryKey);
    if (found == NULL) return NULL;
    return data_dup(found->entry->value);
}

/* Função para remover um elemento da árvore, indicado pela chave key,
 * libertando toda a memória alocada na respetiva operação tree_put.
 * Retorna 0 (ok) ou -1 (key not found).
 * 
 * From wikipedia:
 * 1    BST-Delete(T, z)
 * 2      if z.left = NIL then
 * 3        Shift-Nodes(T, z, z.right)
 * 4      else if z.right = NIL then
 * 5        Shift-Nodes(T, z, z.left)
 * 6      else
 * 7        y := Tree-Successor(z)
 * 8        if y.parent ≠ z then
 * 9          Shift-Nodes(T, y, y.right)
 * 10         y.right := z.right
 * 11         y.right.parent := y
 * 12       end if
 * 13       Shift-Nodes(T, z, y)
 * 14       y.left := z.left
 * 15       y.left.parent := y
 * 16     end if
 */
int tree_del(struct tree_t *tree, char *key) {
    struct node_t *y;
    struct entry_t entryKey;
    entryKey.key = key;
    struct node_t *z = node_find(tree->root, &entryKey);
    if (z == NULL) return -1;

    if (z->left == NULL) {
        shift_nodes(&(tree->root), z, z->right);
    }
    else if (z->right == NULL) {
        shift_nodes(&(tree->root), z, z->left);
    }
    else {
        y = node_successor(z);
        if (y->parent != z) {
            shift_nodes(&(tree->root), y, y->right);
            y->right = z->right;
            y->right->parent = y;
        }
        shift_nodes(&(tree->root), z, y);
        y->left = z->left;
        y->left->parent = y;
    }

    node_destroy(z);
    tree->numNodes--;

    return 0;
}

/* Função que devolve o número de elementos contidos na árvore.
 */
int tree_size(struct tree_t *tree) {
    return tree->numNodes;
}

/* Função que devolve a altura da árvore.
 */
int tree_height(struct tree_t *tree) {
    return node_tree_height(tree->root, 0);
}

/* Função que devolve um array de char* com a cópia de todas as keys da
 * árvore, colocando o último elemento do array com o valor NULL e
 * reservando toda a memória necessária. As keys devem vir ordenadas segundo a ordenação lexicográfica das mesmas.
 */
char **tree_get_keys(struct tree_t *tree) {
    struct node_t **nodes = malloc(sizeof(struct node_t *) * tree->numNodes);
    int count = node_array_subtree(tree->root, nodes);
    char **keys = malloc(sizeof(char *) * (count + 1)); // +1 é para o NULL
    int i;
    for (i=0; i<count; i++) {
        keys[i] = calloc(strlen(nodes[i]->entry->key) + 1, sizeof(char));
        strcpy(keys[i], nodes[i]->entry->key);
    }
    free(nodes);

    /* sort strings */
    int j;
    char *ctmp;
    for (i=0; i<count; i++) {
        for (j=i+1; j<count; j++) {
            if (strcmp(keys[i], keys[j]) > 0) {
                ctmp = keys[i];
                keys[i] = keys[j];
                keys[j] = ctmp;
            }
        }
    }

    keys[count] = NULL;
    return keys;
}

/* Função que devolve um array de void* com a cópia de todas os values da
 * árvore, colocando o último elemento do array com o valor NULL e
 * reservando toda a memória necessária.
 */
void **tree_get_values(struct tree_t *tree) {
    struct node_t **nodes = malloc(sizeof(struct node_t *) * tree->numNodes);
    int count = node_array_subtree(tree->root, nodes);
    void **values = malloc(sizeof(void *) * (count + 1));
    int i;
    for (i=0; i<count; i++) {
        values[i] = data_dup(nodes[i]->entry->value);
    }
    values[count] = NULL;
    return values;
}


/* Função que liberta toda a memória alocada por tree_get_keys().
 */
void tree_free_keys(char **keys) {
    char **iter = keys;
    while (*iter != NULL) {
        free(*iter);
        iter++;
    }
}

/* Função que liberta toda a memória alocada por tree_get_values().
 */
void tree_free_values(void **values) {
    void **iter = values;
    while (*iter != NULL) {
        free(*iter);
        iter++;
    }
}


