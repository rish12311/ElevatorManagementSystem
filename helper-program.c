// TODO: Possibly obfuscate before sharing with students :)

#include "helper-program.h"

struct timeval start, stop;

// Global var with correct answers for current auth strings
char currentAuthStrings[100][ELEVATOR_MAX_CAP + 1];
int turnNumber = 0;

int main(int argc, char* argv[]) {
  srand(time(NULL));

  if (argc < 2) {
    printf(
        "Error: Test case number must be passed as a command line argument.\n");
    exit(1);
  }

  if (argc > 2) {
    printf(
        "Warning: Extra command line arguments passed to helper; these will be "
        "ignored.");
  }

  // Get input params
  int elevatorCount, buildingHeight, solverCount, lastRequestTimestamp,
      passengerRequestCount;

  char testcaseFileName[15];
  sprintf(testcaseFileName, "testcase%s.txt", argv[1]);
  FILE* testcaseFile = fopen(testcaseFileName, "r");
  if (testcaseFile == NULL) {
    perror("Error opening testcase file in helper");
    exit(1);
  }

  fscanf(testcaseFile, "%d", &elevatorCount);
  fscanf(testcaseFile, "%d", &buildingHeight);
  fscanf(testcaseFile, "%d", &solverCount);
  fscanf(testcaseFile, "%d", &lastRequestTimestamp);
  fscanf(testcaseFile, "%d", &passengerRequestCount);

  // Create shared memory segment
  key_t shmKey = rand() % CONSTANT;
  int shmId;
  MainSharedMemory* mainShmPtr;

  shmId = shmget(shmKey, sizeof(MainSharedMemory), PERMS | IPC_CREAT);
  if (shmId == -1) {
    perror("Error in shmget");
    exit(1);
  }

  mainShmPtr = shmat(shmId, NULL, 0);
  if (mainShmPtr == (void*)-1) {
    perror("Error in shmat");
    exit(1);
  }

  // Store and initialize all the passenger requests
  PassengerRequestInfo passengerRequestInfo[passengerRequestCount];
  int startFloor, requestedFloor, arrivalTime;

  for (int i = 0; i < passengerRequestCount; i++) {
    fscanf(testcaseFile, "%d %d %d", &startFloor, &requestedFloor,
           &arrivalTime);
    passengerRequestInfo[i].currentLocation = startFloor;
    passengerRequestInfo[i].granted = 0;
    passengerRequestInfo[i].isInElevator = 0;
    passengerRequestInfo[i].movedThisTurn = 0;
    passengerRequestInfo[i].arrivalTime = arrivalTime;
    passengerRequestInfo[i].request.requestedFloor = requestedFloor;
    passengerRequestInfo[i].request.startFloor = startFloor;
    passengerRequestInfo[i].request.requestId = i;
  }

  // Initialize elevator info
  ElevatorInfo elevatorInfo[elevatorCount];
  for (int i = 0; i < elevatorCount; i++) {
    elevatorInfo[i].currentFloor = 0;
    elevatorInfo[i].passengerCount = 0;
  }

  fclose(testcaseFile);

  // Delete the testcase file before running the student's solution.
  // int deletionProcessId = fork();
  // if (deletionProcessId == -1) {
  //   perror("Error while forking to delete the testcase file");
  //   exit(1);
  // }

  // if (deletionProcessId == 0) {
  //   if (execlp("rm", "rm", testcaseFileName, NULL) == -1) {
  //     perror("Error in execlp while deleting the testcase file");
  //     exit(1);
  //   }
  // }

  // Create the solver processes along with their message queues
  SolverInfo solverInfo[solverCount];
  SolverArguments solverArguments[solverCount];
  for (int i = 0; i < solverCount; i++) {
    solverInfo[i].msgKey = rand() % CONSTANT;

    if ((solverInfo[i].msgId =
             msgget(solverInfo[i].msgKey, PERMS | IPC_CREAT)) == -1) {
      printf("Error in msgget creating message queue for solver %d: ", i);
      perror(NULL);
      exit(1);
    }

    solverArguments[i].solverNumber = i;
    solverArguments[i].messageQueueKey = solverInfo[i].msgKey;

    if (pthread_create(&solverInfo[i].threadId, NULL, solverRoutine,
                       (void*)&solverArguments[i])) {
      printf("Error in pthread_create creating thread for solver %d: ", i);
      perror(NULL);
      exit(1);
    }
  }

  // Create main message queue
  key_t msgKey = rand() % CONSTANT;
  int msgId;

  if ((msgId = msgget(msgKey, PERMS | IPC_CREAT)) == -1) {
    perror("Error in msgget");
    exit(1);
  }

  // Create the input file for the student program
  FILE* inputFile = fopen("input.txt", "w");

  if (inputFile == NULL) {
    perror("Error creating student input file in helper");
    exit(1);
  }

  fprintf(inputFile, "%d\n%d\n%d\n%d\n%d\n%d", elevatorCount, buildingHeight,
          solverCount, lastRequestTimestamp, shmKey, msgKey);

  for (int i = 0; i < solverCount; i++) {
    fprintf(inputFile, "\n%d", solverInfo[i].msgKey);
  }
  fflush(inputFile);
  fclose(inputFile);

  // Get start execution time
  gettimeofday(&start, NULL);

  printf("Testcase %s\n", argv[1]);
  // Run the solution program
  int childId = fork();

  if (childId == -1) {
    perror("Error while forking to run student program");
    exit(1);
  }

  if (childId == 0) {
    if (execlp("./solution", "solution", NULL) == -1) {
      perror("Error in execlp for running student program");
      exit(1);
    }
  }

  // Main Logic
  int requestsRemaining = passengerRequestCount, upcomingRequest = 0,
      errorOccured = 0, totalElevatorMovement = 0;
  TurnChangeRequest turnChangeRequest;
  TurnChangeResponse turnChangeResponse;
  turnChangeResponse.mtype = 2;
  turnChangeResponse.errorOccured = 0;
  turnChangeResponse.finished = 0;

  while (requestsRemaining > 0) {
    // Calculate the new turn's state
    turnNumber++;
    turnChangeResponse.turnNumber = turnNumber;
    turnChangeResponse.newPassengerRequestCount = 0;

    // DEBUG
    // printf("Starting turn number %d\n", turnNumber);

    // Add any new passenger requests
    if (upcomingRequest < passengerRequestCount) {
      while (upcomingRequest < passengerRequestCount &&
             passengerRequestInfo[upcomingRequest].arrivalTime == turnNumber) {
        mainShmPtr->newPassengerRequests[turnChangeResponse
                                             .newPassengerRequestCount] =
            passengerRequestInfo[upcomingRequest].request;

        upcomingRequest++;
        turnChangeResponse.newPassengerRequestCount++;
      }
    }

    // Create authorization strings for each elevator
    for (int i = 0; i < elevatorCount; i++) {
      if (elevatorInfo[i].passengerCount > 0) {
        createNewAuthString(currentAuthStrings[i],
                            elevatorInfo[i].passengerCount);
      }
    }

    // Set the floors of each elevator in shm
    for (int i = 0; i < elevatorCount; i++) {
      mainShmPtr->elevatorFloors[i] = elevatorInfo[i].currentFloor;
    }

    // Send this new state to the student program
    if (msgsnd(msgId, &turnChangeResponse,
               sizeof(turnChangeResponse) - sizeof(turnChangeResponse.mtype),
               0) == -1) {
      printf("Error in msgsnd while sending turn change response for turn %d",
             turnNumber);
      perror(NULL);
      exit(1);
    }

    // Wait for student message to change turns
    if (msgrcv(msgId, &turnChangeRequest,
               sizeof(turnChangeRequest) - sizeof(turnChangeRequest.mtype), 1,
               0) == -1) {
      printf("Error in msgrcv while receiving turn change request for turn %d",
             turnNumber);
      perror(NULL);
      exit(1);
    }

    // Verify all the auth strings
    for (int i = 0; i < elevatorCount; i++) {
      if (elevatorInfo[i].passengerCount > 0 &&
          mainShmPtr->elevatorMovementInstructions[i] != 's') {
        if (strcmp(mainShmPtr->authStrings[i], currentAuthStrings[i]) != 0) {
          printf(
              "Turn %d: Expected authorization string %s for elevator %d, "
              "received %s "
              "instead\n",
              turnNumber, currentAuthStrings[i], i, mainShmPtr->authStrings[i]);
          errorOccured = 1;
          break;
        }
      }
    }

    if (errorOccured) break;

    // Reset passenger movement
    for (int i = 0; i < upcomingRequest; i++) {
      passengerRequestInfo[i].movedThisTurn = 0;
    }

    // Drop passengers
    for (int i = 0; i < turnChangeRequest.droppedPassengersCount; i++) {
      int requestId = mainShmPtr->droppedPassengers[i];

      // Check for errors
      if (passengerRequestInfo[requestId].granted) {
        printf(
            "Turn %d: Attempted to move passenger %d even though their request "
            "has already been fulfilled\n",
            turnNumber, requestId);
        errorOccured = 1;
        break;
      }

      if (passengerRequestInfo[requestId].arrivalTime > turnNumber) {
        printf(
            "Turn %d: Attempted to move passenger %d even though their request "
            "has not arrived yet\n",
            turnNumber, requestId);
        errorOccured = 1;
        break;
      }

      if (!passengerRequestInfo[requestId].isInElevator) {
        printf(
            "Turn %d: Attempted to drop passenger %d even though they aren't "
            "in any elevator\n",
            turnNumber, requestId);
        errorOccured = 1;
        break;
      }

      if (passengerRequestInfo[requestId].movedThisTurn) {
        printf(
            "Turn %d: Attempted to move passenger %d even though they have "
            "already moved this turn\n",
            turnNumber, requestId);
        errorOccured = 1;
        break;
      }

      int elevatorNumber = passengerRequestInfo[requestId].currentLocation;

      // Update state as requested
      passengerRequestInfo[requestId].isInElevator = 0;
      passengerRequestInfo[requestId].currentLocation =
          elevatorInfo[elevatorNumber].currentFloor;
      passengerRequestInfo[requestId].movedThisTurn = 1;

      // Check if the request has been fulfilled
      if (passengerRequestInfo[requestId].request.requestedFloor ==
          passengerRequestInfo[requestId].currentLocation) {
        passengerRequestInfo[requestId].granted = 1;
        requestsRemaining--;
      }

      // Remove passenger from elevator's list
      int found = 0;
      for (int j = 0; j < elevatorInfo[elevatorNumber].passengerCount - 1;
           j++) {
        if (elevatorInfo[elevatorNumber].passengers[j] == requestId) {
          found = 1;
        }

        if (found) {
          elevatorInfo[elevatorNumber].passengers[j] =
              elevatorInfo[elevatorNumber].passengers[j + 1];
        }
      }

      elevatorInfo[elevatorNumber].passengerCount--;
    }

    if (errorOccured) break;

    // Pick up passengers
    for (int i = 0; i < turnChangeRequest.pickedUpPassengersCount; i++) {
      int requestId = mainShmPtr->pickedUpPassengers[i][0];
      int elevatorNumber = mainShmPtr->pickedUpPassengers[i][1];

      // Check for errors
      if (elevatorNumber >= elevatorCount) {
        printf(
            "Turn %d: Attempted to move passenger %d to elevator %d, which "
            "does not exist (There are %d elevators, numbered from 0 to %d)",
            turnNumber, requestId, elevatorNumber, elevatorCount,
            elevatorCount - 1);
        errorOccured = 1;
        break;
      }

      if (passengerRequestInfo[requestId].granted) {
        printf(
            "Turn %d: Attempted to move passenger %d even though their request "
            "has already been fulfilled\n",
            turnNumber, requestId);
        errorOccured = 1;
        break;
      }

      if (passengerRequestInfo[requestId].arrivalTime > turnNumber) {
        printf(
            "Turn %d: Attempted to move passenger %d even though their request "
            "has not arrived yet\n",
            turnNumber, requestId);
        errorOccured = 1;
        break;
      }

      if (passengerRequestInfo[requestId].isInElevator) {
        printf(
            "Turn %d: Attempted to move passenger %d to an elevator even "
            "though they are already in an elevator\n",
            turnNumber, requestId);
        errorOccured = 1;
        break;
      }

      if (passengerRequestInfo[requestId].movedThisTurn) {
        printf(
            "Turn %d: Attempted to move passenger %d even though they have "
            "already moved this turn\n",
            turnNumber, requestId);
        errorOccured = 1;
        break;
      }

      if (passengerRequestInfo[requestId].currentLocation !=
          elevatorInfo[elevatorNumber].currentFloor) {
        printf(
            "Turn %d: Can't move passenger %d to elevator %d, as the passenger "
            "is on floor %d while the elevator is on floor %d\n",
            turnNumber, requestId, elevatorNumber,
            passengerRequestInfo[requestId].currentLocation,
            elevatorInfo[elevatorNumber].currentFloor);
        errorOccured = 1;
        break;
      }

      if (elevatorInfo[elevatorNumber].passengerCount >= ELEVATOR_MAX_CAP) {
        printf(
            "Turn %d: Can't move passenger %d to elevator %d, as the elevator "
            "is already full\n",
            turnNumber, requestId, elevatorNumber);
        errorOccured = 1;
        break;
      }

      // Update state as requested
      passengerRequestInfo[requestId].isInElevator = 1;
      passengerRequestInfo[requestId].currentLocation = elevatorNumber;
      passengerRequestInfo[requestId].movedThisTurn = 1;
      elevatorInfo[elevatorNumber]
          .passengers[elevatorInfo[elevatorNumber].passengerCount] = requestId;
      elevatorInfo[elevatorNumber].passengerCount++;
    }

    if (errorOccured) break;

    // Move elevators as requested
    for (int i = 0; i < elevatorCount; i++) {
      switch (mainShmPtr->elevatorMovementInstructions[i]) {
        case 'u':
          if (elevatorInfo[i].currentFloor >= buildingHeight - 1) {
            printf(
                "Turn %d: Attempted to move elevator %d up even though it is "
                "already on the highest floor\n",
                turnNumber, i);
            errorOccured = 1;
          } else {
            elevatorInfo[i].currentFloor++;
            totalElevatorMovement++;
          }
          break;

        case 'd':
          if (elevatorInfo[i].currentFloor <= 0) {
            printf(
                "Turn %d: Attempted to move elevator %d down even though it is "
                "already on the lowest floor\n",
                turnNumber, i);
            errorOccured = 1;
          } else {
            elevatorInfo[i].currentFloor--;
            totalElevatorMovement++;
          }
          break;

        case 's':
          break;

        default:
          printf("Turn %d: Elevator %d submitted unknown movement command %c\n",
                 turnNumber, i, mainShmPtr->elevatorMovementInstructions[i]);
          errorOccured = 1;
          break;
      }

      if (errorOccured) break;
    }

    if (errorOccured) break;
  }

  if (errorOccured) {
    turnChangeResponse.errorOccured = 1;
    if (msgsnd(msgId, &turnChangeResponse,
               sizeof(turnChangeResponse) - sizeof(turnChangeResponse.mtype),
               0) == -1) {
      printf(
          "Error in msgsnd while sending turn change response due to error for "
          "turn %d",
          turnNumber);
      perror(NULL);
      exit(1);
    }
  }

  // Send finish confirmation message
  turnChangeResponse.finished = 1;
  if (msgsnd(msgId, &turnChangeResponse,
             sizeof(turnChangeResponse) - sizeof(turnChangeResponse.mtype),
             0) == -1) {
    printf(
        "Error in msgsnd while sending turn change response for finishing for "
        "turn %d",
        turnNumber);
    perror(NULL);
    exit(1);
  }

  // Wait for the student program
  wait(NULL);

  gettimeofday(&stop, NULL);
  double result =
      ((stop.tv_sec - start.tv_sec)) + ((stop.tv_usec - start.tv_usec) / 1e6);

  // Print their result
  printf(
      "Your solution took %lf seconds to execute. This time may vary with "
      "server load, and won't be used for final evaluation.\n",
      result);
  if (errorOccured) {
    printf(
        "Your solution took %d turns, with a total elevator movement of %d, "
        "but failed to complete the test case. This number does "
        "not vary with server load.\n",
        turnNumber, totalElevatorMovement);
  } else {
    printf(
        "Your solution took %d turns, with a total elevator movement of %d, to "
        "successfully complete the test case. This number does "
        "not vary with server load.\n",
        turnNumber, totalElevatorMovement);
  }

  // Terminate main message queue
  if (msgctl(msgId, IPC_RMID, NULL) == -1) {
    perror("Error in msgctl for main message queue");
    exit(1);
  }

  // Ask solvers to exit
  SolverRequest solverRequest;
  solverRequest.mtype = 1;
  for (int i = 0; i < solverCount; i++) {
    if (msgsnd(solverInfo[i].msgId, &solverRequest,
               sizeof(solverRequest) - sizeof(solverRequest.mtype), 0) == -1) {
      printf("Error in msgsnd while sending termination request to solver %d\n",
             i);
      perror(NULL);
      exit(1);
    }
  }

  // Wait for solvers to exit
  for (int i = 0; i < solverCount; i++) {
    if (pthread_join(solverInfo[i].threadId, NULL)) {
      printf("Error in pthread_join for solver %d\n", i);
      perror(NULL);
      exit(1);
    }
  }

  // Terminate solver message queues
  for (int i = 0; i < solverCount; i++) {
    if (msgctl(solverInfo[i].msgId, IPC_RMID, NULL) == -1) {
      printf("Error in msgctl for solver %d's message queue", i);
      perror(NULL);
      exit(1);
    }
  }

  // Terminate main shared memory
  if (shmdt(mainShmPtr) == -1) {
    perror("Error in shmdt");
    exit(1);
  }

  if (shmctl(shmId, IPC_RMID, 0) == -1) {
    perror("Error in shmctl");
    return 1;
  }

  return 0;
}

