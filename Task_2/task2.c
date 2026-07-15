/*
 * Task 2 - Memory Management Simulation
 * Scenario: Airport Passenger Record Cache (keeping the same airport theme as Task 1)
 *
 * Basic idea: during check-in rush hour, the airport system can't keep every
 * passenger's booking record loaded in fast memory at the same time. So it
 * has to "page" records in and out of a few fast counter slots, kind of like
 * how an OS pages memory in and out of physical frames.
 *
 * - a passenger booking ID  = a virtual page
 * - a fast counter slot     = a physical frame
 * - record already sitting in a slot  -> HIT
 * - record has to be pulled in        -> PAGE FAULT
 *
 * What this covers:
 *   1. paging system where page size / total size are just #defines up top,
 *      so they're easy to change
 *   2. two replacement algorithms - FIFO and LRU
 *   3. counts hits/faults and works out hit and miss ratio
 *   4. prints out what's in the counter slots after every single step so
 *      you can actually see the paging happening instead of just a final number
 *
 * compile: gcc task2.c -o task2
 * run:     ./task2
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* change these if you want to try a different page size / memory size */
#define PAGE_SIZE_KB           4    // how big one passenger record "page" is (KB)
#define RESERVATION_SYS_KB    64    // total size of the reservation system (KB)
#define MAX_FRAMES            10    // just an upper limit so the arrays are big enough

/* holds the numbers we care about at the end of a run, so we can compare
   FIFO vs LRU side by side afterwards */
typedef struct {
    int page_faults;
    int page_hits;
    double hit_ratio;
    double miss_ratio;
} SimResult;

/* just prints whatever is currently sitting in each counter slot.
   empty slot shows as -- */
void print_counters(int counters[], int num_counters) {
    printf("[ ");
    for (int i = 0; i < num_counters; i++) {
        if (counters[i] == -1) printf("-- ");
        else                   printf("%2d ", counters[i]);
    }
    printf("]");
}

/* -------------------------------------------------------------------
 * FIFO - basically "first one that got loaded is the first one kicked out"
 *
 * Doesn't matter if that record was just used a second ago, if it was the
 * oldest one loaded in, it's the one that gets replaced. Keeping track of
 * this is easy - just a pointer that walks through the slots in order and
 * wraps back around to 0 once it hits the end (like a circular queue).
 * ------------------------------------------------------------------ */
SimResult simulate_fifo(int booking_refs[], int n, int num_counters) {
    int counters[MAX_FRAMES];
    for (int i = 0; i < num_counters; i++) counters[i] = -1; // -1 means empty

    int faults = 0, hits = 0;
    int next_victim = 0; // which slot gets replaced next

    printf("\n--- FIFO Simulation (counter slots = %d) ---\n", num_counters);
    printf("%-6s %-10s %-8s %-s\n", "Step", "BookingID", "Result", "Counter slots after step");
    printf("-------------------------------------------------------------\n");

    for (int i = 0; i < n; i++) {
        int booking = booking_refs[i];
        bool found = false;

        // check if this booking is already sitting in a slot somewhere
        for (int f = 0; f < num_counters; f++) {
            if (counters[f] == booking) { found = true; break; }
        }

        if (found) {
            hits++;
            printf("%-6d %-10d %-8s ", i + 1, booking, "HIT");
        } else {
            // not there - gotta pull it in, kicking out whatever's oldest
            counters[next_victim] = booking;
            next_victim = (next_victim + 1) % num_counters; // move the pointer along
            faults++;
            printf("%-6d %-10d %-8s ", i + 1, booking, "FAULT");
        }
        print_counters(counters, num_counters);
        printf("\n");
    }

    SimResult r;
    r.page_faults = faults;
    r.page_hits   = hits;
    r.hit_ratio   = (double)hits   / n;
    r.miss_ratio  = (double)faults / n;
    return r;
}

/* -------------------------------------------------------------------
 * LRU - kick out whichever record hasn't been touched in the longest time
 *
 * To actually track "longest time since used" I just keep a fake clock
 * that ticks up by 1 every single lookup, and remember the clock value
 * for whichever slot was last touched. When we need to evict something,
 * just scan for the smallest clock value = the one used furthest back.
 * ------------------------------------------------------------------ */
