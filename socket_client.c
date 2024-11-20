#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <ctype.h>

#define BUFFER_SIZE 1024
#define MAX_PSEUDO_LEN 10
#define MAX_PASSWORD_LEN 10
#define MAX_BIO_LINES 10
#define MAX_BIO_LINE_LENGTH 80 // https://en.wikipedia.org/wiki/Characters_per_line

int logged_in = 0;
int answer_received = 0;

char *answer = "ANSWER\n";
char *login_success = "Login successful!\n";
char *reg_success = "Registration successful!\n";
char *logged_out = "Logging out...\n";


const char *LOGOUT = "LOGOUT\n";
const char *SHOW_ONLINE = "SHOW_ONLINE\n";
const char *SHOW_PLAYERS = "SHOW_PLAYERS\n";
const char *SHOW_GAMES = "SHOW_GAMES\n";
const char *CHALLENGE = "CHALLENGE";
const char *ACCEPT = "ACCEPT\n";
const char *DECLINE = "DECLINE\n";
const char *OBSERVE = "OBSERVE";
const char *QUIT_OBSERVE = "QUIT_OBSERVE\n";
const char *VIEW_FRIEND_LIST = "VIEW_FRIEND_LIST\n";
const char *ADD_FRIEND = "ADD_FRIEND";
const char *FRIENDS_ONLY = "FRIENDS_ONLY\n";
const char *PUBLIC = "PUBLIC\n";
const char *VIEW_BIO = "VIEW_BIO\n";
const char *VIEW_PLAYER_BIO = "VIEW_PLAYER_BIO";
const char *UPDATE_BIO = "UPDATE_BIO";
const char *UPDATE_BIO_BODY = "UPDATE_BIO_BODY";
const char *GLOBAL_MESSAGE = "GLOBAL_MESSAGE";
const char *GAME_MESSAGE = "GAME_MESSAGE";
const char *DIRECT_MESSAGE = "DIRECT_MESSAGE";
const char *MAKE_MOVE = "MAKE_MOVE";
const char *END_GAME = "END_GAME";
const char *LEAVE_GAME = "LEAVE_GAME";


/** PROTOTYPES */
int contains_space(const char *str);

void read_line(char *prompt, char *buffer, size_t size);

int send_message(int sockfd, const char *message);

void add_new_line(char *command);

int login_or_register(int server_socket);

void *listen_to_server(void *arg);

void *handle_user_commands(int server_socket);

void handle_help();

void handle_exit(int server_socket);

void handle_online(int server_socket);

void handle_players(int server_socket);

void handle_games(int server_socket);

void handle_obs(int server_socket, const char *command);

void handle_quit_obs(int server_socket);

void handle_bio(int server_socket);

void handle_view_bio(int server_socket, const char *command);

void handle_update_bio(int server_socket, const char *command);

void handle_global_message(int server_socket, const char *command);

void handle_direct_message(int server_socket, const char *command);

void handle_game_message(int server_socket, const char *command);

void handle_make_move(int server_socket, const char *command);

void handle_end_game(int server_socket, const char *command);

void handle_leave_game(int server_socket, const char *command);

void handle_friend_list(int server_socket);

void handle_add_friend(int server_socket, const char *command);

void handle_private(int server_socket);

void handle_public(int server_socket);

void handle_accept(int server_socket);

void handle_decline(int server_socket);

void handle_challenge(int server_socket, const char *command);


/** CODE */

void add_new_line(char *command) {
    strcat(command, "\n");
}

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
            } else {
                answer_received = 0;
            }

            // Wait until login is successful before proceeding
            while (!answer_received) {
                usleep(100);  // Sleep for a short time before checking again
            }
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
        // Check for successful login message

        if (strcmp(buffer, answer) == 0) {
            answer_received = 1;
        } else if (strcmp(buffer, login_success) == 0 || strcmp(reg_success, buffer) == 0) {
            logged_in = 1;  // Set login status to true
        } else if (strcmp(buffer, logged_out) == 0) {
            exit(0);
        } else {
            printf("%s", buffer); // Display any other server messages
        }
    }

    if (bytes_received == 0) {
        printf("Disconnected from the server.\n");
        logged_in = 0;
        exit(0);  // Close the application if the connection is lost
        return NULL;
    } else if (bytes_received < 0) {
        perror("Error receiving data from server");
    }
}

