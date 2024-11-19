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

#define LOGOUT "LOGOUT"
#define SHOW_ONLINE "SHOW_ONLINE"
#define SHOW_PLAYERS "SHOW_PLAYERS"
#define SHOW_GAMES "SHOW_GAMES"
#define CHALLENGE "CHALLENGE"
#define ACCEPT "ACCEPT"
#define DECLINE "DECLINE"
#define OBSERVE_GAME "OBSERVE_GAME"
#define VIEW_FRIEND_LIST "VIEW_FRIEND_LIST"
#define ADD_FRIEND "ADD_FRIEND"
#define FRIENDS_ONLY "FRIENDS_ONLY"
#define PUBLIC_OBSERVE "PUBLIC_OBSERVE"
#define VIEW_BIO "VIEW_BIO"
#define VIEW_PLAYER_BIO "VIEW_PLAYER_BIO"
#define UPDATE_BIO "UPDATE_BIO"
#define UPDATE_BIO_BODY "UPDATE_BIO_BODY"
#define GLOBAL_MESSAGE "GLOBAL_MESSAGE"
#define GAME_MESSAGE "GAME_MESSAGE"
#define DIRECT_MESSAGE "DIRECT_MESSAGE"
#define MAKE_MOVE "MAKE_MOVE"
#define END_GAME "END_GAME"
#define LEAVE_GAME "LEAVE_GAME"


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
#define COMMAND_LENGTH 18
#define MAX_BIO_LINES 10
#define MAX_BIO_LINE_LENGTH 80

#define HASH_SIZE 256


typedef struct Move {
    int pit_index;            // Pit index of the move
    int seeds_before_move;    // Seeds in the pit before the move
    struct Move *next;        // Pointer to the next move in the list
} Move;

typedef struct {
    int socket;

    char pseudo[MAX_PSEUDO_LEN];
    char password[HASH_SIZE];

    bool is_online;
    bool private;
    char bio[MAX_BIO_LINES * MAX_BIO_LINE_LENGTH];
    char friends[MAX_FRIENDS][MAX_PSEUDO_LEN];

    int pits[PITS];
    int store;

    Move *move_history;
    bool in_challenge;
    bool in_game;
    int game_id;
    char *challenged_by;
    char *challenged;
} Player;

typedef struct {
    Player *player1;
    Player *player2;
    char current_turn[MAX_PSEUDO_LEN];
    Player *observers[MAX_PLAYERS];
    int observer_count;
} Game;

typedef struct {
    Player *player1;
    Player *player2;
} Challenge;

Player players[MAX_PLAYERS];
Game *active_games[MAX_GAMES];
int active_game_count = 0;
pthread_mutex_t player_mutex = PTHREAD_MUTEX_INITIALIZER;

/**PROTOTYPES*/
void *handle_client(void *arg);

int send_message(int sockfd, const char *message);

void send_board(int socket, Player *player1, Player *player2);

void send_boards_players(Player *current, Player *opponent);

void send_boards(Game *game);


int answer(int sockfd);

Player *find_player_by_pseudo(const char *pseudo);

Player *handle_registration(char *pseudo, char *password, int client_socket);

Player *handle_login(char *pseudo, char *password, int client_socket);

void handle_logout(Player *player);

void handle_see_bio(Player *player);

void handle_update_bio(Player *player, char *command);

void handle_see_player_bio(Player *player_target, char *command);

void save_player_to_file(Player *player);

void load_players_from_file();

void update_players_file();

bool is_pseudo_taken(const char *pseudo);

void send_active_games(Player *player);

void send_online_players(Player *player);

void send_all_players(Player *player);

void menu(Player *player);

/** GAME */
void initialize_board(Game *game);

void initialize_game(Player *player1, Player *player2);

void send_game_start_message(int client_socket, int challenged_socket, int turn);

int capture_seeds(Player *current_player, Player *opponent, int last_pit);

int is_game_over(Player player1, Player player2);

int dead(Player player);

int is_savior(Player player);

void add_move(Player *player, int pit_index, int seeds_before_move);

int distribute_seeds(Player *current_player, Player *opponent, int pit_index);

void end_game(Player *player1, Player *player2, int result, Game *game);

int add_game(Game *new_game);

void remove_game(int game_id);

void clean_up_game(Game *game);

void clean_player_game_state(Player *player);

void make_move(Player *player, char *command);


/** CHALLENGE */
void handle_challenge(Player *player, char *command);

