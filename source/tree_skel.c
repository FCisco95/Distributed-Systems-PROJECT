#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "sdmessage.pb-c.h"
#include "tree.h"
#include "tree_skel.h"
#include "tree_skel-private.h"
#include "message-private.h"

#include <zookeeper/zookeeper.h>

typedef struct String_vector zoo_string;

struct server_net_t snet = {0};

/* ZooKeeper Znode Data Length (1MB, the max supported) */
#define ZDATALEN 1024 * 1024

static zhandle_t *zh;
static int is_connected;
static char *watcher_ctx = "ZooKeeper Data Watcher";

char *zkServerNodePath = NULL; // will be /chain/node0000000...
char *zkServerNodeId = NULL;

void sortNodeIds(zoo_string *idList)
{
    int not_sorted = 1;
    char *tmp;

    // printf("NOT SORTED!!!!\n");
    // for (int i = 0; i < idList->count; i++)  {
    //     printf("(%d): %s\n", i+1, idList->data[i]);
    // }

    while (not_sorted)
    {
        not_sorted = 0;
        for (int i = 0; i < idList->count - 1; i++)
        {
            for (int j = i + 1; j < idList->count; j++)
            {
                if (strcmp(idList->data[i], idList->data[j]) > 0)
                {
                    not_sorted = 1;
                    tmp = idList->data[j];
                    idList->data[i] = idList->data[j];
                    idList->data[j] = tmp;
                }
            }
        }
    }

    // printf("SORTED!!!!\n");
    // for (int i = 0; i < idList->count; i++)  {
    //     printf("(%d): %s\n", i+1, idList->data[i]);
    // }
}

/**
 * assumes idList is sorted!!!
 */
char *getNextNode(zoo_string *idList, char *nodeId)
{
    for (int i = 0; i < idList->count; i++)
    {
        if (strcmp(idList->data[i], nodeId) == 0)
        {
            if (i == idList->count - 1)
            {
                // end of list
                return NULL;
            }
            return idList->data[i + 1];
        }
    }
    printf("Error: node id not found in list!\n");
    printf("%s\n", nodeId);
    printf("\n");
    for (int i = 0; i < idList->count; i++)
    {
        printf("\n(%d): %s", i + 1, idList->data[i]);
    }
    printf("\n");
    exit(EXIT_FAILURE);
}

/**
 * Watcher function for connection state change events
 */
void connection_watcher(zhandle_t *zzh, int type, int state, const char *path, void *context)
{
    if (type == ZOO_SESSION_EVENT)
    {
        if (state == ZOO_CONNECTED_STATE)
        {
            is_connected = 1;
        }
        else
        {
            is_connected = 0;
        }
    }
}

#define __DEBUG_THREADS__

void *process_request(void *params);
int verify(int op_n);

short n_threads = 1; // for working with ZooKeper
struct tree_t *tree;
int last_assigned = 1;

pthread_t *threads_sec; // pointer to thread pool

struct thread_params
{
    unsigned int tid;
};

struct
{
    int max_proc;
    int *in_progress; /* ids of operations being processed by secondary threads*/
} op_proc;

int find_in_progress(int op_n)
{
    for (int i = 0; i < n_threads; i++)
    {
        if (op_proc.in_progress[i] == op_n)
            return 1;
    }
    return 0;
}

// request queue infrastructure (FIFO)
struct request_t
{
    int op_n;            // o id da operação
    int op;              // a operação a executar. op=0 se for um delete, op=1 se for um put
    char *key;           // a chave a remover ou adicionar
    struct data_t *data; // os dados a adicionar em caso de put, ou NULL em caso de delete
    struct request_t *next;
};
struct request_t *request_fifo_head = NULL;
struct request_t *request_fifo_tail = NULL;
int queue_size = 0;

void request_destroy(struct request_t *request)
{
    free(request->key);
    data_destroy(request->data);
    free(request);
}