// The routine run by all the solver programs
void* solverRoutine(void* args) {
  SolverArguments arguments = *(SolverArguments*)args;
  int targetElevator = 0;
  SolverRequest request;
  SolverResponse response;

  response.mtype = 4;
  response.guessIsCorrect = 0;

  int messageQueueId;
  if ((messageQueueId = msgget(arguments.messageQueueKey, PERMS)) == -1) {
    printf("Turn %d: Error in msgget for solver %d: ", turnNumber,
           arguments.solverNumber);
    perror(NULL);
    exit(1);
  }

  while (1) {
    if (msgrcv(messageQueueId, &request,
               sizeof(request) - sizeof(request.mtype), -3, 0) == -1) {
      printf("Turn %d: Error in msgrcv for solver %d: ", turnNumber,
             arguments.solverNumber);
      perror(NULL);
      exit(1);
    }

    switch (request.mtype) {
      case 1:
        pthread_exit(NULL);
        break;

      case 2:
        targetElevator = request.elevatorNumber;
        break;

      case 3:
        response.guessIsCorrect = 0;
        if (strcmp(currentAuthStrings[targetElevator],
                   request.authStringGuess) == 0) {
          response.guessIsCorrect = 1;
        }

        if (msgsnd(messageQueueId, &response,
                   sizeof(response) - sizeof(response.mtype), 0) == -1) {
          printf("Turn %d: Error in msgsnd for solver %d: ", turnNumber,
                 arguments.solverNumber);
          perror(NULL);
          exit(1);
        }
    }
  }
}

// Function to create auth strings
void createNewAuthString(char* authStringLocation, int length) {
  for (int i = 0; i < length; i++) {
    int offset = rand() % AUTH_STRING_UNIQUE_LETTERS;
    authStringLocation[i] = 'a' + offset;
  }
  authStringLocation[length] = '\0';
}
