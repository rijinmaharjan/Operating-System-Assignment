/*
 * ============================================================================
 *  Task1.c
 *
 *  Scenario: Airport Check-In & Baggage Handling System
 *  -----------------------------------------------------
 *  PART 0 - Process Creation   : fork() spawns a separate "Boarding Pass
 *                                 Printer" OS process; the main check-in
 *                                 process waits for it to finish printing.
 *  PART A - Race Condition     : multiple check-in agent threads update a
 *                                 shared "seats remaining" counter, first
 *                                 unsynchronized (seats get double-booked),
 *                                 then fixed with a mutex.
 *  PART B - Synchronization    : baggage handlers (producers) load bags onto
 *                                 a shared conveyor belt (bounded buffer),
 *                                 baggage loaders (consumers) take them off,
 *                                 coordinated with POSIX semaphores + mutex.
 *  PART C - Scheduling         : Round-Robin simulation of a gate agent
 *                                 giving each flight a fixed time slice of
 *                                 boarding-announcement time.
 *  PART D - Deadlock Prevention: two staff threads each need the check-in
 *                                 counter lock AND the gate lock; deadlock
 *                                 is prevented by always acquiring the
 *                                 locks in the same global order.
 *
 *  Compile : gcc Task1.c -o Task1
 *  Run     : ./Task1
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>

static void msleep(int ms) {
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/* ===========================================================================
 * PART 0 : PROCESS CREATION (fork) - Boarding Pass Printer
 * ------------------------------------------------------------------------ */
void run_part0_process_creation(void) {
    printf("================ PART 0: PROCESS CREATION (fork) ================\n");
    printf("[Check-in System] Main process PID = %d\n", getpid());
    fflush(stdout);

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork failed");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        /* CHILD: simulates a separate boarding-pass printer process */
        printf("  [Printer Process] Started, PID = %d, Parent PID = %d\n",
               getpid(), getppid());
        printf("  [Printer Process] Printing boarding pass...\n");
        msleep(150);
        printf("  [Printer Process] Boarding pass printed. Exiting.\n");
        exit(EXIT_SUCCESS);
    } else {
        /* PARENT: waits for the printer process to finish */
        int status;
        pid_t child = wait(&status);
        if (WIFEXITED(status)) {
            printf("[Check-in System] Printer process %d finished (exit code %d)\n",
                   child, WEXITSTATUS(status));
        }
        printf("[Check-in System] Handing boarding pass to passenger.\n");
    }
}

/* ===========================================================================
 * PART A : RACE CONDITION - Seats Remaining Counter
 * ------------------------------------------------------------------------ */
#define NUM_AGENTS       3
#define BOOKINGS_PER_AGENT 2000

int seats_unsafe = 6000;   /* deliberately large so it never goes negative */
int seats_safe   = 6000;
pthread_mutex_t seats_mutex = PTHREAD_MUTEX_INITIALIZER;

void *agent_unsafe(void *arg) {
    (void)arg;
    for (int i = 0; i < BOOKINGS_PER_AGENT; i++) {
        int temp = seats_unsafe;   /* READ  */
        sched_yield();             /* force interleaving so the race shows */
        temp = temp - 1;           /* MODIFY */
        seats_unsafe = temp;       /* WRITE - another agent may have read
                                       the same "old" seat count in between */
    }
    return NULL;
}

void *agent_safe(void *arg) {
    (void)arg;
    for (int i = 0; i < BOOKINGS_PER_AGENT; i++) {
        pthread_mutex_lock(&seats_mutex);
        seats_safe--;              /* critical section */
        pthread_mutex_unlock(&seats_mutex);
    }
    return NULL;
}

void run_partA_race_condition(void) {
    printf("\n============ PART A: RACE CONDITION (SEAT BOOKINGS) ============\n");
    pthread_t agents[NUM_AGENTS];
    int expected_bookings = NUM_AGENTS * BOOKINGS_PER_AGENT;
    int expected_seats = 6000 - expected_bookings;

    for (int i = 0; i < NUM_AGENTS; i++)
        pthread_create(&agents[i], NULL, agent_unsafe, NULL);
    for (int i = 0; i < NUM_AGENTS; i++)
        pthread_join(agents[i], NULL);

    printf("[Unsynchronized] Expected seats left = %d | Actual = %d  %s\n",
           expected_seats, seats_unsafe,
           (seats_unsafe != expected_seats) ? "<-- RACE CONDITION (double-booked/lost updates)" : "(no corruption this run)");

    for (int i = 0; i < NUM_AGENTS; i++)
        pthread_create(&agents[i], NULL, agent_safe, NULL);
    for (int i = 0; i < NUM_AGENTS; i++)
        pthread_join(agents[i], NULL);

    printf("[Synchronized]   Expected seats left = %d | Actual = %d  %s\n",
           expected_seats, seats_safe,
           (seats_safe == expected_seats) ? "<-- CORRECT (mutex prevented the race)" : "<-- UNEXPECTED ERROR");
}

