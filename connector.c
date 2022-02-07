#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <poll.h>
#include <assert.h>
#include <stdarg.h>
#include "connector.h"
#include "utils.h"
#include "config.h"
#include "sharedMemory.h"
#include "sysprak-client.h"

#define MSG_BUF_SIZE 100
#define SHM_BUF_CAP 1024
#define GAME ((gameInfo *)(gameInfoBuf.start+game))  // point to where the gameInfo struct starts
#define PLAYERS ((playerInfo *)(gameInfoBuf.start+players))  // point to where the playerInfo array starts

void foundError(char *errMsg);
void protocolError(char *expectedMsg, char *receivedMsg);
void sendWithErrorHandling(char *buf, size_t len);
void sendFormatted(char *msg, ...);
void courseOfTheGame(void);
void move(void);

int connSocket = -1;

void connectSocket(void) {
    // resolve the host name
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    struct addrinfo *results; // list of results
    int ret = getaddrinfo(conf.hostname, conf.port, &hints, &results);
    if (ret) {
        fprintf(stderr, "Error in resolving the host name: %s\n", gai_strerror(ret));
        exit(EXIT_FAILURE);
    }

    // go over the results list, only stop when a socket is successfully created and connected
    struct addrinfo *rp;
    for (rp = results; rp != NULL; rp = rp->ai_next) {
        // create the socket
        connSocket = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (connSocket == -1) continue;

        // connect to the socket
        if (connect(connSocket, rp->ai_addr, rp->ai_addrlen) == -1) continue;
        else break;  // successfully connected
    }

    freeaddrinfo(results);

    if (rp == NULL) {
        fprintf(stderr, "Error in creating and connecting to the socket.\n");
        exit(EXIT_FAILURE);
    }
}

// Read a one-line message from the server.
char *getMessage(void) {
    // static message buffer, fixed length, used to store the newly received characters
    static char msgBuf[MSG_BUF_SIZE];
    // keep track of how many characters are stored in msgBuf
    static ssize_t msgBufLength = 0;

    // the current capacity of the result string
    size_t resCapacity = MSG_BUF_SIZE;
    // result string, dynamic length
    char *result = malloc(resCapacity+1);  // one extra byte to accommodate the ending '\0' character
    // keep track of how many characters are currently stored in the result string
    size_t resLength = 0;

    // keep receiving characters and dumping them into the result string until a '\n' is found
    bool newLineFound = false;
    while (!newLineFound) {
        // if msgBuf is empty, receive more characters
        if (msgBufLength == 0) {
            msgBufLength = recv(connSocket, msgBuf, MSG_BUF_SIZE, 0);
            if (msgBufLength < 0) {
                free(result);
                foundError("Error in receiving messages from the server.");
            }
        }
        // copy characters from msgBuf to result
        // only copy till '\n' character, therefore it could happen that there are characters remaining in msgBuf
        int toCopy = msgBufLength;
        for (int i = 0; i < msgBufLength; ++i) {
            if (msgBuf[i] == '\n') {
                toCopy = i + 1;
                newLineFound = true;
                break;
            }
        }
        if (toCopy > (int)(resCapacity - resLength)) {
            result = realloc(result, (resCapacity*=2)+1);  // again, one extra byte for the same purpose mentioned above
        }
        memcpy(&result[resLength], msgBuf, toCopy);
        resLength += toCopy;
        // move the remaining bytes in msgBuf to the beginning
        msgBufLength -= toCopy;
        memmove(msgBuf, &msgBuf[toCopy], msgBufLength);
    }

    result[resLength] = '\0';  // add '\0' character in the end to make it a string
    if (*result == '-') {
        printf("S: %s\n", result);
        free(result);
        foundError("The server sends a negative answer. An error arises. Disconnect...");
    }
    if (verbose) { printf("S: %s", result); }
    return result;
}

