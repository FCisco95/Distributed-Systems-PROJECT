/*
	Usage: tree-client <ip servidor>:<porta servidor>
	Example: ./tree-client 127.0.0.1:54321
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <unistd.h>

#include "tree.h"
#include "message-private.h"
#include "client_stub-private.h"
#include "network_client.h"

#include <zookeeper/zookeeper.h>

typedef struct String_vector zoo_string;

/*
 * Shows allowed commands
 */
void print_usage() {
    printf("\n");
    printf("COMMANDS:\n");
    printf("    size\n");
    printf("    height\n");
    printf("    del <key>\n");
    printf("    get <key>\n");
    printf("    put <key> <data>\n");
    printf("    getkeys\n");
    printf("    getvalues\n");
    printf("    quit\n");
}

#define MAX_COMMAND_LENGTH 2048

char* zookeeperAddress;
static zhandle_t *zh;
static int is_connected;
static char *watcher_ctx = "ZooKeeper Data Watcher";

char *head_address = NULL, *tail_address = NULL;
struct rtree_t *rtree_head = NULL, *rtree_tail = NULL;

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


char* getNodeMetaData(zhandle_t* zh, const char* nodeId) {
			char* nodePath = malloc((7 + strlen(nodeId) + 1) * sizeof(char));
            strcpy(nodePath, "/chain/");
            strcat(nodePath, nodeId); // /chain/node0000...            

            int buffer_len = 1000;
            char *buffer = malloc(1000);
            if (ZOK != zoo_get(zh, nodePath, 0, buffer, &buffer_len, NULL))
            {
                printf("Error getting metadata from %s\n", nodePath);
                exit(EXIT_FAILURE);
            }
            char* server_address = malloc(strlen(buffer));
            strcpy(server_address, buffer);
            free(buffer);
			return server_address;
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
            char* zk_head = children_list->data[0];
            char* zk_tail = children_list->data[children_list->count-1];
            char* new_head_address = getNodeMetaData(zh, zk_head);
            char* new_tail_address = getNodeMetaData(zh, zk_tail);

            if (head_address == NULL) {
                head_address = strdup(new_head_address);
                rtree_head = rtree_connect(head_address);
            }
            else {
                free(head_address);
                free(rtree_head->ip_address);
                free(rtree_head->port);
                free(rtree_head);
                head_address = strdup(new_head_address);
                rtree_head = rtree_connect(head_address);
                printf("Head server changed! New connection established.");
            }

            if (rtree_head == NULL) {
                printf("ERROR: could not connect to HEAD server.\n");
                exit(1);
            }

            if (tail_address == NULL) {
                tail_address = strdup(new_tail_address);
                rtree_tail = rtree_connect(tail_address);
            }
            else {
                free(tail_address);
                free(rtree_tail->ip_address);
                free(rtree_tail->port);
                free(rtree_tail);
                tail_address = strdup(new_tail_address);
                rtree_tail = rtree_connect(tail_address);
                printf("Tail server changed! New connection established.");
            }

            if (rtree_tail == NULL) {
                printf("ERROR: could not connect to TAIL server.\n");
                exit(1);
            }
            
            fprintf(stdout, "\n=== znode listing === [ %s ]", "/chain");
            for (int i = 0; i < children_list->count; i++)
            {
                fprintf(stdout, "\n(%d): %s", i + 1, children_list->data[i]);
            }
            fprintf(stdout, "\n=== done ===\n");
            }
    }
    free(children_list);
}


/*
 * 
 */
