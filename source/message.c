#include "inet.h"
#include "message-private.h"


size_t write_all(int sock, char *buf, size_t len) {
    int bufsize = len;
	int result;

	while(len > 0) {
		result = write(sock, buf, len);

		if(result<0) {
			perror("Erro no write_all");
			return result;
		}

		buf+= result;
		len-= result;
	}

	return bufsize;
}


size_t read_all(int sock, char *buf, size_t len) {
    int bufsize = len;
	int result;

	while(len > 0) {
		result = read(sock, buf, len);

		if(result == 0) 
			return result;

		if(result<0) {
			perror("Erro no read_all");
			return result;
		}

		buf += result;
		len -= result;
	}

	return bufsize;
}

struct message_t * message_init() {
    struct message_t *msg = malloc(sizeof(struct message_t));
    msg->pb_msg = malloc(sizeof(MessageT));
    message_t__init(msg->pb_msg);
    return msg;
}


void message_destroy(struct message_t *msg) {
    message_t__free_unpacked(msg->pb_msg, NULL);
    free(msg);
}

/// @brief Receives a buffer reference and allocates it. Serializes message content with format 
/// [[uint32_t length][bytes (of length)]] so that the reader knows how much to read to get the complete message.
/// @param buffer_ref will be allocated in this function
/// @param msg 
/// @return allocated size
size_t message_to_buffer(void **buffer_ref, struct message_t *msg) {
    size_t packed_message_size;
    void * packed_message_buf;

    if(msg == NULL || msg->pb_msg == NULL) return 0;
    
    packed_message_size = message_t__get_packed_size(msg->pb_msg);
    packed_message_buf = malloc(packed_message_size);
    message_t__pack(msg->pb_msg, packed_message_buf);

    void *mbuff;
    void *it = NULL;
    
    size_t size = sizeof(uint32_t) + packed_message_size;
    mbuff = malloc(size);
    it = mbuff;
    
    // put size in buffer
    *((uint32_t *)it) = htonl(packed_message_size);
    it = it + sizeof(uint32_t);

    // put data in buffer
    memcpy(it, packed_message_buf, packed_message_size);

    *buffer_ref = mbuff;
    return size;
}


char *as_printable(const void *str, const int len) {
    int i;
    char *out = calloc(sizeof(char), len+1);
    memcpy(out, str, len);
    for (i=0; i<len; i++) {
        if (out[i]<20 || out[i]>126) out[i] = '.';
    }
    return out;
}
