#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "inet.h"

#include "data.h"
#include "entry.h"
#include "client_stub-private.h"
#include "message-private.h"
#include "network_client.h"

/* Função para estabelecer uma associação entre o cliente e o servidor, 
 * em que address_port é uma string no formato <hostname>:<port>.
 * Retorna NULL em caso de erro.
 */
struct rtree_t *rtree_connect(const char *address_port) {
    int iarr[5];
    char hostname[50] = {0};
    char port[50] = {0};
    struct rtree_t *rtree;
    
    if (sscanf(address_port, "%d.%d.%d.%d:%d", &iarr[0], &iarr[1], &iarr[2], &iarr[3], &iarr[4]) != 5) {
        fprintf(stderr, "Error: bad format on address_port. Sould be [ip_address:port] but is: %s\n", address_port);
        return NULL;
    }
    sprintf(hostname, "%d.%d.%d.%d", iarr[0], iarr[1], iarr[2], iarr[3]);
    sprintf(port, "%d", iarr[4]);
    
    rtree = malloc(sizeof(struct rtree_t));
    
    rtree->ip_address = strdup(hostname);
    rtree->port = strdup(port);
    if (network_connect(rtree) == -1) {
        printf("Erro em network_connect!");
        sleep(2);
        if (network_connect(rtree) == -1) { 
            printf("Erro em network_connect pela segunda vez!");
            sleep(2);
            if (network_connect(rtree) == -1) { 
                printf("Erro em network_connect pela terceira vez!");
                return NULL;
            }
        }
    }

    return rtree;
}

/* Termina a associação entre o cliente e o servidor, fechando a 
 * ligação com o servidor e libertando toda a memória local.
 * Retorna 0 se tudo correr bem e -1 em caso de erro.
 */
int rtree_disconnect(struct rtree_t *rtree) {
    free(rtree->ip_address);
    free(rtree->port);
    if (close(rtree->socketfd) != 0) {
        printf("Erro ao fechar socket");
        return -1;
    }
    free(rtree);
    return 0;
}

/* Função para adicionar um elemento na árvore.
 * Se a key já existe, vai substituir essa entrada pelos novos dados.
 * Devolve 0 (ok, em adição/substituição) ou -1 (problemas).
 */
int rtree_put(struct rtree_t *rtree, struct entry_t *entry) {
    if(rtree == NULL || entry == NULL)
        return -1;

    struct message_t *msg_in, *msg_out;
    msg_out = message_init();

    msg_out->pb_msg->opcode = MESSAGE_T__OPCODE__OP_PUT;
    msg_out->pb_msg->c_type = MESSAGE_T__C_TYPE__CT_ENTRY;

    struct entry_t *tmpEntry = entry_dup(entry);
    
    EntryT *pb_entry = malloc(sizeof(EntryT));
    entry_t__init(pb_entry);
    pb_entry->key = tmpEntry->key;
    pb_entry->value = malloc(sizeof(DataT));
    data_t__init(pb_entry->value);
    pb_entry->value->datasize = tmpEntry->value->datasize;
    pb_entry->value->data.len = tmpEntry->value->datasize;
    pb_entry->value->data.data = tmpEntry->value->data;
    msg_out->pb_msg->entry = pb_entry;
    msg_out->pb_msg->message_data_case = MESSAGE_T__MESSAGE_DATA_ENTRY;
    free(tmpEntry->value); // free inner structure only
    free(tmpEntry); // free outer structure only

    msg_in = network_send_receive(rtree, msg_out);
    message_destroy(msg_out);

    if(msg_in == NULL){
        printf("ERROR ON SERVER: DID NOT SEND VALID RESPONSE");
        return -1;
    }
    
    if(msg_in->pb_msg->opcode == MESSAGE_T__OPCODE__OP_ERROR){
        printf("Server reported error on: rtree_put ");
        message_destroy(msg_in);
        return -1;
    }

    if (msg_in->pb_msg->opcode != MESSAGE_T__OPCODE__OP_PUT + 1) {
        printf("Server response with unexpected opcode.\n");
        printf("Expected MESSAGE_T__OPCODE__OP_PUT+1 (%d) but got %d", MESSAGE_T__OPCODE__OP_PUT+1, msg_in->pb_msg->opcode);
        message_destroy(msg_in);
        return -1;
    }
    
    printf("OP_N: %d\n", msg_in->pb_msg->op_n);
    message_destroy(msg_in);
    return 0;
}

