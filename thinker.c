#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <sys/signal.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <pthread.h>
#include "thinker.h"
#include "sharedMemory.h"
#include "utils.h"
#include "sysprak-client.h"


void signalHandlerThinker(int signalKey) {
    if (signalKey == SIGUSR1) {
        think();
    } else if (signalKey == SIGINT) {
        exit(EXIT_FAILURE);
    } else if (signalKey == SIGPIPE) {}  // When the connector closes the pipe but the thinker still tries to write to it, do nothing. write() will report the error. This way the process exits properly and cleans up all the resources.
}

// Convert a decimal integer (0-15) to its binary representation and write it into an char array
string16 convertIntToBinary(int num) {
    if (num < 0 || num > 15) exit(EXIT_FAILURE);
    string16 bin = {"0000"};
    int i = 3;
    while (num > 0) {
        bin.str[i] = num % 2 == 0 ? '0' : '1';
        num /= 2;
        --i;
    }
    return bin;
}

void think(void) {
    // attach the shared memory that contains game and player information
    if (!gameInfoShm.addr) {
        gameInfoShm.id = ((int *)shmBeforeFork.addr)[0];
        if (!(gameInfoShm.addr = shmat(gameInfoShm.id, NULL, 0))) {
            perror("Error in attaching gameInfoShm");
            exit(EXIT_FAILURE);
        }
    }
    // attach the shared memory that contains the board
    if (!board.addr) {
        board.id = ((int *)shmBeforeFork.addr)[1];
        if (!(board.addr = shmat(board.id, NULL, 0))) {
            perror("Error in attaching gameInfoShm");
            exit(EXIT_FAILURE);
        }
    }

    gameInfo *game = (gameInfo *)gameInfoShm.addr;
    // all the computations only work for a board of 4 x 4
    if (game->width != 4 || game->height != 4) {
        fprintf(stderr, "The board is not 4x4.\n");
        exit(EXIT_FAILURE);
    }
    int *gameBoard = (int *)board.addr;
    // if the flag is false, then don't do anything, because the signal is not sent by the connector
    if (!game->thinkSigSent) return;

    game->thinkSigSent = false;  // set the flag back

    // print the board on the terminal
    printf("Next: %s\n\n", convertIntToBinary(game->nextPiece).str);

    for (int i = 0; i < game->width; ++i) {
        printf("    %c", 'A'+i);
    }
    printf("\n");
    puts("  +---------------------+  ");

    // prints the fourth line first and first line last
    for (int i = game->height-1; i >= 0; --i) {
        printf("%i | ", i+1);
        for (int j = 0; j < game->width; ++j) {
            int piece = gameBoard[i*game->width+j];
            if (piece == -1) {
                printf("**** ");
            } else {
                printf("%s ", convertIntToBinary(piece).str);
            }
        }
        printf("| %i\n", i+1);
        if (i != 0) puts("  |                     |  ");
    }

    puts("  +---------------------+  ");
    for (int i = 0; i < game->width; ++i) {
        printf("    %c", 'A'+i);
    }
    printf("\n");

    // recreate the pieces array for every round of the game
    // the pieces that are already placed on the board is set true
    bool pieces[16] = {false};
    for (int i = 0; i < 16; ++i) {
        if (gameBoard[i] != -1) pieces[gameBoard[i]] = true;
    }
    pieces[game->nextPiece] = true;

    // create another thread to compute the move
    pthread_t computeThread;
    int selectedPos = -1;
    int selectedPiece = -1;
    int prediction;
    negamaxParam params;
    params.board = gameBoard;
    params.givenPiece = game->nextPiece;
    params.pieces = pieces;
    params.depth = 16;
    params.alpha = -1;
    params.beta = 1;
    params.selectedPos = &selectedPos;
    params.selectedPiece = &selectedPiece;
    params.prediction = &prediction;
    if (pthread_create(&computeThread, NULL, computeMove, &params)) {
        fprintf(stderr, "Error in creating the thread.\n");
        exit(EXIT_FAILURE);
    }

    // the main thread sleeps for certain time and then wakes up to terminate the other thread
    milliSleep(game->moveTime - 500);
    pthread_cancel(computeThread);
    if (pthread_join(computeThread, NULL)) {
        fprintf(stderr, "Error in joining the thread.\n");
        exit(EXIT_FAILURE);
    }

    // craft the message sent to the connector
    char move[6] = {'\0'};  // coordinate (2 chars) + ',' (1 char) + piece No. (1 or 2 char(s)) + '\0' (1 char)
    move[0] = 'A' + selectedPos % 4;  // x-coordinate
    move[1] = '1' + selectedPos / 4;  // y-coordinate
    printf("Piece %s is put at coordinate %c%c.\n", convertIntToBinary(game->nextPiece).str, move[0], move[1]);
    if (selectedPiece != -1) {
        if (selectedPiece > 9) sprintf(move+2, ",%i", selectedPiece);
        else sprintf(move+2, ",%i_", selectedPiece);  // if the piece is of one digit, add a '_' character to fill the space
        printf("The piece given to the opponent is %s.\n", convertIntToBinary(selectedPiece).str);
    }
    printf("AI predicts: %s\n\n", prediction == 0 ? "draw" : (prediction == 1 ? "win" : "lose"));

    // communicate with the connector via pipe
    if (write(pipeFD[1], move, sizeof(move)) != sizeof(move)) {
        perror("Error in writing to the pipe");
        exit(EXIT_FAILURE);
    }
}