/* ===========================================================================
 * PART B : PRODUCER-CONSUMER - Baggage Conveyor Belt
 * ------------------------------------------------------------------------ */
#define BELT_SIZE        5
#define BAGS_TO_HANDLE   10

int belt[BELT_SIZE];
int belt_in = 0, belt_out = 0;

sem_t sem_free_slots;
sem_t sem_bags_ready;
pthread_mutex_t belt_mutex = PTHREAD_MUTEX_INITIALIZER;

int bags_claimed = 0;
pthread_mutex_t claim_mutex = PTHREAD_MUTEX_INITIALIZER;

void *baggage_handler(void *arg) {
    (void)arg;
    for (int i = 1; i <= BAGS_TO_HANDLE; i++) {
        sem_wait(&sem_free_slots);
        pthread_mutex_lock(&belt_mutex);

        belt[belt_in] = i;
        printf("  [Handler]   loaded bag %-2d -> belt slot %d\n", i, belt_in);
        belt_in = (belt_in + 1) % BELT_SIZE;

        pthread_mutex_unlock(&belt_mutex);
        sem_post(&sem_bags_ready);
        msleep(20);
    }
    return NULL;
}

void *baggage_loader(void *arg) {
    int id = *(int *)arg;
    while (1) {
        pthread_mutex_lock(&claim_mutex);
        if (bags_claimed >= BAGS_TO_HANDLE) {
            pthread_mutex_unlock(&claim_mutex);
            break;
        }
        bags_claimed++;
        pthread_mutex_unlock(&claim_mutex);

        sem_wait(&sem_bags_ready);
        pthread_mutex_lock(&belt_mutex);

        int bag = belt[belt_out];
        printf("    [Loader-%d] removed bag %-2d <- belt slot %d\n", id, bag, belt_out);
        belt_out = (belt_out + 1) % BELT_SIZE;

        pthread_mutex_unlock(&belt_mutex);
        sem_post(&sem_free_slots);
        msleep(35);
    }
    return NULL;
}

void run_partB_producer_consumer(void) {
    printf("\n========= PART B: BAGGAGE CONVEYOR (PRODUCER-CONSUMER) =========\n");

    sem_init(&sem_free_slots, 0, BELT_SIZE);
    sem_init(&sem_bags_ready, 0, 0);

    pthread_t handler, loader1, loader2;
    int id1 = 1, id2 = 2;

    pthread_create(&handler, NULL, baggage_handler, NULL);
    pthread_create(&loader1, NULL, baggage_loader, &id1);
    pthread_create(&loader2, NULL, baggage_loader, &id2);

    pthread_join(handler, NULL);
    pthread_join(loader1, NULL);
    pthread_join(loader2, NULL);

    sem_destroy(&sem_free_slots);
    sem_destroy(&sem_bags_ready);

    printf("[Result] All %d bags loaded and claimed with no loss or overwrite.\n", BAGS_TO_HANDLE);
}

/* ===========================================================================
 * PART C : ROUND-ROBIN SCHEDULING - Gate Boarding Announcements
 * ------------------------------------------------------------------------ */
typedef struct {
    int flight_no;
    int arrival_time;
    int announce_time;
    int remaining_time;
    int completion_time;
    int waiting_time;
    int turnaround_time;
} Flight;

#define NUM_FLIGHTS 5
#define QUANTUM     3

