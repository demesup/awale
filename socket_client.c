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

void handle_help();

void handle_exit(int server_socket);

void handle_online(int server_socket);

void handle_players(int server_socket);

void handle_games(int server_socket);

void handle_obs(int server_socket);

void handle_bio(int server_socket);

void handle_view_bio(int server_socket);

void handle_update_bio(int server_socket);

void handle_global_message(int server_socket);

void handle_direct_message(int server_socket);

void handle_make_move(int server_socket);

void handle_end_game(int server_socket);

void handle_leave_game(int server_socket);



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
    char buffer[BUFFER_SIZE];
    char pseudo[MAX_PSEUDO_LEN + 1];
    char password[MAX_PASSWORD_LEN + 1];
    char command[BUFFER_SIZE];
    char *token;
    const char *prompt =
            "Enter your command:\n"
            "/l <pseudo> <password> for login\n"
            "/r <pseudo> <password> for registration\n";
    while (!logged_in) {

        read_line((char *) prompt, buffer, BUFFER_SIZE);

        // Tokenize the input
        token = strtok(buffer, " ");
        if (token == NULL) {
            printf("Invalid command format. Please try again.\n");
            continue;
        }

        // Check if it's a login or register command
        if (strcmp(token, "/l") == 0 || strcmp(token, "/r") == 0) {
            // Extract pseudo
            token = strtok(NULL, " ");
            if (token == NULL || strlen(token) > MAX_PSEUDO_LEN || contains_space(token)) {
                printf("Invalid pseudo. Ensure it has no spaces and is at most %d characters.\n", MAX_PSEUDO_LEN);
                continue;
            }
            strncpy(pseudo, token, MAX_PSEUDO_LEN);
            pseudo[MAX_PSEUDO_LEN] = '\0';

            // Extract password
            token = strtok(NULL, " ");
            if (token == NULL || strlen(token) > MAX_PASSWORD_LEN || contains_space(token)) {
                printf("Invalid password. Ensure it has no spaces and is at most %d characters.\n", MAX_PASSWORD_LEN);
                continue;
            }
            strncpy(password, token, MAX_PASSWORD_LEN);
            password[MAX_PASSWORD_LEN] = '\0';

            // Check for additional tokens (invalid command)
            if (strtok(NULL, " ") != NULL) {
                printf("Too many arguments. Command should only contain pseudo and password.\n");
                continue;
            }

            // Send login or registration request
            if (strcmp(buffer, "/l") == 0) {
                snprintf(command, BUFFER_SIZE, "LOGIN %s %s\n", pseudo, password);
            } else {
                snprintf(command, BUFFER_SIZE, "REGISTER %s %s\n", pseudo, password);
            }

            if (send_message(server_socket, command) < 0) {
                perror("Failed to send login/register request");
                continue;
            }

            // Wait until login is successful before proceeding
            usleep(200);  // Sleep for a short time before checking again
        } else {
            printf("Unknown command. Please use /l for login or /r for registration.\n");
        }
    }

    // Once logged in, proceed to handle user commands
    printf("Login successful! Proceeding to command handling...\n");

}

