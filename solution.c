#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <unistd.h>

#define ELEVATOR_MAX_CAP 20
#define MAX_NEW_REQUESTS 30
#define MAX_ELEVATORS 100
#define MAX_FLOORS 500
#define MAX_QUEUE_SIZE 100000

typedef struct {
    int requestID;
    int startFloor;
    int requestedFloor;
} PassengerRequest;

typedef struct MainSharedMemory {
    char authStrings[100][ELEVATOR_MAX_CAP + 1];
    char elevatorMovementInstructions[100];
    PassengerRequest newPassengerRequests[MAX_NEW_REQUESTS];
    int elevatorFloors[100];
    int droppedPassengers[1000];
    int pickedUpPassengers[1000][2];
} MainSharedMemory;

typedef struct SolverRequest {
    long mtype;
    int elevatorNumber;
    char authStringGuess[ELEVATOR_MAX_CAP + 1];
} SolverRequest;

typedef struct SolverResponse {
    long mtype;
    int guessIsCorrect;
} SolverResponse;

typedef struct TurnChangeResponse {
    long mtype;
    int turnNumber;
    int newPassengerRequestCount;
    int errorOccured;
    int finished;
} TurnChangeResponse;

typedef struct TurnChangeRequest {
    long mtype;
    int droppedPassengersCount;
    int pickedUpPassengersCount;
} TurnChangeRequest;

enum Movement {
    MOVING_UP,
    MOVING_DOWN,
    STAY
};

typedef struct {
    int current_floor;
    enum Movement movement;
    int destination[3];
    int is_occupied;
    int passenger_ids[3];
    int pickup_floors[3];
    bool going_to_pickup[3];
    int num_passengers; 
} Elevator;

typedef struct {
    Elevator elevators[MAX_ELEVATORS];
    int num_elevators;
    int num_floors;
} ElevatorSystem;

typedef struct {
    PassengerRequest queue[MAX_QUEUE_SIZE];
    int front;
    int rear;
    int size;
} RequestQueue;

struct MainSharedMemory *shm;
key_t mainQueueId;
int N, K, M, T;
long long int shmkey, messkey;
int droppedPassengersCount = 0;
int pickedUpPassengersCount = 0;
RequestQueue requestQueue;

void initializeQueue() {
    requestQueue.front = 0;
    requestQueue.rear = -1;
    requestQueue.size = 0;
}

bool enqueueRequest(PassengerRequest request) {
    if (requestQueue.size >= MAX_QUEUE_SIZE) {
        return false;
    }
    requestQueue.rear = (requestQueue.rear + 1) % MAX_QUEUE_SIZE;
    requestQueue.queue[requestQueue.rear] = request;
    requestQueue.size++;
    return true;
}

bool dequeueRequest(PassengerRequest *request) {
    if (requestQueue.size == 0) {
        return false;
    }
    *request = requestQueue.queue[requestQueue.front];
    requestQueue.front = (requestQueue.front + 1) % MAX_QUEUE_SIZE;
    requestQueue.size--;
    return true;
}

int getAuthorization(int* solverQueueIds, int elevatorNumber, int numPeople) {
    char guess[ELEVATOR_MAX_CAP + 1] = {0};

    if (numPeople == 1) {
        guess[0] = 'a';
        guess[1] = '\0';
    } else if (numPeople == 2) {
        guess[0] = 'a';
        guess[1] = 'a';
        guess[2] = '\0';
    } else if (numPeople == 3) {
        guess[0] = 'a';
        guess[1] = 'a';
        guess[2] = 'a';
        guess[3] = '\0';
    }

    SolverRequest sreq;
    sreq.mtype = 2;
    sreq.elevatorNumber = elevatorNumber;

    if (msgsnd(solverQueueIds[0], &sreq, sizeof(SolverRequest) - sizeof(long), 0) == -1) {
        perror("msgsnd failed for solver request");
        exit(-8);
    }
    sreq.mtype = 3;
    SolverResponse sres;

    while (1) {
        strcpy(sreq.authStringGuess, guess);

        if (msgsnd(solverQueueIds[0], &sreq, sizeof(SolverRequest) - sizeof(long), 0) == -1) {
            perror("msgsnd failed for solver request retry");
            exit(-8);
        }

        if (msgrcv(solverQueueIds[0], &sres, sizeof(SolverResponse) - sizeof(long), 4, 0) == -1) {
            perror("msgrcv failed for solver response");
            exit(-9);
        }

        if (sres.guessIsCorrect) {
            strcpy(shm->authStrings[elevatorNumber], guess);
            return 1;
        }

        if (numPeople == 1) {
            guess[0]++;
            if (guess[0] > 'f') return 0;
        } else if (numPeople == 2) {
            guess[1]++;
            if (guess[1] > 'f') {
                guess[1] = 'a';
                guess[0]++;
                if (guess[0] > 'f') return 0;
            }
        } else if (numPeople == 3) {
            guess[2]++;
            if (guess[2] > 'f') {
                guess[2] = 'a';
                guess[1]++;
                if (guess[1] > 'f') {
                    guess[1] = 'a';
                    guess[0]++;
                    if (guess[0] > 'f') return 0;
                }
            }
        }
    }
}