/// @brief append to request queue; key and data are not duplicated (references are kept)
/// @param op_n request id
/// @param op op code
/// @param key
/// @param data data to add to tree or NULL if delete
void queue_apppend_request(int op_n, int op, char *key, struct data_t *data)
{
    struct request_t *request = malloc(sizeof(struct request_t));
    request->op_n = op_n;
    request->op = op;
    request->key = key;
    request->data = data;
    request->next = NULL;

    if (request_fifo_head == NULL)
    {
        request_fifo_head = request;
        request_fifo_tail = request;
        queue_size = 1;
        return;
    }

    request_fifo_tail->next = request;
    request_fifo_tail = request;
    queue_size++;
}

struct request_t *queue_pop_head()
{
    if (request_fifo_head == NULL)
        return NULL;

    struct request_t *tmp = request_fifo_head;
    request_fifo_head = request_fifo_head->next;
    queue_size--;

    if (queue_size == 0)
        request_fifo_tail = NULL;
    return tmp;
}

int queue_find(int op_n)
{
    struct request_t *it = request_fifo_head;
    while (it != NULL)
    {
        if (it->op_n == op_n)
            return 1;
        it = it->next;
    }
    return 0;
}

/**
 * Data Watcher function for this node
 * To be run if there is a change in /chain children
 */
static void child_watcher(zhandle_t *wzh, int type, int state, const char *zpath, void *watcher_ctx)
{
    zoo_string *children_list = (zoo_string *)malloc(sizeof(zoo_string));
    // int zoo_data_len = ZDATALEN;
    if (state == ZOO_CONNECTED_STATE)
    {
        if (type == ZOO_CHILD_EVENT)
        {
            /* Get the updated children and reset the watch */
            if (ZOK != zoo_wget_children(zh, "/chain", child_watcher, watcher_ctx, children_list))
            {
                fprintf(stderr, "Error setting watch at %s!\n", "/chain");
            }

            sortNodeIds(children_list);
            snet.nextNode = getNextNode(children_list, zkServerNodeId);
            if (snet.nextNode == NULL)
            {
                if (snet.nextNodePath != NULL)
                    free(snet.nextNodePath);
                snet.nextNodePath = NULL;
                printf("NEXT NODE: %s\n", "this is TAIL");
            }
            else
            {
                snet.nextNodePath = malloc((7 + strlen(snet.nextNode) + 1) * sizeof(char));
                strcpy(snet.nextNodePath, "/chain/");
                strcat(snet.nextNodePath, snet.nextNode); // /chain/node0000...
                printf("NEXT NODE: %s\n", snet.nextNode);
                printf("NEXT NODE PATH: %s\n", snet.nextNodePath);

                int buffer_len = 1000;
                char *buffer = malloc(1000);
                if (ZOK != zoo_get(zh, snet.nextNodePath, 0, buffer, &buffer_len, NULL))
                {
                    printf("Error getting metadata from %s\n", snet.nextNodePath);
                    exit(EXIT_FAILURE);
                }
                if (snet.nextServerAddress != NULL)
                    free(snet.nextServerAddress);
                snet.nextServerAddress = malloc(strlen(buffer));
                strcpy(snet.nextServerAddress, buffer);
                free(buffer);

                // // setup rtree for comm. with next node
                // char* p = index(snet.nextServerAddress, ':');
                // int n = p - snet.nextServerAddress;
                // snet.next_rtree.ip_address = calloc(n+1, sizeof(char));
                // strncpy(snet.next_rtree.ip_address, snet.nextServerAddress, n);
                // p++;
                // snet.next_rtree.port = malloc(strlen((snet.nextServerAddress) - n) * sizeof(char));
                // strcpy(snet.next_rtree.port, p);

                // connect to next node
                if (snet.next_rtree == NULL)
                {
                    snet.next_rtree = rtree_connect(snet.nextServerAddress);
                }
                else
                {
                    char *str = malloc((strlen(snet.next_rtree->ip_address) + 1 + strlen(snet.next_rtree->port) + 1) * sizeof(char));
                    strcpy(str, snet.next_rtree->ip_address);
                    strcat(str, "");
                    strcat(str, snet.next_rtree->port);
                    if (strcmp(str, snet.nextServerAddress) != 0)
                    {
                        if (snet.next_rtree != NULL)
                        {
                            free(snet.next_rtree->ip_address);
                            free(snet.next_rtree->port);
                            free(snet.next_rtree);
                        }
                        snet.next_rtree = rtree_connect(snet.nextServerAddress);
                        printf("Next server changed! New connection made.");
                    }
                }

                printf("\n");
                // printf("Next server port: %s or numeric %d\n", nextPortStr, nextPort);
                printf("Next server address: %s\n", snet.nextServerAddress);
                printf("Next server ip: %s\n", snet.next_rtree->ip_address);
                printf("Next server port: %s\n", snet.next_rtree->port);
                printf("Next server socketfd: %d\n", snet.next_rtree->socketfd);
            }

            fprintf(stderr, "\n=== znode listing === [ %s ]", "/chain");
            for (int i = 0; i < children_list->count; i++)
            {
                fprintf(stderr, "\n(%d): %s", i + 1, children_list->data[i]);
            }
            fprintf(stderr, "\n=== done ===\n");
        }
    }
    free(children_list);
}

