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
#define USERS_FILE "users.txt"
#define MAX_PENDING_CONNECTIONS 5
#define MAX_USERS 100 // Define the maximum number of users

typedef struct {
    char login[MAX_CREDENTIALS_LENGTH + 1];
    unsigned int password_hash;
} User;

// Global array to store users
User users[MAX_USERS];
int total_users = 0;

// Simple hash function to hash the passwords (for example purposes)
unsigned int hash_password(const char *password) {
    unsigned int hash = 0;
    while (*password) {
        hash = (hash * 31) + (unsigned char)(*password++);
    }
    return hash;
}

// Load users from the file into memory
int load_users() {
    FILE *file = fopen(USERS_FILE, "r");
    if (!file) {
        // If the file does not exist, create it
        file = fopen(USERS_FILE, "w");
        if (!file) {
            perror("Error creating users file");
            return 0; // Return failure if the file couldn't be created
        }
        fclose(file); // Close the file after creating it
        printf("No users file found, created a new one: %s\n", USERS_FILE);
        return 1; // Successfully created the file, continue with no users loaded
    }

    // Load users into the global users array
    while (fscanf(file, "%s %u", users[total_users].login, &users[total_users].password_hash) == 2) {
        total_users++;
        if (total_users >= MAX_USERS) {
            break; // Prevent array overflow
        }
    }

    fclose(file);
    return 1; // Successfully loaded users
}

// Check if a user exists in the in-memory list
int user_exists(const char *login) {
    for (int i = 0; i < total_users; i++) {
        if (strcmp(users[i].login, login) == 0) {
            return 1; // User exists
        }
    }
    return 0; // User not found
}

// Check login credentials (pseudo and password)
int check_login(const char *login, const char *password) {
    unsigned int password_hash = hash_password(password);

    for (int i = 0; i < total_users; i++) {
        if (strcmp(users[i].login, login) == 0 && users[i].password_hash == password_hash) {
            return 1; // Successful login
        }
    }

    return 0; // Incorrect login
}

// Add new user to users list and file
int add_user(const char *login, const char *password) {
    if (user_exists(login)) {
        return 0; // User already exists
    }

    if (total_users >= MAX_USERS) {
        return 0; // Maximum user limit reached
    }

    // Add user to in-memory list
    strcpy(users[total_users].login, login);
    users[total_users].password_hash = hash_password(password);
    total_users++;

    // Append user to file
    FILE *file = fopen(USERS_FILE, "a");
    if (!file) {
        perror("Error opening users file");
        return 0;
    }

    fprintf(file, "%s %u\n", login, hash_password(password));
    fclose(file);

    return 1; // User successfully added
}

// Handle the client request (login or register)
void handle_client(int newsockfd) {
    char action[8];
    char login[MAX_CREDENTIALS_LENGTH + 1];
    char password[MAX_CREDENTIALS_LENGTH + 1];
    char buffer[1024];
    int bytes_read;

    send(newsockfd, "lala", 4);

    // Read the action (REGISTER or LOGIN)
    bytes_read = read(newsockfd, action, sizeof(action) - 1);
    if (bytes_read <= 0) {
        close(newsockfd);
        return;
    }
    action[bytes_read] = '\0'; // Null-terminate the action

    // Read the login and password
    read(newsockfd, login, sizeof(login));
    read(newsockfd, password, sizeof(password));

    write(newsockfd, action, strlen(action));
    if (strcmp(action, "REGISTER:") == 0) {
        if (add_user(login, password)) {
            snprintf(buffer, sizeof(buffer), "Registration successful for %s\n", login);
            write(newsockfd, buffer, strlen(buffer));
        } else {
            snprintf(buffer, sizeof(buffer), "Registration failed: User %s already exists.\n", login);
            write(newsockfd, buffer, strlen(buffer));
        }
    } else if (strcmp(action, "LOGIN:") == 0) {
        if (check_login(login, password)) {
            snprintf(buffer, sizeof(buffer), "Login successful for %s\n", login);
            write(newsockfd, buffer, strlen(buffer));
        } else {
            snprintf(buffer, sizeof(buffer), "Login failed: Incorrect credentials for %s.\n", login);
            write(newsockfd, buffer, strlen(buffer));
        }
    }

    close(newsockfd);
}

int main(int argc, char **argv) {
    int sockfd, newsockfd, clilen;
    struct sockaddr_in cli_addr, serv_addr;

    if (argc != 2) {
        printf("Usage: socket_server port\n");
        exit(0);
    }

    // Load users from file into memory
    if (!load_users()) {
        printf("Error loading users from file. Exiting.\n");
        exit(0);
    }

    // Open the socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Unable to open socket");
        exit(0);
    }

    // Initialize server parameters
    bzero((char *)&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));

    // Bind the socket
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Unable to bind");
        exit(0);
    }

    // Start listening
    listen(sockfd, MAX_PENDING_CONNECTIONS);

    // Handle SIGCHLD to prevent zombie processes
    signal(SIGCHLD, SIG_IGN);

    while (1) {
        clilen = sizeof(cli_addr);
        newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
        if (newsockfd < 0) {
            perror("Accept error");
            continue; // Ignore failed connections and keep listening
        }

        printf("Connection accepted from %s:%d\n", inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));

        // Create a child process to handle the client
        if (fork() == 0) {
            // In child process
            close(sockfd); // Child does not need the listening socket
            handle_client(newsockfd);
            exit(0); // Exit after handling the client
        } else {
            // In parent process
            close(newsockfd); // Parent does not need the connected socket
        }
    }

    // Close the listening socket
    close(sockfd);
    return 0;
}
