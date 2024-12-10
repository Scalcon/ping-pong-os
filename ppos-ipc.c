//souce code of the semaphore implementation
//author: Rodrigo, Scalcon
#include "ppos.h"
#include "ppos-core-globals.h"


// create the semaphore: sem_init
int sem_create(semaphore_t *s, int value)
{
    if (s == NULL)  return -1;
    PPOS_PREEMPT_DISABLE; // disable preemption to stop interruptions
    s->counter = value;
    s->queue = NULL;
    s->active = 1;
    PPOS_PREEMPT_ENABLE; // enable preemption
    return 0;
}

int sem_down(semaphore_t *s)
{
    return 0;
}

int sem_up(semaphore_t *s)
{
    return 0;
}

int sem_destroy(semaphore_t *s)
{
    if (s == NULL || !(s->active)) {
        return -1;
    }
    

    return 0;
}