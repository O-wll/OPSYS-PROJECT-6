#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <string.h>
#include "oss.h"

// Author: Dat Nguyen
// Date: 05/17/2025
// worker.c is the child process that runs when oss.c forks. It simulates processes in page and can terminate at random.

#define READ_BIAS 80  // 80% read, 20% write

int main() {
    srand(getpid() ^ time(NULL));

    // Attach to shared memory clock
    int shmid = shmget(SHM_KEY, sizeof(SimulatedClock), 0666);
    if (shmid == -1) {
        perror("worker: shmget failed");
        exit(1);
    }

    SimulatedClock *clock = (SimulatedClock *)shmat(shmid, NULL, 0);
    if (clock == (void *)-1) {
        perror("worker: shmat failed");
        exit(1);
    }

    // Connect to message queue
    int msgid = msgget(MSG_KEY, 0666);
    if (msgid == -1) {
        perror("worker: msgget failed");
        exit(1);
    }

    int memoryAccessCount = 0;
    int terminateThreshold = 1000 + (rand() % 201);

    while (1) {
        // Choose random page (0 to 31)
        int pageNum = rand() % NUM_PAGES;
        int offset = rand() % PAGE_SIZE;
        int address = pageNum * PAGE_SIZE + offset;

        // Biased random choice: read (80%) or write (20%)
        int isWrite = (rand() % 100 < READ_BIAS) ? 0 : 1;

        // Prepare message to send to OSS
        OssMSG request;
        request.mtype = 1; // Not used by oss, as oss filters by `msg.pid`
        request.pid = getpid();
        request.address = address;
        request.isWrite = isWrite;

        // Send memory access request
        if (msgsnd(msgid, &request, sizeof(OssMSG) - sizeof(long), 0) == -1) {
            perror("worker: msgsnd failed");
            break;
        }

        // Wait for response from OSS
        OssMSG response;
        if (msgrcv(msgid, &response, sizeof(OssMSG) - sizeof(long), getpid(), 0) == -1) {
            perror("worker: msgrcv failed");
            break;
        }

        memoryAccessCount++;

        // Randomly decide whether to terminate after N accesses
        if (memoryAccessCount >= terminateThreshold) {
            break;
        }
    }

    // Detach shared memory
    shmdt(clock);
    return 0;
}

