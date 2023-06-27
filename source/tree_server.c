#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include "network_server.h"
#include "network_server-private.h"

#include "tree_skel-private.h"

#define LOG_SIZE 10000

extern struct server_net_t snet;

void exit_cleanly(int i) {
    printf("FIM!\n\n");    
    continue_loop = 0;
    exit(1);
}

int main(int argc, char **argv){
    short port;
    int socket_de_escuta;
    
    signal(SIGINT, exit_cleanly);

	/* Testar os argumentos de entrada */
    int arguments_ok = 1;
    
    if (argc != 3) arguments_ok = 0;
     
    if (arguments_ok) {
        if (sscanf(argv[1], "%hd", &port) != 1) arguments_ok = 0;
        snet.port = argv[1];
        snet.zookeeperAddress = argv[2];
    }
    
    if (! arguments_ok) {
        printf("USAGE: tree_server <port> <zookeeper address: ip:port>\n");
        exit(1);
    }

	/* inicialização da camada de rede */
	if ((socket_de_escuta = network_server_init(port)) < 0){
	       	return -1;
	}
	
    snet.ip_address = getIPv4();
    printf("snet.ip_address = %s\n", snet.ip_address);

    tree_skel_init();

	network_main_loop(socket_de_escuta);
	
    exit(1);
}
	
	
	
	
	
