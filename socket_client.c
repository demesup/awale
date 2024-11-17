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
#define MAX_PSEUDO_LEN 10
#define MAX_PASSWORD_LEN 10

int logged_in = 0;

/** PROTOTYPES */
int contains_space(const char *str);
void read_line(char *prompt, char *buffer, size_t size);
int send_message(int sockfd, const char *message);
int login_or_register(int server_socket);
void *listen_to_server(void *arg);
void *handle_user_commands(int server_socket);

/** CODE */

// Function to check if a string contains spaces
int contains_space(const char *str) {
    for (size_t i = 0; i < strlen(str); i++) {
        if (str[i] == ' ') {
            return 1;
        }
    }
    return 0;
}

void read_line(char *prompt, char *buffer, size_t size) {
    printf("%s\n", prompt);
    memset(buffer, 0, size);

    ssize_t read = 0;
    while (1) {
        read = getline(&buffer, &size, stdin);
        if (read != -1) {
            buffer[strlen(buffer) - 1] = '\0';
            break;
        }
    }
}

int send_message(int sockfd, const char *message) {
    send(sockfd, message, strlen(message), 0);
}

// Function to handle login or registration
int login_or_register(int server_socket) {
    char pseudo[MAX_PSEUDO_LEN + 1];
    char password[MAX_PASSWORD_LEN + 1];
    char buffer[BUFFER_SIZE];
    char choice[1];

    while (!logged_in) {
        read_line("Choose an option:\n1. Login\n2. Register", choice, BUFFER_SIZE);
        if (strcmp("1", choice) != 0 && strcmp("2", choice) != 0) {
            printf("Invalid choice. Please enter 1 or 2.\n");
            continue;
        }

        read_line("Enter pseudo (max 10 characters, no spaces): ", pseudo, MAX_PSEUDO_LEN + 1);
        if (strlen(pseudo) > MAX_PSEUDO_LEN || contains_space(pseudo)) {
            printf("Invalid pseudo. Ensure it has no spaces and is at most %d characters.\n", MAX_PSEUDO_LEN);
            continue;
        }

        read_line("Enter password (max 10 characters, no spaces): ", password, MAX_PASSWORD_LEN);
        if (strlen(password) > MAX_PASSWORD_LEN || contains_space(password)) {
            printf("Invalid password. Ensure it has no spaces and is at most %d characters.\n", MAX_PASSWORD_LEN);
            continue;
        }

        // Send login or registration request to the server
        if (strcmp("1", choice) == 0) {
            snprintf(buffer, BUFFER_SIZE, "LOGIN %s %s\n", pseudo, password);
        } else {
            snprintf(buffer, BUFFER_SIZE, "REGISTER %s %s\n", pseudo, password);
        }

        if (send_message(server_socket, buffer) < 0) {
            perror("Failed to send login/register request");
        }

        // Wait until login is successful before proceeding
        usleep(100);  // Sleep for a short time before checking again
    }

    // Once logged in, proceed to handle user commands
    printf("Login successful! Proceeding to command handling...\n");
}