/**
 * variables for concurrency control
 */
pthread_mutex_t mx_tree = PTHREAD_MUTEX_INITIALIZER;           /* mutex lock for tree */
pthread_mutex_t mx_queue = PTHREAD_MUTEX_INITIALIZER;          /* mutex lock for request queue */
pthread_mutex_t mx_in_progress = PTHREAD_MUTEX_INITIALIZER;    /* mutex lock for in_progress */
pthread_cond_t cnd_process_request = PTHREAD_COND_INITIALIZER; /* secondary thread (process_request) waits on this cond var */

/* Inicia o skeleton da árvore.
 * O main() do servidor deve chamar esta função antes de poder usar a
 * função invoke().
 * Retorna 0 (OK) ou -1 (erro, por exemplo OUT OF MEMORY)
 */
int tree_skel_init()
{
    struct thread_params *tpar;
    int tid_count = 1001;

    threads_sec = calloc(n_threads, sizeof(pthread_t));

    // initialize thread pool;
    for (int i = 0; i < n_threads; i++)
    {
        tpar = malloc(sizeof(struct thread_params));
        tpar->tid = tid_count++;
        if (pthread_create(&threads_sec[i], NULL, process_request, (void *)tpar) != 0)
        {
            perror("\nError creating thread.\n");
            exit(EXIT_FAILURE);
        }
    }

    // initialize op_proc
    op_proc.max_proc = 0;
    op_proc.in_progress = calloc(n_threads, sizeof(int));

    tree = tree_create();

    // setup connection to zookeeper
    zh = zookeeper_init(snet.zookeeperAddress, connection_watcher, 2000, 0, 0, 0);

    if (zh == NULL)
    {
        fprintf(stderr, "Error connecting to ZooKeeper server[%d]!\n", errno);
        exit(EXIT_FAILURE);
    }
    sleep(3); /* Sleep a little for connection to complete */
    if (is_connected)
    {

        // CREATE ROOT NODE: /chain
        if (ZNONODE == zoo_exists(zh, "/chain", 0, NULL))
        {
            fprintf(stderr, "/chain does not exist! Creating...\n");
            fflush(stderr);

            if (ZOK != zoo_create(zh, "/chain", "chain node", 11 /*10+1*/, &ZOO_OPEN_ACL_UNSAFE, ZOO_PERSISTENT, NULL, 0 /*no new name because no sequence*/))
            {
                fprintf(stderr, "Error creating znode /chain !\n");
                exit(EXIT_FAILURE);
            }
            printf("/chain created!\n");
            fflush(stdout);
        }

        int chain_concluded = 0;
        for (int i = 0; i < 5; i++)
        {
            sleep(3);
            if (ZNONODE != zoo_exists(zh, "/chain", 0, NULL))
            {
                chain_concluded = 1;
                break;
            }
        }
        if (!chain_concluded)
        {
            fprintf(stderr, "Error creating znode /chain verif.!\n");
            exit(EXIT_FAILURE);
        }

        // CREAT CHILD NODE FOR THIS TREE-SERVER
        int new_path_len = 1024;
        char *new_path = malloc(new_path_len);
        printf("Creating child node /chain/node...\n");
        fflush(stdout);

        char *this_ip_port = malloc((strlen(snet.ip_address) + 1 + strlen(snet.port) + 1) * sizeof(char));
        strcpy(this_ip_port, snet.ip_address);
        strcat(this_ip_port, ":");
        strcat(this_ip_port, snet.port);

        // we dont know what they are doing with this_ip_port, so... no free
        int z_result = zoo_create(zh, "/chain/node", this_ip_port, 17, &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL_SEQUENTIAL, new_path, new_path_len);
        if (ZOK != z_result)
        {
            fprintf(stderr, "Error creating znode /chain/node -> %s !\n", new_path);
            if (z_result == ZNODEEXISTS)
                fprintf(stderr, "ZNODEEXISTS\n");
            if (z_result == ZNOCHILDRENFOREPHEMERALS)
                fprintf(stderr, "ZNOCHILDRENFOREPHEMERALS\n");
            exit(EXIT_FAILURE);
        }
        printf("Ephemeral Sequencial ZNode created! ZNode path: %s\n", new_path);
        zkServerNodePath = new_path;
        // /chain/node0000000
        zkServerNodeId = zkServerNodePath + 7;

        int node_concluded = 0;
        for (int i = 0; i < 5; i++)
        {
            sleep(5);
            if (ZNONODE != zoo_exists(zh, new_path, 0, NULL))
            {
                node_concluded = 1;
                break;
            }
        }
        if (!node_concluded)
        {
            fprintf(stderr, "Error creating znode %s !\n", new_path);
            exit(EXIT_FAILURE);
        }

        // SETUP WATCH FOR /chain CHILDREN
        zoo_string *children_list = (zoo_string *)malloc(sizeof(zoo_string));
        if (ZOK != zoo_wget_children(zh, "/chain", child_watcher, watcher_ctx, children_list))
        {
            fprintf(stderr, "Error setting watch at %s!\n", "/chain");
        }

        sortNodeIds(children_list);
        snet.nextNode = getNextNode(children_list, zkServerNodeId);
        if (snet.nextNode == NULL)
        {
            if (snet.nextNodePath != NULL)
                free(snet.nextNodePath);
            snet.nextNodePath = NULL;
            printf("NEXT NODE: %s\n", "this is TAIL");
        }
        else
        {
            snet.nextNodePath = malloc((7 + strlen(snet.nextNode) + 1) * sizeof(char));
            strcpy(snet.nextNodePath, "/chain/");
            strcat(snet.nextNodePath, snet.nextNode); // /chain/node0000...
            printf("NEXT NODE: %s\n", snet.nextNode);
            printf("NEXT NODE PATH: %s\n", snet.nextNodePath);

            int buffer_len = 1000;
            char *buffer = malloc(1000);
            if (ZOK != zoo_get(zh, snet.nextNodePath, 0, buffer, &buffer_len, NULL))
            {
                printf("Error getting metadata from %s\n", snet.nextNodePath);
                exit(EXIT_FAILURE);
            }
            if (snet.nextServerAddress != NULL)
                free(snet.nextServerAddress);
            snet.nextServerAddress = malloc(strlen(buffer));
            strcpy(snet.nextServerAddress, buffer);
            free(buffer);

            // connect to next node
            if (snet.next_rtree == NULL)
            {
                snet.next_rtree = rtree_connect(snet.nextServerAddress);
            }
            else
            {
                char *str = malloc((strlen(snet.next_rtree->ip_address) + 1 + strlen(snet.next_rtree->port) + 1) * sizeof(char));
                strcpy(str, snet.next_rtree->ip_address);
                strcat(str, "");
                strcat(str, snet.next_rtree->port);
                if (strcmp(str, snet.nextServerAddress) != 0)
                {
                    if (snet.next_rtree != NULL)
                    {
                        free(snet.next_rtree->ip_address);
                        free(snet.next_rtree->port);
                        free(snet.next_rtree);
                    }
                    snet.next_rtree = rtree_connect(snet.nextServerAddress);
                    printf("Next server changed! New connection made.");
                }
            }

            printf("\n");
            // printf("Next server port: %s or numeric %d\n", nextPortStr, nextPort);
            printf("Next server address: %s\n", snet.nextServerAddress);
            printf("Next server ip: %s\n", snet.next_rtree->ip_address);
            printf("Next server port: %s\n", snet.next_rtree->port);
            printf("Next server socketfd: %d\n", snet.next_rtree->socketfd);
        }

        fprintf(stderr, "\n=== znode listing at first watch === [ %s ]", "/chain");
        for (int i = 0; i < children_list->count; i++)
        {
            fprintf(stderr, "\n(%d): %s", i + 1, children_list->data[i]);
        }
        fprintf(stderr, "\n=== done ===\n");
    }

    return tree == NULL ? -1 : 0;
}

