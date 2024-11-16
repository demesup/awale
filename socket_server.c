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


#define MAX_PLAYERS 100
#define BUFFER_SIZE 1024
#define PLAYER_FILE "players.txt"
#define PITS 6  // Number of pits per player
#define INITIAL_SEEDS 4  // Initial seeds in each pit
#define NAME_LENGTH 50
#define MAX_BIO_LINES 10
#define MAX_BIO_LENGTH 100
#define HASH_SIZE 256 \


typedef struct Move {
    int pit_index;            // Pit index of the move
    int seeds_before_move;    // Seeds in the pit before the move
    struct Move *next;        // Pointer to the next move in the list
} Move;

typedef struct {
    char pseudo[50];
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
    int current_turn; // 1 for player1, 2 for player2
} Game;

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

void handle_challenge(int client_socket, const char *current_user);

void handle_challenged_response(char response, int client_socket, int challenged_socket, const char *current_user,
                                int challenged_index);

void clean_up_game(Game *game);

bool get_challenged_user(int client_socket, const char *current_user, char *challenge_user);

bool is_valid_challenge(int client_socket, const char *current_user, const char *challenge_user, int challenged_index);

bool is_valid_challenge(int client_socket, const char *current_user, const char *challenge_user, int challenged_index);

void notify_challenge_sent(int client_socket);

void process_challenge_response(int client_socket, const char *current_user, int challenged_index);

void accept_challenge(int client_socket, int challenged_socket, const char *current_user, int challenged_index);

void decline_challenge(int client_socket, int challenged_socket);

void invalid_response(int client_socket, int challenged_socket);

void initialize_game(int client_socket, int challenged_socket, const char *current_user, int challenged_index);

int capture_seeds(Player *current_player, Player *opponent, int last_pit);

void send_board(int client_socket, Player *player1, Player *player2);

void send_boards(int current_socket, int opponent_socket, Player *current, Player *opponent);

void send_message(int socket, const char *message);

void handle_logout(int client_socket, const char *current_user);

void play_game(Game *new_game);

int is_game_over(Player player1, Player player2);

int dead(Player player);

int is_savior(Player player);

void add_move(Player *player, int pit_index, int seeds_before_move);

int distribute_seeds(Player *current_player, Player *opponent, int pit_index);

void end_game(Player *player1, Player *player2, int result, Game *game);

void player_turn(Player *current_player, Player *opponent, Game *game);

void handle_add_bio(int client_socket, const char *current_user);

void handle_see_bio(int client_socket, const char *current_user);

void handle_see_bio_by_username(int client_socket);

void save_or_update_bio(const char *username, const char *bio);

void hash_password(const char *password, char *hash_result);

int compare_hashes(const char *hash1, const char *hash2);

int compare_hashes(const char *hash1, const char *hash2) {
    return strcmp(hash1, hash2) == 0;
}


void hash_password(const char *password, char *hash_result) {
    unsigned int hash = 0;
    for (int i = 0; password[i] != '\0'; i++) {
        hash = (hash * 31 + password[i]) % HASH_SIZE;  // Simple hash logic
    }

    // Convert hash to a string representation
    snprintf(hash_result, HASH_SIZE, "%u", hash);
}


void handle_see_bio_by_username(int client_socket) {
    char buffer[BUFFER_SIZE];
    send_message(client_socket, "Enter the username of the player whose bio you want to see:\n");

    bzero(buffer, BUFFER_SIZE);
    int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received <= 0) return;

    buffer[strcspn(buffer, "\n")] = '\0';  // Remove trailing newline

    char bio_output[MAX_BIO_LINES * MAX_BIO_LENGTH] = "Bio:\n";
    int found = 0;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (strcmp(players[i].pseudo, buffer) == 0) {
            found = 1;
            if (strlen(players[i].bio) > 0) {
                strncat(bio_output, players[i].bio, sizeof(bio_output) - strlen(bio_output) - 1);
            } else {
                strncat(bio_output, "This player hasn't added a bio yet.\n",
                        sizeof(bio_output) - strlen(bio_output) - 1);
            }
            break;
        }
    }

    if (!found) {
        snprintf(bio_output, sizeof(bio_output), "User '%s' not found.\n", buffer);
    }

    send_message(client_socket, bio_output);
}


void handle_see_bio(int client_socket, const char *current_user) {
    char buffer[BUFFER_SIZE] = "Your bio:\n";
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (strcmp(players[i].pseudo, current_user) == 0) {
            if (strlen(players[i].bio) > 0) {
                strncat(buffer, players[i].bio, sizeof(buffer) - strlen(buffer) - 1);
            } else {
                strncat(buffer, "You haven't added a bio yet.\n", sizeof(buffer) - strlen(buffer) - 1);
            }
            break;
        }
    }
    send_message(client_socket, buffer);
}


