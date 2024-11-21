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
#define REVOKE "REVOKE_CHALLENGE"
#define PENDING "PENDING"
#define ACCEPT "ACCEPT"
#define DECLINE "DECLINE"
#define OBSERVE "OBSERVE"
#define QUIT_OBSERVE "QUIT_OBSERVE"
#define VIEW_FRIEND_LIST "VIEW_FRIEND_LIST"
#define ADD_FRIEND "ADD_FRIEND"
#define REMOVE_FRIEND "REMOVE_FRIEND"
#define PUBLIC "PUBLIC"
#define PRIVATE "PRIVATE"
#define ACCESS "ACCESS"
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
#define MAX_FRIENDS 20
#define MAX_GAMES 50
#define BUFFER_SIZE 1024
#define PLAYER_FILE "players.txt"
#define PITS 6  // Number of pits per player
#define INITIAL_SEEDS 4  // Initial seeds in each pit

#define MAX_PSEUDO_LEN 11
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
    int friend_count;

    int pits[PITS];
    int store;

    Move *move_history;
    char observing[MAX_PSEUDO_LEN];
    int game_id;
    char challenged_by[MAX_PSEUDO_LEN];
    char challenged[MAX_PSEUDO_LEN];
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

void notify_move(const char *player_pseudo, int pit_index, Game *game);

int answer(int sockfd);

Player *find_player_by_pseudo(const char *pseudo);

Player *handle_registration(char *pseudo, char *password, int client_socket);

Player *handle_login(char *pseudo, char *password, int client_socket);

void handle_logout(Player *player);

void update_access(Player *player, int private);

void send_access(Player *player);

void send_global_message(Player *player, char *buffer);

void send_game_message(Player *player, char *buffer);

void send_direct_message(Player *player, char *buffer);

void update_observers(int game_id);

bool can_observe(Player *player, int game_id);

bool in_friend_list(Player *player, Player *target);

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

void send_friend_list(Player *player);

void handle_add_friend(Player *player, char *command);

void handle_remove_friend(Player *player, char *command);

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

void handle_observe(Player *player, char *command);

void handle_quit_observe(Player *player);

void add_observer(Player *observer, Player *to_observe);

void remove_observer(Player *observer);

/** CHALLENGE */
void send_pending_challenge(Player *player);

void handle_challenge(Player *player, char *command);

void handle_revoke_challenge(Player *player);

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
        } else if (strcmp(command, REVOKE) == 0) {
            handle_revoke_challenge(player);
        } else if (strcmp(command, PENDING) == 0) {
            send_pending_challenge(player);
        } else if (strcmp(command, ACCEPT) == 0) {
            accept_challenge(player);
        } else if (strcmp(command, DECLINE) == 0) {
            decline_challenge(player);
        } else if (strcmp(command, MAKE_MOVE) == 0) {
            make_move(player, buffer);
        } else if (strcmp(command, SHOW_GAMES) == 0) {
            send_active_games(player);
        } else if (strcmp(command, OBSERVE) == 0) {
            handle_observe(player, buffer);
        } else if (strcmp(command, QUIT_OBSERVE) == 0) {
            handle_quit_observe(player);
        } else if (strcmp(command, ADD_FRIEND) == 0) {
            handle_add_friend(player, buffer);
        } else if (strcmp(command, REMOVE_FRIEND) == 0) {
            handle_remove_friend(player, buffer);
        } else if (strcmp(command, VIEW_FRIEND_LIST) == 0) {
            send_friend_list(player);
        } else if (strcmp(command, PRIVATE) == 0) {
            update_access(player, 1);
        } else if (strcmp(command, PUBLIC) == 0) {
            update_access(player, 0);
        } else if (strcmp(command, ACCESS) == 0) {
            send_access(player);
        } else if (strcmp(command, GLOBAL_MESSAGE) == 0) {
            send_global_message(player, buffer);
        } else if (strcmp(command, GAME_MESSAGE) == 0) {
            send_game_message(player, buffer);
        } else if (strcmp(command, DIRECT_MESSAGE) == 0) {
            send_direct_message(player, buffer);
        } else {
            printf("Unknown command: %s\n", command);
        }
        memset(buffer, 0, sizeof(buffer));
    }
}

