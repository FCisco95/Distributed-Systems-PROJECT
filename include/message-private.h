#ifndef _MESSAGE_PRIVATE_H
#define _MESSAGE_PRIVATE_H

#include "sdmessage.pb-c.h"

// wrapper for MessageT because theres no way to alias with "struct"
struct message_t {
    MessageT *pb_msg;
};


size_t write_all(int sock, char *buf, size_t len);

size_t read_all(int sock, char *buf, size_t len);

struct message_t * message_init();

void message_destroy(struct message_t *msg);

/// @brief Receives a buffer reference and allocates it. Serializes message content with format 
/// [[uint32_t length][bytes (of length)]] so that the reader knows how much to read to get the complete message.
/// @param buffer_ref will be allocated in this function
/// @param msg 
/// @return allocated size
size_t message_to_buffer(void **buffer_ref, struct message_t *msg);

char *as_printable(const void *str, const int len);

#endif

