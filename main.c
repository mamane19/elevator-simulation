
#define _XOPEN_SOURCE 600 /* Or higher */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>

/* Change these experiment settings to try different scenarios. Parameters can 
   also be passed in using gcc flags, e.g. -DELEVATORS=5 */

#ifndef MAX_CAPACITY
#define MAX_CAPACITY 10
#endif

#ifndef ELEVATORS
#define ELEVATORS 1
#endif

#ifndef FLOORS
#define FLOORS 8
#endif

#ifndef PASSENGERS
#define PASSENGERS 10
#endif

#ifndef TRIPS_PER_PASSENGER
#define TRIPS_PER_PASSENGER 1
#endif

// these settings affect only the 'looks', will be tested at log level 1
#ifndef DELAY
#define DELAY 10000
#endif

/* called once on initialization */
void scheduler_init();

/* called whenever a passenger pushes a button in the elevator lobby. 
   call enter / exit to move passengers into / out of elevator
   return only when the passenger is delivered to requested floor
 */
void passenger_request(int passenger, int from_floor, int to_floor, void (*enter)(int, int), void (*exit)(int, int));

/* called whenever the doors are about to close. 
   call move_direction with direction -1 to descend, 1 to ascend.
   must call door_open before letting passengers in, and door_close before moving the elevator 
 */
void elevator_ready(int elevator, int at_floor, void (*move_direction)(int, int), void (*door_open)(int), void (*door_close)(int));

int passenger_count = 0;

struct Passenger
{
    pthread_barrier_t barr;
    enum
    {
        WAITING,
        ENTERED,
        EXITED
    } state;

    int from_floor;
    int to_floor;
    int use_elevator;
    pthread_mutex_t lock;

    int id;
    int in_elevator;

} passengers[PASSENGERS];

struct Elevator
{
    int id;
    int in_elevator;
    int at_floor;
    int floor;
    int open;
    int passengers;
    int trips;
    int current_floor;
    int direction;
    int occupancy;

    int seqno;
    int last_action_seqno;
    pthread_mutex_t lock;
    struct Passenger *p;
    enum
    {
        ELEVATOR_ARRIVED = 1,
        ELEVATOR_OPEN = 2,
        ELEVATOR_CLOSED = 3
    } state;
} elevators[ELEVATORS];

/* Initialize struct, initalize mutex, initalize barrier */
void scheduler_init()
{
    int i;

    /* Initialize all elevators	*/
    for (i = 0; i < ELEVATORS; i++)
    {
        pthread_mutex_init(&elevators[i].lock, NULL);
        elevators[i].current_floor = 0;
        elevators[i].direction = -1;
        elevators[i].occupancy = 0;
        elevators[i].state = ELEVATOR_CLOSED;
        elevators[i].p = NULL;
    }

    /* Initialize all passengers */
    for (i = 0; i < PASSENGERS; i++)
    {
        pthread_barrier_init(&passengers[i].barr, NULL, 2);
    }
}

// ahdhshshh
void passenger_request(int passenger, int from_floor, int to_floor,
                       void (*enter)(int, int),
                       void (*exit)(int, int))
{
    //log(3, "PASSENGER REQUEST FOR %d\n", passenger);
    passengers[passenger].use_elevator = passenger % ELEVATORS;
    // elevators[passengers[passenger].use_elevator].p = &passengers[passenger];
    pthread_mutex_lock(&elevators[passengers[passenger].use_elevator].lock);

    //log(3, "INSIDE PASSENGER REQUEST LOCK\n", NULL);
    int index = passengers[passenger].use_elevator;
    passengers[passenger].id = passenger;
    int pass_id = passengers[passenger].id;
    passengers[passenger].from_floor = from_floor;
    passengers[passenger].to_floor = to_floor;
    elevators[index].p = &passengers[passenger];
    //log(3, "Assigned p for %d!\n", passenger);
    passengers[passenger].state = WAITING;
    passenger_count++;
    //log(3, "\n\nPassenger COUNT: %d\n\n", passenger_count);
    // if (passenger_count == 50) {
    // }

    pthread_mutex_unlock(&elevators[passengers[passenger].use_elevator].lock);
    pthread_mutex_lock(&elevators[passengers[passenger].use_elevator].lock);

    // wait for the elevator to arrive at our origin floor, then get in
    pthread_barrier_wait(&passengers[pass_id].barr);
    enter(pass_id, passengers[pass_id].use_elevator);
    elevators[index].occupancy++;
    passengers[passenger].state = ENTERED;
    pthread_barrier_wait(&passengers[pass_id].barr);

    pthread_mutex_unlock(&elevators[index].lock);

    // wait for the elevator at our destination floor, then get out
    pthread_mutex_lock(&elevators[index].lock);

    pthread_barrier_wait(&passengers[pass_id].barr);
    exit(pass_id, passengers[pass_id].use_elevator);
    passengers[passenger].state = EXITED;
    elevators[index].state = ELEVATOR_ARRIVED;
    elevators[index].occupancy--;
    elevators[index].p = NULL;

    pthread_mutex_unlock(&elevators[index].lock);
    pthread_barrier_wait(&passengers[pass_id].barr);
}