// Load players from file
void load_players_from_file() {
    FILE *file = fopen(PLAYER_FILE, "r");
    if (file) {
        char pseudo[MAX_PSEUDO_LEN], password[HASH_SIZE];
        char line[256];  // Temporary buffer to read lines from file
        int private;

        while (fscanf(file, "%s %s %d", pseudo, password, &private) == 3) {
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (players[i].pseudo[0] == '\0') {
                    // Fill player information
                    strcpy(players[i].pseudo, pseudo);
                    strcpy(players[i].password, password);
                    players[i].is_online = false;
                    players[i].private = private;
                    players[i].socket = -1;
                    players[i].game_id = -1;
                    players[i].friend_count = 0;
                    players[i].challenged_by[0] = '\0';
                    players[i].challenged[0] = '\0';
                    players[i].observing[0] = '\0';
                    players[i].bio[0] = '\0';  // Initialize the bio to be empty

                    // Initialize the friends array to empty strings
                    for (int j = 0; j < MAX_FRIENDS; j++) {
                        players[i].friends[j][0] = '\0';
                    }

                    // Read through the file until "bio:" or "friends:" are processed
                    while (fgets(line, sizeof(line), file)) {
                        // Read friends list (looking for "friends:")
                        if (strncmp(line, "friends:", 8) == 0) {
                            if (line[strlen(line) - 1] == '\n') {
                                line[strlen(line) - 1] = '\0';
                            }
                            char *friends_list = line + 9;
                            char *friend_name = strtok(friends_list, " ");

                            // Extract each friend's name
                            while (friend_name != NULL && players[i].friend_count < MAX_FRIENDS) {
                                if (strcmp(friend_name, "\n") == 0) {
                                    break;
                                }
                                strncpy(players[i].friends[players[i].friend_count], friend_name,
                                        MAX_PSEUDO_LEN - 1);
                                players[i].friends[players[i].friend_count][MAX_PSEUDO_LEN - 1] = '\0';
                                players[i].friend_count++;
                                friend_name = strtok(NULL, " "); // Move to the next name
                            }
                        }

                        // Read bio data (looking for "bio:")
                        if (strncmp(line, "bio:", 4) == 0) {
                            players[i].bio[0] = '\0';  // Clear bio before appending new content
                            while (fgets(line, sizeof(line), file)) {
                                if (strcmp(line, "\r\n") == 0) {
                                    continue;
                                }
                                // If we hit the "-----" separator, we stop reading bio data
                                if (strncmp(line, "-----", 5) == 0) {
                                    break;
                                }
                                // Append bio content with a newline (remove trailing newline first)
                                line[strcspn(line, "\n")] = 0;
                                if (strlen(players[i].bio) > 0) {
                                    strcat(players[i].bio, "\n");
                                }
                                strcat(players[i].bio, line);
                            }
                        }

                        // Stop processing after reaching the separator
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
    pthread_mutex_lock(&player_mutex);

    FILE *file = fopen(PLAYER_FILE, "w");
    if (!file) {
        perror("Error opening file for writing");
        return;
    }

    // Write all players to the file
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].pseudo[0] != '\0') {  // Check if player slot is not empty
            fprintf(file, "%s %s %d\n", players[i].pseudo, players[i].password, players[i].private);

            // Write friends to the file
            fprintf(file, "friends: ");
            for (int j = 0; j < MAX_FRIENDS && players[i].friends[j][0] != '\0'; j++) {
                fprintf(file, "%s ", players[i].friends[j]);
            }
            fprintf(file, "\n");

            // Write bio to the file
            fprintf(file, "bio:\n");
            if (players[i].bio[0] != '\0') {
                fprintf(file, "%s\n", players[i].bio);
            }

            fprintf(file, "-----\n");
        }
    }

    fclose(file);
    pthread_mutex_unlock(&player_mutex);
    printf("File updated successfully.\n");
}

void send_access(Player *player) {
    if (player->private) {
        send_message(player->socket, "Your access level is private.\n");
    } else {
        send_message(player->socket, "Your access level is public.\n");
    }
}

void send_game_message(Player *player, char *buffer) {
    char message[BUFFER_SIZE];

    // Clear the message buffer
    memset(message, 0, sizeof(message));

    // Format the message
    snprintf(message, sizeof(message), "GAME %s: %s\n", player->pseudo, buffer + strlen("GAME_MESSAGE "));

    Game *game = NULL;  // Initialize game to NULL

    if (player->game_id != -1) {
        game = active_games[player->game_id];  // Find game by game_id
    } else if (player->observing[0] != '\0') {
        Player *playing = find_player_by_pseudo(player->observing);  // Find player being observed
        if (playing != NULL) {
            game = active_games[playing->game_id];  // Find game of the observed player
        }
    }

// Check if the game is found
    if (game == NULL) {
        send_message(player->socket, "Game not found\n");  // Send message if game is not found
        memset(message, 0, sizeof(message));  // Clear message buffer
        return;  // Exit the function
    }


    int skip_check = 0;
    if (strcmp(player->pseudo, game->player1->pseudo) == 0) {
        skip_check = 1;
    } else {
        send_message(game->player1->socket, message);
    }

    if (skip_check) {
        send_message(game->player2->socket, message);
    } else if (strcmp(player->pseudo, game->player2->pseudo) == 0) {
        skip_check = 1;
    }

    for (int i = 0; i < game->observer_count; ++i) {
        if (skip_check) {
            send_message(game->observers[i]->socket, message);
        } else if (strcmp(player->pseudo, game->observers[i]->pseudo) == 0) {
            skip_check = 1;
        }
    }

    memset(message, 0, sizeof(message));

}


void send_global_message(Player *player, char *buffer) {
    char message[BUFFER_SIZE];

    // Clear the message buffer
    memset(message, 0, sizeof(message));

    // Format the messagef
    snprintf(message, sizeof(message), "GL %s: %s\n", player->pseudo, buffer + strlen("GLOBAL_MESSAGE "));

    // Iterate over all players
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].is_online) {
            // Skip sending the message to the sender
            if (strcmp(players[i].pseudo, player->pseudo) == 0) {
                continue;
            }
            // Send the message to other online players
            send_message(players[i].socket, message);
        }
    }

    // Ensure the buffer is cleared after use
    memset(message, 0, sizeof(message));
}