// Communicate with the server throughout the game.
void performConnection(char gameID[], int playerNo) {
    // S: + MNM Gameserver << Gameserver Version >> accepting connections
    float gameServerVersion;
    char *msg = getMessage();
    int assignedItems = sscanf(msg, "+ MNM Gameserver v%f accepting connections", &gameServerVersion);
    if (assignedItems != 1) {
        protocolError("+ MNM Gameserver << Gameserver Version >> accepting connections", msg);
    }
    free(msg);
    if ((int)gameServerVersion != 2) {
        foundError("The version of the game server is not supported.");
    }
    printf("+--------------------------------------------------+\n");
    printf("|                                                  |\n");
    printf("|     Welcome to MNM game server (version %.1f)     |\n", gameServerVersion);
    printf("|                                                  |\n");
    printf("+--------------------------------------------------+\n\n");

    // C: VERSION << Client Version >>
    sendFormatted("VERSION 2.3\n");

    // S: + Client version accepted - please send Game-ID to join
    msg = getMessage();
    int dummy = 0;
    sscanf(msg, "+ Client version accepted - please send Game-ID to join%n", &dummy);  // %n makes sure that sscanf reads till the end of the string
    if (dummy == 0) {
        protocolError("+ Client version accepted - please send Game-ID to join", msg);
    }
    free(msg);
    printf("Server accepted the client version.\n\n");

    // C: ID << Game-ID >>
    sendFormatted("ID %s\n", gameID);

    // S: + PLAYING << Gamekind-Name >>
    msg = getMessage();
    int readTillName = 0;
    sscanf(msg, "+ PLAYING %n", &readTillName);
    if (readTillName == 0) {
        protocolError("+ PLAYING << Gamekind-Name >>", msg);
    }
    char *gameKind = substring(msg, readTillName, strlen(msg)-1);
    if (strcmp(conf.gamekind, gameKind) != 0) {
        free(gameKind);
        free(msg);
        foundError("The game is not \"Quarto\".");
    }
    printf("-----------**%s**-----------\n", gameKind);
    free(gameKind);
    free(msg);
    printf("Game ID: %s\n", gameID);

    gameInfoBuf = createBuffer(SHM_BUF_CAP);
    size_t game = allocInBuffer(sizeof(gameInfo), &gameInfoBuf);
    GAME->thinker = getppid();
    GAME->connector = getpid();
    GAME->thinkSigSent = false;
    GAME->width = 0;
    GAME->height = 0;

    // S: + << Game-Name >>
    msg = getMessage();
    size_t gameName = allocInBuffer(strlen(msg)-2, &gameInfoBuf);  // gameName does not contain "+ "
    assignedItems = sscanf(msg, "+ %[^\n]", (char *)(gameInfoBuf.start+gameName));  // '\n' is excluded and '\0' is automatically added where '\n' used to be
    GAME->name = gameName;
    if (assignedItems != 1) {
        protocolError("+ << Game-Name >>", msg);
    }
    free(msg);

    // C: PLAYER [[ Gewünschte Spielernummer ]]
    if (playerNo != -1) { sendFormatted("PLAYER %i\n", playerNo-1); }
    else { sendFormatted("PLAYER\n"); }

    // S: + YOU << Spielernummer >> << Spielername >>
    msg = getMessage();
    size_t playerName = allocInBuffer(strlen(msg)-6, &gameInfoBuf);  // playerName does not contain "+ YOU "
    assignedItems = sscanf(msg, "+ YOU %i %[^\n]", &GAME->playerNo, (char *)(gameInfoBuf.start+playerName));
    if (assignedItems != 2) {
        protocolError("+ YOU << Spielernummer >> << Spielername >>", msg);
    }
    free(msg);

    // S: + TOTAL << Spieleranzahl >>
    msg = getMessage();
    assignedItems = sscanf(msg, "+ TOTAL %i", &GAME->numOfPlayers);
    if (assignedItems != 1) {
        protocolError("+ TOTAL << Spieleranzahl >>", msg);
    }
    free(msg);

    size_t players = allocInBuffer(GAME->numOfPlayers * sizeof(playerInfo), &gameInfoBuf);
    GAME->players = players;
    PLAYERS[0].name = playerName;
    PLAYERS[0].playerNo = GAME->playerNo;
    PLAYERS[0].isReady = true;  // the client is ready by default

    // S: + << Spielernummer >> << Spielername >> << Bereit >>
    // for the game Quarto, the for-loop will be executed exactly once
    for (int i = 1; i < GAME->numOfPlayers; ++i) {
        msg = getMessage();
        char readyByte = strlen(msg)>=2 ? msg[strlen(msg)-2] : '\0';  // avoid accessing out-of-bounds error when the received message is less than two characters long
        int playerNameStart = 0;
        assignedItems = sscanf(msg, "+ %i %n", &PLAYERS[i].playerNo, &playerNameStart);  // "%n" counts how many character have been read to this point
        if (assignedItems != 1 || (readyByte != '0' && readyByte != '1')) {
            protocolError("+ << Spielernummer >> << Spielername >> << Bereit >>", msg);
        }
        PLAYERS[i].isReady = readyByte == '1';
        char *playerName = substring(msg, playerNameStart, strlen(msg)-3);  // extract the player name in the middle of the message
        size_t playerNameOffset = copyToBuffer(playerName, strlen(playerName)+1, &gameInfoBuf);
        PLAYERS[i].name = playerNameOffset;
        free(playerName);
        free(msg);
    }

    // pretty print
    printf("Game name: %s\n", (char *)(gameInfoBuf.start+GAME->name));
    printf("Total number of players: %i\n", GAME->numOfPlayers);
    // always print the client player first
    for (int i = 0; i < GAME->numOfPlayers; ++i) {
        printf("- Player %i (%s) is ", PLAYERS[i].playerNo+1, (char *)(gameInfoBuf.start+PLAYERS[i].name));
        if (PLAYERS[i].isReady) printf("ready\n");
        else printf("not ready yet\n");
    }
    printf("This client is player %i.\n", GAME->playerNo+1);

    gameInfoShm = copyBufferToSharedMem(&gameInfoBuf);
    // the ID of gameInfoShm is stored in the shared memory created before forking, so the thinker can attach to it and read from it
    ((int *)shmBeforeFork.addr)[0] = gameInfoShm.id;

    // S: + ENDPLAYERS
    msg = getMessage();
    if (strcmp(msg, "+ ENDPLAYERS\n") != 0) {
        protocolError("+ ENDPLAYERS", msg);
    }
    free(msg);

    printf("\nPlayers are all in place. Let's start the game!\n\n");

    // playing phase of the protocol
    courseOfTheGame();
}