void initialize_elevator_system(ElevatorSystem *system, int num_elevators, int num_floors) {
    system->num_elevators = num_elevators;
    system->num_floors = num_floors;
    for (int i = 0; i < num_elevators; i++) {
        system->elevators[i].current_floor = 0;
        system->elevators[i].movement = STAY;
        system->elevators[i].is_occupied = 0;
        memset(system->elevators[i].destination, -1, sizeof(system->elevators[i].destination));
        memset(system->elevators[i].passenger_ids, -1, sizeof(system->elevators[i].passenger_ids));
        memset(system->elevators[i].pickup_floors, -1, sizeof(system->elevators[i].pickup_floors));
        memset(system->elevators[i].going_to_pickup, 0, sizeof(system->elevators[i].going_to_pickup));
        system->elevators[i].num_passengers = 0;
        shm->elevatorFloors[i] = 0;
    }
    initializeQueue();
}



int find_best_elevator(ElevatorSystem *system, int start_floor) {
    int best_elevator = -1;
    int min_distance = INT_MAX;
    
    for (int i = 0; i < system->num_elevators; i++) {
        if (!system->elevators[i].is_occupied) {
            int distance = abs(system->elevators[i].current_floor - start_floor);
            if (distance < min_distance) {
                min_distance = distance;
                best_elevator = i;
            }
        }
    }
    return best_elevator;
}

void process_request(ElevatorSystem *system, PassengerRequest request) {
    int best_elevator = find_best_elevator(system, request.startFloor);

    if (best_elevator != -1) {
        Elevator *elevator = &system->elevators[best_elevator];
        int idx = elevator->num_passengers;
        elevator->is_occupied = 1;
        elevator->passenger_ids[idx] = request.requestID;
        elevator->pickup_floors[idx] = request.startFloor;
        elevator->destination[idx] = request.requestedFloor;
        elevator->going_to_pickup[idx] = true;
        elevator->num_passengers++;

        
    } else {
        enqueueRequest(request);
        
    }
}

void process_queue(ElevatorSystem *system) {
    if (requestQueue.size > 0) {
        PassengerRequest request;
        while (dequeueRequest(&request)) {
            int elevator_id = find_best_elevator(system, request.startFloor);
            if (elevator_id != -1) {
                Elevator *elevator = &system->elevators[elevator_id];
                elevator->is_occupied = 1;
                elevator->passenger_ids[elevator->num_passengers] = request.requestID;
                elevator->pickup_floors[elevator->num_passengers] = request.startFloor;
                elevator->destination[elevator->num_passengers] = request.requestedFloor;
                elevator->going_to_pickup[elevator->num_passengers] = true;
                elevator->num_passengers++;
            } else {
                enqueueRequest(request);
                break;
            }
        }
    }
}