void send_direct_message(Player *player, char *buffer) {
    char message[BUFFER_SIZE];
    char pseudo[BUFFER_SIZE];

    memset(message, 0, sizeof(message));
    memset(pseudo, 0, sizeof(pseudo));

    // Extract pseudo and message from the buffer
    if (sscanf(buffer, "DIRECT_MESSAGE %s %[^\n]", pseudo, message) != 2) {
        printf("Invalid command format.\n");
        memset(pseudo, 0, sizeof(pseudo));
        memset(message, 0, sizeof(message));
        return;
    }


    Player *target = find_player_by_pseudo(pseudo);
    if (target == NULL) {
        send_message(player->socket, "Player not found\n");
        memset(pseudo, 0, sizeof(pseudo));
        memset(message, 0, sizeof(message));
        return;
    }
    if (!target->is_online) {
        send_message(player->socket, "Player is not online\n");
        memset(pseudo, 0, sizeof(pseudo));
        memset(message, 0, sizeof(message));
        return;
    }


    char formatted_message[BUFFER_SIZE];
    snprintf(formatted_message, sizeof(formatted_message), "From %s: %s\n", player->pseudo, message);

    send_message(target->socket, formatted_message);

    memset(pseudo, 0, sizeof(pseudo));
    memset(message, 0, sizeof(message));
}

void update_access(Player *player, int private) {
    player->private = private;
    if (player->game_id != -1 && private) {
        update_observers(player->game_id);
    }
    update_players_file();
    send_access(player);
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
    memset(bio_output, 0, sizeof(bio_output));
}