// Function to handle user commands
void *handle_user_commands(int server_socket) {

    char buffer[BUFFER_SIZE];

    printf("You can now issue commands. Type /help to see available commands.\n");

    while (logged_in) {
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
        } else if (strncmp(buffer, "/obs ", 5) == 0) {
            handle_obs(server_socket, buffer);
        } else if (strncmp(buffer, "/qobs ", 5) == 0) {
            handle_quit_obs(server_socket);
        } else if (strcmp(buffer, "/bio") == 0) {
            handle_bio(server_socket);
        } else if (strncmp(buffer, "/pbio ", 5) == 0) {
            handle_view_bio(server_socket, buffer);
        } else if (strncmp(buffer, "/update", 7) == 0) {
            handle_update_bio(server_socket, buffer);
        } else if (strncmp(buffer, "/gl ", 4) == 0) {
            handle_global_message(server_socket, buffer);
        } else if (strncmp(buffer, "/msg ", 5) == 0) {
            handle_direct_message(server_socket, buffer);
        } else if (strncmp(buffer, "/chat ", 6) == 0) {
            handle_game_message(server_socket, buffer);
        } else if (strncmp(buffer, "/m ", 2) == 0) {
            handle_make_move(server_socket, buffer);
        } else if (strcmp(buffer, "/end") == 0) {
            handle_end_game(server_socket, buffer);
        } else if (strcmp(buffer, "/leave") == 0) {
            handle_leave_game(server_socket, buffer);
        } else if (strcmp(buffer, "/fr") == 0) {
            handle_friend_list(server_socket);
        } else if (strncmp(buffer, "/addfr ", 7) == 0) {
            handle_add_friend(server_socket, buffer);
        } else if (strcmp(buffer, "/private") == 0) {
            handle_private(server_socket);
        } else if (strcmp(buffer, "/public") == 0) {
            handle_public(server_socket);
        } else if (strcmp(buffer, "/decline") == 0) {
            handle_decline(server_socket);
        } else if (strcmp(buffer, "/accept") == 0) {
            handle_accept(server_socket);
        } else if (strncmp(buffer, "/challenge", strlen(CHALLENGE) + 1) == 0) {
            handle_challenge(server_socket, buffer);
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
            "/online - Show online players\n"
            "/players - Show all players\n"
            "/games - Show available games\n"
            "/challenge - Challenge player by pseudo\n"
            "/accept - Accept a challenge\n"
            "/decline - Decline a challenge\n"
            "/obs <pseudo> - Observe a game the player <pseudo> is in\n"
            "/qobs - Quit observing the game\n"
            "/fr - View your friend list\n"
            "/addfr <pseudo> - Add a friend \n"
            "/private - Allow only friends to observe the games I am in \n"
            "/public - Allow all players to observe the games I am in \n"
            "/bio - View your bio\n"
            "/pbio <player> - View another player's bio\n"
            "/update - Update your bio\n"
            "/gl \"<message>\" - Send a global message\n"
            "/gmsg \"<message>\" - Send a message to a game players/observers\n"
            "/msg <pseudo> \"<message>\" - Send a direct message to a player\n"
            "/m - Make a move in the current game\n"
            "/end - End the current game\n"
            "/leave - Leave the current game\n"
    );
}


void handle_exit(int server_socket) {
    send_message(server_socket, LOGOUT);
}


void handle_online(int server_socket) {
    send_message(server_socket, SHOW_ONLINE);
}

void handle_players(int server_socket) {
    send_message(server_socket, SHOW_PLAYERS);
}

void handle_games(int server_socket) {
    send_message(server_socket, SHOW_GAMES);
}

void handle_obs(int server_socket, const char *command) {
    char pseudo[MAX_PSEUDO_LEN + 1] = {0}; // Initialize to ensure it's null-terminated
    char buffer[10 + MAX_PSEUDO_LEN] = {0}; // Initialize to ensure it's null-terminated

    // Extract the pseudo from the command
    if (sscanf(command, "/obs %10s", pseudo) == 1) { // Limit pseudo to MAX_PSEUDO_LEN
        // Validate the pseudo
        if (strlen(pseudo) == 0 || strlen(pseudo) > MAX_PSEUDO_LEN || contains_space(pseudo)) {
            printf("Invalid pseudo for challenge. Ensure it is between 1 and %d characters and contains no spaces.\n",
                   MAX_PSEUDO_LEN);
            return;
        }

        strcpy(buffer, OBSERVE);
        strcat(buffer, " ");
        strcat(buffer, pseudo);
        strcat(buffer, "\n");

        send_message(server_socket, buffer);
    } else {
        printf("Failed to extract pseudo. Ensure the command is in the correct format: /challenge <pseudo>\n");
    }
}

