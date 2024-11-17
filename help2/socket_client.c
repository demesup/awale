#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#define BUFFER_SIZE 1024

// Function to listen to messages from the server
void *listen_to_server(void *arg) {
    int server_socket = *(int *)arg;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    while ((bytes_received = recv(server_socket, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[bytes_received] = '\0'; // Null-terminate the received message
        printf("%s", buffer); // Display the message
    }

    if (bytes_received == 0) {
        printf("Disconnected from the server.\n");
    } else if (bytes_received < 0) {
        perror("Error receiving data from server");
    }

    close(server_socket);
    exit(0);
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: %s <server_ip> <server_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int sockfd;
    struct sockaddr_in serv_addr;

    // Create a socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Initialize server address
    bzero((char *)&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[2]));

    // Convert and set the server IP address
    if (inet_pton(AF_INET, argv[1], &serv_addr.sin_addr) <= 0) {
        perror("Invalid server IP address");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Connect to the server
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection to server failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Create a thread to listen to server messages
    pthread_t listener_thread;
    if (pthread_create(&listener_thread, NULL, listen_to_server, &sockfd) != 0) {
        perror("Thread creation failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Send commands to the server
    char buffer[BUFFER_SIZE];
    while (1) {
        if (fgets(buffer, BUFFER_SIZE, stdin) == NULL) {
            break;
        }
        if (send(sockfd, buffer, strlen(buffer), 0) < 0) {
            perror("Failed to send command");
            break;
        }
    }

    // Clean up
    pthread_cancel(listener_thread);
    pthread_join(listener_thread, NULL);
    close(sockfd);
    return 0;
}
