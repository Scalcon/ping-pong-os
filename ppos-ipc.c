//souce code of the semaphore implementation
//author: Rodrigo, Scalcon
#include "ppos.h"
#include "ppos-core-globals.h"


// create the semaphore: sem_init
int sem_create(semaphore_t *s, int value)
{
    if (s == NULL)  return -1;
    PPOS_PREEMPT_DISABLE; // disable preemption to stop race conditions
    s->counter = value;
    s->queue = NULL;
    s->active = 1;
    PPOS_PREEMPT_ENABLE; // enable preemption again
    return 0;
}

int sem_down(semaphore_t *s)
{
    if (s == NULL || !s->active) {
        return -1;
    }

    PPOS_PREEMPT_DISABLE;
    s->counter--;
    
    
    return 0;
}

int sem_up(semaphore_t *s)
{
    return 0;
}


// destroy the semaphore and wake up all tasks that are waiting for it
int sem_destroy(semaphore_t *s)
{
    if (s == NULL || !s->active) {
        return -1;
    }
    PPOS_PREEMPT_DISABLE;
    s->active = 0;
    
    while (s->queue != NULL){
        task_resume(s->queue);
    }

    PPOS_PREEMPT_ENABLE; 

    return 0;
}