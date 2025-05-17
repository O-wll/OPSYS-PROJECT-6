#ifndef OSS_H
#define OSS_H

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <sys/shm.h> // For shared memory

#define SHM_KEY 856050
#define MSG_KEY 875010
#define MAX_PCB 20
#define MAX_PROCESSES 18
#define NUM_PAGES 32            // 32K per process with 1K pages
#define FRAME_COUNT 256         // Total frames in system
#define PAGE_SIZE 1024          // 1K per page

// Author: Dat Nguyen
// oss.h is a header file that holds our structures and constant definitions for memory management and paging

// Simulated system clock
typedef struct SimulatedClock {
    unsigned int seconds;
    unsigned int nanoseconds;
} SimulatedClock;

// Process Control Block
typedef struct PCB {
    int occupied;
    pid_t pid;
    int startSeconds;
    int startNano;
    int pageTable[NUM_PAGES];  // Each index maps to a frame number or -1 if not present
} PCB;

// Frame Table Entry
typedef struct FrameTableEntry {
    int occupied;              // 0 = free, 1 = occupied
    int dirty;                 // 1 = modified (write), 0 = clean
    int processIndex;          // Index of process using it
    int pageNumber;            // Page number within that process
    unsigned int lastRefSec;   // Last reference time (seconds)
    unsigned int lastRefNano;  // Last reference time (nanoseconds)
} FrameTableEntry;

// Message from user to oss
typedef struct OssMSG {
    long mtype;
    pid_t pid;
    int address;   // Requested memory address
    int isWrite;   // 1 = write, 0 = read
} OssMSG;

// Message from oss to user
typedef struct OssResponse {
    long mtype;
    int result;    // 0 = granted, 1 = page fault, 2 = terminated
} OssResponse;

#endif