void handle_update_bio(Player *player, char *command) {
    // Create a buffer to hold the entire bio
    char bio[MAX_BIO_LINES * MAX_BIO_LINE_LENGTH];
    memset(bio, 0, sizeof(bio));  // Clear the buffer before use

    // Extract the bio content from the command
    if (sscanf(command, "UPDATE_BIO %[^\n]", bio) != 1) {
        send_message(player->socket, "Error reading bio\n");
        memset(bio, 0, sizeof(bio));
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
    memset(bio, 0, sizeof(bio));
}


void handle_see_player_bio(Player *player_target, char *command) {
    char pseudo[MAX_PSEUDO_LEN];
    if (sscanf(command, "VIEW_PLAYER_BIO %10s", pseudo) == 1) { // Limit pseudo to MAX_PSEUDO_LEN
        // Validate the pseudo
        if (strlen(pseudo) == 0 || strlen(pseudo) > MAX_PSEUDO_LEN) {
            send_message(player_target->socket, "Error reading pseudo\n");
            memset(pseudo, 0, sizeof(pseudo));
            return;
        }
    }

    Player *player = find_player_by_pseudo(pseudo);
    if (player == NULL) {
        send_message(player_target->socket, "Player not found\n");
    }

    char bio_output[MAX_BIO_LINES * MAX_BIO_LINE_LENGTH] = "Bio:\n";

    if (strlen(player->bio) > 0) {
        strcat(bio_output, player->bio);
        strcat(bio_output, "\n");
        send_message(player_target->socket, bio_output);
    } else {
        send_message(player_target->socket, "The player hasn't added a bio yet.\n");
    }
    memset(pseudo, 0, sizeof(pseudo));
}

void send_active_games(Player *player) {
    pthread_mutex_lock(&player_mutex);
    char response[BUFFER_SIZE];

    strcpy(response, "Active games:\n");
    for (int i = 0; i < active_game_count; i++) {
        Game *game = active_games[i];
        char game_info[MAX_PSEUDO_LEN * 2 + 6];
        snprintf(game_info, sizeof(game_info), "%s VS %s\n",
                 game->player1->pseudo,
                 game->player2->pseudo);
        strcat(response, game_info);
    }
    send_message(player->socket, response);

    memset(response, 0, sizeof(response));
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
    memset(response, 0, sizeof(response));
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
    memset(response, 0, sizeof(response));
    pthread_mutex_unlock(&player_mutex);
}


void save_player_to_file(Player *player) {
    FILE *file = fopen(PLAYER_FILE, "a");
    if (file) {
        fprintf(file, "%s %s %d\n", player->pseudo, player->password, player->private);

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
        answer(client_socket);
        return NULL;
    }

    pthread_mutex_lock(&player_mutex);

    if (is_pseudo_taken(pseudo)) {
        send_message(client_socket, "Pseudo already taken!\n");
        answer(client_socket);
        return NULL;
    }

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].is_online && strcmp(players[i].pseudo, pseudo) == 0) {
            send_message(client_socket, "Pseudo already taken!\n");
            answer(client_socket);
            return NULL;
        }
    }

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!players[i].is_online && players[i].pseudo[0] == '\0') {
            strcpy(players[i].pseudo, pseudo);
            strcpy(players[i].password, password);
            players[i].socket = client_socket;
            players[i].is_online = true;
            players[i].private = false;

            players[i].challenged_by[0] = '\0';
            players[i].challenged[0] = '\0';
            players[i].observing[0] = '\0';
            players[i].game_id = -1;

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
    printf("Logging out: %s\n", player->pseudo);

    if (player->game_id != -1) {
        remove_game(player->game_id);
    }
    pthread_mutex_lock(&player_mutex);

    if (player->challenged_by[0] != '\0') {
        Player *challenged_by_player = find_player_by_pseudo(player->challenged_by);
        if (challenged_by_player != NULL) {
            challenged_by_player->challenged[0] = '\0';
        }
        player->challenged_by[0] = '\0';
    }

    if (player->challenged[0] != '\0') {
        Player *challenged_player = find_player_by_pseudo(player->challenged);
        if (challenged_player != NULL) {
            challenged_player->challenged_by[0] = '\0';
        }
        player->challenged[0] = '\0';
    }

    if (player->observing[0] != '\0') {
        remove_observer(player);
    }

    player->is_online = false;
    player->socket = -1;


    printf("Player logged out: %s\n", player->pseudo);

    close(player->socket);
    pthread_mutex_unlock(&player_mutex);
    pthread_exit(NULL);
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

    pthread_mutex_lock(&player_mutex);
    new_game->observer_count = 0;
    // Assign players to the game
    new_game->player1 = player1;
    new_game->player2 = player2;
    player1->game_id = id;
    player2->game_id = id;
    pthread_mutex_unlock(&player_mutex);

    // Initialize pits for both players
    initialize_board(new_game);

    // Player 1 starts
    srand(time(NULL));
    int turn = rand() % 2;

    if (turn == 1) {
        strcpy(new_game->current_turn, player1->pseudo);
    } else {
        strcpy(new_game->current_turn, player2->pseudo);
    }
    send_game_start_message(player1->socket, player2->socket, turn);
    send_boards_players(new_game->player1, new_game->player2);
}