void move_elevators(ElevatorSystem *system, int* solverQueueIds) {
    for (int i = 0; i < system->num_elevators; i++) {
        Elevator *elevator = &system->elevators[i];
        
        if (elevator->is_occupied) {
            if (!elevator->going_to_pickup[0] && !elevator->going_to_pickup[1])
                getAuthorization(solverQueueIds, i, elevator->num_passengers);
            
            for (int p = 0; p < elevator->num_passengers; p++) {
                if (elevator->going_to_pickup[p]) {
                    if (elevator->current_floor < elevator->pickup_floors[p]) {
                        elevator->movement = MOVING_UP;
                        shm->elevatorMovementInstructions[i] = 'u';
                        elevator->current_floor++;
                    } else if (elevator->current_floor > elevator->pickup_floors[p]) {
                        elevator->movement = MOVING_DOWN;
                        shm->elevatorMovementInstructions[i] = 'd';
                        elevator->current_floor--;
                    } else {
                        elevator->going_to_pickup[p] = false;
                        shm->pickedUpPassengers[pickedUpPassengersCount][0] = elevator->passenger_ids[p];
                        shm->pickedUpPassengers[pickedUpPassengersCount][1] = i;
                        pickedUpPassengersCount++;
                    }
                    shm->elevatorFloors[i] = elevator->current_floor;
                    break;
                }
            }

            if (!elevator->going_to_pickup[0] && !elevator->going_to_pickup[1]) {
                bool moved = false;
                for (int p = 0; p < elevator->num_passengers; p++) {
                    if (elevator->current_floor != elevator->destination[p]) {
                        if (elevator->current_floor < elevator->destination[p]) {
                            elevator->movement = MOVING_UP;
                            shm->elevatorMovementInstructions[i] = 'u';
                            elevator->current_floor++;
                        } else {
                            elevator->movement = MOVING_DOWN;
                            shm->elevatorMovementInstructions[i] = 'd';
                            elevator->current_floor--;
                        }
                        moved = true;
                        break;
                    }
                }

                if (!moved) {
                    for (int p = elevator->num_passengers - 1; p >= 0; p--) {
                        if (elevator->current_floor == elevator->destination[p]) {
                            shm->droppedPassengers[droppedPassengersCount++] = elevator->passenger_ids[p];
                            for (int j = p; j < elevator->num_passengers - 1; j++) {
                                elevator->passenger_ids[j] = elevator->passenger_ids[j + 1];
                                elevator->destination[j] = elevator->destination[j + 1];
                                elevator->going_to_pickup[j] = elevator->going_to_pickup[j + 1];
                            }
                            elevator->num_passengers--;
                        }
                    }

                    if (elevator->num_passengers == 0) {
                        elevator->is_occupied = 0;
                        elevator->movement = STAY;
                        shm->elevatorMovementInstructions[i] = 's';
                        process_queue(system);
                    }
                }
                shm->elevatorFloors[i] = elevator->current_floor;
            }
        } else {
            elevator->movement = STAY;
            shm->elevatorMovementInstructions[i] = 's';
            shm->elevatorFloors[i] = elevator->current_floor;
        }
    }
}

void mainSharedattach(int shmkey) {
    int shmid = shmget(shmkey, sizeof(struct MainSharedMemory), 0666);
    if (shmid == -1) {
        perror("shmget failed");
        exit(-2);
    }
    shm = (struct MainSharedMemory*)shmat(shmid, NULL, 0);
    if (shm == (void*)-1) {
        perror("shmat failed");
        exit(-3);
    }
}

void mainMessageQueueattach(int messkey) {
    mainQueueId = msgget(messkey, 0666);
    if (mainQueueId == -1) {
        perror("msgget failed for main queue");
        exit(-4);
    }
}

int main(int argc, char *argv[]) {
    FILE *ip_file = fopen("input.txt", "r");
    if (!ip_file) {
        perror("Error opening input file");
        exit(-1);
    }

    fscanf(ip_file, "%d %d %d %d %lld %lld", &N, &K, &M, &T, &shmkey, &messkey);
    
    long long int *queue_key = malloc(M * sizeof(long long int));
    for (int i = 0; i < M; i++) {
        fscanf(ip_file, "%lld", &queue_key[i]);
    }

    fclose(ip_file);

    mainSharedattach(shmkey);
    mainMessageQueueattach(messkey);

    int* solverQueueIds = malloc(M * sizeof(int));
    for (int i = 0; i < M; i++) {
        solverQueueIds[i] = msgget(queue_key[i], 0666);
        if (solverQueueIds[i] == -1) {
            perror("msgget failed for solver queue");
            exit(-5);
        }
    }

    ElevatorSystem system;
    initialize_elevator_system(&system, N, K);
    
    while (1) {
        TurnChangeResponse tresponse;
        if (msgrcv(mainQueueId, &tresponse, sizeof(TurnChangeResponse) - sizeof(long), 2, 0) == -1) {
            perror("msgrcv failed");
            exit(-6);
        }

        if (tresponse.finished) {
            break;
        }
        
        if (tresponse.errorOccured) {
            break;
        }
        
        pickedUpPassengersCount=0;
        droppedPassengersCount=0;
        
        for (int i = 0; i < tresponse.newPassengerRequestCount; i++) {
            PassengerRequest request = shm->newPassengerRequests[i];
            process_request(&system, request);
        }

        move_elevators(&system, solverQueueIds);

        TurnChangeRequest tcrequest;
        tcrequest.mtype = 1;
        tcrequest.droppedPassengersCount = droppedPassengersCount;
        tcrequest.pickedUpPassengersCount = pickedUpPassengersCount;
        
        if (msgsnd(mainQueueId, &tcrequest, sizeof(TurnChangeRequest) - sizeof(long), 0) == -1) {
            perror("msgsnd failed for turn change request");
            exit(-10);
        }
    }

    free(queue_key);
    free(solverQueueIds);
    shmdt(shm);
    return 0;
}