SimResult simulate_lru(int booking_refs[], int n, int num_counters) {
    int counters[MAX_FRAMES];
    int last_used[MAX_FRAMES];
    for (int i = 0; i < num_counters; i++) { counters[i] = -1; last_used[i] = -1; }

    int faults = 0, hits = 0;
    int clock = 0; // fake clock, just goes up by 1 each lookup

    printf("\n--- LRU Simulation (counter slots = %d) ---\n", num_counters);
    printf("%-6s %-10s %-8s %-s\n", "Step", "BookingID", "Result", "Counter slots after step");
    printf("-------------------------------------------------------------\n");

    for (int i = 0; i < n; i++) {
        int booking = booking_refs[i];
        int found_at = -1;

        for (int f = 0; f < num_counters; f++) {
            if (counters[f] == booking) { found_at = f; break; }
        }

        if (found_at != -1) {
            hits++;
            last_used[found_at] = clock; // touched it, so bump its timestamp
            printf("%-6d %-10d %-8s ", i + 1, booking, "HIT");
        } else {
            // first try an empty slot if there is one
            int target = -1;
            for (int f = 0; f < num_counters; f++) {
                if (counters[f] == -1) { target = f; break; }
            }
            // no free slot? evict whichever one has the oldest timestamp
            if (target == -1) {
                int min_time = last_used[0], min_idx = 0;
                for (int f = 1; f < num_counters; f++) {
                    if (last_used[f] < min_time) { min_time = last_used[f]; min_idx = f; }
                }
                target = min_idx;
            }
            counters[target] = booking;
            last_used[target] = clock;
            faults++;
            printf("%-6d %-10d %-8s ", i + 1, booking, "FAULT");
        }
        clock++;
        print_counters(counters, num_counters);
        printf("\n");
    }

    SimResult r;
    r.page_faults = faults;
    r.page_hits   = hits;
    r.hit_ratio   = (double)hits   / n;
    r.miss_ratio  = (double)faults / n;
    return r;
}

/* runs both algorithms on the same sequence and prints a little summary
   comparing them, so it's easy to see which one did better */
void run_test_case(const char *label, int booking_refs[], int n, int num_counters) {
    printf("\n===============================================================\n");
    printf(" TEST CASE: %s\n", label);
    printf(" Record size = %d KB | Reservation system size = %d KB | Total pages = %d\n",
           PAGE_SIZE_KB, RESERVATION_SYS_KB, RESERVATION_SYS_KB / PAGE_SIZE_KB);
    printf(" Counter slots available = %d | Booking lookups = %d\n", num_counters, n);
    printf(" Booking reference sequence: ");
    for (int i = 0; i < n; i++) printf("%d ", booking_refs[i]);
    printf("\n===============================================================\n");

    SimResult fifo = simulate_fifo(booking_refs, n, num_counters);
    SimResult lru  = simulate_lru(booking_refs, n, num_counters);

    printf("\n--- Summary for \"%s\" ---\n", label);
    printf("%-12s %-8s %-8s %-10s %-10s\n", "Algorithm", "Faults", "Hits", "Hit Ratio", "Miss Ratio");
    printf("---------------------------------------------------------\n");
    printf("%-12s %-8d %-8d %-10.2f %-10.2f\n", "FIFO", fifo.page_faults, fifo.page_hits, fifo.hit_ratio, fifo.miss_ratio);
    printf("%-12s %-8d %-8d %-10.2f %-10.2f\n", "LRU",  lru.page_faults,  lru.page_hits,  lru.hit_ratio,  lru.miss_ratio);

    if (fifo.page_faults < lru.page_faults)
        printf("-> FIFO did better here (fewer faults).\n");
    else if (lru.page_faults < fifo.page_faults)
        printf("-> LRU did better here (fewer faults).\n");
    else
        printf("-> Both algorithms ended up the same on this one.\n");
}

/* just runs through a bunch of different booking sequences / counter
   counts so we've got a decent variety of test cases to compare FIFO
   and LRU on, instead of just one example */
int main(void) {
    printf("################################################################\n");
    printf("#   Airport Passenger Record Cache - Paging & Replacement Sim   #\n");
    printf("################################################################\n");

    // basic textbook-style sequence, 3 slots
    int refs1[] = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
    run_test_case("Morning rush booking lookups, 3 counters", refs1, 12, 3);

    // same sequence but with an extra slot this time
    run_test_case("Morning rush booking lookups, 4 counters", refs1, 12, 4);

    // this exact sequence is known to make FIFO do WORSE with more frames
    // (Belady's Anomaly) while LRU doesn't have that problem
    run_test_case("Belady's Anomaly check, 3 counters", refs1, 12, 3);
    run_test_case("Belady's Anomaly check, 4 counters", refs1, 12, 4);

    // a few "regulars" keep getting looked up over and over - this is where
    // LRU should actually pull ahead of FIFO
    int refs2[] = {7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2};
    run_test_case("Frequent-flyer locality pattern, 3 counters", refs2, 13, 3);

    // worst case - keeps cycling through more bookings than we have slots,
    // so literally every single lookup is a fault no matter what algorithm
    int refs3[] = {1, 2, 3, 4, 5, 1, 2, 3, 4, 5, 1, 2, 3, 4, 5};
    run_test_case("Large tour group cyclic check-in, 3 counters", refs3, 15, 3);

    printf("\nAll test cases completed.\n");
    return 0;
}