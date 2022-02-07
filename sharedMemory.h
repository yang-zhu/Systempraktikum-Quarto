#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>

typedef struct {
    void *start;
    size_t used;
    size_t capacity;
} bufInfo;

typedef struct {
    int playerNo;
    size_t name;  // shared memory offset of a string
    bool isReady;
} playerInfo;

typedef struct {
    size_t name;  // shared memory offset of a string
    int playerNo;
    int numOfPlayers;
    size_t players;  // shared memory offset of an array of playerInfo of length numOfPlayers
    pid_t thinker;
    pid_t connector;
    int moveTime;  // a move is required within moveTime
    bool thinkSigSent;  // set when the signal is sent by the connector
    int width;
    int height;
    int nextPiece;
} gameInfo;

typedef struct {
    int id;
    void *addr;
    pid_t creator;
} shmInfo;

extern shmInfo shmBeforeFork;
extern bufInfo gameInfoBuf;
extern shmInfo gameInfoShm;
extern shmInfo board;

bufInfo createBuffer(size_t cap);
size_t allocInBuffer(size_t objSize, bufInfo *buf);
size_t copyToBuffer(void *obj, size_t objSize, bufInfo *buf);
shmInfo createSharedMem(size_t cap);
shmInfo copyBufferToSharedMem(bufInfo *buf);
void cleanUpSharedMems(void);

#endif