void elevator_ready(int elevator, int at_floor,
                    void (*move_direction)(int, int),
                    void (*door_open)(int), void (*door_close)(int))
{

    //log(3, "The ELEVATOR: %d\n", elevator);
    //log(3, "The ELEVATOR STATE: %d\n", elevators[elevator].state);

    /* Check if at correct floor, if not, go to correct floor */
    if (elevators[elevator].state == ELEVATOR_CLOSED)
    {
        int difference;
        if (elevators[elevator].occupancy == 1)
        {
            //log(3, "In occupancy == 1.\n", NULL);

            difference = (elevators[elevator].p->to_floor - elevators[elevator].current_floor);
            elevators[elevator].current_floor = elevators[elevator].p->to_floor;
        }
        else
        {
            //log(3, "In occupancy == 0.\n", NULL);
            //log(3, "before calculated Difference VALUE: %d\n", difference);
            if (elevators[elevator].p == NULL)
            {
                //log(3, "in if null statement\n", NULL);
                return;
                //log(3, "From FLOOR of p: %d\n", elevators[elevator].p->from_floor);
                //log(3, "Null POINTER?: %d\n", &elevators[elevator].p);
            }

            difference = (elevators[elevator].p->from_floor - elevators[elevator].current_floor);
            //log(3, "Difference VALUE: %d\n", difference);
            elevators[elevator].current_floor = elevators[elevator].p->from_floor;
        }
        //log(3, "End of IF.\n", NULL);

        move_direction(elevator, difference);
        elevators[elevator].state = ELEVATOR_ARRIVED;
    }

    if (elevators[elevator].state == ELEVATOR_OPEN)
    {
        pthread_barrier_wait(&elevators[elevator].p->barr);

        door_close(elevator);
        elevators[elevator].state = ELEVATOR_CLOSED;
    }

    if (elevators[elevator].state == ELEVATOR_ARRIVED)
    {
        door_open(elevator);
        elevators[elevator].state = ELEVATOR_OPEN;
        pthread_barrier_wait(&elevators[elevator].p->barr);
    }
}

// when stop == 1, all threads quit voluntarily
static int stop = 0;

void elevator_check(int elevator)
{
    /*
       if(elevators[elevator].seqno == elevators[elevator].last_action_seqno) {
       log(0,"VIOLATION: elevator %d make at most one action call per elevator_ready()\n",elevator);
       exit(1);
       }*/
    if (elevators[elevator].passengers > MAX_CAPACITY || elevators[elevator].passengers < 0)
    {
        // log(0, "VIOLATION: elevator %d over capacity, or negative passenger count %d!\n", elevator, elevators[elevator].passengers);
        printf("VIOLATION: elevator %d over capacity, or negative passenger count %d!\n\n", elevator, elevators[elevator].passengers);
        exit(1);
    }
    elevators[elevator].last_action_seqno = elevators[elevator].seqno;
}