void clean_player_game_state(Player *player) {
    player->game_id = -1;
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
    pthread_mutex_lock(&player_mutex);
    for (int i = 0; i < PITS; i++) {
        game->player1->pits[i] = INITIAL_SEEDS;
        game->player2->pits[i] = INITIAL_SEEDS;
    }
    game->player1->store = 0;
    game->player2->store = 0;
    pthread_mutex_unlock(&player_mutex);
}


int add_game(Game *new_game) {
    pthread_mutex_lock(&player_mutex);

    if (active_game_count >= MAX_GAMES) {
        return -1; // Cannot add more games, array is full
    }

    active_games[active_game_count++] = new_game;
    pthread_mutex_unlock(&player_mutex);
    return active_game_count - 1;
}

void remove_game(int game_id) {
    pthread_mutex_lock(&player_mutex);
    for (int i = 0; i < active_game_count; i++) {
        if (i == game_id) {
            // Shift all games after the found game to fill the gap
            clean_up_game(active_games[game_id]);
            free(active_games[game_id]);
            for (int j = i; j < active_game_count - 1; j++) {
                active_games[j + 1]->player1->game_id = j;
                active_games[j + 1]->player2->game_id = j;
                active_games[j] = active_games[j + 1];
            }
            active_game_count--;
            break;
        }
    }
    pthread_mutex_unlock(&player_mutex);
}

void decline_challenge(Player *player) {
    if (player->challenged_by[0] == '\0') {
        send_message(player->socket, "You do not have a pending challenge!\n");
        return;
    }

    Player *challenger = find_player_by_pseudo(player->challenged_by);
    if (challenger == NULL || !challenger->is_online) {
        player->challenged_by[0] = '\0';
        send_message(player->socket, "User is not online anymore!\n");
        return;
    }

    pthread_mutex_lock(&player_mutex);
    player->challenged_by[0] = '\0';
    challenger->challenged[0] = '\0';
    pthread_mutex_unlock(&player_mutex);

    send_message(challenger->socket, "Your challenge has been declined.\n");
    send_message(player->socket, "You declined the challenge.\n");
}

void accept_challenge(Player *player) {
    if (player->challenged_by[0] == '\0') {
        send_message(player->socket, "You do not have a pending challenge!\n");
        return;
    }

    Player *challenger = find_player_by_pseudo(player->challenged_by);
    if (challenger == NULL || !challenger->is_online) {
        player->challenged_by[0] = '\0';
        send_message(player->socket, "User is not online anymore!\n");
        return;
    }

    if (player->observing[0] != '\0') {
        player->observing[0] = '\0';
    }

    pthread_mutex_lock(&player_mutex);
    player->challenged_by[0] = '\0';
    challenger->challenged[0] = '\0';
    pthread_mutex_unlock(&player_mutex);

    send_message(player->socket, "You accepted the challenge!\n");
    send_message(challenger->socket, "Your challenge has been accepted!\n");

    initialize_game(player, challenger);
}


void notify_move(const char *player_pseudo, int pit_index, Game *game) {
    char message[32];  // To hold the message "<pseudo> chose pit <index>"

    // Compose the message
    snprintf(message, sizeof(message), "%s chose pit %d\n", player_pseudo, pit_index + 1);

    if (strcmp(game->player1->pseudo, player_pseudo) == 0) {
        send_message(game->player2->socket, message);
    } else {
        send_message(game->player1->socket, message);
    }

    for (int i = 0; i < game->observer_count; i++) {
        send_message(game->observers[i]->socket, message);
    }
    memset(message, 0, sizeof(message));
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
    memset(board, 0, sizeof(board));
}

