#include <stdio.h>
#include <string.h>
#include <sys/shm.h>
#include <unistd.h>
#include "sharedMemory.h"

shmInfo shmBeforeFork;  // shared memory created before forking for the processes to exchange IDs of the later created shared memories
bufInfo gameInfoBuf;  // a dynamically allocated buffer that holds all the information of the game and players, later the information is copied to the shared memory gameInfoShm and the buffer is freed
                      // this way we know the size of the information we are going to store and can create the shared memory of exact size
shmInfo gameInfoShm;  // shared memory that contains the game and player info
shmInfo board;

// Create a buffer in the heap.
bufInfo createBuffer(size_t cap) {
    bufInfo buf;
    buf.capacity = cap;
    buf.start = malloc(buf.capacity);
    buf.used = 0;
    return buf;
}

// Allocate an 8-byte aligned segment in the buffer and return the offset of the segment.
size_t allocInBuffer(size_t objSize, bufInfo *buf) {
    objSize = objSize%8 == 0 ? objSize : objSize + (8 - objSize%8);  // make sure that all allocations are 8-byte aligned
    size_t offset = buf->used;
    if ((buf->used+=objSize) > (buf->capacity)) {
        size_t doubleCap = buf->capacity * 2;
        buf->capacity = doubleCap < buf->used ? buf->used : doubleCap;  // doubling the capacity could still be too small
        buf->start = realloc(buf->start, buf->capacity);
    }
    return offset;
}

// Copy an object into the buffer.
size_t copyToBuffer(void *obj, size_t objSize, bufInfo *buf) {
    size_t offset = allocInBuffer(objSize, buf);
    memcpy(buf->start+offset, obj, objSize);
    return offset;
}

// Create a shared memory segment and return its ID as well as its start address.
shmInfo createSharedMem(size_t cap) {
    shmInfo shm;
    int memID;
    if ((memID = shmget(IPC_PRIVATE, cap, 0600)) == -1) {
        perror("Error in creating a shared memory segment");
        exit(EXIT_FAILURE);
    }
    void *memAddr;
    if ((memAddr = shmat(memID, NULL, 0)) == (void *)-1) {
        perror("Error in attaching the shared memory to the calling process");
        exit(EXIT_FAILURE);
    }
    shm.id = memID;
    shm.addr = memAddr;
    shm.creator = getpid();
    return shm;
}

// Copy the content of the buffer into a newly created shared memory.
shmInfo copyBufferToSharedMem(bufInfo *buf) {
    shmInfo shm = createSharedMem(buf->used);  // make sure that the size of the shared memory is exactly the size of the information in the buffer
    memcpy(shm.addr, buf->start, buf->used);
    return shm;
}

// Delete and remove one shared memory segment.
void cleanUpSharedMem(shmInfo shm) {
    if (shm.addr) {  // make sure the shared memory exists
        if (shmdt(shm.addr) == -1) {
            perror("Error in detaching the shared memory from the calling process");
        }
        if (shm.creator == getpid()) {  // only the creator of the shared memory is allowed to remove the shared memory (both processes trying to remove will cause clash)
            if (shmctl(shm.id, IPC_RMID, NULL) == -1) {
                // fprintf(stderr, "process: %i\n", getpid());
                perror("Error in removing the shared memory segment");
            }
        }
    }
}

// Delete and remove all the shared memory segments, registered with atexit().
void cleanUpSharedMems(void) {
    cleanUpSharedMem(shmBeforeFork);
    free(gameInfoBuf.start);  // free a NULL pointer is safe
    cleanUpSharedMem(gameInfoShm);
    cleanUpSharedMem(board);
}