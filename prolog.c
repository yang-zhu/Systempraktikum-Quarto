#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>

#define GAMEKINDNAME "Quarto"
#define PORTNUMBER 1357
#define HOSTNAME "sysprak.priv.lab.nm.ifi.lmu.de"
#define GAME_ID_LENGTH 13
#define MSG_BUF_SIZE 100


void printUsage() {
    printf("Usage:\n");
    printf("-g <13 digit number>: Game-ID (obligatory)\n");
    printf("-p <1 or 2>: desirable player number (optional)\n");
    exit(EXIT_FAILURE);
}

char *getMessage(int socket) {
    static char msgBuf[MSG_BUF_SIZE];
    static char *bufStart = msgBuf;
    ssize_t recvBytes = bufStart - msgBuf;

    size_t resCapacity = MSG_BUF_SIZE;
    char *result = malloc(resCapacity+1);
    size_t resLength = 0;
    bool newLineFound = false;

    while (!newLineFound) {
        // 1. If msgBuf is empty, receive more characters
        if (bufStart == msgBuf) {
            recvBytes = recv(socket, bufStart, MSG_BUF_SIZE, 0);            
        }
        // 2. copy characters to result
        int toCopy = recvBytes;
        for (int i = 0; i < recvBytes; ++i) {
            if (msgBuf[i] == '\n') {
                toCopy = i + 1;                
                newLineFound = true;
                break;
            }
        }
        if (toCopy > resCapacity - resLength) {
            result = realloc(result, (resCapacity*=2)+1);
        }
        memcpy(&result[resLength], msgBuf, toCopy);
        resLength += toCopy;
        memmove(msgBuf, &msgBuf[toCopy], recvBytes-toCopy);
        bufStart = msgBuf + (recvBytes - toCopy);
    }

    result[resLength] = '\0';
    printf("S: %s\n", result);
    return result;
}

void foundError(int socket, char *errMsg) {
    fprintf(stderr, "%s\n", errMsg);
    close(socket);
    exit(EXIT_FAILURE);
}

void sendWithErrorHandling(int socket, char *buffer) {
    ssize_t sentBytes = send(socket, buffer, strlen(buffer), 0);
    printf("%s", buffer);
    if (sentBytes != strlen(buffer)) {
        foundError(socket, "The message was not succefully sent.");
    }
}

void performConnection(int socket, char gameID[], int playerNo) {
    // S: + MNM Gameserver << Gameserver Version >> accepting connections
    float gameServerVersion;
    char *msg = getMessage(socket);
    int assignedItems = sscanf(msg, "+ MNM Gameserver v%f accepting connections", &gameServerVersion);
    free(msg);
    if (assignedItems != 1) {
        foundError(socket, "Received message does not match the protocol.");
    }
    if ((int)gameServerVersion != 2) {
        foundError(socket, "The version of the game server is not supported.");
    }
    // C: VERSION << Client Version >>
    sendWithErrorHandling(socket, "VERSION 2.3\n");

    // S: + Client version accepted - please send Game-ID to join
    msg = getMessage(socket);
    assignedItems = sscanf(msg, "+ Client version accepted - please send Game-ID to join");
    free(msg);
    if (assignedItems != 0) {
        foundError(socket, "Received message does not match the protocol.");
    }
    // C: ID << Game-ID >>
    sendWithErrorHandling(socket, "ID ");
    sendWithErrorHandling(socket, gameID);
    sendWithErrorHandling(socket, "\n");

    // S: + PLAYING << Gamekind-Name >>
    char gameKind[7];
    msg = getMessage(socket);
    assignedItems = sscanf(msg, "+ PLAYING %6s", gameKind);
    gameKind[6] = '\0';  // makes sure that the gameKind char array is NUL terminated
    free(msg);
    if (assignedItems != 1) {
        foundError(socket, "Received message does not match the protocol.");
    }
    if (strcmp(GAMEKINDNAME, gameKind) != 0) {
        foundError(socket, "The game is not \"Quatro\".");
    }
    // S: + << Game-Name >>
    msg = getMessage(socket);
    char *gameName = malloc(strlen(msg)-2);
    assignedItems = sscanf(msg, "+ %[^\n]", gameName);
    printf("%s\n", gameName);
    free(gameName);
    free(msg);
    if (assignedItems != 1) {
        foundError(socket, "Received message does not match the protocol.");
    }
    // C: PLAYER [[ Gewünschte Spielernummer ]]
    sendWithErrorHandling(socket, "PLAYER ");
    if (playerNo != -1) {
        char temp[2];
        snprintf(temp, 2, "%i", playerNo);
        sendWithErrorHandling(socket, temp);
    }
    sendWithErrorHandling(socket, "\n");

    // S: + YOU << Spielernummer >> << Spielername >>
    msg = getMessage(socket);
    char *playerName = malloc(strlen(msg)-5);
    assignedItems = sscanf(msg, "+ YOU %i %[^\n]", &playerNo, playerName);
    printf("%s\n", playerName);
    free(playerName);
    free(msg);
    if (assignedItems != 2) {
        foundError(socket, "Received message does not match the protocol.");
    }

    // S: + TOTAL << Spieleranzahl >>
    int numOfPlayers;
    msg = getMessage(socket);
    assignedItems = sscanf(msg, "+ TOTAL %i", &numOfPlayers);
    free(msg);
    if (assignedItems != 1) {
        foundError(socket, "Received message does not match the protocol.");
    }

    // S: + << Spielernummer >> << Spielername >> << Bereit >>
    for (int i = 0; i < numOfPlayers; ++i) {
        msg = getMessage(socket);
        assignedItems = sscanf(msg, "+ ");  // ignore the player number and player name for now, because they are not used anywhere
        free(msg);  // discard the remaining message
        if (assignedItems != 0) {
            foundError(socket, "Received message does not match the protocol.");
        }
    }

    // S: + ENDPLAYERS
    msg = getMessage(socket);
    assignedItems = sscanf(msg, "+ ENDPLAYERS");
    free(msg);
    if (assignedItems != 0) {
        foundError(socket, "Received message does not match the protocol.");
    }
}

int main(int argc, char *argv[]) {
    // process command line arguments
    bool gameIDGiven = false;
    char gameID[GAME_ID_LENGTH+1]; // +1 to hold the NUL character
    int playerNo = -1;
    int ret;
    while ((ret = getopt(argc, argv, "g:p:")) != -1) {
        switch(ret) {
            case 'g':
                if (strlen(optarg) != GAME_ID_LENGTH) printUsage();
                strcpy(gameID, optarg);
                gameIDGiven = true;
                break;
            case 'p':
                playerNo = atoi(optarg);
                if (playerNo != 0 && playerNo != 1) printUsage();
                break;
            default:
                printUsage();
        }
    }
    if (!gameIDGiven) printUsage();

    // create the socket and connect with the server
    int sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("Error in creating socket");
        close(sock);
        exit(EXIT_FAILURE);
    }
    
    // use DNS to get the IP address from the host name
    struct hostent *hostInfo = gethostbyname(HOSTNAME);
    if (!hostInfo) {
        herror("Error in the host name");
    }
    
    // set up the socket to connect with the server
    struct sockaddr_in saddr;
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(PORTNUMBER);
    saddr.sin_addr = *(struct in_addr *)hostInfo->h_addr_list[0];
    if (connect(sock, (struct sockaddr *)&saddr, sizeof(saddr)) == -1) {
        perror("Error in connecting to the server");
        close(sock);
        exit(EXIT_FAILURE);
    }

    performConnection(sock, gameID, playerNo);
}