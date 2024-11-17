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
#include <time.h>

#define MAX_CREDENTIALS_LENGTH 30

#define MAX_PLAYERS 100
#define MAX_GAMES 50

#define BUFFER_SIZE 1024
#define PLAYER_FILE "players.txt"

#define PITS 6  // Number of pits per player
#define INITIAL_SEEDS 4  // Initial seeds in each pit

#define MAX_BIO_LINES 10
#define MAX_BIO_LENGTH 100

#define HASH_SIZE 256

typedef struct Move {
    int pit_index;            // Pit index of the move
    int seeds_before_move;    // Seeds in the pit before the move
    struct Move *next;        // Pointer to the next move in the list
} Move;

typedef struct {
    int pits[PITS];
    int store;
    Move *move_history;

    bool in_game;
    bool is_online;

    int socket;

    char bio[MAX_BIO_LINES * MAX_BIO_LENGTH];
    char password[HASH_SIZE];
    char pseudo[MAX_CREDENTIALS_LENGTH + 1];
    unsigned int password_hash;
} Player;


typedef struct {
    Player *player1;
    Player *player2;
    int current_turn;
    Player *observers[MAX_PLAYERS];
    int observer_count;
} Game;

Player players[MAX_PLAYERS];
Game *active_games[MAX_GAMES];
int active_game_count = 0;
pthread_mutex_t player_mutex = PTHREAD_MUTEX_INITIALIZER;

unsigned int hash_password(const char *password);

int load_players_from_file();

void send_message(int socket, const char *message);

void *handle_client(void *arg);

void handle_logout(Player *player);

// Simple hash function to hash the passwords (for example purposes)
unsigned int hash_password(const char *password) {
    unsigned int hash = 0;
    while (*password) {
        hash = (hash * 31) + (unsigned char) (*password++);
    }
    return hash;
}

// Load users from the file into memory
int load_players_from_file() {
    FILE *file = fopen(PLAYER_FILE, "r");
    if (file) {
        // File exists, proceed with reading players
        char pseudo[50], password[50];
        char line[256];  // Declare the line variable here

        while (fscanf(file, "%s %s", pseudo, password) == 2) {
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (players[i].pseudo[0] == '\0') {
                    strcpy(players[i].pseudo, pseudo);
                    strcpy(players[i].password, password);
                    players[i].is_online = false;
                    players[i].socket = -1;
                    players[i].in_game = false;
                    players[i].bio[0] = '\0';  // Initialize the bio to be empty

                    // Read the bio data (assuming it starts after "bio:")
                    while (fgets(line, sizeof(line), file)) {
                        // If we hit the "-----" separator, we stop reading bio data
                        if (strncmp(line, "-----", 5) == 0) {
                            break;
                        }
                        // Append the line to the player's bio
                        strcat(players[i].bio, line);
                    }
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
            return 0;
        }
    }
    return 1;
}

void send_message(int socket, const char *message) {
    send(socket, message, strlen(message), 0);
}

int handle_visit(int client_socket) {
    char action[8];
    char pseudo[MAX_CREDENTIALS_LENGTH + 1];
    char password[MAX_CREDENTIALS_LENGTH + 1];
    char buffer[BUFFER_SIZE];
    bzero(buffer, BUFFER_SIZE);

    int bytes_read;

    bytes_read = recv(client_socket, action, sizeof(action) - 1, 0);
    if (bytes_read <= 0) {
        close(client_socket);
        return 0;
    }
    action[bytes_read] = '\0'; // Null-terminate the action

    // Read the login and password
    recv(client_socket, pseudo, sizeof(pseudo), 0);
    recv(client_socket, password, sizeof(password), 0);

    send_message(client_socket, action);
    if (strcmp(action, "REGISTER:") == 0) {
        send_message(client_socket, "Registering...");
        return 1;
    } else if (strcmp(action, "LOGIN:") == 0) {
        send_message(client_socket, "Logging in");
        return 1;
    }
    return 0;
}

void *handle_client(void *arg) {
    int client_socket = *((int *) arg);
    free(arg);

    char buffer[BUFFER_SIZE];
    bzero(buffer, BUFFER_SIZE);

    // Welcome message and instructions sent to the client
    send_message(client_socket, "Welcome to the server!\n");

    close(client_socket);
    return NULL;
}


void handle_logout(Player *player) {
    send_message(player->socket, "Logging out...\n");

    pthread_mutex_lock(&player_mutex);

    player->is_online = false;
    player->in_game = false;  // Ensure they are not in-game
    player->socket = -1;     // Reset the socket

    pthread_mutex_unlock(&player_mutex);

    printf("Player logged out: %s\n", player->pseudo);

    close(player->socket);
    pthread_exit(NULL);
}
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
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));

    // Bind the socket
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
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
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
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
}