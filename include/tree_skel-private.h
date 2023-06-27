#ifndef _TREE_SKEL_H_PRIVATE_
#define _TREE_SKEL_H_PRIVATE_

#include "client_stub.h"
#include "client_stub-private.h"

struct server_net_t {    
    char* zookeeperAddress;     // ip:port
    char* ip_address;           // this server ip
    char* port;                 // this server port

    char* nextNode;             // zookeeper id of next node
    char* nextNodePath;         // next node full path
    char* nextServerAddress;    // next server ip:port
    struct rtree_t *next_rtree;
};

#endif