bool in_friend_list(Player *player, Player *target) {
    for (int i = 0; i < target->friend_count; i++) {
        if (strcmp(player->pseudo, target->friends[i]) == 0) {
            return 1;  // Player is in the friend list, they can observe
        }
    }

    return 0;
}


bool can_observe(Player *player, int game_id) {
    Game *game = active_games[game_id];

    if (game->player1->private || game->player2->private) {
        return in_friend_list(player, game->player1) || in_friend_list(player, game->player2);
    }

    return 1;
}

void update_observers(int game_id) {
    Game *game = active_games[game_id];
    if (game == NULL) {
        pthread_mutex_unlock(&player_mutex);
        return;
    }

    if (game->observer_count == 0) {
        pthread_mutex_unlock(&player_mutex);
        return;
    }

    for (int i = 0; i < game->observer_count; ++i) {
        Player *obs = game->observers[i];
        if (!can_observe(obs, game_id)) {
            send_message(obs->socket, "One or both players is/are in private mode, only friends can observe\n");
            remove_observer(obs);
        }
    }

}

void add_observer(Player *observer, Player *to_observe) {
    pthread_mutex_lock(&player_mutex);
    Game *game = active_games[to_observe->game_id];

    if (game == NULL) {
        send_message(observer->socket, "Error finding the game...\n");
        pthread_mutex_unlock(&player_mutex);
        return;
    }

    if (game->observer_count >= MAX_PLAYERS) {
        send_message(observer->socket, "Observer limit reached for this game\n");
        pthread_mutex_unlock(&player_mutex);
        return;
    }

    strcpy(observer->observing, to_observe->pseudo);
    game->observers[game->observer_count] = observer;
    game->observer_count++;
    send_message(observer->socket, "Now you are observing the game\n");

    pthread_mutex_unlock(&player_mutex);
}

void remove_observer(Player *observer) {
    Player *player = find_player_by_pseudo(observer->observing);
    if (player == NULL) {
        send_message(observer->socket, "Error finding the game...\n");
        return;
    }

    Game *game = active_games[player->game_id];

    if (game == NULL) {
        send_message(observer->socket, "Error finding the game...\n");
        return;
    }

    pthread_mutex_lock(&player_mutex);

    int observer_index = -1;
    for (int i = 0; i < game->observer_count; i++) {
        if (game->observers[i] == observer) {
            observer_index = i;
            break;
        }
    }

    if (observer_index == -1) {
        send_message(observer->socket, "You are not observing this game\n");
        return;
    }

    for (int i = observer_index; i < game->observer_count - 1; i++) {
        game->observers[i] = game->observers[i + 1];
    }

    game->observers[game->observer_count - 1] = NULL; // Clear the last entry
    game->observer_count--;

    send_message(observer->socket, "You have been removed from observing the game\n");
    pthread_mutex_unlock(&player_mutex);
}

void handle_quit_observe(Player *player) {
    if (player->observing[0] == '\0') {
        send_message(player->socket, "You are not currently observing any game\n");
        return;
    }

    remove_observer(player);
}

void handle_observe(Player *player, char *command) {
    char pseudo[MAX_PSEUDO_LEN];
    if (sscanf(command, "OBSERVE %10s", pseudo) == 1) { // Limit pseudo to MAX_PSEUDO_LEN
        // Validate the pseudo
        if (strlen(pseudo) == 0 || strlen(pseudo) > MAX_PSEUDO_LEN) {
            send_message(player->socket, "Error reading challenged pseudo\n");
            memset(pseudo, 0, sizeof(pseudo));
            return;
        }
    }
    if (strlen(pseudo) == 0) {
        send_message(player->socket, "Error reading challenged pseudo\n");
        memset(pseudo, 0, sizeof(pseudo));
        return;
    }

    pthread_mutex_lock(&player_mutex);
    Player *to_observe = find_player_by_pseudo(pseudo);
    memset(pseudo, 0, sizeof(pseudo));

    if (to_observe->game_id == -1) {
        send_message(player->socket, "Player is not in the game\n");
        return;
    }

    if (!can_observe(player, to_observe->game_id)) {
        send_message(player->socket, "One or both players is/are in private mode, only friends can observe\n");
        return;
    }
    pthread_mutex_unlock(&player_mutex);

    add_observer(player, to_observe);

}

