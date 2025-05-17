#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <sys/shm.h> // For shared memory
#include <sys/ipc.h> // Also for shared memory, allows worker class to access shared memory
#include <time.h>
#include <string.h> // For memset
#include "oss.h"

#define NANO_TO_SEC 1000000000

void incrementClock(SimulatedClock *clock, int addSec, int addNano); // Clock increment
void signalHandler(int sig);
void help();

PCB processTable[MAX_PCB]; // Process Table
FrameTableEntry frameTable[FRAME_COUNT]; // Frame Table

int main(int argc, char **argv) {
	int userInput = 0;
	int totalProcesses = 40;
	int simul = 18;
	int interval = 500;
	int launched = 0;
	int activeProcesses = 0;
	int nextLaunchTime = 0;
	time_t startTime = time(NULL);
	char *logFileName = "oss.log";

	while ((userInput = getopt(argc, argv, "n:s:i:f:hv")) != -1) {
		switch(userInput) {
			case 'n': // How many child processes to launch.
				totalProcesses = atoi(optarg);
				if (totalProcesses <= 0) {
					printf("Error: Total child processes must be at least one. \n");
					exit(1);
				}
				break;
			case 's': // How many simulations to run at once.
				simul = atoi(optarg);
				// Ensuring simulations isn't zero or below for the program to work.
				if (simul < 0) {
					printf("Error: Simulations must be positive. \n");
					exit(1);
				}

				if (simul > 18) {
					printf("Simulations CANNOT exceed 18 \n");
					simul = 18;
				}
				break;
			case 'i': // How often to launch child interval
				interval = atoi(optarg);
				if (interval <= 0) {
					printf("Error: interval must be positive. \n");
                                        exit(1);
                                }
				break;
			case 'f': // Input name of log file
				logFileName = optarg;
                                break;
			case 'h': // Prints out help function.
				help();
				return 0;
			case '?': // Invalid user argument handling.
				printf("Error: Invalid argument detected \n");
				printf("Usage: ./oss.c -h to learn how to use this program \n");
				exit(1);
		}
	}

	// Start Alarm
	alarm(60);
	signal(SIGINT, signalHandler);
	signal(SIGALRM, signalHandler);

	FILE *file = fopen(logFileName, "w");
	if (!file) {
		printf("Error: failed opening log file. \n");
		exit(1);
	}

	// SIMULATED CLOCK
	int shmid = shmget(SHM_KEY, sizeof(SimulatedClock), IPC_CREAT | 0666); // Creating shared memory using shmget.
	if (shmid == -1) { // If shmid is -1 as a result of shmget failing and returning -1, error message will print.
        	printf("Error: OSS shmget failed. \n");
        	exit(1);
    	}

	SimulatedClock *clock = (SimulatedClock *)shmat(shmid, NULL, 0); // Attach shared memory, clock is now a pointer to SimulatedClock structure.
	if (clock == (void *)-1) { // if shmat, the attaching shared memory function, fails, it returns an invalid memory address.
		printf("Error: OSS shared memory attachment failed. \n");
		exit(1);
	}

	// MESSAGE QUEUE
	int msgid = msgget(MSG_KEY, IPC_CREAT | 0666); // Setting up msg queue.
        if (msgid == -1) {
                printf("Error: OSS msgget failed. \n");
                exit(1);
        }

	// Initialize clock.
	clock->seconds = 0;
	clock->nanoseconds = 0;

	for (int i = 0; i < MAX_PCB; i++) { // Initialize process table
		processTable[i].occupied = 0;
		processTable[i].pid = -1;
		processTable[i].startSeconds = 0;
		processTable[i].startNano = 0;
		for (int j = 0; j < NUM_PAGES; j++) {
			processTable[i].pageTable[j] = -1; // -1 = not in memory
		}
	}

	for (int i = 0; i < FRAME_COUNT; i++) { // Initialize frame table
		frameTable[i].occupied = 0;
		frameTable[i].dirty = 0;
		frameTable[i].processIndex = -1;
		frameTable[i].pageNumber = -1;
		frameTable[i].lastRefSec = 0;
		frameTable[i].lastRefNano = 0;
	}

	// Main Loop
	while (launched < totalProcesses || activeProcesses > 0) {
		
		// Random clock increment
		int randomNano = (rand() % 90001) + 10000; // Random increment
		incrementClock(clock, 0, randomNano);

		if (difftime(time(NULL), startTime) >= 5) { // Terminate after 5 real seconds.
		    	printf("OSS: 5 real seconds passed. Terminating.\n");
			fprintf(file, "OSS: Real-time limit of 5 seconds reached. Terminating simulation.\n");
		    	break;
		}
		
		// Status of process
		int status;
		pid_t pid = waitpid(-1, &status, WNOHANG);
		if (pid > 0) { // Check if PID  is terminating
		    	for (int i = 0; i < MAX_PCB; i++) { // If so, find PID and free up process index.
				if (processTable[i].occupied && processTable[i].pid == pid) {
			    		processTable[i].occupied = 0;
					
					printf("OSS: Process %d terminated at time %u:%u\n", pid, clock->seconds, clock->nanoseconds);
					fprintf(file, "OSS: Process %d terminated at time %u:%u\n", pid, clock->seconds, clock->nanoseconds);
					
					activeProcesses--;
			    		break;
				}
		    	}
		}


		if (launched < totalProcesses && activeProcesses < simul && (clock->seconds * NANO_TO_SEC + clock->nanoseconds) >= nextLaunchTime) { // Launch processes 
			// Find free PCB slot
			int pcbIndex = -1;
			for (int i = 0; i < MAX_PCB; i++) {
			    	if (!processTable[i].occupied) { // Set PCB index once fouund.
					pcbIndex = i;
					break;
			    	}
			}

			if (pcbIndex != -1) { // Launch child
				pid_t childPid = fork();
				if (childPid == 0) {
			    		execl("./worker", "./worker", NULL);
			    		perror("execl failed");
			    		exit(1);
				}
				//  Update PCB Table
				 processTable[pcbIndex].occupied = 1;
	 			 processTable[pcbIndex].pid = childPid;
	 			 processTable[pcbIndex].startSeconds = clock->seconds;
	 			 processTable[pcbIndex].startNano = clock->nanoseconds;
			
	 			 for (int j = 0; j < NUM_PAGES; j++) {
	 				 processTable[pcbIndex].pageTable[j] = -1;
	 			 }
	 
				 // Update variables
				  launched++;
	  			  activeProcesses++;
	  			  nextLaunchTime = clock->seconds * NANO_TO_SEC + clock->nanoseconds + interval * 1000000;
			}
		}

		// Receive message from worker.
		OssMSG msg;
		while (msgrcv(msgid, &msg, sizeof(OssMSG) - sizeof(long), 0, IPC_NOWAIT) > 0) {
		    	int pcbIndex = -1; // Get PCB index that corresponds to PID in message.
		    	for (int i = 0; i < MAX_PCB; i++) {
				if (processTable[i].occupied && processTable[i].pid == msg.pid) {
			    		pcbIndex = i;
			     		break;
				}
		    	}
		    	
			if (pcbIndex == -1) { // Skip if not found
				continue;
			}

			// Get memory address, check for write or read operation
		    	int address = msg.address;
		    	int isWrite = msg.isWrite;
			// Convert address to page number
		    	int page = address / 1024;

			// Look up frame number for requested page
			int frameIndex = processTable[pcbIndex].pageTable[page];	
			
			if (frameIndex != -1) { // Page Hit
			    	// Update LRU, when it was last accessed.
				frameTable[frameIndex].lastRefSec = clock->seconds;
			    	frameTable[frameIndex].lastRefNano = clock->nanoseconds;
			   
				if (isWrite) { // Update dirty bit if write operation.
					frameTable[frameIndex].dirty = 1;
				}
				
				// Send message back to worker process.
				OssMSG response;
			    	response.mtype = msg.pid;
			    	response.pid = msg.pid;
			    	response.address = address;
			    	response.isWrite = isWrite;
			    	msgsnd(msgid, &response, sizeof(OssMSG) - sizeof(long), 0);	
				
				// Output
				printf("OSS: P%d accessed page %d (frame %d) at %u:%u (%s)\n", msg.pid, page, frameIndex, clock->seconds, clock->nanoseconds, isWrite ? "WRITE" : "READ");

			    	fprintf(file, "OSS: P%d accessed page %d (frame %d) at %u:%u (%s)\n", msg.pid, page, frameIndex, clock->seconds, clock->nanoseconds, isWrite ? "WRITE" : "READ");
				continue;  // Skip further handling for this message
			}

			// Page fault
			fprintf(file, "OSS: PAGE FAULT for P%d on page %d at time %u:%u\n", msg.pid, page, clock->seconds, clock->nanoseconds);
       			printf("OSS: PAGE FAULT for P%d on page %d at time %u:%u\n", msg.pid, page, clock->seconds, clock->nanoseconds);

			int chosenFrame = -1; // Find a free frame
			for (int i = 0; i < FRAME_COUNT; i++) {
			    	if (!frameTable[i].occupied) {
					chosenFrame = i;
					break;
			    	}
			}

			if (chosenFrame == -1) { // If free frame was not found
				// Prepare variables for LRU
				unsigned int oldestSec = 0;
				unsigned int oldestNano = 0;
				int firstFound = 1;
				

				for (int i = 0; i < FRAME_COUNT; i++) { // Go through each frame and choose the least recently used frame.
					if (firstFound || frameTable[i].lastRefSec < oldestSec || (frameTable[i].lastRefSec == oldestSec && frameTable[i].lastRefNano < oldestNano)) {
						oldestSec = frameTable[i].lastRefSec;
						oldestNano = frameTable[i].lastRefNano;
						chosenFrame = i;
						firstFound = 0;
					}
				}

			}
		    
			if (frameTable[chosenFrame].dirty) { // Simulate writing
				fprintf(file, "OSS: Evicting dirty frame %d, adding 14ms I/O delay\n", chosenFrame);       
			       	incrementClock(clock, 0, 14000000); // 14ms
		    	}

			// Remove old page from page table.
		    	int oldPIDIndex = frameTable[chosenFrame].processIndex;
		    	int oldPage = frameTable[chosenFrame].pageNumber;
			if (oldPIDIndex != -1 && oldPage != -1) {
				processTable[oldPIDIndex].pageTable[oldPage] = -1;
		    	}

			// Load new page into chosen frame
			frameTable[chosenFrame].occupied = 1;
			frameTable[chosenFrame].dirty = isWrite;
			frameTable[chosenFrame].lastRefSec = clock->seconds;
			frameTable[chosenFrame].lastRefNano = clock->nanoseconds;
			frameTable[chosenFrame].processIndex = pcbIndex;
			frameTable[chosenFrame].pageNumber = page;

			// Update this process's page table
			processTable[pcbIndex].pageTable[page] = chosenFrame;

			// Respond to worker
			OssMSG response;
			response.mtype = msg.pid;
			response.pid = msg.pid;
			response.address = address;
			response.isWrite = isWrite;
			msgsnd(msgid, &response, sizeof(OssMSG) - sizeof(long), 0);
			
			fprintf(file, "OSS: Loaded page %d of P%d into frame %d at %u:%u (%s)\n", page, msg.pid, chosenFrame, clock->seconds, clock->nanoseconds, isWrite ? "WRITE" : "READ");
			printf("OSS: Loaded page %d of P%d into frame %d at %u:%u (%s)\n", page, msg.pid, chosenFrame, clock->seconds, clock->nanoseconds, isWrite ? "WRITE" : "READ");

		}
	}
	// Detach shared memory
    	if (shmdt(clock) == -1) {
        	printf("Error: OSS Shared memory detachment failed \n");
		exit(1);
    	}

    	// Remove shared memory
    	if (shmctl(shmid, IPC_RMID, NULL) == -1) {
        	printf("Error: Removing memory failed \n");
		exit(1);
    	}

	// Remove message queue
	if (msgctl(msgid, IPC_RMID, NULL) == -1) {
		printf("Error: Removing msg queue failed. \n");
		exit(1);
	}
       
	fclose(file);

	return 0;
}

