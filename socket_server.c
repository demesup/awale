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
#define OBSERVE_GAME "OBSERVE_GAME"
#define VIEW_FRIEND_LIST "VIEW_FRIEND_LIST"
#define ADD_FRIEND "ADD_FRIEND"
#define FRIENDS_ONLY "FRIENDS_ONLY"
#define PUBLIC_OBSERVE "PUBLIC_OBSERVE"
#define VIEW_BIO "VIEW_BIO"
#define VIEW_PLAYER_BIO "VIEW_PLAYER_BIO"
#define UPDATE_BIO "UPDATE_BIO"
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

    bool private;
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

void send_board(int socket, Player *player1, Player *player2);

void send_boards_players(Player *current, Player *opponent);

void send_boards(Game *game);


int answer(int sockfd);

Player *find_player_by_pseudo(const char *pseudo);

Player *handle_registration(char *pseudo, char *password, int client_socket);

Player *handle_login(char *pseudo, char *password, int client_socket);

void handle_logout(Player *player);

void handle_see_bio(Player *player, char *pseudo);

void save_player_to_file(Player *player);

void load_players_from_file();

bool is_pseudo_taken(const char *pseudo);

void send_online_players(Player *player);

void send_all_players(Player *player);

void menu(Player *player);

/** GAME */
void initialize_board(Game *game);

void initialize_game(Player *player1, Player *player2);

void send_game_start_message(int client_socket, int challenged_socket, int current_turn);

int capture_seeds(Player *current_player, Player *opponent, int last_pit);

void play_game(Game *new_game);

int is_game_over(Player player1, Player player2);

int dead(Player player);

int is_savior(Player player);

void add_move(Player *player, int pit_index, int seeds_before_move);

int distribute_seeds(Player *current_player, Player *opponent, int pit_index);

void end_game(Player *player1, Player *player2, int result, Game *game);

void player_turn(Player *current_player, Player *opponent, Game *game);

bool add_game(Game *new_game);

void remove_game(Game *game);

void clean_up_game(Game *game);

/** CHALLENGE */
void handle_challenge(Player *player);

bool get_challenged_user(Player *player, char *challenge_user);

bool is_valid_challenge(Player *player, Player *challenged);

void notify_challenge_sent(int socket);

void handle_challenged_response(char response, Player *player, Player *challenged);

void process_challenge_response(Player *player, Player *challenged);

void accept_challenge(Player *player, Player *challenged);

void decline_challenge(int client_socket, int challenged_socket);

void invalid_response(int client_socket, int challenged_socket);


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
    char command[BUFFER_SIZE];  // Buffer to hold the command received from the client
    int bytes_received;  // To store the number of bytes received from the socket

    while (1) {
        // Read the command from the player's socket
        bytes_received = read(player->socket, command, sizeof(command));
        if (bytes_received <= 0) {
            if (bytes_received == 0) {
                handle_logout(player);
            } else {
                printf("Error reading from client\n");
            }
            return;
        }
        command[bytes_received] = '\0'; // Null-terminate the received string

        // Extract the command (first word) before any space
        sscanf(command, "%s", command);  // This will extract the first word into 'command'

        printf(command);
        // Process the command
        if (strcmp(command, LOGOUT) == 0) {
            handle_logout(player);
        } else if (strcmp(command, SHOW_PLAYERS) == 0) {
            send_all_players(player);
        } else if (strcmp(command, SHOW_ONLINE) == 0) {
            send_online_players(player);
        } else if (strcmp(command, VIEW_BIO) == 0) {
            handle_see_bio(player, player->pseudo);
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
                                strcat(players[i].bio, line);
                                strcat(players[i].bio, " ");  // Add space after each line of bio
                            }
                        }

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