int main(int argc, char **argv) {
    int arguments_ok = 1;
    int i;
    char *cp;
    int iaddrn[5] = {0};
    char command[MAX_COMMAND_LENGTH];
    char *key;
    char *data;
    struct data_t *dat;
    struct entry_t *entry;
    char **keys, **itk;
    struct data_t **values, **itv;
    char *str;
    char *op_n_str;
    int op_n;

	/* Entry arguments */
    if (argc != 2) arguments_ok = 0;
    
    if (arguments_ok) {
        zookeeperAddress = argv[1];
        
        i = sscanf(zookeeperAddress, "%d.%d.%d.%d:%d", &iaddrn[0], &iaddrn[1], &iaddrn[2], &iaddrn[3], &iaddrn[4]);
        if (i !=5 ) arguments_ok = 0;
    }
    
    if (! arguments_ok) {
        printf("USAGE: tree-client <ip_address:port>\n");
        exit(1);
    }

    // setup connection to zookeeper
    zh = zookeeper_init(zookeeperAddress, connection_watcher, 2000, 0, 0, 0);

    if (zh == NULL)
    {
        fprintf(stderr, "Error connecting to ZooKeeper server[%d]!\n", errno);
        exit(EXIT_FAILURE);
    }
    sleep(3); /* Sleep a little for connection to complete */
    if (is_connected)
    {
        // SETUP WATCH FOR /chain CHILDREN
        zoo_string *children_list = (zoo_string *)malloc(sizeof(zoo_string));
        if (ZOK != zoo_wget_children(zh, "/chain", child_watcher, watcher_ctx, children_list))
        {
            fprintf(stderr, "Error setting watch at %s!\n", "/chain");
        }

        sortNodeIds(children_list);
        char* zk_head = children_list->data[0];
        char* zk_tail = children_list->data[children_list->count-1];
        char* new_head_address = getNodeMetaData(zh, zk_head);
        char* new_tail_address = getNodeMetaData(zh, zk_tail);

        if (head_address == NULL) {
            head_address = strdup(new_head_address);
            rtree_head = rtree_connect(head_address);
        }
        else {
            free(head_address);
            free(rtree_head->ip_address);
            free(rtree_head->port);
            free(rtree_head);
            head_address = strdup(new_head_address);
            rtree_head = rtree_connect(head_address);
            printf("Head server changed! New connection established.");
        }

        if (rtree_head == NULL) {
            printf("ERROR: could not connect to HEAD server.\n");
            exit(1);
        }

        if (tail_address == NULL) {
            tail_address = strdup(new_tail_address);
            rtree_tail = rtree_connect(tail_address);
        }
        else {
            free(tail_address);
            free(rtree_tail->ip_address);
            free(rtree_tail->port);
            free(rtree_tail);
            tail_address = strdup(new_tail_address);
            rtree_tail = rtree_connect(tail_address);
            printf("Tail server changed! New connection established.");
        }

        if (rtree_tail == NULL) {
            printf("ERROR: could not connect to TAIL server.\n");
            exit(1);
        }
        
        fprintf(stdout, "\n=== znode listing === [ %s ]", "/chain");
        for (int i = 0; i < children_list->count; i++)
        {
            fprintf(stdout, "\n(%d): %s", i + 1, children_list->data[i]);
        }
        fprintf(stdout, "\n=== done ===\n");
    }

    while (1) {
        memset(command, 0, MAX_COMMAND_LENGTH);
        print_usage();
        printf(">>> ");
        if (fgets(command, MAX_COMMAND_LENGTH, stdin) == NULL) {
            perror("Erro a ler o comando.");
            exit(1);
        }
        
        cp = strtok(command, " \n");
        if (cp == NULL) {
            print_usage();
            continue;
        }
        
        if (strcmp(cp, "size") == 0) {
                i = rtree_size(rtree_tail);
                if (i == -1)
                perror("Error on rtree_size");
            printf("Tree size = %d\n", i);
            continue;
        }

        if (strcmp(cp, "height") == 0) {
                i = rtree_height(rtree_tail);
                if (i == -1)
                perror("Error on rtree_height");
            printf("Tree height = %d\n", i);
            continue;
        }


        if (strcmp(cp, "quit") == 0) {
            return rtree_disconnect(rtree_head) +
                rtree_disconnect(rtree_tail);
        }

        if (strcmp(cp, "getkeys") == 0) {
            keys = rtree_get_keys(rtree_tail);
            if (keys == NULL) perror("Erro no rtree_get_keys");
            itk = keys;
            while (*itk != NULL) {
                printf("%s\n", *itk);
                itk++;
            }
            tree_free_keys(keys);
            continue;
        }

        if (strcmp(cp, "getvalues") == 0) {
            values = (struct data_t**)rtree_get_values(rtree_tail);
            if (values == NULL) perror("Erro no rtree_get_values");
            itv = values;
            while (*itv != NULL) {
                printf("%s\n", as_printable((*itv)->data, (*itv)->datasize));
                itv++;
            }
            tree_free_values((void**)values);
            continue;
        }
        
        if (strcmp(cp, "get") == 0) {
            key = strtok(NULL, "\n");
            if(key == NULL){
                printf("Insira uma key no comando get. Por favor tente de novo! \n");                
                continue;
            }
            else {
                dat = rtree_get(rtree_tail, key);
                if(dat == NULL) {
                    perror("Erro no rtree_get");
                    continue;
                }
                printf("data size = %d\n", dat->datasize);
                str = as_printable(dat->data, dat->datasize);
                printf("%s\n", str);
                free(str);
                data_destroy(dat);
            }
            continue;
        }
        
        if (strcmp(cp, "put") == 0) {
            key = strtok(NULL, " ");
            data = strtok(NULL, "\n");
            if(key == NULL || data == NULL){
                printf("Erro nos dados inseridos, por favor tente de novo! \n");
                continue;
            }else{
                key = strdup(key);
                data = strdup(data);
                dat = data_create2(strlen(data), data);
                if(dat == NULL) {
                    free(key);
                    return -1;
                }
                entry = entry_create(key, dat);
                if(entry == NULL) {
                    free(key);
                    if (dat != NULL) data_destroy(dat);
                    return -1;
                }
                if(rtree_put(rtree_head,entry) == -1)
                    perror("Erro no rtree_put");
                printf("Comando put efectuado com sucesso!");
                entry_destroy(entry);
            }
            continue;
        }
        
        if (strcmp(cp, "del") == 0) {
            key = strtok(NULL , "\n");
            if(key == NULL){
                printf("Insira uma key no comando del. Tente de novo! \n");
                continue;
            }
            else{
                if(rtree_del(rtree_head, key) == -1)
                    perror("Erro no rtree_del");
            }
            continue;
        }

        if (strcmp(cp, "verify") == 0) {
            op_n_str = strtok(NULL , "\n");
            if(op_n_str == NULL){
                printf("Insira um id de operação no comando verify. Tente de novo! \n");
                continue;
            }
            else{

                if (sscanf(op_n_str, "%d", &op_n) != 1) {
                    printf("O id de operação tem que ser um número. Tente de novo! \n");
                    continue;
                }

                if(rtree_verify(rtree_tail, op_n) == -1)
                    perror("Erro no rtree_verify");
            }
            continue;
        }

        printf("Comando nao reconhecido, por favor tente de novo! \n");
    }

    exit(0);
}


