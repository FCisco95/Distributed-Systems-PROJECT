#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "inet.h"

#include "sdmessage.pb-c.h"
#include "client_stub-private.h"
#include "message-private.h"
#include "network_client.h"

#define RETRY_TIME 5

/* Esta função deve:
 * - Obter o endereço do servidor (struct sockaddr_in) a base da
 *   informação guardada na estrutura rtree;
 * - Estabelecer a ligação com o servidor;
 * - Guardar toda a informação necessária (e.g., descritor do socket)
 *   na estrutura rtree;
 * - Retornar 0 (OK) ou -1 (erro).
 */
int network_connect(struct rtree_t *rtree) {
struct addrinfo hints, *res, *p;
    int status;
    char ipstr[INET6_ADDRSTRLEN];
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    if ((status = getaddrinfo(rtree->ip_address, rtree->port, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return -1;
    }

#ifdef __DEBUG_GETADDRINFO__
    struct addrinfo *p;
    char ipstr[INET6_ADDRSTRLEN];
    printf("hostname:port = %s:%s\n", hostname, port);
    for(p = res;p != NULL; p = p->ai_next) {
        void *addr;
        char *ipver;

        // get the pointer to the address itself,
        // different fields in IPv4 and IPv6:
        if (p->ai_family == AF_INET) { // IPv4
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
            addr = &(ipv4->sin_addr);
            ipver = "IPv4";
        } else { // IPv6
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)p->ai_addr;
            addr = &(ipv6->sin6_addr);
            ipver = "IPv6";
        }

        // convert the IP to a string and print it:
        inet_ntop(p->ai_family, addr, ipstr, sizeof(ipstr));
        printf("  %s: %s\n", ipver, ipstr);
    }
#endif
    
    // loop through all the results and connect to the first we can
    for(p = res; p != NULL; p = p->ai_next) {
        if ((rtree->socketfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

        if (connect(rtree->socketfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(rtree->socketfd);
            perror("client: connect");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        return -1;
    }

    inet_ntop(p->ai_family, &(((struct sockaddr_in*)p->ai_addr)->sin_addr), ipstr, sizeof(ipstr));
    //printf("client: connecting to %s\n", ipstr);

    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    setsockopt(rtree->socketfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    freeaddrinfo(res); // all done with this structure
    
    return 0;
}

/* Esta função deve:
 * - Obter o descritor da ligação (socket) da estrutura rtree_t;
 * - Serializar a mensagem contida em msg;
 * - Enviar a mensagem serializada para o servidor;
 * - Esperar a resposta do servidor;
 * - De-serializar a mensagem de resposta;
 * - Retornar a mensagem de-serializada ou NULL em caso de erro.
 */
struct message_t *network_send_receive(struct rtree_t * rtree, struct message_t *msg) {
    char *message_out_buf;
    char *packed_message_buf;
	size_t result, out_size;

    uint32_t read_size_netlong;
    uint32_t read_size;

    struct message_t *msg_res;

    // sends message to server
    out_size = message_to_buffer((void*)&message_out_buf, msg);
    
    if (out_size <= 0) {
        perror("Erro na serialização da mensagem!");
        exit(1);
    }
    
    if (write_all(rtree->socketfd, message_out_buf, out_size) != out_size) {
       	perror("Erro no envio da mensagem!");
	
    }
    
    free(message_out_buf);
    
    // next: process response
    
    // read response message size
    result = read_all(rtree->socketfd, (char *) &read_size_netlong, sizeof(uint32_t));

    if(result != sizeof(uint32_t)) {
		//fecha a conexao			
		printf("Erro na conexao com o servidor, aguarde um momento!\n");
		 if (close(rtree->socketfd) != 0) {
        		perror("Erro ao fechar socket");
       			exit(1);
   		 }

		
		//vai dormir
		sleep(RETRY_TIME);
		
		printf("A tentar a Reconexao\n");
	
		//Restabelecer a ligacao apenas uma vez
		if(network_connect(rtree) == -1){
			perror("Erro ao conectar pela a segunda vez! Tente mais tarde");
			exit(1);		
		}

        out_size = message_to_buffer((void*)&message_out_buf, msg);
    
            if (out_size <= 0) {
      		  	perror("Erro na serialização da mensagem!");
        		exit(1);
    		}
    
   		if (write_all(rtree->socketfd, message_out_buf, out_size) != out_size) {
        		perror("Erro no envio da mensagem!");
        		exit(1);
    		}
    
  		 free(message_out_buf);
    
   		// agora vai processar a resposta
    
        // read response message size
        result = read_all(rtree->socketfd, (char *) &read_size_netlong, sizeof(uint32_t));

        if(result != sizeof(uint32_t)) {
            printf("Error reading size of message: %s %d\n", __FILE__, __LINE__);
            return NULL;
        }			
	}	
 		
    // if reaches this point, then managed to send message and read size of response

    read_size = ntohl(read_size_netlong);
    packed_message_buf = malloc(read_size);

    result = read_all(rtree->socketfd, packed_message_buf, read_size);

    if (result != read_size) {
        perror("Error reading message: incorrect size.");
        return NULL;
    }

    msg_res = malloc(sizeof(struct message_t));
    msg_res->pb_msg = message_t__unpack(NULL, read_size, (uint8_t*)packed_message_buf);

    if(msg_res->pb_msg == NULL){
		perror("Error deserializing protobuf message");
        free(packed_message_buf);
		return NULL;
	}
    
    free(packed_message_buf);
    return msg_res;
}


/* A função network_close() fecha a ligação estabelecida por
 * network_connect().
 */
int network_close(struct rtree_t * rtree) {
    return 0;
}