void handle_see_bio(Player *player, char *pseudo) {
    char bio_output[MAX_BIO_LINES * MAX_BIO_LENGTH] = "Bio:\n";
    int found = 0;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (strcmp(players[i].pseudo, pseudo) == 0) {
            found = 1;
            if (strlen(players[i].bio) > 0) {
                strcat(bio_output, players[i].bio);
                strcat(bio_output, "\n");
                send_message(player->socket, bio_output);
            } else {
                send_message(player->socket, "This player hasn't added a bio yet.\n");
            }
            break;
        }
    }

    if (!found) {
        snprintf(bio_output, sizeof(bio_output), "User '%s' not found.\n", pseudo);
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
    printf("SEnding");

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


bool add_game(Game *new_game) {
    if (active_game_count >= MAX_GAMES) {
        return false; // Cannot add more games, array is full
    }

    active_games[active_game_count++] = new_game;
    return true;
}


void initialize_game(Player *player1, Player *player2) {
    Game *new_game = malloc(sizeof(Game));
    if (!add_game(new_game)) {
        send_message(player1->socket, "Failed to start the game. Server capacity reached.\n");
        send_message(player2->socket, "Failed to start the game. Server capacity reached.\n");
        free(new_game);
        return;
    }


    // Assign players to the game
    new_game->player1 = player1;
    new_game->player2 = player2;

    // Initialize pits for both players
    initialize_board(new_game);

    // Player 1 starts
    srand(time(NULL));
    new_game->current_turn = (rand() % 2) + 1;

    send_game_start_message(player1->socket, player2->socket, new_game->current_turn);
    send_boards_players(new_game->player1, new_game->player2);

    play_game(new_game);

    clean_up_game(new_game);

}

void play_game(Game *game) {
    while (true) {
        Player *current_player, *opponent_player;
        if (game->current_turn == 1) {
            current_player = game->player1;
            opponent_player = game->player2;
        } else {
            current_player = game->player2;
            opponent_player = game->player1;
        }

        player_turn(current_player, opponent_player, game);

        game->current_turn = (game->current_turn == 1) ? 2 : 1;
    }
}

void clean_up_game(Game *game) {
    game->player1->in_game = false;
    game->player1->store = 0;
    game->player1->move_history = NULL;
    game->player2->in_game = false;
    game->player2->store = 0;
    game->player2->move_history = NULL;
    remove_game(game);
    free(game);
}


void send_game_start_message(int client_socket, int challenged_socket, int current_turn) {
    if (current_turn == 1) {
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

void remove_game(Game *game) {
    for (int i = 0; i < active_game_count; i++) {
        if (active_games[i] == game) {
            // Shift all games after the found game to fill the gap
            for (int j = i; j < active_game_count - 1; j++) {
                active_games[j] = active_games[j + 1];
            }
            active_game_count--;
            break;
        }
    }
}

void decline_challenge(int client_socket, int challenged_socket) {
    send_message(client_socket, "Your challenge has been declined.\n");
    send_message(challenged_socket, "You declined the challenge.\n");
}

void invalid_response(int client_socket, int challenged_socket) {
    send_message(client_socket, "Invalid response. Challenge declined.\n");
    send_message(challenged_socket, "Invalid response. Challenge declined.\n");
}

void accept_challenge(Player *player, Player *challenged) {
    send_message(challenged->socket, "You accepted the challenge!\n");
    send_message(player->socket, "Your challenge has been accepted!\n");

    // Set players as in a game
    player->in_game = true;
    challenged->in_game = true;

    initialize_game(player, challenged);
}


void send_boards_players(Player *current, Player *opponent) {
    send_board(current->socket, current, opponent);
    send_board(opponent->socket, opponent, current);
}

void send_boards(Game *game) {
    send_board(game->player1->socket, game->player1, game->player2);
    send_board(game->player2->socket, game->player2, game->player1);

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (game->observers[i]->socket > 0) { // Ensure valid socket
            send_board(game->observers[i]->socket, game->player1, game->player2);
        }
    }
    // send boards to all observers
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


void handle_challenge(Player *player) {
    char challenge_user[50];

    printf("Challenge");
    if (!get_challenged_user(player, challenge_user)) {
        return;
    }

    Player *challenged = find_player_by_pseudo(challenge_user);
    if (!is_valid_challenge(player, challenged)) {
        return;
    }

    notify_challenge_sent(player->socket);
    process_challenge_response(player, challenged);
}

bool get_challenged_user(Player *player, char *challenge_user) {
    char buffer[BUFFER_SIZE];
    send_message(player->socket, "Enter the username to challenge:\n");

    bzero(buffer, BUFFER_SIZE);
    int bytes_received = recv(player->socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received <= 0) {
        handle_logout(player);
        return false;
    }

    buffer[bytes_received] = '\0';
    sscanf(buffer, "%s", challenge_user);

    if (strcmp(player->pseudo, challenge_user) == 0) {
        send_message(player->socket, "You cannot challenge yourself!\n");
        return false;
    }
    return true;
}

bool is_valid_challenge(Player *player, Player *challenged) {
    if (!challenged->is_online) {
        send_message(player->socket, "The player is not online or does not exist.\n");
        return false;
    }
    if (challenged->in_game) {
        send_message(player->socket, "The player is already in game. If you want to observe type O/o/0");
        return false;
    }
    return true;
}

void notify_challenge_sent(int socket) {
    send_message(socket, "Challenge sent. Waiting for response...\n");
}

void process_challenge_response(Player *player, Player *challenged) {
    char challenge_notification[BUFFER_SIZE];
    snprintf(challenge_notification, sizeof(challenge_notification),
             "%s is challenging you! Do you accept? (1/A/a to accept, 2/D/d to decline)\n", player->pseudo);
    send_message(challenged->socket, challenge_notification);

    char buffer[BUFFER_SIZE];
    bzero(buffer, BUFFER_SIZE);
    int challenged_response = recv(challenged->socket, buffer, BUFFER_SIZE - 1, 0);
    if (challenged_response <= 0) {
        printf("Challenged player disconnected.\n");
        return;
    }

    buffer[challenged_response] = '\0';
    handle_challenged_response(buffer[0], player, challenged);
}

void handle_challenged_response(char response, Player *player, Player *challenged) {
    if (response == '1' || response == 'A' || response == 'a') {
        accept_challenge(player, challenged);
    } else if (response == '2' || response == 'D' || response == 'd') {
        decline_challenge(player->socket, challenged->socket);
    } else {
        invalid_response(player->socket, challenged->socket);
        return;
    }
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
    clean_up_game(game);

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

void player_turn(Player *current_player, Player *opponent, Game *game) {
    int pit_index;

    // Prompt the player to choose a pit
    send_message(current_player->socket, "Your turn! Choose a pit (1-6):\n");
    char buffer[BUFFER_SIZE];
    bzero(buffer, BUFFER_SIZE);

    int bytes_received = recv(current_player->socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received <= 0) {
        send_message(current_player->socket, "You have disconnected. Game over.\n");
        send_message(opponent->socket, "Your opponent disconnected. Game over.\n");
        return;
    }

    pit_index = atoi(buffer) - 1;  // Convert to 0-indexed
    if (pit_index < 0 || pit_index >= PITS || current_player->pits[pit_index] == 0) {
        send_message(current_player->socket, "Invalid pit selection. Please choose again.\n");
        player_turn(current_player, opponent, game); // Recursively ask for valid input
        return;
    }

    add_move(current_player, pit_index, current_player->pits[pit_index]); // Save the move
    int last_pit = distribute_seeds(current_player, opponent, pit_index); // Distribute seeds
    send_boards_players(current_player, opponent);

    // Check for special conditions (capture seeds, skip turn, etc.)


    // If game over conditions are met, end the game
    if (is_game_over(*current_player, *opponent)) {
        end_game(current_player, opponent,
                 current_player->store > opponent->store ? 1 : (current_player->store < opponent->store ? -1 : 0),
                 game);
        return;
    }

    // Otherwise, end the current player's turn and pass to the opponent
    send_message(current_player->socket, "Your turn is over.\n");
    send_message(opponent->socket, "Your turn!\n");
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