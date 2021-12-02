
#define _XOPEN_SOURCE 600

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>

#ifndef MAX_CAPACITY
#define MAX_CAPACITY 10
#endif

#ifndef ELEVATORS
#define ELEVATORS 1
#endif

#ifndef FLOORS
#define FLOORS 8
#endif

#ifndef userS
#define userS 10
#endif

#ifndef TRIPS_PER_user
#define TRIPS_PER_user 1
#endif

// these settings affect only the 'looks', will be tested at log level 1
#ifndef DELAY
#define DELAY 10000
#endif

/* called once on initialization */
void scheduler_init();

/* called whenever a user pushes a button in the elevator lobby. 
   call enter / exit to move users into / out of elevator
   return only when the user is delivered to requested floor
 */
void user_request(int user, int from_floor, int to_floor, void (*enter)(int, int), void (*exit)(int, int));

/* called whenever the doors are about to close. 
   call move_direction with direction -1 to descend, 1 to ascend.
   must call door_open before letting users in, and door_close before moving the elevator 
 */
void elevator_ready(int elevator, int at_floor, void (*move_direction)(int, int), void (*door_open)(int), void (*door_close)(int));

int user_count = 0;

struct user
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

} users[userS];

struct Elevator
{
    int id;
    int in_elevator;
    int at_floor;
    int floor;
    int open;
    int users;
    int trips;
    int current_floor;
    int direction;
    int occupancy;

    int seqno;
    int last_action_seqno;
    pthread_mutex_t lock;
    struct user *p;
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

    /* Initialize all users */
    for (i = 0; i < userS; i++)
    {
        pthread_barrier_init(&users[i].barr, NULL, 2);
    }
}

// when a user requests the elevator
void user_request(int user, int from_floor, int to_floor,
                       void (*enter)(int, int),
                       void (*exit)(int, int))
{
    users[user].use_elevator = user % ELEVATORS;
    pthread_mutex_lock(&elevators[users[user].use_elevator].lock);

    int index = users[user].use_elevator;
    users[user].id = user;
    int pass_id = users[user].id;
    users[user].from_floor = from_floor;
    users[user].to_floor = to_floor;
    elevators[index].p = &users[user];
    users[user].state = WAITING;
    user_count++;

    pthread_mutex_unlock(&elevators[users[user].use_elevator].lock);
    pthread_mutex_lock(&elevators[users[user].use_elevator].lock);

    // wait for the elevator to arrive at our origin floor, then get in
    pthread_barrier_wait(&users[pass_id].barr);
    enter(pass_id, users[pass_id].use_elevator);
    elevators[index].occupancy++;
    users[user].state = ENTERED;
    pthread_barrier_wait(&users[pass_id].barr);

    pthread_mutex_unlock(&elevators[index].lock);

    // wait for the elevator at our destination floor, then get out
    pthread_mutex_lock(&elevators[index].lock);

    pthread_barrier_wait(&users[pass_id].barr);
    exit(pass_id, users[pass_id].use_elevator);
    users[user].state = EXITED;
    elevators[index].state = ELEVATOR_ARRIVED;
    elevators[index].occupancy--;
    elevators[index].p = NULL;

    pthread_mutex_unlock(&elevators[index].lock);
    pthread_barrier_wait(&users[pass_id].barr);
}

void elevator_ready(int elevator, int at_floor,
                    void (*move_direction)(int, int),
                    void (*door_open)(int), void (*door_close)(int))
{

    /* Check if at correct floor, if not, go to correct floor */
    if (elevators[elevator].state == ELEVATOR_CLOSED)
    {
        int difference;
        if (elevators[elevator].occupancy == 1)
        {
            difference = (elevators[elevator].p->to_floor - elevators[elevator].current_floor);
            elevators[elevator].current_floor = elevators[elevator].p->to_floor;
        }
        else
        {

            if (elevators[elevator].p == NULL)
            {
                return;
            }

            difference = (elevators[elevator].p->from_floor - elevators[elevator].current_floor);
            elevators[elevator].current_floor = elevators[elevator].p->from_floor;
        }

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
       log(0,"Technical Error: elevator %d make at most one action call per elevator_ready()\n",elevator);
       exit(1);
       }*/
    if (elevators[elevator].users > MAX_CAPACITY || elevators[elevator].users < 0)
    {
        printf("Technical Error: elevator over capacity, or negative user count %d!\n\n", elevators[elevator].users);
        exit(1);
    }
    elevators[elevator].last_action_seqno = elevators[elevator].seqno;
}

void elevator_move_direction(int elevator, int direction)
{
    elevator_check(elevator);
    printf("Moving elevator %s from %d\n", (direction == -1 ? "down" : "up"), elevators[elevator].floor);
    if (elevators[elevator].open)
    {
        printf("Technical Error: attempted to move elevator with door open.\n");
        exit(1);
    }
    if (elevators[elevator].floor >= FLOORS || elevators[elevator].floor < 0)
    {
        printf("Technical Error: attempted to move elevator outside of building!\n");
        exit(1);
    }

    sched_yield();
    usleep(DELAY);
    elevators[elevator].floor += direction;
}

