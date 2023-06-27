
#include "inet.h"
#include "tree_skel.h"
#include "message-private.h"

#include <poll.h>

#include "network_server.h"
#include "network_server-private.h"

#define BACKLOG 40 /* how many pending connections queue will hold */
#define NFDESC 600 // Número de sockets (uma para listening)
#define TIMEOUT 1000 // em milisegundos

int continue_loop = 1;

/* Função para preparar uma socket de receção de pedidos de ligação
 * num determinado porto.
 * Retornar descritor do socket (OK) ou -1 (erro).
 */
int network_server_init(short port) {
    struct addrinfo hints, *res, *rp;
    int status, sfd;
    char sport[10];
    int yes = 1;
    
    sprintf(sport, "%d", port);
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; /*fill in host IP*/
    
    if ((status = getaddrinfo(NULL, sport, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return -1;
    }
    
    /* 
     * DO MANUAL DE getaddrinfo:
     * getaddrinfo() returns a list of address structures.
      Try each address until we successfully bind(2).
      If socket(2) (or bind(2)) fails, we (close the socket
      and) try the next address. */
    
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1)
            continue; /* failed */
           
        if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0)
            break; /* Success */

        close(sfd);
    }

    if (rp == NULL) {
        fprintf(stderr, "ERROR: could not bind!");
        return -1;
    }
    
    freeaddrinfo(res);   /* No longer needed */
    
    if (listen(sfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }
    
    return sfd; /* não era o que estava no comentário do cabeçalho, mas é o que tem que ser */
}

/* Esta função deve:
 * - Aceitar uma conexão de um cliente;
 * - Receber uma mensagem usando a função network_receive;
 * - Entregar a mensagem de-serializada ao skeleton para ser processada;
 * - Esperar a resposta do skeleton;
 * - Enviar a resposta ao cliente usando a função network_send.
 */
