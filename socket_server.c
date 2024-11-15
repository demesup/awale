#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>

#define MAX_PLAYERS 100
#define BUFFER_SIZE 1024
#define PLAYER_FILE "players.txt"

typedef struct {
    char pseudo[50];
    char password[50];
    int socket;
    bool in_game;
    bool is_online;
} Player;

Player players[MAX_PLAYERS];
pthread_mutex_t player_mutex = PTHREAD_MUTEX_INITIALIZER;

// Function prototypes
void handle_registration(int client_socket, char *pseudo, char *password);
void handle_login(int client_socket, char *pseudo, char *password);
void send_online_players(int client_socket);
void save_player_to_file(Player *player);
void load_players_from_file();
int find_player_by_pseudo(const char *pseudo);
void handle_visit(int client_socket);

void handle_challenge(int client_socket, const char *current_user) {
    char buffer[BUFFER_SIZE];
    send(client_socket, "Enter the username to challenge: ", 34, 0);

    bzero(buffer, BUFFER_SIZE);
    int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received <= 0) {
        printf("Client disconnected.\n");
        close(client_socket);
        return;
    }

    buffer[bytes_received] = '\0';
    char challenge_user[50];
    sscanf(buffer, "%s", challenge_user);

    pthread_mutex_lock(&player_mutex);
    int challenge_index = find_player_by_pseudo(challenge_user);
    if (challenge_index >= 0 && players[challenge_index].is_online) {
        char challenge_msg[BUFFER_SIZE];
        sprintf(challenge_msg, "You have been challenged by %s!\n", current_user);
        send(players[challenge_index].socket, challenge_msg, strlen(challenge_msg), 0);
        send(client_socket, "Challenge sent successfully!\n", 29, 0);
    } else {
        send(client_socket, "Player not found or not online.\n", 31, 0);
    }
    pthread_mutex_unlock(&player_mutex);
}

void handle_logout(int client_socket, const char *current_user) {
    send(client_socket, "Logging out...\n", 16, 0);
    pthread_mutex_lock(&player_mutex);

    int player_index = find_player_by_pseudo(current_user);
    if (player_index >= 0) {
        players[player_index].is_online = false;
    }

    pthread_mutex_unlock(&player_mutex);
    printf("Player logged out: %s\n", current_user);
}

void handle_logged_in_menu(int client_socket, const char *current_user) {
    char buffer[BUFFER_SIZE];

    while (1) {
        send(client_socket,
             "\nMenu:\n1. List all online players\n2. Challenge a player by username\n3. Logout\nEnter your choice:\n",
             97, 0);

        bzero(buffer, BUFFER_SIZE);
        int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) {
            printf("Client disconnected.\n");
            close(client_socket);
            return;
        }

        int choice = atoi(buffer);

        switch (choice) {
            case 1:
                send_online_players(client_socket);
                break;

            case 2:
                handle_challenge(client_socket, current_user);
                break;

            case 3:
                handle_logout(client_socket, current_user);
                return;

            default:
                send(client_socket, "Invalid choice. Try again.\n", 27, 0);
                break;
        }
    }
}

void *handle_client(void *arg) {
    int client_socket = *((int *)arg);
    free(arg);

    char buffer[BUFFER_SIZE];
    bzero(buffer, BUFFER_SIZE);

    // Welcome message and instructions sent to the client
    send(client_socket, "Welcome to the server!\n", 23, 0);

    handle_visit(client_socket);


    close(client_socket);  // Close the socket after registration or login is handled
    return NULL;
}


// New handle_visit method to handle client interaction during registration and login
void handle_visit(int client_socket) {
    char buffer[BUFFER_SIZE];
    bzero(buffer, BUFFER_SIZE);

    // Send instructions to the client
    send(client_socket, "REGISTER <pseudo> <password>\nLOGIN <pseudo> <password>\n", 55, 0);

    char current_user[50] = {0}; // Stores the current logged-in user

    // Main loop to handle commands from the client
    while (1) {
        bzero(buffer, BUFFER_SIZE);
        int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) {
            printf("Client disconnected.\n");
            close(client_socket);
            return;  // Exit if the client disconnects
        }

        buffer[bytes_received] = '\0';  // Null-terminate the received string
        char command[BUFFER_SIZE], pseudo[50], password[50];
        sscanf(buffer, "%s %s %s", command, pseudo, password);  // Parse the command

        if (strcmp(command, "REGISTER") == 0) {
            handle_registration(client_socket, pseudo, password);  // Handle registration
        } else if (strcmp(command, "LOGIN") == 0) {
            handle_login(client_socket, pseudo, password);  // Handle login
        } else {
            send(client_socket, "Invalid command! Use REGISTER or LOGIN.\n", 40, 0);  // Invalid command
        }
    }

    // The client is now logged in or registered, you can add any further handling if needed
}


bool is_pseudo_in_file(const char *pseudo) {
    FILE *file = fopen(PLAYER_FILE, "r");
    if (!file) {
        perror("Failed to open player file");
        return false;
    }

    char file_pseudo[50], file_password[50];
    while (fscanf(file, "%s %s", file_pseudo, file_password) == 2) {
        if (strcmp(file_pseudo, pseudo) == 0) {
            fclose(file);
            return true;  // Pseudo found in file
        }
    }

    fclose(file);
    return false;  // Pseudo not found
}