// Function to listen to messages from the server
void *listen_to_server(void *arg) {
    int server_socket = *(int *)arg;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    while ((bytes_received = recv(server_socket, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[bytes_received] = '\0'; // Null-terminate the received message
        buffer[strlen(buffer) - 1] = '\0'; // Removing any trailing newlines

        printf("Received from server: |%s|\n", buffer);

        // Check for successful login message
        if (strcmp(buffer, "Login successful!") == 0) {
            logged_in = 1;  // Set login status to true
        } else {
            printf("%s", buffer); // Display any other server messages
        }
    }

    if (bytes_received == 0) {
        printf("Disconnected from the server.\n");
    } else if (bytes_received < 0) {
        perror("Error receiving data from server");
    }

    exit(0);  // Close the application if the connection is lost
    return NULL;
}

// Function to handle user commands
void *handle_user_commands(int server_socket) {
    char buffer[BUFFER_SIZE];
    char formatted_command[BUFFER_SIZE];

    printf("You can now issue commands.\n");

    if (!logged_in) {
        printf("You must be logged in to issue commands.\n");
        return NULL; // Exit if not logged in
    }

    while (1) {
        if (fgets(buffer, BUFFER_SIZE, stdin) == NULL) {
            break;
        }

        // Remove trailing newline from input
        buffer[strcspn(buffer, "\n")] = '\0';

        // Format the command based on input
        if (strcmp(buffer, "/exit") == 0) {
            snprintf(formatted_command, BUFFER_SIZE, "LOGOUT\n");
        } else if (strcmp(buffer, "/online") == 0) {
            snprintf(formatted_command, BUFFER_SIZE, "ONLINE\n");
        } else if (strcmp(buffer, "/players") == 0) {
            snprintf(formatted_command, BUFFER_SIZE, "PLAYERS\n");
        } else if (strcmp(buffer, "/games") == 0) {
            snprintf(formatted_command, BUFFER_SIZE, "GAMES\n");
        } else if (strncmp(buffer, "/obs ", 5) == 0) {
            snprintf(formatted_command, BUFFER_SIZE, "OBSERVE %s\n", buffer + 5);
        } else if (strcmp(buffer, "/bio") == 0) {
            snprintf(formatted_command, BUFFER_SIZE, "BIO\n");
        } else if (strncmp(buffer, "/bio ", 5) == 0) {
            snprintf(formatted_command, BUFFER_SIZE, "BIO %s\n", buffer + 5);
        } else if (strncmp(buffer, "/update ", 8) == 0) {
            snprintf(formatted_command, BUFFER_SIZE, "UPDATE_BIO %s\n", buffer + 8);
        } else if (strncmp(buffer, "/gl ", 4) == 0) {
            snprintf(formatted_command, BUFFER_SIZE, "GLOBAL %s\n", buffer + 4);
        } else if (strncmp(buffer, "/msg ", 5) == 0) {
            snprintf(formatted_command, BUFFER_SIZE, "MESSAGE %s\n", buffer + 5);
        } else if (strncmp(buffer, "/gmsg ", 6) == 0) {
            snprintf(formatted_command, BUFFER_SIZE, "MESSAGE %s\n", buffer + 6);
        } else if (strncmp(buffer, "/m ", 3) == 0) {
            snprintf(formatted_command, BUFFER_SIZE, "MOVE %s\n", buffer + 3);
        } else if (strcmp(buffer, "/end") == 0) {
            snprintf(formatted_command, BUFFER_SIZE, "END\n");
        } else if (strcmp(buffer, "/leave") == 0) {
            snprintf(formatted_command, BUFFER_SIZE, "LEAVE\n");
        } else {
            printf("Unknown command: %s\n", buffer);
            continue;
        }

        // Send the formatted command to the server
        if (send(server_socket, formatted_command, strlen(formatted_command), 0) < 0) {
            perror("Failed to send command");
            break;
        }
    }

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
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[2]));

    // Convert and set the server IP address
    if (inet_pton(AF_INET, argv[1], &serv_addr.sin_addr) <= 0) {
        perror("Invalid server IP address");
        exit(EXIT_FAILURE);
    }

    // Connect to the server
    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection to server failed");
        exit(EXIT_FAILURE);
    }

    printf("Connected to the server.\n");

    pthread_t listener_thread;
    if (pthread_create(&listener_thread, NULL, listen_to_server, &sockfd) != 0) {
        perror("Thread creation failed");
        exit(EXIT_FAILURE);
    }

    // Handle login or registration
    login_or_register(sockfd);


    handle_user_commands(sockfd);

    pthread_join(listener_thread, NULL);
    // Create a thread to listen to server messages


    // Create a thread to handle user commands


    // Wait for threads to finish

}
