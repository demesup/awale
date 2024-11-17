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

#define MAX_ONLINE_PLAYERS 100
#define MAX_PLAYERS 1000
#define MAX_GAMES 50
#define BUFFER_SIZE 1024
#define PLAYER_FILE "players.txt"
#define PITS 6  // Number of pits per player
#define INITIAL_SEEDS 4  // Initial seeds in each pit

#define MAX_PSEUDO_LEN 10
#define MAX_PASSWORD_LEN 10
#define COMMAND_LENGTH 10
#define MAX_BIO_LINES 10
#define MAX_BIO_LENGTH 100

#define HASH_SIZE 256


typedef struct Move {
    int pit_index;            // Pit index of the move
    int seeds_before_move;    // Seeds in the pit before the move
    struct Move *next;        // Pointer to the next move in the list
} Move;

typedef struct {
    char pseudo[MAX_PSEUDO_LEN];
    char password[HASH_SIZE];
    int pits[PITS];
    int store;
    Move *move_history;
    int socket;
    bool in_game;
    bool is_online;
    char bio[MAX_BIO_LINES * MAX_BIO_LENGTH];
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

/**PROTOTYPES*/
void *handle_client(void *arg);

int send_message(int sockfd, const char *message);

Player *find_player_by_pseudo(const char *pseudo);

int handle_registration(char *pseudo, char *password, int client_socket);

int handle_login(char *pseudo, char *password, int client_socket);

void save_player_to_file(Player *player);

void load_players_from_file();

void send_online_players(Player *player);

void send_all_players(Player *player);


/**CODE*/

int send_message(int sockfd, const char *message) {
    send(sockfd, message, strlen(message), 0);
}

int main(int argc, char **argv) {
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
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }


    load_players_from_file();

    /* Initialize parameters */
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));

    /* Perform the bind */
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* Initialize listening */
    listen(sockfd, MAX_ONLINE_PLAYERS);

    printf("Server listening on port %s...\n", argv[1]);

    while (1) {
        /* Accept a client connection */
        clilen = sizeof(cli_addr);
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
        if (newsockfd < 0) {
            printf("Accept failed\n");
            continue; // Continue to accept other clients
        }

        printf("Connection accepted from %s:%d\n",
               inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));

        /* Create a new thread for each client */
        int *client_socket = malloc(sizeof(int));
        *client_socket = newsockfd;

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, client_socket) != 0) {
            perror("Thread creation failed");
            close(newsockfd);
            free(client_socket);
        } else {
            pthread_detach(thread_id); // Detach the thread, so it cleans up automatically
        }
    }

    close(sockfd);
    return 0;
}

void *handle_client(void *arg) {
    int client_socket = *((int *) arg);
    free(arg);

    char buffer[BUFFER_SIZE];
    int n;
    while ((n = read(client_socket, buffer, BUFFER_SIZE - 1)) > 0) {
        buffer[n] = '\0'; // Null-terminate the received string
        printf("Received command: %s", buffer); // Print the full command
        char type[COMMAND_LENGTH];     // To hold "REGISTER" or "LOGIN"
        char pseudo[MAX_PSEUDO_LEN];   // To hold the pseudo
        char password[MAX_PASSWORD_LEN]; // To hold the password


        if (sscanf(buffer, "%s %s %s", type, pseudo, password) == 3) {
            if (strcmp(type, "REGISTER") == 0 || strcmp(type, "LOGIN") == 0) {
                printf("Command type: %s\n", type);
                printf("Pseudo: %s\n", pseudo);
                printf("Password: %s\n", password);

                if (strncmp(type, "LOGIN", 5) == 0) {
                    handle_login(pseudo, password, client_socket);
                } else if (strncmp(type, "REGISTER", 8) == 0) {
                    handle_registration(pseudo, password, client_socket);
                }
            } else {
                printf("Invalid command type.\n");
            }
        } else {
            printf("Invalid command format.\n");
        }

    }

    if (n == 0) {
        printf("Client disconnected\n");
        close(client_socket);
    } else {
        printf("Error reading from client\n");
    }

    return NULL;
}


// Load players from file
void load_players_from_file() {
    FILE *file = fopen(PLAYER_FILE, "r");
    if (file) {
        // File exists, proceed with reading players
        char pseudo[MAX_PSEUDO_LEN], password[HASH_SIZE];
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
        }
    }
}

// Send list of online players
void send_online_players(Player *player) {
    pthread_mutex_lock(&player_mutex);

    char response[BUFFER_SIZE];
    strcpy(response, "Online players:\n");
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].is_online) {
            if (strcmp(players[i].pseudo, player->pseudo) == 0) {
                continue;
            }
            strcat(response, players[i].pseudo);
            strcat(response, "\n");
        }
    }
    send_message(player->socket, response);

    pthread_mutex_unlock(&player_mutex);
}

void send_all_players(Player *player) {
    pthread_mutex_lock(&player_mutex);

    char response[BUFFER_SIZE];
    strcpy(response, "All players:\n");
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (strcmp(players[i].pseudo, player->pseudo) == 0) {
            continue;
        }
        strcat(response, players[i].pseudo);
        strcat(response, "\n");
    }
    send_message(player->socket, response);

    pthread_mutex_unlock(&player_mutex);
}


void save_player_to_file(Player *player) {
    FILE *file = fopen(PLAYER_FILE, "a");
    if (file) {
        fprintf(file, "%s %s\n", player->pseudo, player->password);
        fprintf(file, "-----\n");
        fclose(file);
    } else {
        perror("Failed to open player file");
    }
}

int handle_login(char *pseudo, char *password, int client_socket) {
    pthread_mutex_lock(&player_mutex);  // Locking the mutex to ensure thread-safety
    Player *player = find_player_by_pseudo(pseudo);
    if (player == NULL) {
        send_message(client_socket, "Player not found!\n");
        pthread_mutex_unlock(&player_mutex);
        return 0;

    } else {
        if (player->is_online) {
            // If the player is already online
            send_message(client_socket, "You are already logged in!\n");
            pthread_mutex_unlock(&player_mutex);  // Unlock mutex before returning
            return 0;
        }

        // Step 3: Verify password
//        char hashed_password[HASH_SIZE];
//        hash_password(password, hashed_password);  // Hash the input password

//        if (!compare_hashes(player->password, hashed_password)) {
        if (strcmp(player->password, password) != 0) {
            send_message(client_socket, "Incorrect password!\n");
            pthread_mutex_unlock(&player_mutex);  // Unlock mutex before returning
            return 0;
        }

        // Step 4: Mark the user as online and set their socket
        player->is_online = true;
        player->socket = client_socket;

        send_message(client_socket, "Login successful!\n");
        printf("Player logged in: %s\n", pseudo);

        pthread_mutex_unlock(&player_mutex);  // Unlock mutex after handling the login
        return 1;
    }

}

int handle_registration(char *pseudo, char *password, int client_socket) {

}

Player *find_player_by_pseudo(const char *pseudo) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (strcmp(players[i].pseudo, pseudo) == 0) {
            return &players[i];
        }
    }
    return NULL;
}