/* Função para obter um elemento da árvore.
 * Em caso de erro, devolve NULL.
 */
struct data_t *rtree_get(struct rtree_t *rtree, char *key) {
    if(rtree == NULL || key == NULL)
        return NULL;

    struct message_t *msg_in, *msg_out;
    msg_out = message_init();

    msg_out->pb_msg->opcode = MESSAGE_T__OPCODE__OP_GET;
    msg_out->pb_msg->c_type = MESSAGE_T__C_TYPE__CT_KEY;

    msg_out->pb_msg->key = malloc(strlen(key)+1);
    strcpy(msg_out->pb_msg->key, key);
    msg_out->pb_msg->message_data_case = MESSAGE_T__MESSAGE_DATA_KEY;

    msg_in = network_send_receive(rtree, msg_out);
    message_destroy(msg_out);

    if(msg_in == NULL){
        printf("ERROR ON SERVER: DID NOT SEND VALID RESPONSE");
        return NULL;
    }

    if(msg_in->pb_msg->opcode == MESSAGE_T__OPCODE__OP_ERROR){
        printf("Server reported error on: rtree_get ");
        message_destroy(msg_in);
        return NULL;
    }

    if (msg_in->pb_msg->opcode != MESSAGE_T__OPCODE__OP_GET + 1) {
        printf("Server response with unexpected opcode.\n");
        printf("Expected MESSAGE_T__OPCODE__OP_GET+1 (%d) but got %d", MESSAGE_T__OPCODE__OP_GET+1, msg_in->pb_msg->opcode);
        message_destroy(msg_in);
        return NULL;
    }
    
    struct data_t *data = data_create(msg_in->pb_msg->value->datasize);
    memcpy(data->data, msg_in->pb_msg->value->data.data, data->datasize);

    message_destroy(msg_in);
    return data;
}

/* Função para remover um elemento da árvore. Vai libertar 
 * toda a memoria alocada na respetiva operação rtree_put().
 * Devolve: 0 (ok), -1 (key not found ou problemas).
 */
int rtree_del(struct rtree_t *rtree, char *key) {
    if(rtree == NULL || key == NULL)
        return -1;

    struct message_t *msg_in, *msg_out;
    msg_out = message_init();

    msg_out->pb_msg->opcode = MESSAGE_T__OPCODE__OP_DEL;
    msg_out->pb_msg->c_type = MESSAGE_T__C_TYPE__CT_KEY;

    msg_out->pb_msg->key = malloc(strlen(key)+1);
    strcpy(msg_out->pb_msg->key, key);
    msg_out->pb_msg->message_data_case = MESSAGE_T__MESSAGE_DATA_KEY;

    msg_in = network_send_receive(rtree, msg_out);
    message_destroy(msg_out);

    if(msg_in == NULL){
        printf("ERROR ON SERVER: DID NOT SEND VALID RESPONSE");
        return -1;
    }

    if(msg_in->pb_msg->opcode == MESSAGE_T__OPCODE__OP_ERROR){
        printf("Server reported error on: rtree_del ");
        message_destroy(msg_in);
        return -1;
    }

    if (msg_in->pb_msg->opcode != MESSAGE_T__OPCODE__OP_DEL + 1) {
        printf("Server response with unexpected opcode.\n");
        printf("Expected MESSAGE_T__OPCODE__OP_DEL+1 (%d) but got %d", MESSAGE_T__OPCODE__OP_DEL+1, msg_in->pb_msg->opcode);
        message_destroy(msg_in);
        return -1;
    }
    
    printf("OP_N: %d\n", msg_in->pb_msg->op_n);
    message_destroy(msg_in);
    return 0;
}

/* Devolve o número de elementos contidos na árvore.
 */