void checkDiagonalWin(int *selectedPos, bool *immediateWin, int posArr[4], int *board, int piece) {
    int bitwiseAnd = 15;
    int bitwiseOr = 0;
    int emptyCells = 0;
    int emptyCellpos;
    for (int i = 0; i < 4; ++i) {
        int pos = posArr[i];
        if (board[pos] == -1) {
            emptyCells += 1;
            emptyCellpos = pos;
            continue;
        }
        bitwiseAnd &= board[pos];
        bitwiseOr |= board[pos];
    }
    if (emptyCells == 1) {
        bitwiseAnd &= piece;
        bitwiseOr |= piece;
        if (bitwiseAnd != 0 || bitwiseOr != 15) {
            *selectedPos = emptyCellpos;
            *immediateWin = true;
        }
    }
}

// Check if putting the given piece in a certain cell results in an immediate win.
bool isImmediateWin(int *selectedPos, int *board, int piece) {
    int width = 4;
    int height = 4;

    bool immediateWin = false;
    // diagonal indices
    int upperLeftToLowerRight[4] = {0, 5, 10, 15};
    int upperRightToLowerLeft[4] = {3, 6, 9, 12};

    // check all the rows
    for (int i = 0; i < height; ++i) {
        int bitwiseAnd = 15;  // binary 1111, all properties are 1
        int bitwiseOr = 0;  // binary 0000, all properties are 0
        int emptyCells = 0;
        int xCoord;
        // count the number of empty cells
        // immediate win only happens in a row with one empty cell
        for (int j = 0; j < width; ++j) {
            if (board[i*width+j] == -1) {
                emptyCells += 1;
                xCoord = j;
                continue;  // skip over empty cells in computing bitwise-and and bitwise-or
            }
            bitwiseAnd &= board[i*width+j];
            bitwiseOr |= board[i*width+j];
        }
        if (emptyCells == 1) {
            bitwiseAnd &= piece;
            bitwiseOr |= piece;
            // bitwiseAnd != 0 means that at least one property is 1 for all four pieces
            // bitwiseOr != 15 means that at least one property is 0 for all four pieces
            if (bitwiseAnd != 0 || bitwiseOr != 15) {
                *selectedPos = i*width+xCoord;
                immediateWin = true;
            }
        }
    }

    // check all the columns
    if (!immediateWin) {
        for (int i = 0; i < width; ++i) {
            int bitwiseAnd = 15;
            int bitwiseOr = 0;
            int emptyCells = 0;
            int yCoord;
            for (int j = 0; j < height; ++j) {
                if (board[j*width+i] == -1) {
                    emptyCells += 1;
                    yCoord = j;
                    continue;
                }
                bitwiseAnd &= board[j*width+i];
                bitwiseOr |= board[j*width+i];
            }
            if (emptyCells == 1) {
                bitwiseAnd &= piece;
                bitwiseOr |= piece;
                if (bitwiseAnd != 0 || bitwiseOr != 15) {
                    *selectedPos = yCoord*width+i;
                    immediateWin = true;
                }
            }
        }
    }

    // check the diagonals
    if (!immediateWin) {
        checkDiagonalWin(selectedPos, &immediateWin, upperLeftToLowerRight, board, piece);
    }
    if (!immediateWin) {
        checkDiagonalWin(selectedPos, &immediateWin, upperRightToLowerLeft, board, piece);
    }

    return immediateWin;
}