void handle_quit_obs(int server_socket) {
    send_message(server_socket, QUIT_OBSERVE);
}

void handle_bio(int server_socket) {
    send_message(server_socket, VIEW_BIO);
}

void handle_view_bio(int server_socket, const char *command) {
    char pseudo[MAX_PSEUDO_LEN + 1] = {0}; // Initialize to ensure it's null-terminated
    char buffer[19 + MAX_PSEUDO_LEN] = {0}; // Initialize to ensure it's null-terminated

    // Extract the pseudo from the command
    if (sscanf(command, "/pbio %10s", pseudo) == 1) { // Limit pseudo to MAX_PSEUDO_LEN
        // Validate the pseudo
        if (strlen(pseudo) == 0 || strlen(pseudo) > MAX_PSEUDO_LEN || contains_space(pseudo)) {
            printf("Invalid pseudo for challenge. Ensure it is between 1 and %d characters and contains no spaces.\n",
                   MAX_PSEUDO_LEN);
            return;
        }

        // Perform additional logic for handling the challenge
        strcpy(buffer, VIEW_PLAYER_BIO);
        strcat(buffer, " ");
        strcat(buffer, pseudo);
        strcat(buffer, "\n");

        send_message(server_socket, buffer);
    } else {
        printf("Failed to extract pseudo. Ensure the command is in the correct format: /challenge <pseudo>\n");
    }

}

//todo: do not allow \n and \o
void handle_update_bio(int server_socket, const char *command) {
    // read max 10 lines of new bio, concat then together, still separate each line with /n, do not allow empty lines
    char bio[BUFFER_SIZE] = ""; // Buffer to hold the concatenated bio
    char line[BUFFER_SIZE]; // Temporary buffer for each line
    int line_count = 0;

    while (line_count < MAX_BIO_LINES) {
        read_line("Enter a new line for your bio (or press Enter to skip):", line, sizeof(line));

        // Skip empty lines
        if (strlen(line) == 0) {
            break;
        }

        // Concatenate the line to the bio
        if (strlen(line) < MAX_BIO_LINE_LENGTH) {
            strcat(bio, line);
            strcat(bio, "\\n"); // Separate each line with a newline character
            line_count++;
        } else if ((strlen(line) + strlen(bio)) > MAX_BIO_LINE_LENGTH * MAX_BIO_LINES) {
            printf("\nThe line entered is too long, try again. \n\tChars left: %d\n\t Chars passed: %d\n",
                   MAX_BIO_LINE_LENGTH * MAX_BIO_LINES - strlen((bio)), strlen((line)));
            continue;
        } else {
            int additional_lines_needed = (strlen(line) + MAX_BIO_LINE_LENGTH - 1) / MAX_BIO_LINE_LENGTH; // Round up

            if (line_count + additional_lines_needed > MAX_BIO_LINES) {
                printf("\nThe entered line exceeds the maximum bio size.\n"
                       "You need %d additional lines, but only %d lines are available.\n",
                       additional_lines_needed, MAX_BIO_LINES - line_count);
                continue; // Ask the user to re-enter the line
            }
            printf("The line entered is too long. Breaking it down...\n");

            // Split the line into smaller parts
            char *line_part = line;
            while (strlen(line_part) > MAX_BIO_LINE_LENGTH) {
                // Temporarily cut the line at the max length and append to bio
                char temp[BUFFER_SIZE];
                strncpy(temp, line_part, MAX_BIO_LINE_LENGTH);
                temp[MAX_BIO_LINE_LENGTH] = '\0'; // Ensure the part is null-terminated
                strcat(bio, temp);
                strcat(bio, "\\n");
                line_count++;

                line_part += MAX_BIO_LINE_LENGTH; // Move to the next part
            }

            // If there's any remaining part, add it to the bio
            if (line_count < MAX_BIO_LINES && strlen(line_part) > 0) {
                strcat(bio, line_part);
                strcat(bio, "\\n");
                line_count++;
            }
        }
    }

    char buffer[BUFFER_SIZE] = {0}; // Initialize to ensure it's null-terminated
    strcat(buffer, "UPDATE_BIO");
    strcat(buffer, " ");
    strcat(buffer, bio);
    strcat(buffer, "\n");

    send_message(server_socket, buffer);
}