int rtree_size(struct rtree_t *rtree) {
    if(rtree == NULL)
        return -1;

    struct message_t *msg_in, *msg_out;
    msg_out = message_init();

    msg_out->pb_msg->opcode = MESSAGE_T__OPCODE__OP_SIZE;
    msg_out->pb_msg->c_type = MESSAGE_T__C_TYPE__CT_NONE;

    msg_in = network_send_receive(rtree, msg_out);
    message_destroy(msg_out);

    if(msg_in == NULL){
        printf("ERROR ON SERVER: DID NOT SEND VALID RESPONSE");
        return -1;
    }

    if(msg_in->pb_msg->opcode == MESSAGE_T__OPCODE__OP_ERROR){
        printf("Server reported error on: rtree_size ");
        message_destroy(msg_in);
        return -1;
    }

    if (msg_in->pb_msg->opcode != MESSAGE_T__OPCODE__OP_SIZE + 1) {
        printf("Server response with unexpected opcode.\n");
        printf("Expected MESSAGE_T__OPCODE__OP_SIZE+1 (%d) but got %d", MESSAGE_T__OPCODE__OP_SIZE+1, msg_in->pb_msg->opcode);
        message_destroy(msg_in);
        return -1;
    }
    
    int size = msg_in->pb_msg->size;
    message_destroy(msg_in);
    return size;
}

/* Função que devolve a altura da árvore.
 */
int rtree_height(struct rtree_t *rtree) {
    if(rtree == NULL)
        return -1;

    struct message_t *msg_in, *msg_out;
    msg_out = message_init();

    msg_out->pb_msg->opcode = MESSAGE_T__OPCODE__OP_HEIGHT;
    msg_out->pb_msg->c_type = MESSAGE_T__C_TYPE__CT_NONE;

    msg_in = network_send_receive(rtree, msg_out);
    message_destroy(msg_out);

    if(msg_in == NULL){
        printf("ERROR ON SERVER: DID NOT SEND VALID RESPONSE");
        return -1;
    }

    if(msg_in->pb_msg->opcode == MESSAGE_T__OPCODE__OP_ERROR){
        printf("Server reported error on: rtree_height ");
        message_destroy(msg_in);
        return -1;
    }

    if (msg_in->pb_msg->opcode != MESSAGE_T__OPCODE__OP_HEIGHT + 1) {
        printf("Server response with unexpected opcode.\n");
        printf("Expected MESSAGE_T__OPCODE__OP_HEIGHT+1 (%d) but got %d", MESSAGE_T__OPCODE__OP_HEIGHT+1, msg_in->pb_msg->opcode);
        message_destroy(msg_in);
        return -1;
    }
    
    int height = msg_in->pb_msg->height;
    message_destroy(msg_in);
    return height;
}

/* Devolve um array de char* com a cópia de todas as keys da árvore,
 * colocando um último elemento a NULL.
 */
char **rtree_get_keys(struct rtree_t *rtree) {
    if(rtree == NULL)
        return NULL;

    struct message_t *msg_in, *msg_out;
    msg_out = message_init();

    msg_out->pb_msg->opcode = MESSAGE_T__OPCODE__OP_GETKEYS;
    msg_out->pb_msg->c_type = MESSAGE_T__C_TYPE__CT_NONE;

    msg_in = network_send_receive(rtree, msg_out);
    message_destroy(msg_out);

    if(msg_in == NULL){
        printf("ERROR ON SERVER: DID NOT SEND VALID RESPONSE");
        return NULL;
    }

    if(msg_in->pb_msg->opcode == MESSAGE_T__OPCODE__OP_ERROR){
        printf("Server reported error on: rtree_getkeys ");
        message_destroy(msg_in);
        return NULL;
    }

    if (msg_in->pb_msg->opcode != MESSAGE_T__OPCODE__OP_GETKEYS + 1) {
        printf("Server response with unexpected opcode.\n");
        printf("Expected MESSAGE_T__OPCODE__OP_GETKEYS+1 (%d) but got %d", MESSAGE_T__OPCODE__OP_GETKEYS+1, msg_in->pb_msg->opcode);
        message_destroy(msg_in);
        return NULL;
    }
    
    char** keys = calloc(msg_in->pb_msg->n_keys + 1, sizeof(char*));
    int i;
    for (i=0; i<msg_in->pb_msg->n_keys; i++) keys[i] = strdup(msg_in->pb_msg->keys[i]);
    message_destroy(msg_in);
    return keys;
}