// Check if there is only one empty cell left on the board.
bool oneEmptyCellLeft(int *board, int *selectedPos) {
    int numOfEmptyCells = 0;
    int emptyCell = -1;
    for (int i = 0; i < 16; ++i) {
        if (board[i] == -1) {
            ++numOfEmptyCells;
            emptyCell = i;
        }
        if (numOfEmptyCells > 1) return false;
    }
    *selectedPos = emptyCell;
    return true;
}

// Compute where to put the given piece and which piece to give to the opponent player.
void *computeMove(void *voidParams) {
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    negamaxParam *params = (negamaxParam *)voidParams;
    int tempPos = *params->selectedPos;
    int tempPiece = *params->selectedPiece;
    for (int depth = 3; depth <= params->depth; ++depth) {  // starts from depth 3, because the result is not very informative when the searching is not deep enough
        clock_t start, end;
        start = clock();
        // during negamax() the thread can be cancelled anytime
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        *params->prediction = negamax(params->board, params->givenPiece, params->pieces, depth, params->alpha, params->beta, &tempPos, &tempPiece);
        // the thread does not want to be interrupted when it is writing to params
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        end = clock();
        // only write to params when one round of computation is properly finished
        *params->selectedPos = tempPos;
        *params->selectedPiece = tempPiece;
        if (verbose) {
            printf("depth: %i; selected pos: %i; selected piece: %i; time taken: %f\n", depth, *params->selectedPos, *params->selectedPiece, ((double)(end-start))/CLOCKS_PER_SEC);
            printf("AI predicts: %s\n", *params->prediction == 0 ? "draw" : (*params->prediction == 1 ? "win" : "lose"));
        }
    }
    return NULL;  // the function is required to return a void pointer
}

// Negamax with alpha beta pruning
int negamax(int *board, int givenPiece, bool pieces[16], int depth, int alpha, int beta, int *selectedPos, int *selectedPiece) {
    // base cases
    if (depth == 0) return 0;
    if (isImmediateWin(selectedPos, board, givenPiece)) return 1;
    if (oneEmptyCellLeft(board, selectedPos)) return 0;

    pthread_testcancel();  // checks if the thread is cancelled, if so, aborts the computation

    // explore the search space of board configurations
    // the search is randomized
    int maxScore = INT_MIN;
    int randNum1 = rand() % 16;
    int randNum2 = rand() % 16;
    for (int i = 0; i < 16; ++i) {
        int cell = (randNum1 + i) % 16;  // choose a random cell
        if (board[cell] == -1) board[cell] = givenPiece;  // suppose the given piece is put at position cell
        else continue;
        for (int j = 0; j < 16; ++j) {
            int piece = (randNum2 + j) % 16;  // choose a random piece
            if (pieces[piece] == false) {
                pieces[piece] = true;  // suppose that we select piece
                int dummy1, dummy2;  // thrown away
                int score = -negamax(board, piece, pieces, depth-1, -beta, -alpha, &dummy1, &dummy2);
                // the path with the highest score will be selected
                if (score > maxScore) {
                    maxScore = score;
                    *selectedPos = cell;
                    *selectedPiece = piece;
                    alpha = maxScore;  // update the lower bound
                }
                pieces[piece] = false;
                if (alpha >= beta) break;  // stop searching when an optimal path is found
            } else continue;
        }
        board[cell] = -1;
        if (alpha >= beta) break;
    }

    return maxScore;
}

// Clean up function for the thinker, to be registered in main().
void thinkerCleanUp(void) {
    close(pipeFD[1]);
}