/* Liberta toda a memória e recursos alocados pela função tree_skel_init.
 */
void tree_skel_destroy()
{
    if (tree == NULL)
        return;
    tree_destroy(tree);
    free(op_proc.in_progress);
    free(threads_sec);
}

/* Executa uma operação na árvore (indicada pelo opcode contido em msg)
 * e utiliza a mesma estrutura message_t para devolver o resultado.
 * Retorna 0 (OK) ou -1 (erro, por exemplo, árvore nao incializada)
 */
int invoke(struct message_t *msg)
{
    struct data_t *data;
    char **keys;
    struct data_t **values;
    int size, height;
    int i;
    char *key_dup;
    int res;

    if (msg == NULL)
    {
        perror("Mensagem a null");
        return -1;
    }

    if (msg->pb_msg->opcode == MESSAGE_T__OPCODE__OP_SIZE)
    {
        pthread_mutex_lock(&mx_tree);
        size = tree_size(tree);
        pthread_mutex_unlock(&mx_tree);
        printf("Tree size is: %d\n", size);
        msg->pb_msg->opcode++;
        msg->pb_msg->c_type = MESSAGE_T__C_TYPE__CT_RESULT;
        msg->pb_msg->size = size;
        msg->pb_msg->message_data_case = MESSAGE_T__MESSAGE_DATA_SIZE;
        return 0;
    }

    if (msg->pb_msg->opcode == MESSAGE_T__OPCODE__OP_HEIGHT)
    {
        pthread_mutex_lock(&mx_tree);
        height = tree_height(tree);
        pthread_mutex_unlock(&mx_tree);
        printf("Tree height is: %d\n", height);
        msg->pb_msg->opcode++;
        msg->pb_msg->c_type = MESSAGE_T__C_TYPE__CT_RESULT;
        msg->pb_msg->height = height;
        msg->pb_msg->message_data_case = MESSAGE_T__MESSAGE_DATA_HEIGHT;
        return 0;
    }

    if (msg->pb_msg->opcode == MESSAGE_T__OPCODE__OP_DEL)
    {
        printf("Queuing delete: %s\n", msg->pb_msg->key);
        msg->pb_msg->opcode++;
        msg->pb_msg->c_type = MESSAGE_T__C_TYPE__CT_RESULT;
        key_dup = strdup(msg->pb_msg->key);

        pthread_mutex_lock(&mx_queue);
        queue_apppend_request(last_assigned, 0, key_dup, NULL);
        pthread_mutex_unlock(&mx_queue);

        pthread_cond_signal(&cnd_process_request);

        free(msg->pb_msg->key);
        msg->pb_msg->message_data_case = MESSAGE_T__MESSAGE_DATA_OP_N;
        msg->pb_msg->op_n = last_assigned;
        last_assigned++;
        return 0;
    }

    if (msg->pb_msg->opcode == MESSAGE_T__OPCODE__OP_GET)
    {
        printf("Get: %s\n", msg->pb_msg->key);
        msg->pb_msg->opcode++;
        msg->pb_msg->c_type = MESSAGE_T__C_TYPE__CT_VALUE;
        pthread_mutex_lock(&mx_tree);
        data = tree_get(tree, msg->pb_msg->key);
        pthread_mutex_unlock(&mx_tree);
        if (data == NULL)
        {
            msg->pb_msg->opcode = MESSAGE_T__OPCODE__OP_ERROR;
            msg->pb_msg->c_type = MESSAGE_T__C_TYPE__CT_NONE;
            return -1;
        }
        msg->pb_msg->value = malloc(sizeof(DataT));
        data_t__init(msg->pb_msg->value);
        msg->pb_msg->value->datasize = data->datasize;
        msg->pb_msg->value->data.len = data->datasize;
        msg->pb_msg->value->data.data = data->data;
        msg->pb_msg->message_data_case = MESSAGE_T__MESSAGE_DATA_VALUE;
        free(data); // free structure but not inner data pointer
        return 0;
    }

    if (msg->pb_msg->opcode == MESSAGE_T__OPCODE__OP_PUT)
    {
        printf("Put: %s\n", msg->pb_msg->entry->key);
        msg->pb_msg->opcode++;
        msg->pb_msg->c_type = MESSAGE_T__C_TYPE__CT_RESULT;
        data = data_create2(msg->pb_msg->entry->value->data.len, msg->pb_msg->entry->value->data.data); // len or datasize. they are the same
        key_dup = strdup(msg->pb_msg->entry->key);

        pthread_mutex_lock(&mx_queue);
        queue_apppend_request(last_assigned, 1, key_dup, data_dup(data));
        pthread_mutex_unlock(&mx_queue);

        pthread_cond_signal(&cnd_process_request);

        free(data);
        free(msg->pb_msg->entry->value->data.data);
        free(msg->pb_msg->entry->value);
        free(msg->pb_msg->entry->key);

        msg->pb_msg->message_data_case = MESSAGE_T__MESSAGE_DATA_OP_N;
        msg->pb_msg->op_n = last_assigned;
        last_assigned++;

        // pthread_mutex_lock(&mx_tree);
        // printf("Tree size: %d\n", tree_size(tree));
        // pthread_mutex_unlock(&mx_tree);
        return 0;
    }

    if (msg->pb_msg->opcode == MESSAGE_T__OPCODE__OP_GETKEYS)
    {
        printf("GetKeys\n");
        msg->pb_msg->opcode++;
        msg->pb_msg->c_type = MESSAGE_T__C_TYPE__CT_KEYS;
        pthread_mutex_lock(&mx_tree);
        keys = tree_get_keys(tree);
        size = tree_size(tree);
        pthread_mutex_unlock(&mx_tree);
        if (keys == NULL)
        {
            msg->pb_msg->opcode = MESSAGE_T__OPCODE__OP_ERROR;
            msg->pb_msg->c_type = MESSAGE_T__C_TYPE__CT_NONE;
            return -1;
        }
        msg->pb_msg->n_keys = size;
        msg->pb_msg->keys = malloc(sizeof(char *) * size);
        // we need to get rid of the last NULL pointer becuase of protobuf message packing of repeated!
        for (i = 0; i < size; i++)
            msg->pb_msg->keys[i] = keys[i];
        free(keys);
        return 0;
    }

    if (msg->pb_msg->opcode == MESSAGE_T__OPCODE__OP_GETVALUES)
    {
        printf("GetValues\n");
        msg->pb_msg->opcode++;
        msg->pb_msg->c_type = MESSAGE_T__C_TYPE__CT_VALUES;

        pthread_mutex_lock(&mx_tree);
        size = tree_size(tree);
        values = (struct data_t **)tree_get_values(tree);
        pthread_mutex_unlock(&mx_tree);

        if (values == NULL)
        {
            msg->pb_msg->opcode = MESSAGE_T__OPCODE__OP_ERROR;
            msg->pb_msg->c_type = MESSAGE_T__C_TYPE__CT_NONE;
            return -1;
        }
        msg->pb_msg->values = malloc(size * sizeof(DataT));
        for (int i = 0; i < size; i++)
        {
            msg->pb_msg->values[i] = malloc(sizeof(DataT));
            data_t__init(msg->pb_msg->values[i]);
            msg->pb_msg->values[i]->datasize = values[i]->datasize;
            msg->pb_msg->values[i]->data.len = values[i]->datasize;
            msg->pb_msg->values[i]->data.data = values[i]->data;
            free(values[i]); // only wrapper structure
        }
        msg->pb_msg->n_values = size;
        free(values);
        return 0;
    }

    if (msg->pb_msg->opcode == MESSAGE_T__OPCODE__OP_VERIFY)
    {
        printf("Verifyig op_n: %d\n", msg->pb_msg->op_n);
        msg->pb_msg->opcode++;
        msg->pb_msg->c_type = MESSAGE_T__C_TYPE__CT_RESULT;
        res = verify(msg->pb_msg->op_n);
        msg->pb_msg->message_data_case = MESSAGE_T__MESSAGE_DATA_OP_N;
        msg->pb_msg->op_n = res; // 0 if concluded, op_n if not
        return 0;
    }

    return -1;
}