void send_friend_list(Player *player) {
    if (player->friend_count == 0) {
        send_message(player->socket, "You did not add any friends yet...\n");
        return;
    }

    // Build the list of friends as a message
    char message[1024] = "Your friends are:\n";

    for (int i = 0; i < player->friend_count; i++) {
        // Append each friend's pseudo to the message
        strcat(message, player->friends[i]);
        strcat(message, "\n");
    }

    // Send the list of friends to the player
    send_message(player->socket, message);
    memset(message, 0, sizeof(message));
}

void handle_remove_friend(Player *player, char *command) {
    char pseudo[MAX_PSEUDO_LEN];
    if (sscanf(command, "REMOVE_FRIEND %10s", pseudo) == 1) { // Limit pseudo to MAX_PSEUDO_LEN
        // Validate the pseudo
        if (strlen(pseudo) == 0 || strlen(pseudo) > MAX_PSEUDO_LEN) {
            send_message(player->socket, "Error reading pseudo\n");
            memset(pseudo, 0, sizeof(pseudo));
            return;
        }
    }

    int friend_index = -1;
    for (int i = 0; i < player->friend_count; i++) {
        if (strcmp(player->friends[i], pseudo) == 0) {
            friend_index = i;
            break;
        }
    }

    memset(pseudo, 0, sizeof(pseudo));  // Clear the temporary variable
    if (friend_index == -1) {
        send_message(player->socket, "The player is not in your friend list\n");
        return;
    }

    for (int i = friend_index; i < player->friend_count - 1; i++) {
        strcpy(player->friends[i], player->friends[i + 1]);
    }
    player->friend_count--;
    send_message(player->socket, "Friend removed successfully\n");
}

void handle_add_friend(Player *player, char *command) {
    char pseudo[MAX_PSEUDO_LEN];
    if (sscanf(command, "ADD_FRIEND %10s", pseudo) == 1) { // Limit pseudo to MAX_PSEUDO_LEN
        // Validate the pseudo
        if (strlen(pseudo) == 0 || strlen(pseudo) > MAX_PSEUDO_LEN) {
            send_message(player->socket, "Error reading pseudo\n");
            memset(pseudo, 0, sizeof(pseudo));
            return;
        }
    }
    Player *friend_player = find_player_by_pseudo(pseudo);

    if (friend_player == NULL) {
        send_message(player->socket, "Player not found\n");
        memset(pseudo, 0, sizeof(pseudo));
        return;
    }

    if (strcmp(friend_player->pseudo, player->pseudo) == 0) {
        send_message(player->socket, "You can not add yourself as a friend\n");
        memset(pseudo, 0, sizeof(pseudo));
        return;
    }

    if (friend_player->friend_count >= MAX_FRIENDS) {
        send_message(player->socket, "Your friend's friend list is full\n");
        memset(pseudo, 0, sizeof(pseudo));
        return;
    }

    for (int i = 0; i < player->friend_count; i++) {
        if (strcmp(player->friends[i], pseudo) == 0) {
            send_message(player->socket, "This player is already your friend\n");
            return;
        }
    }

    strcpy(player->friends[player->friend_count], pseudo);
    player->friend_count++;
    printf("%d", player->friend_count);
    memset(pseudo, 0, sizeof(pseudo));

    update_players_file();

    send_message(player->socket, "Friend added successfully\n");
}

void send_pending_challenge(Player *player) {
    if (player->challenged_by[0] == '\0') {
        send_message(player->socket, "You do not have a pending challenge!\n");
        return;
    }

    char message[MAX_PSEUDO_LEN + 19];
    snprintf(message, sizeof(message), "Challenge from %s\n", player->challenged_by);
    send_message(player->socket, message);

    memset(message, 0, sizeof(message));
}

