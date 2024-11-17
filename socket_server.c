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
#define MAX_FRIENDS 15
#define MAX_GAMES 50
#define BUFFER_SIZE 1024
#define PLAYER_FILE "players.txt"
#define PITS 6  // Number of pits per player
#define INITIAL_SEEDS 4  // Initial seeds in each pit

#define MAX_PSEUDO_LEN 10
#define MAX_PASSWORD_LEN 10
#define COMMAND_LENGTH 10
#define MAX_BIO_LINES 10
#define MAX_BIO_LENGTH 1024

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
    char friends[MAX_FRIENDS][MAX_PSEUDO_LEN];
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

int answer(int sockfd);

Player *find_player_by_pseudo(const char *pseudo);

Player *handle_registration(char *pseudo, char *password, int client_socket);

Player *handle_login(char *pseudo, char *password, int client_socket);

void handle_logout(Player *player);

void save_player_to_file(Player *player);

void load_players_from_file();

bool is_pseudo_taken(const char *pseudo);

void send_online_players(Player *player);

void send_all_players(Player *player);

void menu(Player *player);


/**CODE*/

int answer(int sockfd) {
    usleep(300);
    send_message(sockfd, "ANSWER\n");
    usleep(300);
}

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
    Player *player;

    do {
        n = read(client_socket, buffer, BUFFER_SIZE - 1);
        if (n > 0) {
            buffer[n] = '\0'; // Null-terminate the received string
            char type[COMMAND_LENGTH];       // To hold "REGISTER" or "LOGIN"
            char pseudo[MAX_PSEUDO_LEN];     // To hold the pseudo
            char password[MAX_PASSWORD_LEN]; // To hold the password

            if (sscanf(buffer, "%s %s %s", type, pseudo, password) == 3) {
                if (strcmp(type, "REGISTER") == 0) {
                    player = handle_registration(pseudo, password, client_socket);
                } else if (strcmp(type, "LOGIN") == 0) {
                    player = handle_login(pseudo, password, client_socket);
                } else {
                    printf("Invalid command type.\n");
                    send_message(client_socket, "Invalid command type\n");
                }
            } else {
                printf("Invalid command format.\n");
                send_message(client_socket, "Invalid command format\n");
            }
        } else {
            if (n == 0) {
                printf("Client disconnected\n");
                close(client_socket);
            } else {
                printf("Error reading from client\n");
            }
            return NULL;
        }
    } while (player == NULL);

    // User is logged in or registered, enter endless interaction loop
    menu(player);
}

void menu(Player *player) {
    int n;
    char buffer[BUFFER_SIZE];
    char command[COMMAND_LENGTH];     // To hold the pseudo
    char params[BUFFER_SIZE - MAX_PSEUDO_LEN];     // To hold the pseudo
    while (1) {
        n = read(player->socket, buffer, BUFFER_SIZE - 1);
        if (n > 0) {
            buffer[n] = '\0'; // Null-terminate the received string
            printf("Received from %s: %s\n", command, params);

            // Respond to client commands
            if (strcmp(command, "LOGOUT") == 0) {
                handle_logout(player);
            }

            // Example echo back or process commands
            send_message(player->socket, buffer);
        } else {
            if (n == 0) {
                printf("Client disconnected\n");
                handle_logout(player);
            } else {
                printf("Error reading from client\n");
            }
        }
    }
}