void save_or_update_bio(const char *username, const char *bio) {
    // Step 1: Find the player by username
    int player_found = -1;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (strcmp(players[i].pseudo, username) == 0) {
            player_found = i;
            break;
        }
    }

    if (player_found == -1) {
        printf("Player with username '%s' not found.\n", username);
        return;
    }

    // Step 2: Update the bio for the found player
    strncpy(players[player_found].bio, bio, MAX_BIO_LENGTH - 1);
    players[player_found].bio[MAX_BIO_LENGTH - 1] = '\0'; // Ensure null termination

    // Step 3: Rewrite the entire file with updated bio information
    FILE *file = fopen(PLAYER_FILE, "w");
    if (!file) {
        perror("Error opening file for writing");
        return;
    }

    // Write all players to the file
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].pseudo[0] != '\0') {  // Check if player slot is not empty
            fprintf(file, "%s %s\n", players[i].pseudo, players[i].password);

            // Write the bio, ensuring the "bio:" label
            fprintf(file, "bio: %s\n", players[i].bio);

            // Write the "-----" separator after each player's data
            fprintf(file, "-----\n");
        }
    }

    fclose(file);
    printf("Bio for player '%s' updated successfully.\n", username);
}


void handle_add_bio(int client_socket, const char *current_user) {
    char buffer[BUFFER_SIZE];
    send_message(client_socket, "Enter your bio (up to 10 lines, 100 chars each):\n");

    char new_bio[MAX_BIO_LINES * MAX_BIO_LENGTH] = "";
    for (int i = 0; i < MAX_BIO_LINES; i++) {
        bzero(buffer, BUFFER_SIZE);
        int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0 || strncmp(buffer, "END", 3) == 0) break;  // User ends input with "END"

        strncat(new_bio, buffer, sizeof(new_bio) - strlen(new_bio) - 1);
        if (i < MAX_BIO_LINES - 1) strncat(new_bio, "\n", sizeof(new_bio) - strlen(new_bio) - 1);
    }

    // Save the bio to the player's file
    save_or_update_bio(current_user, new_bio);

    // Update the in-memory player list
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (strcmp(players[i].pseudo, current_user) == 0) {
            strncpy(players[i].bio, new_bio, sizeof(players[i].bio) - 1);
            break;
        }
    }

    send_message(client_socket, "Your bio has been updated.\n");
}


void end_game(Player *player1, Player *player2, int result, Game *game) {
    char winner[NAME_LENGTH];
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

    // Display the current board state
    send_boards(current_player->socket, opponent->socket, current_player, opponent);

    // Prompt the player to choose a pit
    send_message(current_player->socket, "Your turn! Choose a pit (1-6): ");
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
    pit_index--;
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

void handle_challenge(int client_socket, const char *current_user) {
    char challenge_user[50];

    printf("Challenge");
    if (!get_challenged_user(client_socket, current_user, challenge_user)) {
        return;
    }

    int challenged_index = find_player_by_pseudo(challenge_user);
    if (!is_valid_challenge(client_socket, current_user, challenge_user, challenged_index)) {
        return;
    }

    notify_challenge_sent(client_socket);
    process_challenge_response(client_socket, current_user, challenged_index);
}

bool get_challenged_user(int client_socket, const char *current_user, char *challenge_user) {
    char buffer[BUFFER_SIZE];
    send_message(client_socket, "Enter the username to challenge:\n");

    bzero(buffer, BUFFER_SIZE);
    int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received <= 0) {
        handle_logout(client_socket, current_user);
        return false;
    }

    buffer[bytes_received] = '\0';
    sscanf(buffer, "%s", challenge_user);

    if (strcmp(current_user, challenge_user) == 0) {
        send_message(client_socket, "You cannot challenge yourself!\n");
        return false;
    }
    return true;
}

bool is_valid_challenge(int client_socket, const char *current_user, const char *challenge_user, int challenged_index) {
    if (challenged_index < 0 || !players[challenged_index].is_online) {
        send_message(client_socket, "The player is not online or does not exist.\n");
        return false;
    }
    if (players[challenged_index].in_game) {
        send_message(client_socket, "The player is already in game. If you want to observe type O/o/0");
        return false;
    }
    return true;
}

void notify_challenge_sent(int client_socket) {
    send_message(client_socket, "Challenge sent. Waiting for response...\n");
}