void handle_revoke_challenge(Player *player) {
    if (player->challenged[0] == '\0') {
        send_message(player->socket, "You did not challenge anyone yet\n");
        return;
    }

    Player *challenged = find_player_by_pseudo(player->challenged);
    if (challenged == NULL) {
        send_message(player->socket, "Error finding challenged player\n");
        return;
    }

    pthread_mutex_lock(&player_mutex);
    challenged->challenged_by[0] = '\0';
    player->challenged[0] = '\0';
    pthread_mutex_unlock(&player_mutex);

    send_message(player->socket, "You have revoked the challenge\n");
    send_message(challenged->socket, "The challenge was revoked\n");
}

void handle_challenge(Player *player, char *command) {
    if (player->observing[0] != '\0') {
        send_message(player->socket, "Stop observing before challenging\n");
        return;
    }
    if (player->challenged[0] != '\0') {
        send_message(player->socket, "Wait for a response from previous player challenged\n");
        return;
    }

    if (player->challenged_by[0] != '\0') {
        send_message(player->socket, "Accept or decline the pending challenge\n");
        return;
    }
    if (player->game_id != -1) {
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

    if (!verify_not_self_challenge(player, challenge_user)) {
        return;
    }

    Player *challenged = find_player_by_pseudo(challenge_user);

    if (!is_valid_challenge(player, challenged)) {
        return;
    }

    pthread_mutex_lock(&player_mutex);
    strcpy(challenged->challenged_by, player->pseudo);
    strcpy(player->challenged, challenged->pseudo);
    pthread_mutex_unlock(&player_mutex);

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
    if (challenged->game_id != -1) {
        send_message(player->socket, "The player is already in game.\n");
        return false;
    }
    if ((challenged->challenged_by[0] != '\0') || (challenged->challenged[0] != '\0')) {
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
    memset(challenge_notification, 0, sizeof(challenge_notification));
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
    memset(winner, 0, sizeof(winner));

    // Clean up game state
    remove_game(player1->game_id);
}

int capture_seeds(Player *current_player, Player *opponent, int last_pit) {
    char message[BUFFER_SIZE];
    int captured_seeds = 0;

    // Notify both players that we are checking for captures
//    snprintf(message, sizeof(message), "Checking for captures at pit %d...\n", last_pit);
//    send_message(current_player->socket, message);
//    send_message(opponent->socket, message);

    while (last_pit >= 0 && (opponent->pits[last_pit] == 2 || opponent->pits[last_pit] == 3)) {
        // Capture the seeds from the opponent's pit
        captured_seeds += opponent->pits[last_pit];
        opponent->pits[last_pit] = 0; // Empty the opponent's pit

        last_pit--; // Move to the next pit on the opponent's side
    }

    // Add the captured seeds to the current player's store
    current_player->store += captured_seeds;

    // Notify both players about the capture result
    if (captured_seeds > 0) {
        snprintf(message, sizeof(message), "%s captured %d seeds. Their store now has %d seeds.\n",
                 current_player->pseudo, captured_seeds, current_player->store);
        send_message(current_player->socket, message);
        snprintf(message, sizeof(message), "%s captured seeds from your side. Their store now has %d seeds.\n",
                 current_player->pseudo, current_player->store);
        send_message(opponent->socket, message);
    }

    memset(message, 0, sizeof(message));
    return captured_seeds;
}

void make_move(Player *player, char *command) {
    Game *game = active_games[player->game_id];
    if (game == NULL) {
        send_message(player->socket, "You are not currently in a game!\n");
        return;
    }

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

        notify_move(player->pseudo, pit_index, game);
        distribute_seeds(player, opponent, pit_index);

        send_boards(game);

        if (is_game_over(*player, *opponent)) {
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
}


//void tie(Player *current_player, Player *opponent, Game game) {
//    char response;
//    send_message(current_player->socket, "Do you want to propose a tie? (y/n): ");
//    recv(current_player->socket, &response, 1, 0); // Get response from player
//
//    if (response == 'y' || response == 'Y') {
//        send_message(opponent->socket, "Your opponent proposed a tie. Do you accept? (y/n): ");
//        recv(opponent->socket, &response, 1, 0); // Get response from opponent
//
//        if (response == 'y' || response == 'Y') {
//            end_game(current_player, opponent, 0, &game); // Tie the game
//        } else {
//            send_message(current_player->socket, "The tie proposal was rejected. The game continues.\n");
//        }
//    }
//    memset(*response, 0, sizeof(response));
//}

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