void elevator_move_direction(int elevator, int direction)
{
    elevator_check(elevator);
    // log(8, "Moving elevator %d %s from %d\n", elevator, (direction == -1 ? "down" : "up"), elevators[elevator].floor);
    printf("Moving elevator %d %s from %d\n", elevator, (direction == -1 ? "down" : "up"), elevators[elevator].floor);
    if (elevators[elevator].open)
    {
        // log(0, "VIOLATION: attempted to move elevator %d with door open.\n", elevator);
        printf("VIOLATION: attempted to move elevator %d with door open.\n", elevator);
        exit(1);
    }
    if (elevators[elevator].floor >= FLOORS || elevators[elevator].floor < 0)
    {
        // log(0, "VIOLATION: attempted to move elevator %d outside of building!\n", elevator);
        printf("VIOLATION: attempted to move elevator %d outside of building!\n", elevator);
        exit(1);
    }

    sched_yield();
    usleep(DELAY);
    elevators[elevator].floor += direction;
}

void elevator_open_door(int elevator)
{
    elevator_check(elevator);
    // log(9, "Opening elevator %d at floor %d\n", elevator, elevators[elevator].floor);
    printf("Opening elevator %d at floor %d\n", elevator, elevators[elevator].floor);
    if (elevators[elevator].open)
    {
        // log(0, "VIOLATION: attempted to open elevator %d door when already open.\n", elevator);
        printf("VIOLATION: attempted to open elevator %d door when already open.\n", elevator);
        exit(1);
    }
    elevators[elevator].open = 1;
    usleep(10 * DELAY);
}

void elevator_close_door(int elevator)
{
    elevator_check(elevator);
    // log(9, "Closing elevator %d at floor %d\n", elevator, elevators[elevator].floor);
    printf("Closing elevator %d at floor %d\n", elevator, elevators[elevator].floor);
    if (!elevators[elevator].open)
    {
        // log(0, "VIOLATION: attempted to close elevator %d door when already closed.\n", elevator);
        printf("VIOLATION: attempted to close elevator %d door when already closed.\n", elevator);
        exit(1);
    }
    sched_yield();
    usleep(10 * DELAY);
    elevators[elevator].open = 0;
}

void *start_elevator(void *arg)
{
    size_t elevator = (size_t)arg;
    struct Elevator *e = &elevators[elevator];
    e->last_action_seqno = 0;
    e->seqno = 1;
    e->passengers = 0;
    e->trips = 0;
    // log(6, "Starting elevator %lu\n", elevator);
    printf("Starting elevator %lu\n", elevator);

    e->floor = 0; //elevator % (FLOORS-1);
    while (!stop)
    {
        e->seqno++;
        elevator_ready(elevator, e->floor, elevator_move_direction, elevator_open_door, elevator_close_door);
        sched_yield();
    }
}

void passenger_enter(int passenger, int elevator)
{
    if (passengers[passenger].from_floor != elevators[elevator].floor)
    {
        // log(0, "VIOLATION: let passenger %d on on wrong floor %d!=%d.\n", passengers[passenger].id, passengers[passenger].from_floor, elevators[elevator].floor);
        printf("VIOLATION: let passenger %d on on wrong floor %d!=%d.\n", passengers[passenger].id, passengers[passenger].from_floor, elevators[elevator].floor);
        exit(1);
    }
    if (!elevators[elevator].open)
    {
        // log(0, "VIOLATION: passenger %d walked into a closed door entering elevator %d.\n", passengers[passenger].id, elevator);
        printf("VIOLATION: passenger %d walked into a closed door entering elevator %d.\n", passengers[passenger].id, elevator);
        exit(1);
    }
    if (elevators[elevator].passengers == MAX_CAPACITY)
    {
        // log(0, "VIOLATION: passenger %d attempted to board full elevator %d.\n", passengers[passenger].id, elevator);
        printf("VIOLATION: passenger %d attempted to board full elevator %d.\n", passengers[passenger].id, elevator);
        exit(1);
    }
    if (passengers[passenger].state != WAITING)
    {
        // log(0, "VIOLATION: passenger %d told to board elevator %d, was not waiting.\n", passengers[passenger].id, elevator);
        printf("VIOLATION: passenger %d told to board elevator %d, was not waiting.\n", passengers[passenger].id, elevator);
        exit(1);
    }

    // log(6, "Passenger %d got on elevator %d at %d, requested %d\n", passengers[passenger].id, elevator, passengers[passenger].from_floor, elevators[elevator].floor);
    printf("Passenger %d got on elevator %d at %d, requested %d\n", passengers[passenger].id, elevator, passengers[passenger].from_floor, elevators[elevator].floor);
    elevators[elevator].passengers++;
    passengers[passenger].in_elevator = elevator;
    passengers[passenger].state = ENTERED;

    sched_yield();
    usleep(DELAY);
}