// Receive and store the board in the shared memory.
void receiveAndStoreBoard(void) {
    gameInfo *game = (gameInfo *)gameInfoShm.addr;

    // receives the width and height of the board, makes sure they are valid
    char *msg = getMessage();
    int width;
    int height;
    int assignedItems = sscanf(msg, "+ FIELD %i , %i", &width, &height);
    if (assignedItems != 2) {
        protocolError("+ FIELD << Spielfeld Breite >> , << Spielfeld Höhe >>", msg);
    }
    if (width <= 0 || height <= 0) {
        free(msg);
        foundError("Invalid dimensions are given.");
    }
    if (game->width == 0 && game->height == 0) {
        game->width = width;
        game->height = height;
    }
    if (width != game->width || height != game->height) {
        free(msg);
        foundError("The given width and height have changed.");
    }
    free(msg);

    // creates the shared memory that stores the board, only when the shared memory has not yet been filled
    if (!board.addr) {
        board = createSharedMem(game->width * game->height * sizeof(int));
        ((int *)shmBeforeFork.addr)[1] = board.id;
    }

    // initialize all the cells with -1
    for (int i = 0; i < game->width*game->height; ++i) {
        ((int *)board.addr)[i] = -1;
    }

    // receives the board and stores it in the shared memory
    for (int i = 0; i < height; ++i) {
        msg = getMessage();
        int line;
        int offset;  // keeps track of how many characters are read
        assignedItems = sscanf(msg, "+ %i %n", &line, &offset);
        if (assignedItems != 1) {
            protocolError("+ << Y >> << Stein_{1Y} >> << Stein_{2Y} >> ... << Stein_{X_{max}Y} >>", msg);
        }
        if (line <= 0 || line > height) {
            free(msg);
            foundError("The line number is not valid.");
        }
        for (int j = 0; j < width; ++j) {
            int readBytes = 0;
            int piece;
            if (sscanf(msg+offset, "%i %n", &piece, &readBytes) != 1) {
                // makes sure that there is really a '*' at the position and consumes the '*'
                if (sscanf(msg+offset, "* %n", &readBytes), readBytes == 0) {
                    protocolError("+ << Y >> << Stein_{1Y} >> << Stein_{2Y} >> ... << Stein_{X_{max}Y} >>", msg);
                }
            } else {
                if (piece < 0 || piece > 15) {
                    free(msg);
                    foundError("Invalid piece is given.");
                }
                ((int *)board.addr)[(line-1)*width+j] = piece;
            }
            offset += readBytes;
        }
        free(msg);
    }

    msg = getMessage();
    if (strcmp(msg, "+ ENDFIELD\n") != 0) {
        protocolError("+ ENDFIELD", msg);
    }
    free(msg);
}

