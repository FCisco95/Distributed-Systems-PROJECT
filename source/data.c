#include <stdlib.h>
#include <string.h>
#include "data.h"

/* Função que cria um novo elemento de dados data_t, reservando a memória
 * necessária para armazenar os dados, especificada pelo parâmetro size 
 */
struct data_t *data_create(int size) {
    if (size<=0) return NULL;
    struct data_t *new_data = malloc(sizeof(struct data_t));
    new_data->datasize = size;
    new_data->data = malloc(size);
    return new_data;
}

/* Função que cria um novo elemento de dados data_t, inicializando o campo
 * data com o valor passado no parâmetro data, sem necessidade de reservar
 * memória para os dados.
 */
struct data_t *data_create2(int size, void *data) {
    if (size<=0) return NULL;
    if (data==NULL) return NULL;
    struct data_t *new_data = malloc(sizeof(struct data_t));
    new_data->datasize = size;
    new_data->data = data;
    return new_data;
} 

/* Função que elimina um bloco de dados, apontado pelo parâmetro data,
 * libertando toda a memória por ele ocupada.
 */
void data_destroy(struct data_t *data) {
    if (data==NULL) return;
    free(data->data);
    free(data);
}

/* Função que duplica uma estrutura data_t, reservando toda a memória
 * necessária para a nova estrutura, inclusivamente dados.
 */
struct data_t *data_dup(struct data_t *data) {
    if (data==NULL) return NULL;
    if (data->datasize<0) return NULL;
    if (data->data==NULL) return NULL;
    struct data_t *new_data = malloc(sizeof(struct data_t));
    new_data->datasize = data->datasize;
    new_data->data = malloc(data->datasize);
    // struct data_t *new_data = data_create(data->datasize);
    memcpy(new_data->data, data->data, data->datasize);
    return new_data;
}

/* Função que substitui o conteúdo de um elemento de dados data_t.
*  Deve assegurar que destroi o conteúdo antigo do mesmo.
*/
void data_replace(struct data_t *data, int new_size, void *new_data) {
    if (data==NULL || new_size<0 || new_data==NULL) return;
    if (data->data != NULL) free(data->data);
    data->datasize = new_size;
    data->data = new_data;
}