void incrementClock(SimulatedClock *clock, int addSec, int addNano) { // This function simulates the increment of our simulated clock.
	clock->seconds += addSec;
	clock->nanoseconds += addNano;

	while (clock->nanoseconds >= NANO_TO_SEC) {
		clock->seconds++;
        	clock->nanoseconds -= NANO_TO_SEC;
    }
}


void signalHandler(int sig) { // Signal handler
       	// Catching signal
	if (sig == SIGALRM) { // 60 seconds have passed
	       	fprintf(stderr, "Alarm signal caught, terminating all processes.\n");
       	}
     	else if (sig == SIGINT) { // Ctrl-C caught
	       	fprintf(stderr, "Ctrl-C signal caught, terminating all processes.\n");
       	}

	for (int i = 0; i < MAX_PCB; i++) { // Kill all processes.
		if (processTable[i].occupied) {
			kill(processTable[i].pid, SIGTERM);
	    	}
	}

	// Cleanup shared memory
    	int shmid = shmget(SHM_KEY, sizeof(SimulatedClock), 0666);
    	if (shmid != -1) {
		SimulatedClock *clock = (SimulatedClock *)shmat(shmid, NULL, 0);
		if (clock != (void *)-1) { // Detach shared memory
			if (shmdt(clock) == -1) {
				printf("Error: OSS Shared memory detachment failed \n");
				exit(1);
			}
		}
		// Remove shared memory
		if (shmctl(shmid, IPC_RMID, NULL) == -1) {
                	printf("Error: Removing memory failed \n");
                	exit(1);
        	}
	}

	// Cleanup message queue
    	int msgid = msgget(MSG_KEY, 0666);
    	if (msgid != -1) {
		if (msgctl(msgid, IPC_RMID, NULL) == -1) {
		    	printf("Error: Removing msg queue failed. \n");
		       	exit(1);
	       	}
       	}



	exit(1);
}

void help() {
	printf("Usage: ./oss [-h] [-n proc] [-s simul] [-i interval] [-f logfile] [-v]\n");
    	printf("Options:\n");
    	printf("-h 	      Show this help message and exit.\n");
    	printf("-n proc       Total number of user processes to launch (default: 40).\n");
    	printf("-s simul      Maximum number of simultaneous processes (max: 18).\n");
    	printf("-i interval   Time interval (ms) between process launches (default: 500).\n");
	printf("-f logfile    Name of the log file to write output (default: oss.log).\n");
    	printf("-v            Enable verbose output to both screen and file.\n");
}

