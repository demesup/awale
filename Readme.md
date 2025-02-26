# Multiplayer Game Server - Command Reference

## Introduction
This project implements a multiplayer game server where players can interact, challenge each other, and communicate using a set of predefined commands. Below is a list of available commands and their functionalities.

## Commands List

### User Management
- `LOGOUT` - Logs the user out of the system.
- `VIEW_BIO` - Views the current user's bio.
- `VIEW_PLAYER_BIO <player_name>` - Views another player's bio.
- `UPDATE_BIO` - Initiates the process to update the bio.
- `UPDATE_BIO_BODY <new_bio>` - Updates the user's bio with the provided text.

### Player and Game Information
- `SHOW_ONLINE` - Displays a list of currently online players.
- `SHOW_PLAYERS` - Lists all registered players.
- `SHOW_GAMES` - Displays currently active games.

### Friend Management
- `VIEW_FRIEND_LIST` - Shows the user's friend list.
- `ADD_FRIEND <player_name>` - Sends a friend request to another player.
- `REMOVE_FRIEND <player_name>` - Removes a player from the friend list.

### Challenge System
- `CHALLENGE <player_name>` - Challenges another player to a game.
- `REVOKE_CHALLENGE` - Revokes a pending challenge.
- `PENDING` - Shows pending challenges.
- `ACCEPT` - Accepts a challenge.
- `DECLINE` - Declines a challenge.

### Game Observing
- `OBSERVE <game_id>` - Starts observing a specific game.
- `QUIT_OBSERVE` - Stops observing a game.

### Game Privacy Settings
- `FRIENDS_ONLY` - Restricts game access to friends only.
- `PUBLIC` - Makes the game publicly accessible.
- `PRIVATE` - Makes the game private.
- `ACCESS` - Checks the current access setting.

### Messaging
- `GLOBAL_MESSAGE <message>` - Sends a message to all players.
- `GAME_MESSAGE <message>` - Sends a message to players in the current game.
- `DIRECT_MESSAGE <player_name> <message>` - Sends a private message to a specific player.

### Gameplay
- `MAKE_MOVE <move_data>` - Makes a move in an active game.
- `END_GAME` - Ends the current game.
- `LEAVE_GAME` - Leaves the current game.

## Notes
- If the latest version of the system does not work as expected, consider rolling back to the previous commit.
- Ensure all commands are formatted correctly to avoid unexpected behavior.

## Running the Server and Client
### Compiling the Server and Client
To compile the server, use the following command:
`gcc socket_server.c -o server`

To compile the client, use the following command: 
`gcc socket_client.c -o client`

### Running the Server
After compiling the server, you can run it with a specific port number: ./server 9999 Replace 9999 with the desired port number.
### Running the Client
After compiling the client, you can run it with the server's IP address and port number: ./client [IP_ADDRESS] 9999 
Replace [IP_ADDRESS] with the actual IP, and 9999 with the port number used by the server.
