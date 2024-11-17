/* Serveur sockets TCP - Multithreaded
 * Handles multiple clients simultaneously.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#define MAX_CLIENTS 10

void* client_handler(void* client_socket);

int main(int argc, char** argv) {
    int sockfd, newsockfd, clilen;
    struct sockaddr_in cli_addr, serv_addr;

    if (argc != 2) {
        printf("Usage: socket_server port\n");
        exit(0);
    }

    printf("Server starting...\n");

    /* Open the socket */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        printf("Unable to open socket\n");
        exit(0);
    }

    /* Initialize parameters */
    bzero((char*) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));

    /* Perform the bind */
    if (bind(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0) {
        printf("Unable to bind\n");
        exit(0);
    }

    /* Initialize listening */
    listen(sockfd, MAX_CLIENTS);

    printf("Waiting for client connections...\n");

    while (1) {
        /* Accept a client connection */
        clilen = sizeof(cli_addr);
        newsockfd = accept(sockfd, (struct sockaddr*) &cli_addr, &clilen);
        if (newsockfd < 0) {
            printf("Accept error\n");
            continue; // Continue to accept other clients
        }

        printf("Connection accepted from %s:%d\n",
               inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));

        /* Create a new thread for each client */
        pthread_t thread_id;
        int* pclient = malloc(sizeof(int)); // Allocate memory for the client socket
        *pclient = newsockfd;

        if (pthread_create(&thread_id, NULL, client_handler, pclient) != 0) {
            printf("Failed to create thread\n");
            close(newsockfd);
            free(pclient);
        }

        /* Detach the thread to clean up resources when it exits */
        pthread_detach(thread_id);
    }

    close(sockfd);
    return 0;
}

/* Client handler function */
void* client_handler(void* client_socket) {
    int sock = *(int*)client_socket;
    free(client_socket); // Free allocated memory for the socket descriptor

    char c;
    while (read(sock, &c, 1) > 0) {
        printf("Received: %c\n", c);
    }

    printf("Client disconnected\n");
    close(sock);
    return NULL;
}