void elevator_open_door(int elevator)
{
    elevator_check(elevator);
    printf("Opening elevator at floor %d\n", elevators[elevator].floor);
    if (elevators[elevator].open)
    {
        printf("Technical Error: attempted to open elevator door when already open.\n");
        exit(1);
    }
    elevators[elevator].open = 1;
    usleep(10 * DELAY);
}

void elevator_close_door(int elevator)
{
    elevator_check(elevator);
    printf("Closing elevator at floor %d\n", elevators[elevator].floor);
    if (!elevators[elevator].open)
    {
        printf("Technical Error: attempted to close elevator door when already closed.\n");
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
    e->users = 0;
    e->trips = 0;
    printf("Starting elevator...\n");

    e->floor = 0;
    while (!stop)
    {
        e->seqno++;
        elevator_ready(elevator, e->floor, elevator_move_direction, elevator_open_door, elevator_close_door);
        sched_yield();
    }
}

void user_enter(int user, int elevator)
{
    if (users[user].from_floor != elevators[elevator].floor)
    {
        printf("Technical Error: let user %d on on wrong floor (%d), destination was %d.\n", users[user].id, users[user].from_floor, elevators[elevator].floor);
        exit(1);
    }
    if (!elevators[elevator].open)
    {
        printf("Technical Error: user %d walked into a closed door entering elevator %d.\n", users[user].id, elevator);
        exit(1);
    }
    if (elevators[elevator].users == MAX_CAPACITY)
    {
        printf("Technical Error: user %d attempted to board full elevator %d.\n", users[user].id, elevator);
        exit(1);
    }
    if (users[user].state != WAITING)
    {
        printf("Technical Error: user %d told to board elevator, was not waiting.\n", users[user].id);
        exit(1);
    }

    printf("user %d got on elevator at their request floor, %d\n", users[user].id, users[user].from_floor);
    elevators[elevator].users++;
    users[user].in_elevator = elevator;
    users[user].state = ENTERED;

    sched_yield();
    usleep(DELAY);
}

void user_exit(int user, int elevator)
{
    if (users[user].to_floor != elevators[elevator].floor)
    {
        printf("Technical Error: let user %d off on wrong floor (%d), destination was %d.\n", users[user].id, users[user].to_floor, elevators[elevator].floor);
        exit(1);
    }
    if (!elevators[elevator].open)
    {
        printf("Technical Error: user %d walked into a closed door leaving elevator %d.\n", users[user].id, elevator);
        exit(1);
    }
    if (users[user].state != ENTERED)
    {
        printf("Technical Error: user %d told to board elevator %d, was not waiting.\n", users[user].id, elevator);
        exit(1);
    }

    printf("user %d got off elevator at their destination, %d\n", users[user].id, users[user].to_floor);
    elevators[elevator].users--;
    elevators[elevator].trips++;
    users[user].in_elevator = -1;
    users[user].state = EXITED;

    sched_yield();
    usleep(DELAY);
}

void *start_user(void *arg)
{
    size_t user = (size_t)arg;
    struct user *p = &users[user];
    printf("Starting user %lu\n", user);
    p->from_floor = random() % FLOORS;
    p->in_elevator = -1;
    p->id = user;
    int trips = TRIPS_PER_user;
    while (!stop && trips-- > 0)
    {
        p->to_floor = random() % FLOORS;

        printf("user %lu requesting from floor %d to %d\n",
               user, p->from_floor, p->to_floor);

        struct timeval before;
        gettimeofday(&before, 0);
        users[user].state = WAITING;
        user_request(user, p->from_floor, p->to_floor, user_enter, user_exit);
        struct timeval after;
        gettimeofday(&after, 0);
        int ms = (after.tv_sec - before.tv_sec) * 1000 + (after.tv_usec - before.tv_usec) / 1000;
        printf("\n\nuser %lu arrived!\n\n", user);

        p->from_floor = p->to_floor;
        usleep(10000);
    }
}

int main(int argc, char **argv)
{

    scheduler_init();

    pthread_t user_t[userS];

    for (size_t i = 0; i < userS; i++)
    {
        pthread_create(&user_t[i], NULL, start_user, (void *)i);
    }

    usleep(100000);

    pthread_t elevator_t[ELEVATORS];
    for (size_t i = 0; i < ELEVATORS; i++)
    {
        pthread_create(&elevator_t[i], NULL, start_elevator, (void *)i);
    }

    /* wait for all trips to complete */
    for (int i = 0; i < userS; i++)
        pthread_join(user_t[i], NULL);
    stop = 1;
    for (int i = 0; i < ELEVATORS; i++)
        pthread_join(elevator_t[i], NULL);

    printf("All %d users arrived to their destinations!\n", userS);
    return 0;
}
