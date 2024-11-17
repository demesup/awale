/* Client pour les sockets
 *    socket_client ip_server port
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>

#define MAX_CREDENTIALS_LENGTH 30

void format_message(const char *action, const char *login, const char *password, char *formatted_message, int buffer_size);

void get_input(const char *prompt, char *buffer, int max_length);

int validate_credentials_input(const char *input);

void get_input(const char *prompt, char *buffer, int max_length);

void login_loop(int sockfd);

int get_credentials(char *login, char *password) {
    // Get and validate login
    get_input("Enter pseudo (max 30 chars): ", login, MAX_CREDENTIALS_LENGTH);
    if (!validate_credentials_input(login)) {
        printf("Invalid pseudo. Please try again.\n");
        return 0;
    }

    // Get and validate password
    get_input("Enter password (max 30 chars): ", password, MAX_CREDENTIALS_LENGTH);
    if (!validate_credentials_input(password)) {
        printf("Invalid password. Please try again.\n");
        return 0;
    }

    return 1; // Return true (1) if both are valid
}

int validate_credentials_input(const char *input) {
    int length = strlen(input);
    return length > 0 && length <= MAX_CREDENTIALS_LENGTH;
}

void format_message(const char* action, const char* pseudo, const char* password, char* formatted_message, int buffer_size) {
    snprintf(formatted_message, buffer_size, "%s: %s %s\n", action, pseudo, password);
}

void get_input(const char* prompt, char* buffer, int max_length) {
    printf("%s", prompt);
    fflush(stdout);
    fgets(buffer, max_length + 1, stdin);
    buffer[strcspn(buffer, "\n")] = '\0'; // Remove trailing newline
}

void login_loop(int sockfd) {
    char login[MAX_CREDENTIALS_LENGTH + 1];
    char password[MAX_CREDENTIALS_LENGTH + 1];
    char formatted_message[MAX_CREDENTIALS_LENGTH * 2 + 11]; // "REGISTER: " + login + password + spaces
    char action_choice;
    char action[1];

    while (1) {
        if (!get_credentials(login, password)) {
            continue;
        }

        get_input("Do you want to register (r/1) or login (l/2)? ", action, 1);
        action_choice = tolower(action[0]); // Normalize to lower case
        const char* action = (action_choice == 'r' || action_choice == '1') ? "REGISTER" : "LOGIN";
        format_message(action, login, password, formatted_message, sizeof(formatted_message));
        if (write(sockfd, formatted_message, strlen(formatted_message)) < 0) {
            perror("Error writing to socket");
        } else {
            printf("Message sent: %s\n", formatted_message);
            break;
        }
    }
}

int main(int argc, char **argv) {
    int sockfd, newsockfd, clilen, chilpid, ok, nleft, nbwriten;
    char c;
    struct sockaddr_in cli_addr, serv_addr;

    if (argc != 3) {
        printf("usage  socket_client server port\n");
        exit(0);
    }


    /*
     *  partie client
     */
    printf("client starting\n");

    /* initialise la structure de donnee */
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));

    /* ouvre le socket */
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("socket error\n");
        exit(0);
    }

    /* effectue la connection */
    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        printf("socket error\n");
        exit(0);
    }


    login_loop(sockfd);

    /* repete dans le socket tout ce qu'il entend */
    while (1) {
       /* if /all is entered then send request PLAYERS
        * /games -> GAMES
        * /ug -> GAMES: <pseudo>
        * /ch -> CHALLENGE: <challenged_pseudo>
        * /a -> ACCEPT
        * /d -> DECLINE
        * /obs -> OBSERVE: <game_id>
        * /bio -> SEE_BIO: <current_pseudo> -this pseudo is taken from the session(when logging in/registering the
        * /exit -> LOGOUT
        * */
    }

    /*  attention il s'agit d'une boucle infinie
     *  le socket n'est jamais ferme !
     */

    return 1;

}