void run_partC_round_robin(void) {
    printf("\n=========== PART C: ROUND-ROBIN GATE ANNOUNCEMENT SCHEDULER ===========\n");

    Flight flights[NUM_FLIGHTS] = {
        {101, 0, 10, 10, 0, 0, 0},
        {102, 1, 5,  5,  0, 0, 0},
        {103, 2, 8,  8,  0, 0, 0},
        {104, 3, 4,  4,  0, 0, 0},
        {105, 4, 6,  6,  0, 0, 0},
    };

    printf("Quantum = %d minutes\n\n", QUANTUM);
    printf(" Flight | Arrival | Announce Time\n");
    printf("--------+---------+--------------\n");
    for (int i = 0; i < NUM_FLIGHTS; i++)
        printf("  %-5d |   %-5d |     %-5d\n",
               flights[i].flight_no, flights[i].arrival_time, flights[i].announce_time);

    int queue[100], front = 0, rear = 0, qcount = 0;
    int time = 0, completed = 0;
    int in_queue[NUM_FLIGHTS];
    memset(in_queue, 0, sizeof(in_queue));

    char gantt[2000] = {0};
    char segment[32];

    #define ENQUEUE(x) do { queue[rear] = (x); rear = (rear + 1) % 100; qcount++; } while (0)
    #define DEQUEUE(x) do { (x) = queue[front]; front = (front + 1) % 100; qcount--; } while (0)

    for (int i = 0; i < NUM_FLIGHTS; i++) {
        if (flights[i].arrival_time <= time && !in_queue[i]) {
            ENQUEUE(i);
            in_queue[i] = 1;
        }
    }

    while (completed < NUM_FLIGHTS) {
        if (qcount == 0) {
            time++;
            for (int i = 0; i < NUM_FLIGHTS; i++) {
                if (flights[i].arrival_time <= time && !in_queue[i] && flights[i].remaining_time > 0) {
                    ENQUEUE(i);
                    in_queue[i] = 1;
                }
            }
            continue;
        }

        int idx;
        DEQUEUE(idx);

        int slice = (flights[idx].remaining_time < QUANTUM) ? flights[idx].remaining_time : QUANTUM;
        int start_time = time;
        time += slice;
        flights[idx].remaining_time -= slice;

        snprintf(segment, sizeof(segment), "F%d[%d-%d] ", flights[idx].flight_no, start_time, time);
        strncat(gantt, segment, sizeof(gantt) - strlen(gantt) - 1);

        for (int i = 0; i < NUM_FLIGHTS; i++) {
            if (flights[i].arrival_time <= time && !in_queue[i] && flights[i].remaining_time > 0) {
                ENQUEUE(i);
                in_queue[i] = 1;
            }
        }

        if (flights[idx].remaining_time > 0) {
            ENQUEUE(idx);
        } else {
            flights[idx].completion_time = time;
            flights[idx].turnaround_time = flights[idx].completion_time - flights[idx].arrival_time;
            flights[idx].waiting_time = flights[idx].turnaround_time - flights[idx].announce_time;
            completed++;
            in_queue[idx] = 0;
        }
    }

    printf("\nGantt Chart:\n%s\n", gantt);

    printf("\n Flight | Completion | Turnaround | Waiting\n");
    printf("--------+------------+------------+--------\n");
    double total_tat = 0, total_wt = 0;
    for (int i = 0; i < NUM_FLIGHTS; i++) {
        printf("  %-5d |     %-6d |     %-6d |  %-4d\n",
               flights[i].flight_no, flights[i].completion_time,
               flights[i].turnaround_time, flights[i].waiting_time);
        total_tat += flights[i].turnaround_time;
        total_wt  += flights[i].waiting_time;
    }
    printf("\nAverage Turnaround Time = %.2f min\n", total_tat / NUM_FLIGHTS);
    printf("Average Waiting Time    = %.2f min\n", total_wt  / NUM_FLIGHTS);
}

/* ===========================================================================
 * PART D : DEADLOCK PREVENTION - Counter Lock + Gate Lock
 * ------------------------------------------------------------------------ */
pthread_mutex_t counter_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gate_lock    = PTHREAD_MUTEX_INITIALIZER;

void *staff_task1(void *arg) {
    (void)arg;
    printf("  [Staff-1] wants counter_lock then gate_lock\n");
    pthread_mutex_lock(&counter_lock);
    printf("  [Staff-1] acquired counter_lock\n");
    msleep(50);

    pthread_mutex_lock(&gate_lock);   /* consistent order: counter then gate */
    printf("  [Staff-1] acquired gate_lock -> passenger processed\n");

    pthread_mutex_unlock(&gate_lock);
    pthread_mutex_unlock(&counter_lock);
    printf("  [Staff-1] released both locks\n");
    return NULL;
}

void *staff_task2(void *arg) {
    (void)arg;
    printf("  [Staff-2] wants counter_lock then gate_lock (same order, prevents deadlock)\n");
    msleep(10);

    pthread_mutex_lock(&counter_lock);   /* SAME order as Staff-1: never gate-then-counter */
    printf("  [Staff-2] acquired counter_lock\n");
    msleep(50);

    pthread_mutex_lock(&gate_lock);
    printf("  [Staff-2] acquired gate_lock -> passenger processed\n");

    pthread_mutex_unlock(&gate_lock);
    pthread_mutex_unlock(&counter_lock);
    printf("  [Staff-2] released both locks\n");
    return NULL;
}

void run_partD_deadlock_prevention(void) {
    printf("\n======== PART D: DEADLOCK PREVENTION (LOCK ORDERING) ========\n");

    pthread_t t1, t2;
    pthread_create(&t1, NULL, staff_task1, NULL);
    pthread_create(&t2, NULL, staff_task2, NULL);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    printf("[Result] Both staff threads completed. No deadlock because both "
           "always acquire counter_lock before gate_lock (consistent global "
           "ordering breaks the circular-wait condition).\n");
}

/* ===========================================================================
 * MAIN
 * ------------------------------------------------------------------------ */
int main(void) {
    printf("############################################################\n");
    printf("#     Airport Check-In & Baggage Handling System            #\n");
    printf("############################################################\n");

    run_part0_process_creation();
    run_partA_race_condition();
    run_partB_producer_consumer();
    run_partC_round_robin();
    run_partD_deadlock_prevention();

    printf("\nAll demonstrations completed successfully.\n");
    return 0;
}