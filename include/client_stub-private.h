
#ifndef _CLIENT_STUB_PRIVATE_H
#define _CLIENT_STUB_PRIVATE_H

#include "client_stub.h"

/* Remote tree. A definir pelo grupo em client_stub-private.h
 */
struct rtree_t {
    char *ip_address;
    char *port;
    int socketfd;
};

int rtree_verify(struct rtree_t *rtree, int op_n);

#endif
