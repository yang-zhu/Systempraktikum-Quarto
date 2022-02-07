#include <stdio.h>
#include <stdlib.h>
#include <sys/signal.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/shm.h>
#include "config.h"
#include "sharedMemory.h"
#include "connector.h"
#include "sysprak-client.h"
#include "thinker.h"

#define GAME_ID_LENGTH 13
#define SHARED_MEM_CAP 1<<20

int pipeFD[2];
bool verbose;

// The method is called when the command line arguments are provided wrongly.
// Print the usage information on the terminal and end the program.
void printUsage() {
    printf("Usage:\n");
    printf("-g <13 digit number>: Game-ID (obligatory)\n");
    printf("-p <1 or 2>: desired player number (optional)\n");
    printf("-c <file path>: the path of the configuration file (optional)\n");
    printf("-v: verbose mode\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    // process command line arguments
    bool gameIDGiven = false;
    char gameID[GAME_ID_LENGTH+1];  // +1 to hold the NUL character
    int playerNo = -1;
    char *configFile = "client.conf";
    int ret;
    verbose = false;  // verbose mode by default disabled
    while ((ret = getopt(argc, argv, "g:p:c:v")) != -1) {
        switch(ret) {
            case 'g':
                if (strlen(optarg) != GAME_ID_LENGTH) printUsage();
                strcpy(gameID, optarg);
                gameIDGiven = true;
                break;
            case 'p':
                playerNo = atoi(optarg);
                if (playerNo != 1 && playerNo != 2) printUsage();
                break;
            case 'c':
                configFile = optarg;  // the memory optarg points to lives till the end of the program
                break;
            case 'v':
                verbose = true;
                break;
            default:
                printUsage();
        }
    }
    if (!gameIDGiven || optind != argc) printUsage();

    // set up all the configurations
    setConfig(configFile, &conf);

    // initialize the random number generator for the thinker
    srand(time(0));

    // detach and remove all the shared memory segments wherever exit() is called
    atexit(cleanUpSharedMems);
    // creating the shared memory before forking gives us the convenience that it will be mapped to the same virtual address in the two processes
    shmBeforeFork = createSharedMem(2*sizeof(int));  // stores two shared memory IDs

    // create the pipe before forking
    if (pipe(pipeFD) < 0) {
        perror("Error in creating the pipe");
        exit(EXIT_FAILURE);
    }

    pid_t pid;
    if ((pid = fork()) < 0) {
        perror("Error in creating a process");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        // close the write end of the pipe
        close(pipeFD[1]);
        atexit(connectorCleanUp);
        signal(SIGINT, signalHandlerConnector);  // CTRL-C
        signal(SIGPIPE, signalHandlerConnector);
        // create the socket and connect with the server
        connectSocket();
        // communicate with the server
        performConnection(gameID, playerNo);
    } else {
        // close the read end of the pipe
        close(pipeFD[0]);
        atexit(thinkerCleanUp);
        // register the signal handler
        signal(SIGUSR1, signalHandlerThinker);
        signal(SIGINT, signalHandlerThinker);
        signal(SIGPIPE, signalHandlerThinker);
        if (waitpid(pid, NULL, 0) != pid) {
            perror("Error in waiting for the child process to terminate");
            exit(EXIT_FAILURE);
        }
    }
    return EXIT_SUCCESS;
}