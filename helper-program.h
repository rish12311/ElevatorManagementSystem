#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PERMS 0666
#define CONSTANT 100000000
#define ELEVATOR_MAX_CAP 20
#define AUTH_STRING_UNIQUE_LETTERS 6
#define MAX_NEW_REQUESTS 30

// These are the messages sent by the student program to the helper
typedef struct TurnChangeRequest {
  long mtype;
  int droppedPassengersCount;
  int pickedUpPassengersCount;
} TurnChangeRequest;

// These are the messages the helper responds with when a new turn starts
typedef struct TurnChangeResponse {
  long mtype;
  int turnNumber;
  int newPassengerRequestCount;
  int errorOccured;  // if True, there was an error with the previous turn
                     // change request
  int finished;  // if True, all passenger requests have been granted and the
                 // student program may exit
} TurnChangeResponse;

// Represents a request by a passenger
typedef struct PassengerRequest {
  int requestId;
  int startFloor;
  int requestedFloor;
} PassengerRequest;

// Stuff for the helper to keep track of each request
typedef struct PassengerRequestInfo {
  PassengerRequest request;
  int granted;          // if True, the request is finished.
  int isInElevator;     // if True, the passenger is in an elevator. Else, on
                        // some floor
  int currentLocation;  // if 'isInElevator' is True, this is elevator number.
                        // Otherwise, it is floor number
  int arrivalTime;
  int movedThisTurn;  // if True, the passenger has already been moved in/out of
                      // an elevator this turn, and may not be moved again
} PassengerRequestInfo;

// Stuff for the helper to keep track of each elevator
typedef struct ElevatorInfo {
  int currentFloor;
  int passengerCount;
  int passengers[ELEVATOR_MAX_CAP];
} ElevatorInfo;

// The shared memory shared between helper and student program
typedef struct MainSharedMemory {
  // Out of 100, use elements equal to number of elevators. Set by the student
  // program
  char authStrings[100][ELEVATOR_MAX_CAP + 1];

  // 'u' = up, 'd' = down, 's' = stay, others are invalid. Set by the student
  // program
  char elevatorMovementInstructions[100];

  // Each element is a new request that has come this turn. Set by the helper.
  PassengerRequest newPassengerRequests[MAX_NEW_REQUESTS];

  // Represents which floor each elevator is now on. Set by the helper
  int elevatorFloors[100];

  // Each element will be a passenger request ID. Set by the student program.
  int droppedPassengers[1000];

  // In each element, the first number will be the passenger request ID, and the
  // second will be the elevator number (starting from 0). Set by the student
  // program.
  int pickedUpPassengers[1000][2];
} MainSharedMemory;

// These are the messages sent by the student program to the solvers
typedef struct SolverRequest {
  long mtype;
  int elevatorNumber;
  char authStringGuess[ELEVATOR_MAX_CAP + 1];
} SolverRequest;

// These are the messages the solver responds with when a guess is made
typedef struct SolverResponse {
  long mtype;
  int guessIsCorrect;  // Is nonzero if the guess was correct
} SolverResponse;

typedef struct SolverArguments {
  int solverNumber;
  int messageQueueKey;
} SolverArguments;

typedef struct SolverInfo {
  pthread_t threadId;
  key_t msgKey;
  int msgId;
} SolverInfo;

void* solverRoutine(void* args);
void createNewAuthString(char* authStringLocation, int length);