void process_challenge_response(int client_socket, const char *current_user, int challenged_index) {
    int challenged_socket = players[challenged_index].socket;
    char challenge_notification[BUFFER_SIZE];
    snprintf(challenge_notification, sizeof(challenge_notification),
             "%s is challenging you! Do you accept? (1/A/a to accept, 2/D/d to decline)\n", current_user);
    send_message(challenged_socket, challenge_notification);

    char buffer[BUFFER_SIZE];
    bzero(buffer, BUFFER_SIZE);
    int challenged_response = recv(challenged_socket, buffer, BUFFER_SIZE - 1, 0);
    if (challenged_response <= 0) {
        printf("Challenged player disconnected.\n");
        return;
    }

    buffer[challenged_response] = '\0';
    handle_challenged_response(buffer[0], client_socket, challenged_socket, current_user, challenged_index);
}

void handle_challenged_response(char response, int client_socket, int challenged_socket, const char *current_user,
                                int challenged_index) {
    if (response == '1' || response == 'A' || response == 'a') {
        accept_challenge(client_socket, challenged_socket, current_user, challenged_index);
    } else if (response == '2' || response == 'D' || response == 'd') {
        decline_challenge(client_socket, challenged_socket);
    } else {
        invalid_response(client_socket, challenged_socket);
        return;
    }
}

void accept_challenge(int client_socket, int challenged_socket, const char *current_user, int challenged_index) {
    send_message(challenged_socket, "You accepted the challenge!\n");
    send_message(client_socket, "Your challenge has been accepted!\n");

    // Set players as in a game
    players[find_player_by_pseudo(current_user)].in_game = true;
    players[challenged_index].in_game = true;

    initialize_game(client_socket, challenged_socket, current_user, challenged_index);
}

void send_boards(int current_socket, int opponent_socket, Player *current, Player *opponent) {
    send_board(current_socket, current, opponent);
    send_board(opponent_socket, opponent, current);
}

void send_board(int client_socket, Player *player1, Player *player2) {
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

    send_message(client_socket, board);

}

void send_message(int socket, const char *message) {
    send(socket, message, strlen(message), 0);
}

void decline_challenge(int client_socket, int challenged_socket) {
    send_message(client_socket, "Your challenge has been declined.\n");
    send_message(challenged_socket, "You declined the challenge.\n");
}

void invalid_response(int client_socket, int challenged_socket) {
    send_message(client_socket, "Invalid response. Challenge declined.\n");
    send_message(challenged_socket, "Invalid response. Challenge declined.\n");
}