/* Função da thread secundária que vai processar pedidos de escrita.
 */
void *process_request(void *params)
{

    struct thread_params *tpar = (struct thread_params *)params;

#ifdef __DEBUG_THREADS__
    printf("Thread %d starting...\n", tpar->tid);
    fflush(stdout);
#endif

    while (1)
    {
#ifdef __DEBUG_THREADS__
        printf("Thread %d: request wait\n", tpar->tid);
        fflush(stdout);
#endif
        pthread_mutex_lock(&mx_queue); // get exclusive access to queue
        if (queue_size < 0)
            exit(1);            /* underflow */
        while (queue_size == 0) /* block if request queue empty */
            pthread_cond_wait(&cnd_process_request, &mx_queue);

            /* if executing here, queue is not empty so let's remove first request */

#ifdef __DEBUG_THREADS__
        printf("Thread %d: request processing\n", tpar->tid);
        fflush(stdout);
#endif

        struct request_t *local_request = queue_pop_head();
        pthread_mutex_unlock(&mx_queue);

        // put in op_proc.in_progress
        int i_in_progress;
        pthread_mutex_lock(&mx_in_progress);
        for (i_in_progress = 0; i_in_progress < n_threads; i_in_progress++)
        {
            if (op_proc.in_progress[i_in_progress] == 0)
            {
                op_proc.in_progress[i_in_progress] = local_request->op_n;
                break;
            }
        }
        pthread_mutex_unlock(&mx_in_progress);

        if (i_in_progress == n_threads)
        {
            printf("ERROR: no free space in op_proc.in_progress!!!!!");
            exit(1);
        }

        // start processing the request
        // sleep(20); // artificially make the task very computationally expensive

        if (local_request->op == 0)
        { /* delete */

            // propagate to next server
            rtree_del(snet.next_rtree, local_request->key);

            // then execute in this server
            pthread_mutex_lock(&mx_tree);
            if (tree_del(tree, local_request->key) == -1)
            {
                // no way to report error with current infrastructure
            }
            pthread_mutex_unlock(&mx_tree);
        }
        else if (local_request->op == 1)
        { /* put */

            // propagate to next server
            struct entry_t *entry = entry_create(local_request->key, local_request->data);
            rtree_put(snet.next_rtree, entry);
            free(entry); // only outer shell

            // then execute in this server
            pthread_mutex_lock(&mx_tree);
            // printf("thread put [%s] [%s]\n", local_request->key, (char*)local_request->data->data);
            int res = tree_put(tree, local_request->key, local_request->data);
            if (res == -1)
            {
                // no way to report error with current infrastructure
            }
            pthread_mutex_unlock(&mx_tree);
        }
        else
        {
            // printf()
        }

        pthread_mutex_lock(&mx_in_progress);
        if (op_proc.max_proc < op_proc.in_progress[i_in_progress])
        {
            op_proc.max_proc = op_proc.in_progress[i_in_progress];
        }
        op_proc.in_progress[i_in_progress] = 0; // finished processing request
        pthread_mutex_unlock(&mx_in_progress);

#ifdef __DEBUG_THREADS__
        printf("Thread %d: request %d concluded\n", tpar->tid, local_request->op_n);
        fflush(stdout);
#endif

        request_destroy(local_request);
    }
}

/**
 * Receives operation id (op_n) and returns 0 if concluded, op_n otherwise
 * Thread-safe!
 */
int verify(int op_n)
{
    int found_in_queue = 0;
    int found_in_progress = 0;

    // verify queue
    pthread_mutex_lock(&mx_queue);
    found_in_queue = queue_find(op_n);
    pthread_mutex_unlock(&mx_queue);
    if (found_in_queue)
        return op_n;

    // verify in_progress
    pthread_mutex_lock(&mx_in_progress);
    found_in_progress = find_in_progress(op_n);
    pthread_mutex_unlock(&mx_in_progress);
    if (found_in_progress)
        return op_n;

    return 0;
}