void handle_registration(int client_socket, char *pseudo, char *password) {
    // Validate input: pseudo and password cannot be empty
    if (strlen(pseudo) == 0 || strlen(password) == 0) {
        send(client_socket, "Pseudo and password cannot be empty!\n", 38, 0);
        return;
    }

    // Check if pseudo already exists in the file
    if (is_pseudo_in_file(pseudo)) {
        send(client_socket, "Pseudo already taken!\n", 23, 0);
        return;
    }

    pthread_mutex_lock(&player_mutex);

    // Check if pseudo already exists among online players
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].is_online && strcmp(players[i].pseudo, pseudo) == 0) {
            send(client_socket, "Pseudo already taken!\n", 23, 0);
            pthread_mutex_unlock(&player_mutex);
            handle_visit(client_socket);
            return;
        }
    }

    // Register the new player
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!players[i].is_online && players[i].pseudo[0] == '\0') {
            strcpy(players[i].pseudo, pseudo);
            strcpy(players[i].password, password);
            players[i].socket = client_socket;
            players[i].is_online = true;
            players[i].in_game = false;

            save_player_to_file(&players[i]); // Save to file

            send(client_socket, "Registration successful!\n", 25, 0);
            printf("Player registered: %s\n", pseudo);

            pthread_mutex_unlock(&player_mutex);
            handle_logged_in_menu(client_socket, pseudo);
            return;
        }
    }

    send(client_socket, "Server full!\n", 13, 0);
    pthread_mutex_unlock(&player_mutex);
}

void handle_login(int client_socket, char *pseudo, char *password) {
    // Step 1: Validate input
    if (strlen(pseudo) == 0 || strlen(password) == 0) {
        send(client_socket, "Pseudo and password cannot be empty!\n", 37, 0);
        handle_visit(client_socket);
        return;
    }

    pthread_mutex_lock(&player_mutex);  // Locking the mutex to ensure thread-safety

    // Step 2: Check if the user is already online
    int player_index = find_player_by_pseudo(pseudo);
    if (player_index >= 0) {
        if (players[player_index].is_online) {
            // If the player is already online
            send(client_socket, "You are already logged in!\n", 27, 0);
            pthread_mutex_unlock(&player_mutex);  // Unlock mutex before returning
            handle_visit(client_socket);
            return;
        }

        // Step 3: Verify password
        if (strcmp(players[player_index].password, password) != 0) {
            // If password is incorrect
            send(client_socket, "Incorrect password!\n", 20, 0);
            pthread_mutex_unlock(&player_mutex);  // Unlock mutex before returning
            handle_visit(client_socket);
            return;
        }

        // Step 4: Mark the user as online and set their socket
        players[player_index].is_online = true;
        players[player_index].socket = client_socket;

        send(client_socket, "Login successful!\n", 18, 0);
        printf("Player logged in: %s\n", pseudo);

        pthread_mutex_unlock(&player_mutex);  // Unlock mutex after handling the login
        handle_logged_in_menu(client_socket, pseudo);

    } else {
        // If the user doesn't exist
        send(client_socket, "Player not found!\n", 18, 0);
        pthread_mutex_unlock(&player_mutex);
        handle_visit(client_socket);
    }

}


// Save player to file
void save_player_to_file(Player *player) {
    FILE *file = fopen(PLAYER_FILE, "a");
    if (file) {
        fprintf(file, "%s %s\n", player->pseudo, player->password);
        fclose(file);
    } else {
        perror("Failed to open player file");
    }
}

// Load players from file
void load_players_from_file() {
    FILE *file = fopen(PLAYER_FILE, "r");
    if (file) {
        // File exists, proceed with reading players
        char pseudo[50], password[50];
        while (fscanf(file, "%s %s", pseudo, password) == 2) {
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (players[i].pseudo[0] == '\0') {
                    strcpy(players[i].pseudo, pseudo);
                    strcpy(players[i].password, password);
                    players[i].is_online = false;
                    players[i].socket = -1;
                    players[i].in_game = false;
                    break;
                }
            }
        }
        fclose(file);
    } else {
        // If file doesn't exist, create it and print a message
        file = fopen(PLAYER_FILE, "a");
        if (file) {
            printf("Player file created.\n");
            fclose(file);
        } else {
            perror("Failed to create player file");
        }
    }
}

// Send list of online players
void send_online_players(int client_socket) {
    pthread_mutex_lock(&player_mutex);

    char response[BUFFER_SIZE];
    strcpy(response, "Online players:\n");
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].is_online) {
            strcat(response, players[i].pseudo);
            strcat(response, "\n");
        }
    }
    send(client_socket, response, strlen(response), 0);

    pthread_mutex_unlock(&player_mutex);
}

// Find a player by pseudo
int find_player_by_pseudo(const char *pseudo) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (strcmp(players[i].pseudo, pseudo) == 0) {
            return i;
        }
    }
    return -1;
}

// Main server function
int main(int argc, char **argv) {
    int sockfd, newsockfd;
    socklen_t clilen;
    struct sockaddr_in cli_addr, serv_addr;

    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        exit(0);
    }

    printf("Server starting...\n");

    // Initialize player data
    for (int i = 0; i < MAX_PLAYERS; i++) {
        players[i].pseudo[0] = '\0';
        players[i].is_online = false;
        players[i].socket = -1;
    }

    load_players_from_file(); // Load players from file

    // Open socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Initialize server parameters
    bzero((char *)&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));

    // Bind the socket
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(sockfd, 5) < 0) {
        perror("Listen failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %s...\n", argv[1]);

    while (1) {
        clilen = sizeof(cli_addr);
        newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
        if (newsockfd < 0) {
            perror("Accept failed");
            continue;
        }

        int *client_socket = malloc(sizeof(int));
        *client_socket = newsockfd;

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, client_socket) != 0) {
            perror("Thread creation failed");
            close(newsockfd);
            free(client_socket);
        } else {
            pthread_detach(thread_id);
        }
    }

    close(sockfd);
    return 0;
}