bool verify_not_self_challenge(Player *player, char *challenge_user);

bool is_valid_challenge(Player *player, Player *challenged);

void notify_challenge_sent(int socket);

void send_challenge(Player *player, Player *challenged);

void accept_challenge(Player *player);

void decline_challenge(Player *player);

void invalid_response(int client_socket, int challenged_socket);

void clean_bio(char *bio);

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
    char buffer[BUFFER_SIZE];  // Buffer to hold the command received from the client
    char command[COMMAND_LENGTH];  // Buffer to hold the command received from the client
    int bytes_received;  // To store the number of bytes received from the socket

    while (1) {
        // Read the command from the player's socket
        bytes_received = read(player->socket, buffer, sizeof(buffer));
        if (bytes_received <= 0) {
            if (bytes_received == 0) {
                handle_logout(player);
            } else {
                fflush(stdout);
                printf("Error reading from client\n");
            }
            return;
        }
//        command[bytes_received] = '\0'; // Null-terminate the received string

        // Extract the command (first word) before any space
        sscanf(buffer, "%s", command);  // This will extract the first word into 'command'
        // Process the command
        if (strcmp(command, LOGOUT) == 0) {
            handle_logout(player);
        } else if (strcmp(command, SHOW_PLAYERS) == 0) {
            send_all_players(player);
        } else if (strcmp(command, SHOW_ONLINE) == 0) {
            send_online_players(player);
        } else if (strcmp(command, SHOW_GAMES) == 0) {
            send_active_games(player);
        } else if (strcmp(command, VIEW_BIO) == 0) {
            handle_see_bio(player);
        } else if (strcmp(command, VIEW_PLAYER_BIO) == 0) {
            handle_see_player_bio(player, buffer);
        } else if (strcmp(command, UPDATE_BIO) == 0) {
            handle_update_bio(player, buffer);
        } else if (strcmp(command, CHALLENGE) == 0) {
            handle_challenge(player, buffer);
        } else if (strcmp(command, ACCEPT) == 0) {
            accept_challenge(player);
        } else if (strcmp(command, DECLINE) == 0) {
            decline_challenge(player);
        } else if (strcmp(command, MAKE_MOVE) == 0) {
            printf("Handling MAKE_MOVE command...\n");
            make_move(player, buffer);
        } else if (strcmp(command, SHOW_GAMES) == 0) {
            printf("Handling SHOW_GAMES command...\n");
            // Handle the SHOW_GAMES command here
        } else if (strcmp(command, OBSERVE_GAME) == 0) {
            printf("Handling OBSERVE_GAME command...\n");
            // Handle the OBSERVE_GAME command here, but without parameters
        } else if (strcmp(command, ADD_FRIEND) == 0) {
            printf("Handling ADD_FRIEND command...\n");
            // Handle the ADD_FRIEND command here
        } else if (strcmp(command, FRIENDS_ONLY) == 0) {
            printf("Handling FRIENDS_ONLY command...\n");
            // Handle the FRIENDS_ONLY command here
        } else {
            printf("Unknown command: %s\n", command);
        }
    }
}