void courseOfTheGame(void) {
    char *msg;
    int assignedItems;
    gameInfo *game = (gameInfo *)gameInfoShm.addr;  // points to the beginning of the shared memory that stores game info

    while (true) {
        msg = getMessage();
        int moveTimeBudget;
        int piece;
        // S: + WAIT
        if (strcmp(msg, "+ WAIT\n") == 0) {
            free(msg);

            // C: OKWAIT
            sendFormatted("OKWAIT\n");
        // S: MOVE << Maximale Zugzeit >>
        } else if (sscanf(msg, "+ MOVE %i", &moveTimeBudget) == 1) {
            game->moveTime = moveTimeBudget;
            free(msg);

            // S: + NEXT << Zu setzender Spielstein >>
            msg = getMessage();
            assignedItems = sscanf(msg, "+ NEXT %i", &piece);
            if (assignedItems != 1) {
                protocolError("+ NEXT << Zu setzender Spielstein >>", msg);
            }
            free(msg);
            if (piece < 0 || piece > 15) {
                foundError("Invalid piece is given.");
            }
            game->nextPiece = piece;

            // S: + FIELD << Spielfeld Breite >> , << Spielfeld Höhe >>
            // S: + << Y >> << Stein_{1Y} >> << Stein_{2Y} >> ... << Stein_{X_{max}Y} >>
            // S: + ENDFIELD
            receiveAndStoreBoard();

            // C: THINKING
            sendFormatted("THINKING\n");

            // signals the thinker to start computing
            game->thinkSigSent = true;
            kill(getppid(), SIGUSR1);

            // S: + OKTHINK
            msg = getMessage();
            if (strcmp(msg, "+ OKTHINK\n") != 0) {
                protocolError("+ OKTHINK", msg);
            }
            free(msg);

            // sends the move to the server
            move();
        // S: + GAMEOVER
        } else if (strcmp(msg, "+ GAMEOVER\n") == 0) {
            free(msg);

            // S: + FIELD << Spielfeld Breite >> << Spielfeld Höhe >>
            // S: + << Y >> << Stein_{1Y} >> << Stein_{2Y} >> ... << Stein_{X_{max}Y} >>
            // S: + ENDFIELD
            receiveAndStoreBoard();

            printf("★★★★★★★★★★★★★★★★★★★★★★★★★★★★★\n");

            // S: + PLAYER0WON <<'Yes' oder 'No'>>
            msg = getMessage();
            char yOrN1;
            assignedItems = sscanf(msg, "+ PLAYER0WON %c", &yOrN1);
            if (assignedItems != 1) {
                protocolError("+ PLAYER0WON <<'Yes' oder 'No'>>", msg);
            }
            free(msg);

            // S: + PLAYER1WON <<'Yes' oder 'No'>>
            msg = getMessage();
            char yOrN2;
            assignedItems = sscanf(msg, "+ PLAYER1WON %c", &yOrN2);
            if (assignedItems != 1) {
                protocolError("+ PLAYER1WON <<'Yes' oder 'No'>>", msg);
            }
            free(msg);
            if (yOrN1 == 'Y' && yOrN2 == 'Y') {
                printf("The game ends in a draw.  🤝\n");
            } else if (yOrN1 == 'Y') {
                printf("Player 1 wins. Congrats!  🏆\n");
            } else {
                assert(yOrN2 == 'Y');
                printf("Player 2 wins. Congrats!  🏆\n");
            }
            printf("★★★★★★★★★★★★★★★★★★★★★★★★★★★★★\n");

            // S: + QUIT
            msg = getMessage();
            if (strcmp(msg, "+ QUIT\n") != 0) {
                protocolError("+ QUIT", msg);
            }
            free(msg);
            exit(EXIT_SUCCESS);
        } else {
            protocolError("+ WAIT\" or \"+ MOVE << Maximale Zugzeit >>\" or \"+ GAMEOVER", msg);
        }
    }
}