/* Devolve um array de void* com a cópia de todas os values da árvore,
 * colocando um último elemento a NULL.
 */
void **rtree_get_values(struct rtree_t *rtree) {
    if(rtree == NULL)
        return NULL;

    struct message_t *msg_in, *msg_out;
    msg_out = message_init();

    msg_out->pb_msg->opcode = MESSAGE_T__OPCODE__OP_GETVALUES;
    msg_out->pb_msg->c_type = MESSAGE_T__C_TYPE__CT_NONE;

    msg_in = network_send_receive(rtree, msg_out);
    message_destroy(msg_out);

    if(msg_in == NULL){
        printf("ERROR ON SERVER: DID NOT SEND VALID RESPONSE");
        return NULL;
    }

    if(msg_in->pb_msg->opcode == MESSAGE_T__OPCODE__OP_ERROR){
        printf("Server reported error on: rtree_getvalues ");
        message_destroy(msg_in);
        return NULL;
    }

    if (msg_in->pb_msg->opcode != MESSAGE_T__OPCODE__OP_GETVALUES + 1) {
        printf("Server response with unexpected opcode.\n");
        printf("Expected MESSAGE_T__OPCODE__OP_GETVALUES+1 (%d) but got %d", MESSAGE_T__OPCODE__OP_GETVALUES+1, msg_in->pb_msg->opcode);
        message_destroy(msg_in);
        return NULL;
    }
    
    struct data_t **values = calloc(msg_in->pb_msg->n_values + 1, sizeof(struct data_t*));
    int i;
    for (i=0; i<msg_in->pb_msg->n_values; i++) {
        struct data_t *value = malloc(sizeof(struct data_t));
        value->datasize = msg_in->pb_msg->values[i]->datasize;
        value->data = malloc(value->datasize);
        memcpy(value->data, msg_in->pb_msg->values[i]->data.data, value->datasize);
        values[i] = value;
    }
    message_destroy(msg_in);
    return (void**)values;
}


int rtree_verify(struct rtree_t *rtree, int op_n) {
    if(rtree == NULL)
        return -1;

    struct message_t *msg_in, *msg_out;
    msg_out = message_init();

    msg_out->pb_msg->opcode = MESSAGE_T__OPCODE__OP_VERIFY;
    msg_out->pb_msg->c_type = MESSAGE_T__C_TYPE__CT_RESULT;
    msg_out->pb_msg->message_data_case = MESSAGE_T__MESSAGE_DATA_OP_N;
    msg_out->pb_msg->op_n = op_n;

    msg_in = network_send_receive(rtree, msg_out);
    message_destroy(msg_out);

    if(msg_in == NULL){
        printf("ERROR ON SERVER: DID NOT SEND VALID RESPONSE");
        return -1;
    }

    if(msg_in->pb_msg->opcode == MESSAGE_T__OPCODE__OP_ERROR){
        printf("Server reported error on: rtree_verify ");
        message_destroy(msg_in);
        return -1;
    }

    if (msg_in->pb_msg->opcode != MESSAGE_T__OPCODE__OP_VERIFY + 1) {
        printf("Server response with unexpected opcode.\n");
        printf("Expected MESSAGE_T__OPCODE__OP_VERIFY+1 (%d) but got %d", MESSAGE_T__OPCODE__OP_VERIFY+1, msg_in->pb_msg->opcode);
        message_destroy(msg_in);
        return -1;
    }
    
    if (msg_in->pb_msg->op_n == 0) {
        printf("Operation %d is concluded!", op_n);
    }
    else {
        printf("Operation %d is pending!", op_n);
    }
    
    message_destroy(msg_in);
    return op_n;
}