// Load players from file
void load_players_from_file() {
    FILE *file = fopen(PLAYER_FILE, "r");
    if (file) {
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

                    // Initialize the friends array to empty strings
                    for (int j = 0; j < MAX_FRIENDS; j++) {
                        players[i].friends[j][0] = '\0';
                    }

                    // Read the friend list (assuming it starts after "friends:")
                    if (fgets(line, sizeof(line), file)) {
                        if (strncmp(line, "friends:", 8) == 0) {
                            char friend_pseudo[MAX_PSEUDO_LEN];
                            int friend_index = 0;

                            // Read friend pairs until "bio:" or "-----"
                            while (fscanf(file, "%s", friend_pseudo) == 1 && friend_index < MAX_FRIENDS) {
                                strcpy(players[i].friends[friend_index], friend_pseudo);
                                friend_index++;
                            }
                        }
                    }

                    // Read the bio data (assuming it starts after "bio:")
                    if (fgets(line, sizeof(line), file)) {
                        if (strncmp(line, "bio:", 4) == 0) {
                            while (fgets(line, sizeof(line), file)) {
                                // If we hit the "-----" separator, we stop reading bio data
                                if (strncmp(line, "-----", 5) == 0) {
                                    break;
                                }
                                // Append the line to the player's bio
                                strcat(players[i].bio, line);
                            }
                        }
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


Player *find_player_by_pseudo(const char *pseudo) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (strcmp(players[i].pseudo, pseudo) == 0) {
            return &players[i];
        }
    }
    return NULL;
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

bool is_pseudo_taken(const char *pseudo) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (strcmp(players[i].pseudo, pseudo) == 0) {
            return 1;
        }
    }
    return 0;
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

        // Write friends to the file
        fprintf(file, "friends: ");
        for (int i = 0; i < MAX_FRIENDS && player->friends[i][0] != '\0'; i++) {
            fprintf(file, "%s ", player->friends[i]);
        }
        fprintf(file, "\n");

        // Write bio to the file
        fprintf(file, "bio:\n");
        for (int i = 0; i < MAX_BIO_LINES; i++) {
            if (player->bio[i] != '\0') {
                fprintf(file, "%s\n", player->bio[i]);
            }
        }

        fprintf(file, "-----\n");
        fclose(file);
    } else {
        perror("Failed to open player file");
    }
}


Player *handle_login(char *pseudo, char *password, int client_socket) {
    pthread_mutex_lock(&player_mutex);  // Locking the mutex to ensure thread-safety
    Player *player = find_player_by_pseudo(pseudo);
    if (player == NULL) {
        send_message(client_socket, "Player not found!\n");
        pthread_mutex_unlock(&player_mutex);
        answer(client_socket);
        return NULL;
    } else {
        if (player->is_online) {
            // If the player is already online
            send_message(client_socket, "You are already logged in!\n");
            pthread_mutex_unlock(&player_mutex);  // Unlock mutex before returning
            answer(client_socket);
            return NULL;
        }

        // Step 3: Verify password
//        char hashed_password[HASH_SIZE];
//        hash_password(password, hashed_password);  // Hash the input password

//        if (!compare_hashes(player->password, hashed_password)) {
        if (strcmp(player->password, password) != 0) {
            send_message(client_socket, "Incorrect password!\n");
            pthread_mutex_unlock(&player_mutex);  // Unlock mutex before returning
            answer(client_socket);
            return NULL;
        }

        // Step 4: Mark the user as online and set their socket
        player->is_online = true;
        player->socket = client_socket;

        send_message(client_socket, "Login successful!\n");
        answer(client_socket);
        printf("Player logged in: %s\n", pseudo);

        pthread_mutex_unlock(&player_mutex);  // Unlock mutex after handling the login
        return player;
    }

}

Player *handle_registration(char *pseudo, char *password, int client_socket) {
    if (strlen(pseudo) == 0 || strlen(password) == 0) {
        send_message(client_socket, "Pseudo and password cannot be empty!\n");
        pthread_mutex_lock(&player_mutex);
        answer(client_socket);
        return NULL;
    }

    if (is_pseudo_taken(pseudo)) {
        send_message(client_socket, "Pseudo already taken!\n");
        pthread_mutex_lock(&player_mutex);
        answer(client_socket);
        return NULL;
    }

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].is_online && strcmp(players[i].pseudo, pseudo) == 0) {
            send_message(client_socket, "Pseudo already taken!\n");
            pthread_mutex_unlock(&player_mutex);
            answer(client_socket);
            return NULL;
        }
    }

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!players[i].is_online && players[i].pseudo[0] == '\0') {

//            char hashed_password[HASH_SIZE];
//            hash_password(password, hashed_password);

            strcpy(players[i].pseudo, pseudo);
            strcpy(players[i].password, password);
            players[i].socket = client_socket;
            players[i].is_online = true;
            players[i].in_game = false;

            save_player_to_file(&players[i]); // Save to file
            send_message(client_socket, "Registration successful!\n");
            printf("Player registered: %s\n", pseudo);

            pthread_mutex_unlock(&player_mutex);
            answer(client_socket);
            return &players[i];
        }
    }

    send_message(client_socket, "Server full!\n");
    answer(client_socket);
    pthread_mutex_unlock(&player_mutex);
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