// Send the coordinate and the selected next piece to the server.
void move(void) {
    char toSend[6];
    int readBytes = 0;
    // file descriptor of the thinker
    struct pollfd thinkerFD;
    thinkerFD.fd = pipeFD[0];
    thinkerFD.events = POLLIN;
    thinkerFD.revents = 0;
    // filer descriptor of the server
    struct pollfd serverFD;
    serverFD.fd = connSocket;
    serverFD.events = POLLIN;
    serverFD.revents = 0;

    struct pollfd fdArr[2] = {thinkerFD, serverFD};
    // 6 bytes may not be fully consumed with one read
    while(readBytes < 6) {
        int ret = poll(fdArr, 2, ((gameInfo *)gameInfoShm.addr)->moveTime+1000);
        if (ret == -1) {
            perror("Error in poll()");
            exit(EXIT_FAILURE);
        }
        if (ret == 0) {
            fprintf(stderr, "Neither the thinker nor the server is ready. poll() times out on its own.\n");
            exit(EXIT_FAILURE);
        }
        if (fdArr[0].revents != 0) {
            ssize_t temp = read(pipeFD[0], toSend+readBytes, 6-readBytes);
            if (temp <= 0) {
                perror("Error in reading from the pipe");
                exit(EXIT_FAILURE);
            }
            readBytes += temp;
        } else {
            assert(fdArr[1].revents != 0);
            char *msg = getMessage();
            protocolError("- TIMEOUT Be faster next time", msg);
        }
    }
    if (toSend[4]=='_') toSend[4] = '\0';  // if the piece number is within 0-9, don't send the filling-in-character '_' (replacing it with `\0` is a bit of a hack XD)
    sendFormatted("PLAY %s\n", toSend);

    char *msg = getMessage();
    if (strcmp(msg, "+ MOVEOK\n") != 0) {
        protocolError("+ MOVEOK", msg);
    }
    free(msg);
}

// Print the error message and end the program.
void foundError(char *errMsg) {
    fprintf(stderr, "%s\n", errMsg);
    exit(EXIT_FAILURE);
}

// Print a more concrete message when the protocol does not match.
void protocolError(char *expectedMsg, char *receivedMsg) {
    fprintf(stderr, "Received message does not match the protocol:\nexpected \"%s\",\nbut received \"%s\"\n", expectedMsg, receivedMsg);
    free(receivedMsg);
    exit(EXIT_FAILURE);
}

void sendWithErrorHandling(char *buf, size_t len) {
    ssize_t sentBytes = send(connSocket, buf, len, 0);
    if (verbose) { printf("%.*s", (int)len, buf); } // print at most len many characters
    if (sentBytes != (ssize_t)len) {
        foundError("The message was not succefully sent.");
    }
}

// send messages using a format string
// sendFormatted() is a variadic function
void sendFormatted(char *msg, ...) {
    va_list args;
    va_start(args, msg);

    if (verbose) { printf("C: "); }
    while (*msg != '\0') {
        if (*msg == '%') {
            ++msg;
            if (*msg == 's') {  // %s
                char *buf = va_arg(args, char *);
                sendWithErrorHandling(buf, strlen(buf));
            } else if (*msg == 'i' || *msg == 'd') {  // %i or %d
                size_t bufsize = 3*sizeof(int)+2;
                char buf[bufsize];
                int num = va_arg(args, int);
                snprintf(buf, bufsize, "%i", num);
                sendWithErrorHandling(buf, strlen(buf));
            } else if (*msg == 'c') {  // %c, not really used, just for completeness
                int ch = va_arg(args, int);
                char buf[2] = {(char)ch, '\0'};
                sendWithErrorHandling(buf, 2);
            }
            ++msg;
        } else { // read normal characters until a format specifier or the end of the message and send all these characters together
            size_t i = 0;
            while (*(msg+i) != '%' && *(msg+i) != '\0') { ++i; }
            sendWithErrorHandling(msg, i);
            msg += i;
        }
    }
    va_end(args);
}

// signal handler that handles the SIGINT and the SIGPIPE signals
void signalHandlerConnector(int signalKey) {
    if (signalKey == SIGINT) {
        exit(EXIT_FAILURE);
    } else if (signalKey == SIGPIPE) {}
}

// Clean up function that closes the socket and the pipe, to be registered in main().
void connectorCleanUp(void) {
    if (connSocket != -1) close(connSocket);
    close(pipeFD[0]);
}