// Function to listen to messages from the server
void *listen_to_server(void *arg) {
    int server_socket = *(int *) arg;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    while ((bytes_received = recv(server_socket, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[bytes_received] = '\0'; // Null-terminate the received message
        buffer[strlen(buffer) - 1] = '\0'; // Removing any trailing newlines

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

    printf("You can now issue commands. Type /help to see available commands.\n");

    while (1) {
        read_line("", buffer, BUFFER_SIZE);

        if (strcmp(buffer, "/help") == 0) {
            handle_help();
        } else if (strcmp(buffer, "/exit") == 0) {
            handle_exit(server_socket);
        } else if (strcmp(buffer, "/online") == 0) {
            handle_online(server_socket);
        } else if (strcmp(buffer, "/players") == 0) {
            handle_players(server_socket);
        } else if (strcmp(buffer, "/games") == 0) {
            handle_games(server_socket);
        } else if (strcmp(buffer, "/obs") == 0) {
            handle_obs(server_socket);
        } else if (strcmp(buffer, "/bio") == 0) {
            handle_bio(server_socket);
        } else if (strncmp(buffer, "/bio ", 5) == 0) {
            handle_view_bio(server_socket);
        } else if (strncmp(buffer, "/update ", 8) == 0) {
            handle_update_bio(server_socket);
        } else if (strncmp(buffer, "/gl ", 4) == 0) {
            handle_global_message(server_socket);
        } else if (strncmp(buffer, "/msg ", 5) == 0) {
            handle_direct_message(server_socket);
        } else if (strncmp(buffer, "/m ", 3) == 0) {
            handle_make_move(server_socket);
        } else if (strcmp(buffer, "/end") == 0) {
            handle_end_game(server_socket);
        } else if (strcmp(buffer, "/leave") == 0) {
            handle_leave_game(server_socket);
        } else {
            printf("Unknown command: %s\n", buffer);
        }
    }
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

    login_or_register(sockfd);


    handle_user_commands(sockfd);
}

void handle_help() {
    printf(
            "Available commands:\n"
            "/help - Show this help message\n"
            "/exit - Logout and exit the application\n"
            "/online - Show online users\n"
            "/players - Show available players\n"
            "/games - Show available games\n"
            "/obs - Observe a game\n"
            "/bio - View your bio\n"
            "/bio <player> - View another player's bio\n"
            "/update - Update your bio\n"
            "/gl - Send a global message\n"
            "/msg - Send a direct message to a player\n"
            "/m - Make a move in the current game\n"
            "/end - End the current game\n"
            "/leave - Leave the current game\n"
    );
}


void handle_exit(int server_socket) {
    const char *command = "LOGOUT\n";
    if (send(server_socket, command, strlen(command), 0) < 0) {
        perror("Failed to send LOGOUT command");
    } else {
        printf("Logged out successfully. Exiting...\n");
        exit(0);
    }
}


void handle_online(int server_socket) {
    printf("Sending ONLINE request\n");
    const char *command = "ONLINE\n";
    send(server_socket, command, strlen(command), 0);
}

void handle_players(int server_socket) {
    printf("Sending PLAYERS request\n");
    const char *command = "PLAYERS\n";
    send(server_socket, command, strlen(command), 0);
}

void handle_games(int server_socket) {
    printf("Sending GAMES request\n");
    const char *command = "GAMES\n";
    send(server_socket, command, strlen(command), 0);
}

void handle_obs(int server_socket) {
    printf("Sending OBSERVE request\n");
    // Add prompt for specific game ID in the future
    const char *command = "OBSERVE\n";
    send(server_socket, command, strlen(command), 0);
}

void handle_bio(int server_socket) {
    printf("Sending BIO request (current user)\n");
    const char *command = "BIO\n";
    send(server_socket, command, strlen(command), 0);
}

void handle_view_bio(int server_socket) {
    printf("Sending BIO <player> request\n");
    // Add prompt for player name in the future
    const char *command = "BIO player_name\n";
    send(server_socket, command, strlen(command), 0);
}

void handle_update_bio(int server_socket) {
    printf("Sending UPDATE request\n");
    // Add prompt for bio content in the future
    const char *command = "UPDATE bio_content\n";
    send(server_socket, command, strlen(command), 0);
}

void handle_global_message(int server_socket) {
    printf("Sending GLOBAL MESSAGE request\n");
    // Add prompt for message content in the future
    const char *command = "GLOBAL_MESSAGE message_content\n";
    send(server_socket, command, strlen(command), 0);
}

void handle_direct_message(int server_socket) {
    printf("Sending DIRECT MESSAGE request\n");
    // Add prompt for player and message content in the future
    const char *command = "DIRECT_MESSAGE player_name message_content\n";
    send(server_socket, command, strlen(command), 0);
}

void handle_make_move(int server_socket) {
    printf("Sending MAKE MOVE request\n");
    // Add prompt for move details in the future
    const char *command = "MAKE_MOVE move_details\n";
    send(server_socket, command, strlen(command), 0);
}

void handle_end_game(int server_socket) {
    printf("Sending END GAME request\n");
    const char *command = "END_GAME\n";
    send(server_socket, command, strlen(command), 0);
}

void handle_leave_game(int server_socket) {
    printf("Sending LEAVE GAME request\n");
    const char *command = "LEAVE_GAME\n";
    send(server_socket, command, strlen(command), 0);
}