void passenger_exit(int passenger, int elevator)
{
    if (passengers[passenger].to_floor != elevators[elevator].floor)
    {
        // log(0, "VIOLATION: let passenger %d off on wrong floor %d!=%d.\n", passengers[passenger].id, passengers[passenger].to_floor, elevators[elevator].floor);
        printf("VIOLATION: let passenger %d off on wrong floor %d!=%d.\n", passengers[passenger].id, passengers[passenger].to_floor, elevators[elevator].floor);
        exit(1);
    }
    if (!elevators[elevator].open)
    {
        // log(0, "VIOLATION: passenger %d walked into a closed door leaving elevator %d.\n", passengers[passenger].id, elevator);
        printf("VIOLATION: passenger %d walked into a closed door leaving elevator %d.\n", passengers[passenger].id, elevator);
        exit(1);
    }
    if (passengers[passenger].state != ENTERED)
    {
        // log(0, "VIOLATION: passenger %d told to board elevator %d, was not waiting.\n", passengers[passenger].id, elevator);
        printf("VIOLATION: passenger %d told to board elevator %d, was not waiting.\n", passengers[passenger].id, elevator);
        exit(1);
    }

    // log(6, "Passenger %d got off elevator %d at %d, requested %d\n", passengers[passenger].id, elevator, passengers[passenger].to_floor, elevators[elevator].floor);
    printf("Passenger %d got off elevator %d at %d, requested %d\n", passengers[passenger].id, elevator, passengers[passenger].to_floor, elevators[elevator].floor);
    elevators[elevator].passengers--;
    elevators[elevator].trips++;
    passengers[passenger].in_elevator = -1;
    passengers[passenger].state = EXITED;

    sched_yield();
    usleep(DELAY);
}

void *start_passenger(void *arg)
{
    size_t passenger = (size_t)arg;
    struct Passenger *p = &passengers[passenger];
    // log(6, "Starting passenger %lu\n", passenger);
    printf("Starting passenger %lu\n", passenger);
    p->from_floor = random() % FLOORS;
    p->in_elevator = -1;
    p->id = passenger;
    int trips = TRIPS_PER_PASSENGER;
    while (!stop && trips-- > 0)
    {
        p->to_floor = random() % FLOORS;

        printf("Passenger %lu requesting from floor %d to %d\n",
               passenger, p->from_floor, p->to_floor);

        struct timeval before;
        gettimeofday(&before, 0);
        passengers[passenger].state = WAITING;
        passenger_request(passenger, p->from_floor, p->to_floor, passenger_enter, passenger_exit);
        struct timeval after;
        gettimeofday(&after, 0);
        int ms = (after.tv_sec - before.tv_sec) * 1000 + (after.tv_usec - before.tv_usec) / 1000;
        // log(1, "Passenger %lu trip duration %d ms, %d slots\n", passenger, ms, ms * 1000 / DELAY);
        printf("\n\nPassenger %lu arrived!\n\n", passenger);

        p->from_floor = p->to_floor;
        usleep(10000);
    }
}

int main(int argc, char **argv)
{

    scheduler_init();

    pthread_t passenger_t[PASSENGERS];

    for (size_t i = 0; i < PASSENGERS; i++)
    {
        pthread_create(&passenger_t[i], NULL, start_passenger, (void *)i);
    }

    usleep(100000);

    pthread_t elevator_t[ELEVATORS];
    for (size_t i = 0; i < ELEVATORS; i++)
    {
        pthread_create(&elevator_t[i], NULL, start_elevator, (void *)i);
    }

    // pthread_create(&draw_t, NULL, draw_state, NULL);

    /* wait for all trips to complete */
    for (int i = 0; i < PASSENGERS; i++)
        pthread_join(passenger_t[i], NULL);
    stop = 1;
    for (int i = 0; i < ELEVATORS; i++)
        pthread_join(elevator_t[i], NULL);

    printf("All %d passengers finished their %d trips each.\n", PASSENGERS, TRIPS_PER_PASSENGER);
    return 0;
}