// Load players from file
void load_players_from_file() {
    FILE *file = fopen(PLAYER_FILE, "r");
    if (file) {
        char pseudo[MAX_PSEUDO_LEN], password[HASH_SIZE];
        char line[256];  // Temporary buffer to read lines from file

        while (fscanf(file, "%s %s", pseudo, password) == 2) {
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (players[i].pseudo[0] == '\0') {
                    // Fill player information
                    strcpy(players[i].pseudo, pseudo);
                    strcpy(players[i].password, password);
                    players[i].is_online = false;
                    players[i].in_challenge = false;
                    players[i].socket = -1;
                    players[i].in_game = false;
                    players[i].bio[0] = '\0';  // Initialize the bio to be empty

                    // Initialize the friends array to empty strings
                    for (int j = 0; j < MAX_FRIENDS; j++) {
                        players[i].friends[j][0] = '\0';
                    }

                    // Read through the file until "bio:" or "friends:" are processed
                    int friend_index = 0;
                    while (fgets(line, sizeof(line), file)) {
                        // Read friends list (looking for "friends:")
                        if (strncmp(line, "friends:", 8) == 0) {
                            while (fscanf(file, "%s", line) == 1 && friend_index < MAX_FRIENDS) {
                                // Stop if we reach bio or separator
                                if (strncmp(line, "bio:", 4) == 0 || strncmp(line, "-----", 5) == 0) {
                                    break;
                                }
                                strcpy(players[i].friends[friend_index], line);
                                friend_index++;
                            }
                        }

                        // Read bio data (looking for "bio:")
                        if (strncmp(line, "bio:", 4) == 0) {
                            players[i].bio[0] = '\0';  // Clear bio before appending new content
                            while (fgets(line, sizeof(line), file)) {
                                // If we hit the "-----" separator, we stop reading bio data
                                if (strncmp(line, "-----", 5) == 0) {
                                    break;
                                }
                                // Append bio content (remove newline character)
                                line[strcspn(line, "\n")] = 0;  // Remove trailing newline
                                strcat(players[i].bio, "\n");
                                strcat(players[i].bio, line);
                                strcat(players[i].bio, " ");  // Add space after each line of bio
                            }
                        }

                        clean_bio(players[i].bio);


                        // Stop processing after reaching end of bio and friends
                        if (strncmp(line, "-----", 5) == 0) {
                            break;
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

/*
void backup_file() {
    char backup_path[256];
    snprintf(backup_path, sizeof(backup_path), "%s.bak", PLAYER_FILE);

    FILE *original_file = fopen(PLAYER_FILE, "r");
    if (!original_file) {
        perror("Error opening original file for backup");
        return;
    }

    FILE *backup_file = fopen(backup_path, "w");
    if (!backup_file) {
        perror("Error opening backup file");
        fclose(original_file);
        return;
    }

    // Copy the contents of the original file to the backup file
    char ch;
    while ((ch = fgetc(original_file)) != EOF) {
        fputc(ch, backup_file);
    }

    fclose(original_file);
    fclose(backup_file);
}

void restore_from_backup() {
    char backup_path[256];
    snprintf(backup_path, sizeof(backup_path), "%s.bak", PLAYER_FILE);

    FILE *backup_file = fopen(backup_path, "r");
    if (!backup_file) {
        perror("Error opening backup file for restoration");
        return;
    }

    FILE *file = fopen(PLAYER_FILE, "w");
    if (!file) {
        perror("Error opening original file for restoration");
        fclose(backup_file);
        return;
    }

    // Copy the contents of the backup file to the original file
    char ch;
    while ((ch = fgetc(backup_file)) != EOF) {
        fputc(ch, file);
    }

    fclose(backup_file);
    fclose(file);
    printf("File restored from backup.\n");
}
 */

void update_players_file() {
    FILE *file = fopen(PLAYER_FILE, "w");
    if (!file) {
        perror("Error opening file for writing");
        return;
    }

    // Write all players to the file
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].pseudo[0] != '\0') {  // Check if player slot is not empty
            fprintf(file, "%s %s\n", players[i].pseudo, players[i].password);

            // Write friends to the file
            fprintf(file, "friends: ");
            for (int i = 0; i < MAX_FRIENDS && players[i].friends[i][0] != '\0'; i++) {
                fprintf(file, "%s ", players[i].friends[i]);
            }
            fprintf(file, "\n");

            // Write bio to the file
            fprintf(file, "bio:\n");
            for (int i = 0; i < MAX_BIO_LINES; i++) {
                if (players[i].bio[0] != '\0' && strspn(players[i].bio, " \t\n") != strlen(players[i].bio)) {
                    // Write the bio line if it's not empty or whitespace-only
                    fprintf(file, "%s\n", players[i].bio);
                }            }

            fprintf(file, "-----\n");
        }
    }

    fclose(file);
    printf("FIle updated successfully.\n");
}

void handle_see_bio(Player *player) {
    char bio_output[MAX_BIO_LINES * MAX_BIO_LINE_LENGTH] = "Bio:\n";

    if (strlen(player->bio) > 0) {
        strcat(bio_output, player->bio);
        strcat(bio_output, "\n");
        send_message(player->socket, bio_output);
    } else {
        send_message(player->socket, "You haven't added a bio yet.\n");
    }
}

void handle_update_bio(Player *player, char *command) {
    // Create a buffer to hold the entire bio
    char bio[MAX_BIO_LINES * MAX_BIO_LINE_LENGTH];
    memset(bio, 0, sizeof(bio));  // Clear the buffer before use

    // Extract the bio content from the command
    if (sscanf(command, "UPDATE_BIO %[^\n]", bio) != 1) {
        send_message(player->socket, "Error reading bio\n");
        return;
    }
    char processed_bio[MAX_BIO_LINES * MAX_BIO_LINE_LENGTH];
    int j = 0;
    for (int i = 0; bio[i] != '\0'; i++) {
        if (bio[i] == '\\' && bio[i + 1] == 'n') {
            processed_bio[j++] = '\n'; // Add a newline
            i++;                      // Skip the 'n'
        } else {
            processed_bio[j++] = bio[i];
        }
    }
    processed_bio[j] = '\0'; // Null-terminate the string
    if (j > 0 && processed_bio[j - 1] == '\n') {
        processed_bio[j - 1] = '\0';
    }

    // Save the processed bio to the player's bio
    strncpy(player->bio, processed_bio, sizeof(player->bio) - 1);
    player->bio[sizeof(player->bio) - 1] = '\0'; // Ensure null termination

    update_players_file();

    // Send confirmation to the client
    send_message(player->socket, "Your bio has been updated successfully.\n");

}


void handle_see_player_bio(Player *player_target, char *command) {
    char pseudo[MAX_PSEUDO_LEN];
    if (sscanf(command, "VIEW_PLAYER_BIO %10s", pseudo) == 1) { // Limit pseudo to MAX_PSEUDO_LEN
        // Validate the pseudo
        if (strlen(pseudo) == 0 || strlen(pseudo) > MAX_PSEUDO_LEN) {
            send_message(player_target->socket, "Error reading challenged pseudo\n");
            return;
        }
    }
    Player *player = find_player_by_pseudo(pseudo);

    char bio_output[MAX_BIO_LINES * MAX_BIO_LINE_LENGTH] = "Bio:\n";

    if (strlen(player->bio) > 0) {
        strcat(bio_output, player->bio);
        strcat(bio_output, "\n");
        send_message(player_target->socket, bio_output);
    } else {
        send_message(player_target->socket, "The player hasn't added a bio yet.\n");
    }
}

void send_active_games(Player *player) {
    pthread_mutex_lock(&player_mutex);

    char response[BUFFER_SIZE];
    strcpy(response, "Active games:\n");
    for (int i = 0; i < active_game_count; i++) {
        Game *game = active_games[i];
        char game_info[100];
        snprintf(game_info, sizeof(game_info), "%d: %s VS %s\n",
                 i + 1,
                 game->player1->pseudo,
                 game->player2->pseudo);
        strcat(response, game_info);
    }
    send_message(player->socket, response);

    pthread_mutex_unlock(&player_mutex);
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
        if (players[i].pseudo[0] != '\0' && strcmp(players[i].pseudo, player->pseudo) != 0) {
            strcat(response, players[i].pseudo);
            strcat(response, "\n");
        }

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


void save_bio_to_file(Player *player) {
    FILE *file = fopen(PLAYER_FILE, "w");
    if (!file) {
        perror("Error opening file for writing");
        return;
    }

    // Write all players to the file
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].pseudo[0] != '\0') {  // Check if player slot is not empty
            fprintf(file, "%s %s\n", players[i].pseudo, players[i].password);

            fprintf(file, "friends: ");
            for (int i = 0; i < MAX_FRIENDS && player->friends[i][0] != '\0'; i++) {
                fprintf(file, "%s ", player->friends[i]);
            }
            fprintf(file, "\n");
            // Write the bio, ensuring the "bio:" label
            fprintf(file, "bio:\n%s\n", players[i].bio);

            // Write the "-----" separator after each player's data
            fprintf(file, "-----\n");
        }
    }

    fclose(file);
    printf("Players file updates.\n");
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
            players[i].in_challenge = false;
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
    if (player->in_game) {
        remove_game(player->game_id);
    }

    if (player->in_challenge) {
        if (player->challenged_by != NULL) {
            Player *challenged_by_player = find_player_by_pseudo(player->challenged_by);
            if (challenged_by_player != NULL) {
                challenged_by_player->in_challenge = false;
                challenged_by_player->challenged = NULL;
            }
        }

        if (player->challenged != NULL) {
            Player *challenged_player = find_player_by_pseudo(player->challenged);
            if (challenged_player != NULL) {
                challenged_player->in_challenge = false;
                challenged_player->challenged_by = NULL;
            }
        }

        player->in_challenge = false;
        player->challenged_by = NULL;
        player->challenged = NULL;
    }

    player->is_online = false;
    player->socket = -1;

    pthread_mutex_unlock(&player_mutex);

    printf("Player logged out: %s\n", player->pseudo);

    close(player->socket);
    pthread_exit(NULL);
}


int add_game(Game *new_game) {
    if (active_game_count >= MAX_GAMES) {
        return -1; // Cannot add more games, array is full
    }

    active_games[active_game_count++] = new_game;
    return active_game_count - 1;
}


void initialize_game(Player *player1, Player *player2) {
    Game *new_game = malloc(sizeof(Game));
    int id = add_game(new_game);
    if (id == -1) {
        send_message(player1->socket, "Failed to start the game. Server capacity reached.\n");
        send_message(player2->socket, "Failed to start the game. Server capacity reached.\n");
        free(new_game);
        return;
    }

    new_game->observer_count = 0;
    // Assign players to the game
    new_game->player1 = player1;
    new_game->player2 = player2;
    player1->game_id = id;
    player2->game_id = id;

    // Initialize pits for both players
    initialize_board(new_game);

    // Player 1 starts
    srand(time(NULL));
    int turn = rand() % 2;

    printf("%d\n", turn);

    if (turn == 1) {
        strcpy(new_game->current_turn, player1->pseudo);
    } else {
        strcpy(new_game->current_turn, player2->pseudo);
    }
    send_game_start_message(player1->socket, player2->socket, turn);
    send_boards_players(new_game->player1, new_game->player2);
}

void clean_player_game_state(Player *player) {
    player->in_game = false;
    player->store = 0;
    memset(player->pits, 0, sizeof(player->pits));

    Move *current_move = player->move_history;
    while (current_move != NULL) {
        Move *next_move = current_move->next;
        free(current_move);
        current_move = next_move;
    }
    player->move_history = NULL; // Ensure the pointer is reset
}

void clean_up_game(Game *game) {
    send_message(game->player1->socket, "Game finished\n");
    send_message(game->player2->socket, "Game finished\n");
    clean_player_game_state(game->player1);
    clean_player_game_state(game->player2);
}


void send_game_start_message(int client_socket, int challenged_socket, int turn) {
    if (turn == 1) {
        send_message(client_socket, "Game is starting! You go first.\n");
        send_message(challenged_socket, "Game is starting! Wait for your turn.\n");
    } else {
        send_message(challenged_socket, "Game is starting! You go first.\n");
        send_message(client_socket, "Game is starting! Wait for your turn.\n");
    }
}

void initialize_board(Game *game) {
    for (int i = 0; i < PITS; i++) {
        game->player1->pits[i] = INITIAL_SEEDS;
        game->player2->pits[i] = INITIAL_SEEDS;
    }
    game->player1->store = 0;
    game->player2->store = 0;
}


void reset_player(Player *player) {
    if (player == NULL) return;

    // Handle in-game state
    if (player->in_game) {
        remove_game(player->game_id); // Call remove_game with the player's game ID
    }

    // Handle in-challenge state
    if (player->in_challenge) {
        if (player->challenged_by != NULL) {
            Player *challenged_by_player = find_player_by_pseudo(player->challenged_by);
            if (challenged_by_player != NULL) {
                challenged_by_player->in_challenge = false;
                challenged_by_player->challenged = NULL;
            }
        }

        if (player->challenged != NULL) {
            Player *challenged_player = find_player_by_pseudo(player->challenged);
            if (challenged_player != NULL) {
                challenged_player->in_challenge = false;
                challenged_player->challenged_by = NULL;
            }
        }

        player->in_challenge = false;
        player->challenged_by = NULL;
        player->challenged = NULL;
    }

    // Reset all other fields
    memset(player->pseudo, 0, sizeof(player->pseudo));
    memset(player->password, 0, sizeof(player->password));
    memset(player->pits, 0, sizeof(player->pits));
    player->store = 0;

    if (player->move_history != NULL) {
        free(player->move_history); // Free dynamic memory for move_history
        player->move_history = NULL;
    }

    player->socket = -1;
    player->is_online = false;
    player->private = false;
    memset(player->bio, 0, sizeof(player->bio));
    memset(player->friends, 0, sizeof(player->friends));
}

void remove_game(int game_id) {
    for (int i = 0; i < active_game_count; i++) {
        if (i == game_id) {
            // Shift all games after the found game to fill the gap
            clean_up_game(active_games[game_id]);
            free(active_games[game_id]);
            for (int j = i; j < active_game_count - 1; j++) {
                active_games[j] = active_games[j + 1];
            }
            active_game_count--;
            break;
        }
    }
}

void decline_challenge(Player *player) {
    if (!player->in_challenge) {
        send_message(player->socket, "You do not have pending challenge!\n");
        return;
    }
    Player *challenger = find_player_by_pseudo(player->challenged_by);
    if (challenger == NULL || !challenger->is_online) {
        player->challenged_by = NULL;
        send_message(player->socket, "User is not online anymore!\n");
        return;
    }
    // Set players as in a game
    send_message(challenger->socket, "Your challenge has been declined.\n");
    send_message(player->socket, "You declined the challenge.\n");

    // Set players as in a game
    player->in_challenge = false;
    player->challenged_by = NULL;
    challenger->in_challenge = false;
    challenger->challenged = NULL;
}

void accept_challenge(Player *player) {
    if (!player->in_challenge) {
        send_message(player->socket, "You do not have pending challenge!\n");
        return;
    }
    Player *challenger = find_player_by_pseudo(player->challenged_by);
    if (challenger == NULL || !challenger->is_online) {
        player->challenged_by = NULL;
        send_message(player->socket, "User is not online anymore!\n");
        return;
    }
    send_message(player->socket, "You accepted the challenge!\n");
    send_message(challenger->socket, "Your challenge has been accepted!\n");

    player->in_challenge = false;
    player->challenged_by = NULL;
    challenger->in_challenge = false;
    challenger->challenged = NULL;

    player->in_game = true;
    challenger->in_game = true;

    initialize_game(player, challenger);
}


void send_boards_players(Player *current, Player *opponent) {
    send_board(current->socket, current, opponent);
    send_board(opponent->socket, opponent, current);
}

void send_boards(Game *game) {
    send_board(game->player1->socket, game->player1, game->player2);
    send_board(game->player2->socket, game->player2, game->player1);

    for (int i = 0; i < game->observer_count; i++) {
        if (game->observers[i]->socket > 0) { // Ensure valid socket
            send_board(game->observers[i]->socket, game->player1, game->player2);
        }
    }
}


void send_board(int socket, Player *player1, Player *player2) {
    char board[BUFFER_SIZE];

// Send board to client
    snprintf(board, sizeof(board),
             "\nGame Board:\n"
             "      +----+----+----+----+----+----+ %s\n"
             "      | %2d | %2d | %2d | %2d | %2d | %2d |Store: %2d\n"
             "      +----+----+----+----+----+----+\n"
             "      +----+----+----+----+----+----+\n"
             "      | %2d | %2d | %2d | %2d | %2d | %2d |Store: %2d\n"
             "      +----+----+----+----+----+----+ %s\n",
             player2->pseudo,
             player2->pits[5], player2->pits[4], player2->pits[3], player2->pits[2], player2->pits[1],
             player2->pits[0],
             player2->store,
             player1->pits[0], player1->pits[1], player1->pits[2], player1->pits[3], player1->pits[4],
             player1->pits[5],
             player1->store,
             player1->pseudo
    );

    send_message(socket, board);
}


void handle_challenge(Player *player, char *command) {
    if (player->in_challenge) {
        send_message(player->socket, "Wait for a response from previous player challenged\n");
        return;
    }
    if (player->in_game) {
        send_message(player->socket, "You are already in game\n");
        return;
    }

    char challenge_user[MAX_PSEUDO_LEN];
    if (sscanf(command, "CHALLENGE %10s", challenge_user) == 1) { // Limit pseudo to MAX_PSEUDO_LEN
        // Validate the pseudo
        if (strlen(challenge_user) == 0 || strlen(challenge_user) > MAX_PSEUDO_LEN) {
            send_message(player->socket, "Error reading challenged pseudo\n");
            return;
        }
    }

    verify_not_self_challenge(player, challenge_user);

    Player *challenged = find_player_by_pseudo(challenge_user);

    if (!is_valid_challenge(player, challenged)) {
        return;
    }

    player->in_challenge = true;
    challenged->in_challenge = true;

    challenged->challenged_by = player->pseudo;
    player->challenged = challenged->pseudo;

    send_challenge(player, challenged);
    notify_challenge_sent(player->socket);
}

bool verify_not_self_challenge(Player *player, char *challenge_user) {
    if (strcmp(player->pseudo, challenge_user) == 0) {
        send_message(player->socket, "You cannot challenge yourself!\n");
        return false;
    }
    return true;
}

bool is_valid_challenge(Player *player, Player *challenged) {
    if (challenged == NULL) {
        send_message(player->socket, "The player does not exist.\n");
        return false;
    }
    if (!challenged->is_online) {
        send_message(player->socket, "The player is not online.\n");
        return false;
    }
    if (challenged->in_game) {
        send_message(player->socket, "The player is already in game.\n");
        return false;
    }
    if (challenged->in_challenge) {
        send_message(player->socket, "The player is already challenged by another user\n");
        return false;
    }
    return true;
}

void notify_challenge_sent(int socket) {
    send_message(socket, "Challenge sent. Waiting for response...\n");
}

void send_challenge(Player *player, Player *challenged) {
    char challenge_notification[BUFFER_SIZE];
    snprintf(challenge_notification, sizeof(challenge_notification),
             "%s is challenging you! Do you accept?\n", player->pseudo);
    send_message(challenged->socket, challenge_notification);
}

void end_game(Player *player1, Player *player2, int result, Game *game) {
    char winner[MAX_PSEUDO_LEN];
    if (result == 0) {
        // Tie condition
        snprintf(winner, sizeof(winner), "It's a tie!\n");
    } else if (result == 1) {
        // Player 1 wins
        snprintf(winner, sizeof(winner), "%s wins!\n", player1->pseudo);
    } else {
        // Player 2 wins
        snprintf(winner, sizeof(winner), "%s wins!\n", player2->pseudo);
    }

    // Send the result to both players
    send_message(player1->socket, winner);
    send_message(player2->socket, winner);

    // Clean up game state
    remove_game(player1->game_id);

    // Exit the game loop or close sockets if necessary
}

int capture_seeds(Player *current_player, Player *opponent, int last_pit) {
    char message[BUFFER_SIZE];
    int captured_seeds = 0;

    // Notify both players that we are checking for captures
    snprintf(message, sizeof(message), "Checking for captures at pit %d...\n", last_pit);
    send_message(current_player->socket, message);
    send_message(opponent->socket, message);

    while (last_pit >= 0 && (opponent->pits[last_pit] == 2 || opponent->pits[last_pit] == 3)) {
        // Capture the seeds from the opponent's pit
        captured_seeds += opponent->pits[last_pit];
        opponent->pits[last_pit] = 0; // Empty the opponent's pit

        last_pit--; // Move to the next pit on the opponent's side
    }

    // Add the captured seeds to the current player's store
    current_player->store += captured_seeds;

    // Notify both players about the capture result
    snprintf(message, sizeof(message), "%s captured %d seeds. Their store now has %d seeds.\n",
             current_player->pseudo, captured_seeds, current_player->store);
    send_message(current_player->socket, message);

    snprintf(message, sizeof(message), "%s captured seeds from your side. Their store now has %d seeds.\n",
             current_player->pseudo, current_player->store);
    send_message(opponent->socket, message);

    return captured_seeds;
}

void make_move(Player *player, char *command) {
    Game *game = active_games[player->game_id];
    if (game == NULL) {
        send_message(player->socket, "You are not currently in a game!\n");
        return;
    }

    printf("%s\n", game->current_turn);
    if (strcmp(player->pseudo, game->current_turn) != 0) {
        send_message(player->socket, "Wait for your turn!\n");
        return;
    }

    int pit_index = -1;

    if (sscanf(command, "MAKE_MOVE %2d", &pit_index) == 1) { // Limit to 2 digits

        if (pit_index < 1 || pit_index > PITS) {
            send_message(player->socket, "Invalid pit selection. Please choose a valid pit.\n");
            return;
        }

        if (player->pits[pit_index - 1] == 0) { // Adjust for 0-based indexing
            send_message(player->socket, "Pit has no seeds. Please choose again.\n");
            return;
        }
        pit_index--; // Convert to 0-based indexing

        add_move(player, pit_index, player->pits[pit_index]);

        Player *opponent;
        if (strcmp(player->pseudo, game->player1->pseudo) == 0) {
            opponent = game->player2;
        } else {
            opponent = game->player1;
        }

        if (opponent == NULL) {
            fprintf(stderr, "ERROR: Opponent is NULL\n");
            return;
        }

        distribute_seeds(player, opponent, pit_index);

        printf("AFTER DISTRIBUTE\n");
        send_boards(game);

        if (is_game_over(*player, *opponent)) {
            printf("SADDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD\n");
            int result = player->store > opponent->store ? 1 : (player->store < opponent->store ? -1 : 0);
            end_game(player, opponent, result, game);
            return;
        }

        send_message(player->socket, "Your turn is over.\n");
        send_message(opponent->socket, "Your turn!\n");
        strcpy(game->current_turn, opponent->pseudo);


    } else {
        send_message(player->socket, "Invalid command format. Use: MAKE_MOVE <pit_number>\n");
    }

    printf("DEBUG: Exiting make_move function\n");
}


void tie(Player *current_player, Player *opponent, Game game) {
    char response;
    send_message(current_player->socket, "Do you want to propose a tie? (y/n): ");
    recv(current_player->socket, &response, 1, 0); // Get response from player

    if (response == 'y' || response == 'Y') {
        send_message(opponent->socket, "Your opponent proposed a tie. Do you accept? (y/n): ");
        recv(opponent->socket, &response, 1, 0); // Get response from opponent

        if (response == 'y' || response == 'Y') {
            end_game(current_player, opponent, 0, &game); // Tie the game
        } else {
            send_message(current_player->socket, "The tie proposal was rejected. The game continues.\n");
        }
    }
}

void add_move(Player *player, int pit_index, int seeds_before_move) {
    Move *new_move = (Move *) malloc(sizeof(Move));
    if (!new_move) {
        perror("Failed to allocate memory for move");
        exit(1);
    }

    new_move->pit_index = pit_index;
    new_move->seeds_before_move = seeds_before_move;
    new_move->next = NULL;

    if (!player->move_history) {
        player->move_history = new_move;
    } else {
        Move *current = player->move_history;
        while (current->next) {
            current = current->next;
        }
        current->next = new_move;
    }
}

int distribute_seeds(Player *current_player, Player *opponent, int pit_index) {
    int curr_pit = pit_index;
    int seeds = current_player->pits[pit_index];
    current_player->pits[pit_index] = 0;

    while (1) {
        for (; curr_pit < PITS && seeds > 0; curr_pit++) {
            if (pit_index == curr_pit) continue;
            seeds--;
            current_player->pits[curr_pit]++;
        }
        if (seeds == 0) break;
        curr_pit = 0;
        for (; curr_pit < PITS && seeds > 0; curr_pit++) {
            seeds--;
            opponent->pits[curr_pit]++;
            if (seeds == 0) {
                capture_seeds(current_player, opponent, curr_pit);
            }

        }
        curr_pit = 0;
        if (seeds == 0) break;
    }

    return curr_pit; // Return the last pit index
}

int is_game_over(Player player1, Player player2) {
    printf("ULALLALALL\n");

    if (player1.store >= 25 || player2.store >= 25) {
        return 1; // Game is over
    }

    if (dead(player1) && !is_savior(player2)) {
        return 1; // Player 1 has no seeds and Player 2 is not a savior
    } else if (dead(player2) && !is_savior(player1)) {
        return 1; // Player 2 has no seeds and Player 1 is not a savior
    }

    return 0; // Game continues
}

// Function to check if a player is dead (no seeds left in pits)
int dead(Player player) {
    for (int i = 0; i < PITS; i++) {
        if (player.pits[i] > 0) {
            return 0; // Player is not dead
        }
    }
    return 1; // Player is dead
}

// Function to check if a player can act as a savior (repopulate seeds)
int is_savior(Player player) {
    int total_seeds = 0;
    for (int i = 0; i < PITS; i++) {
        total_seeds += player.pits[i];
    }
    return total_seeds > 1; // Can act as savior if more than 1 seed
}

void clean_bio(char *bio) {
    char *read_ptr = bio;  // Pointer for reading through the bio
    char *write_ptr = bio; // Pointer for writing cleaned bio
    bool in_text = false;  // Tracks if we've encountered text on a line
    bool last_line_empty = false; // Tracks consecutive empty lines

    while (*read_ptr) {
        // Skip leading spaces and tabs at the start of each line
        while (*read_ptr == ' ' || *read_ptr == '\t') {
            read_ptr++;
        }

        // Check if the current line is empty
        if (*read_ptr == '\n') {
            if (in_text && !last_line_empty) {
                *write_ptr++ = '\n'; // Write one newline for an empty line
                last_line_empty = true; // Mark the line as empty
            }
            read_ptr++; // Move to the next character
        } else {
            // Copy the current line or character
            *write_ptr++ = *read_ptr++;
            in_text = true;       // Mark that we're inside text
            last_line_empty = false; // Reset empty line tracking
        }
    }

    // Remove trailing newlines
    if (write_ptr > bio && *(write_ptr - 1) == '\n') {
        write_ptr--;
    }

    *write_ptr = '\0'; // Add null terminator to mark the end of the cleaned bio
}