void handle_global_message(int server_socket, const char *command) {
    printf("Sending GLOBAL MESSAGE request: %s\n", command);
    send(server_socket, command, strlen(command), 0);
}

void handle_direct_message(int server_socket, const char *command) {
    printf("Sending DIRECT MESSAGE request: %s\n", command);
    send(server_socket, command, strlen(command), 0);
}

void handle_game_message(int server_socket, const char *command) {
    printf("Sending GAME MESSAGE request: %s\n", command);
    send(server_socket, command, strlen(command), 0);
}

// command: /m 3
void handle_make_move(int server_socket, const char *command) {
    const char *args = command + 3;

    // Check if the pit number is a single digit
    if (!isdigit(*args) || *(args + 1) != '\0') {
        fprintf(stderr, "Error: Invalid pit number or extra arguments\n");
        return;
    }

    // Convert the pit number to an integer
    int pit_number = *args - '0';

    // Check if the pit number is within the valid range (1-6)
    if (pit_number < 1 || pit_number > 6) {
        fprintf(stderr, "Error: Pit number must be between 1 and 6\n");
        return;
    }

    // Construct the MOVE message
    char message[BUFFER_SIZE];
    snprintf(message, BUFFER_SIZE, "%s %d\n", MAKE_MOVE, pit_number);

    // Send the message to the server
    send_message(server_socket, message);
}

void handle_end_game(int server_socket, const char *command) {
    printf("Sending END GAME request: %s\n", command);
    send(server_socket, command, strlen(command), 0);
}

void handle_leave_game(int server_socket, const char *command) {
    printf("Sending LEAVE GAME request: %s\n", command);
    send(server_socket, command, strlen(command), 0);
}

void handle_friend_list(int server_socket) {
    const char *command = "FRIEND_LIST\n";
    send(server_socket, command, strlen(command), 0);
}

void handle_add_friend(int server_socket, const char *command) {
    // Example command format: /addfr <pseudo>
    char pseudo[MAX_PSEUDO_LEN + 1];
    sscanf(command + 7, "%s", pseudo);

    if (strlen(pseudo) == 0 || strlen(pseudo) > MAX_PSEUDO_LEN) {
        printf("Invalid pseudo for friend addition. Ensure it is between 1 and %d characters.\n", MAX_PSEUDO_LEN);
        return;
    }

    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "ADD_FRIEND %s\n", pseudo);
    send(server_socket, buffer, strlen(buffer), 0);
}

void handle_private(int server_socket) {
    const char *command = "PRIVATE\n";
    send(server_socket, command, strlen(command), 0);
}

void handle_public(int server_socket) {
    const char *command = "PUBLIC\n";
    send(server_socket, command, strlen(command), 0);
}

void handle_decline(int server_socket) {
    send_message(server_socket, DECLINE);
}

void handle_accept(int server_socket) {
    send_message(server_socket, ACCEPT);
}


void handle_challenge(int server_socket, const char *command) {
    char pseudo[MAX_PSEUDO_LEN + 1] = {0}; // Initialize to ensure it's null-terminated
    char buffer[19 + MAX_PSEUDO_LEN] = {0}; // Initialize to ensure it's null-terminated

    // Extract the pseudo from the command
    if (sscanf(command, "/challenge %10s", pseudo) == 1) { // Limit pseudo to MAX_PSEUDO_LEN
        // Validate the pseudo
        if (strlen(pseudo) == 0 || strlen(pseudo) > MAX_PSEUDO_LEN || contains_space(pseudo)) {
            printf("Invalid pseudo for challenge. Ensure it is between 1 and %d characters and contains no spaces.\n",
                   MAX_PSEUDO_LEN);
            return;
        }

        // Perform additional logic for handling the challenge
        strcat(buffer, CHALLENGE);
        strcat(buffer, " ");
        strcat(buffer, pseudo);
        strcat(buffer, "\n");

        send_message(server_socket, buffer);
    } else {
        printf("Failed to extract pseudo. Ensure the command is in the correct format: /challenge <pseudo>\n");
    }
}