void initialize_board(Game *game) {
    for (int i = 0; i < PITS; i++) {
        game->player1->pits[i] = INITIAL_SEEDS;
        game->player2->pits[i] = INITIAL_SEEDS;
    }
    game->player1->store = 0;
    game->player2->store = 0;
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

void clean_up_game(Game *game) {
    game->player1->in_game = false;
    game->player1->store = 0;
    game->player1->move_history = NULL;
    game->player2->in_game = false;
    game->player2->store = 0;
    game->player2->move_history = NULL;
    free(game);
}

void initialize_game(int client_socket, int challenged_socket, const char *current_user, int challenged_index) {
    Game *new_game = malloc(sizeof(Game));
    if (!new_game) {
        send_message(client_socket, "Failed to start the game due to server error.\n");
        send_message(challenged_socket, "Failed to start the game due to server error.\n");
        return;
    }

    // Assign players to the game
    new_game->player1 = &players[find_player_by_pseudo(current_user)];
    new_game->player2 = &players[challenged_index];

    // Initialize pits for both players
    initialize_board(new_game);

    // Player 1 starts
    srand(time(NULL));
    new_game->current_turn = (rand() % 2) + 1;

    send_game_start_message(client_socket, challenged_socket, new_game->current_turn);

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


void handle_logout(int client_socket, const char *current_user) {
    send_message(client_socket, "Logging out...\n");

    pthread_mutex_lock(&player_mutex);

    int player_index = find_player_by_pseudo(current_user);
    if (player_index >= 0) {
        players[player_index].is_online = false;
        players[player_index].in_game = false;  // Ensure they are not in-game
        players[player_index].socket = -1;     // Reset the socket
    }

    pthread_mutex_unlock(&player_mutex);

    printf("Player logged out: %s\n", current_user);

    close(client_socket);
    pthread_exit(NULL);
}

void handle_logged_in_menu(int client_socket, const char *current_user) {
    char buffer[BUFFER_SIZE];

    while (1) {
        send_message(client_socket,
                     "\nMenu:\n"
                     "1. List all online players\n"
                     "2. Challenge a player by username\n"
                     "3. Add/retype bio\n"
                     "4. See my bio\n"
                     "5. See bio by username\n"
                     "13. Logout\nEnter your choice:\n"
        );

        bzero(buffer, BUFFER_SIZE);
        int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) {
            handle_logout(client_socket, current_user);
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
                handle_add_bio(client_socket, current_user);
                break;

            case 4:
                handle_see_bio(client_socket, current_user);
                break;

            case 5:
                handle_see_bio_by_username(client_socket);
                break;

            case 13:
                handle_logout(client_socket, current_user);
                return;

            default:
                send_message(client_socket, "Invalid choice. Try again.\n");
                break;
        }
    }
}

void *handle_client(void *arg) {
    int client_socket = *((int *) arg);
    free(arg);

    char buffer[BUFFER_SIZE];
    bzero(buffer, BUFFER_SIZE);

    // Welcome message and instructions sent to the client
    send_message(client_socket, "Welcome to the server!\n");

    handle_visit(client_socket);


    close(client_socket);
    return NULL;
}


// New handle_visit method to handle client interaction during registration and login
void handle_visit(int client_socket) {
    send_online_players(client_socket);
    char buffer[BUFFER_SIZE];
    bzero(buffer, BUFFER_SIZE);

    // Send instructions to the client
    // Main loop to handle commands from the client
    while (1) {
        send_message(client_socket, "REGISTER <pseudo> <password>\nLOGIN <pseudo> <password>\n");

        char current_user[50] = {0}; // Stores the current logged-in user

        bzero(buffer, BUFFER_SIZE);
        int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) {
            handle_logout(client_socket, current_user);
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
            send_message(client_socket, "Invalid command! Use REGISTER or LOGIN.\n");
            // Invalid command
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
        send_message(client_socket, "Pseudo and password cannot be empty!\n");
        pthread_mutex_lock(&player_mutex);
        return;
    }

    // Check if pseudo already exists in the file
    if (is_pseudo_in_file(pseudo)) {
        send_message(client_socket, "Pseudo already taken!\n");
        pthread_mutex_lock(&player_mutex);
        return;
    }

    // Check if pseudo already exists among online players
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].is_online && strcmp(players[i].pseudo, pseudo) == 0) {
            send_message(client_socket, "Pseudo already taken!\n");
            pthread_mutex_unlock(&player_mutex);
            return;
        }
    }

    // Register the new player
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!players[i].is_online && players[i].pseudo[0] == '\0') {

            char hashed_password[HASH_SIZE];
            hash_password(password, hashed_password);

            strcpy(players[i].pseudo, pseudo);
            strcpy(players[i].password, hashed_password);
            players[i].socket = client_socket;
            players[i].is_online = true;
            players[i].in_game = false;

            save_player_to_file(&players[i]); // Save to file

            send_message(client_socket, "Registration successful!\n");
            printf("Player registered: %s\n", pseudo);

            pthread_mutex_unlock(&player_mutex);
            handle_logged_in_menu(client_socket, pseudo);
            return;
        }
    }

    send_message(client_socket, "Server full!\n");
    pthread_mutex_unlock(&player_mutex);
}

void handle_login(int client_socket, char *pseudo, char *password) {
    // Step 1: Validate input
    if (strlen(pseudo) == 0 || strlen(password) == 0) {
        send_message(client_socket, "Pseudo and password cannot be empty!\n");
        return;
    }

    pthread_mutex_lock(&player_mutex);  // Locking the mutex to ensure thread-safety

    // Step 2: Check if the user is already online
    int player_index = find_player_by_pseudo(pseudo);
    if (player_index >= 0) {
        if (players[player_index].is_online) {
            // If the player is already online
            send_message(client_socket, "You are already logged in!\n");
            pthread_mutex_unlock(&player_mutex);  // Unlock mutex before returning
            return;
        }

        // Step 3: Verify password
        char hashed_password[HASH_SIZE];
        hash_password(password, hashed_password);  // Hash the input password

        if (!compare_hashes(players[player_index].password, hashed_password)) {
            send_message(client_socket, "Incorrect password!\n");
            pthread_mutex_unlock(&player_mutex);  // Unlock mutex before returning
            return;
        }

        // Step 4: Mark the user as online and set their socket
        players[player_index].is_online = true;
        players[player_index].socket = client_socket;

        send_message(client_socket, "Login successful!\n");
        printf("Player logged in: %s\n", pseudo);

        pthread_mutex_unlock(&player_mutex);  // Unlock mutex after handling the login
        handle_logged_in_menu(client_socket, pseudo);

    } else {
        // If the user doesn't exist
        send_message(client_socket, "Player not found!\n");
        pthread_mutex_unlock(&player_mutex);
    }

}


// Save player to file
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

// Load players from file
void load_players_from_file() {
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
    send_message(client_socket, response);

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

    close(sockfd);
    return 0;
}