int network_main_loop(int listening_socket) {
    socklen_t sin_size;
    struct sockaddr_storage their_addr; // connector's address information
    int socket_de_cliente;
    struct message_t *message;
    char ipstr[INET6_ADDRSTRLEN];
    char *buffer;
    size_t buf_size=0;
    struct pollfd connections[NFDESC];
    char ipstr_ref[NFDESC][INET6_ADDRSTRLEN];
    int i, nfds, kfds, lfds, iconn;
   
    struct pollfd default_pollfd;
    default_pollfd.fd = -1;
    default_pollfd.events = POLLIN;
    default_pollfd.revents = 0;
    
    nfds = 0;
    
    for (i=0;i<NFDESC;i++)
        connections[i] = default_pollfd;
    
    // inserir socket do servidor na lista de sockets a sondar
    connections[0].fd = listening_socket;
    nfds++;
    
    printf("BEGIN SERVER MAIN LOOP\n");
    //int last_kfds = -1;
    
    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;

	while ((kfds = poll(connections, nfds, TIMEOUT)) >= 0 && continue_loop) { // kfds == 0 significa timeout sem eventos
//        if (kfds != last_kfds || kfds == 0) {
//        if (kfds != last_kfds) {
//            printf("LOOP kfds = %d\n", kfds);
//            printf("LOOP nfds = %d\n", nfds);
////            for (i=0;i<NFDESC;i++) printf("%2d ", i);
////                printf("\n");
//            for (i=0;i<NFDESC;i++) printf("%2d ", connections[i].fd); 
//                printf("\n");
//            last_kfds = kfds;
//        }
        
        
        if (kfds>0) { // então há fds para ler
            lfds = 0;
            for (iconn=0; iconn<NFDESC; iconn++) {
                if (lfds>=kfds) break;
//                printf("outer iconn = %d    lfds = %d\n", iconn, lfds);
                
                if (connections[iconn].revents & POLLIN) {
                    lfds++; // contar os lidos
                    
//                    printf("inner iconn = %d    lfds = %d\n", iconn, lfds);
                    
                    if (iconn==0) {  // socket de escuta do servidor
                        if (nfds<NFDESC) {  // ainda há espaço no array para fds
                            sin_size = sizeof their_addr;
                            socket_de_cliente = accept(listening_socket, (struct sockaddr *)&their_addr, &sin_size);
                            setsockopt(socket_de_cliente, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
                            if (socket_de_cliente == -1) {
                                perror("accept");
                                continue;
                            }
                            // procura posição vazia e adicionar fd
                            for (i=1;i<NFDESC;i++) {
                                if (connections[i].fd == -1) {
                                    connections[i] = default_pollfd;
                                    connections[i].fd = socket_de_cliente;
                                    break;
                                }
                            }
                            if (i==NFDESC) {
                                fprintf(stderr, "FATALITY!: não encontrou posição livre quando devia existir uma!\n");
                                exit(1);
                            }
                            
                            inet_ntop(their_addr.ss_family, &(((struct sockaddr_in*)&their_addr)->sin_addr), ipstr, sizeof ipstr);
//                            printf("server: got connection from %s\n", ipstr);
                            strcpy(ipstr_ref[iconn], ipstr);
                            nfds++;
                        }
                        continue;
                    }
                    
                    // se chega aqui, então é um socket de cliente
                    
                    //ler e processar mensagem do cliente
                    printf("begin read from %s\n", ipstr_ref[iconn]);
                    socket_de_cliente = connections[iconn].fd;
                    message = network_receive(socket_de_cliente);
                    if(message == NULL) {
                        printf("closing client socket por não ter enviado nada ou  mensagem inválida\n");
                        close(socket_de_cliente);
                        connections[iconn] = default_pollfd;
                        nfds--;
                        continue;
                    } else {/* processamento da requisição e da resposta */

                        if (invoke(message) == -1) {
                            printf("Error in invoke!");
                        }

                        // send response message
                        
                        buf_size = message_to_buffer((void *)&buffer, message);
                        printf("begin write\n");
                        if (write_all(socket_de_cliente, buffer, buf_size) != buf_size) {
                            perror("Erro no envio da resposta.");
                            free(buffer);
                            printf("closing client socket por haver erro no envio da resposta\n");
                            connections[iconn] = default_pollfd;
                            close(socket_de_cliente);
                            nfds--;
                            continue;
                        }
                        free(buffer);
                    }
                }                
            }
        }
        
    }
        
    return 0;
}


/* Esta função deve:
 * - Ler os bytes da rede, a partir do client_socket indicado;
 * - De-serializar estes bytes e construir a mensagem com o pedido,
 *   reservando a memória necessária para a estrutura message_t.
 */
struct message_t *network_receive(int client_socket) {
    uint32_t read_size_netlong;
    uint32_t read_size;
    char *packed_message_buf;
    struct message_t *msg;
    int result;
    
	result = read_all(client_socket, (char *) &read_size_netlong, sizeof(uint32_t));

	if(result != sizeof(uint32_t)) {
    	printf("Error reading size of message. %s %d\n", __FILE__, __LINE__);
		return NULL;
	}
    
    read_size = ntohl(read_size_netlong);
    packed_message_buf = malloc(read_size);

    result = read_all(client_socket, packed_message_buf, read_size);

    if (result != read_size) {
        perror("Error reading message: incorrect size.");
        return NULL;
    }

    msg = malloc(sizeof(struct message_t));
    msg->pb_msg = message_t__unpack(NULL, read_size, (uint8_t*)packed_message_buf);
    
    return msg;
}


/* Esta função deve:
 * - Serializar a mensagem de resposta contida em msg;
 * - Libertar a memória ocupada por esta mensagem;
 * - Enviar a mensagem serializada, através do client_socket.
 */
int network_send(int client_socket, struct message_t *msg) {
    void *message_out_buf;
	size_t result, out_size;

    if(msg == NULL || msg->pb_msg == NULL){
        return -1;
    }

    out_size = message_to_buffer(&message_out_buf, msg);

    result = write_all(client_socket, message_out_buf, out_size);
    if(result != out_size){
        perror("Erro ao escrever a mensagem");
        close(client_socket);
        return -1;
    }
   
    free(message_out_buf);
    message_destroy(msg);
   
    return 0;
}

/* A função network_server_close() liberta os recursos alocados por
 * network_server_init().
 */
int network_server_close();


char* getIPv4()
{
    struct ifaddrs *ifaddr, *ifa;
    int s;
    char host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1)
    {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == NULL)
            continue;

        s=getnameinfo(ifa->ifa_addr,sizeof(struct sockaddr_in),host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

        //printf("Interface: %s\n", ifa->ifa_name);

        if( (strcmp(ifa->ifa_name,"eth0")==0 || strcmp(ifa->ifa_name,"enp0s3")==0) && ifa->ifa_addr->sa_family==AF_INET)
        {
            if (s != 0)
            {
                printf("getnameinfo() failed: %s\n", gai_strerror(s));
                exit(EXIT_FAILURE);
            }
            printf("\tInterface : <%s>\n",ifa->ifa_name );
            printf("\t  Address : <%s>\n", host);
            char* hostStr =  malloc((strlen(host)+1) * sizeof(char));
            strcpy(hostStr, host);
            return hostStr;
        }
    }
    freeifaddrs(ifaddr);
    return NULL;
}
