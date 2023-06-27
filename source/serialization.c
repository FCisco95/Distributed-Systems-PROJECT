
#include "serialization.h"

/* Serializa todas as keys presentes no array de strings keys
 * para o buffer keys_buf que será alocado dentro da função.
 * O array de keys a passar em argumento pode ser obtido através 
 * da função tree_get_keys. Para além disso, retorna o tamanho do
 * buffer alocado ou -1 em caso de erro.
 */
int keyArray_to_buffer(char **keys, char **keys_buf) {
    int bufferSize = 0;
    
    // determinar memoria necessária
    char* iter = keys;
    while (iter != NULL) {
        bufferSize += strlen(iter) + 1;
        iter++;
    }
    *keys_buf = malloc(bufferSize);

    // copiar as strings para o buffer
    iter = keys;
    char *posDest = *keys_buf; // posição para onde copiar dentro do keys_buf
    int len;
    while (iter != NULL) {
        len = strlen(iter) + 1;
        strcpy(posDest, iter); 
        iter++;
        posDest += (len+1) * sizeof(char);
    }

    return bufferSize;
}

/* De-serializa a mensagem contida em keys_buf, com tamanho
 * keys_buf_size, colocando-a e retornando-a num array char**,
 * cujo espaco em memória deve ser reservado. Devolve NULL
 * em caso de erro.
 */
char** buffer_to_keyArray(char *keys_buf, int keys_buf_size) {
    int numKeys = 0;
    
    int i;
    for (i=0; i<keys_buf_size; i++) {
        if (keys_buf[i] == 0) numKeys++;
    }

    char **keys = malloc(numKeys * sizeof(char*));

    char *posSrc = keys_buf;
    char *iter = keys;
    int i;
    for (i=0; i<numKeys; i++) {
        strcpy(iter, posSrc);
        posSrc += (strlen(iter) + 1) * sizeof(char);
        iter++;
    }

    return keys;
}


// /* Serializa todas as keys presentes no array de strings keys
//  * para o buffer keys_buf que será alocado dentro da função.
//  * O array de keys a passar em argumento pode ser obtido através 
//  * da função tree_get_keys. Para além disso, retorna o tamanho do
//  * buffer alocado ou -1 em caso de erro.
//  */
// int keyArray_to_buffer(char **keys, char **keys_buf) {
//     int bufferSize = 0;
//     in numKeys = 0;
    
//     // determinar memoria necessária
//     char* iter = keys;
//     while (iter != NULL) {
//         bufferSize += strlen(iter) + 1;
//         numKeys++;
//         iter++;
//     }
//     *keys_buf = malloc(bufferSize + sizeof(int));

//     memcpy(*keys_buf, &numKeys, sizeof(int));
    
//     // copiar as strings para o buffer
//     iter = keys;
//     char *posDest = *keys_buf + sizeof(int); // posição para onde copiar dentro do keys_buf
//     int len;
//     while (iter != NULL) {
//         len = strlen(iter) + 1;
//         strcpy(posDest, iter); 
//         iter++;
//         posDest += (len+1) * sizeof(char);
//     }

//     return bufferSize;
// }

// /* De-serializa a mensagem contida em keys_buf, com tamanho
//  * keys_buf_size, colocando-a e retornando-a num array char**,
//  * cujo espaco em memória deve ser reservado. Devolve NULL
//  * em caso de erro.
//  */
// char** buffer_to_keyArray(char *keys_buf, int keys_buf_size) {
//     int numKeys;
    
//     memcpy(&numKeys, keys_buf, sizeof(int));
//     char **keys = malloc(numKeys * sizeof(char*));

//     char *posSrc = keys_buf + sizeof(int);
//     char *iter = keys;
//     int i;
//     for (i=0; i<numKeys; i++) {
//         strcpy(iter, posSrc);
//         posSrc += (strlen(iter) + 1) * sizeof(char);
//         iter++;
//     }

//     return keys;
// }

