#include "ppos.h"
#include "ppos-core-globals.h"

int sem_create(semaphore_t* s, int value) {
    if (s == NULL) {
        return -1;
    }

    PPOS_PREEMPT_DISABLE; // Impede preempção

    before_sem_create(s, value);

    s->queue = NULL;
    s->value = value;
    s->active = 1;

    after_sem_create(s, value);

    PPOS_PREEMPT_ENABLE; // Retoma preempção

    return 0;
}

int sem_down(semaphore_t* s) {
    if (s == NULL || !(s->active)) {
        return -1;
    }

    PPOS_PREEMPT_DISABLE; // Impede preempção

    before_sem_down(s);

    s->value--;
    if (s->value < 0) {
        task_suspend(taskExec, &(s->queue));

        after_sem_down(s);

        PPOS_PREEMPT_ENABLE; // Retoma preempção

        task_yield();

        // Se a tarefa foi acordada devido a um sem_destroy, retorna -1.
        if (!(s->active)) {
            return -1;
        }
        
        return 0;
    }
    
    after_sem_down(s);

    #ifdef PPOS_TIME_SHARING
    if(((task_data_t*)taskExec->custom_data)->quantum <= 0) {
        PPOS_PREEMPT_ENABLE; // Retoma preempção
        //printf("\tSEM_DOWN-Quantum"); fflush(stdout);
        task_yield();
    }
    #endif
    PPOS_PREEMPT_ENABLE; // Retoma preempção
    return 0;
}

int sem_up(semaphore_t* s) {
    PPOS_PREEMPT_DISABLE; // Impede preempção
   
    if (s == NULL || !(s->active)) {
        return -1;
    }
    before_sem_up(s);
   
    s->value++;
    if (s->value <= 0) {
        task_resume(s->queue);
    }

    after_sem_up(s);
    
    #ifdef PPOS_TIME_SHARING
    if(((task_data_t*)taskExec->custom_data)->quantum <= 0) {
        PPOS_PREEMPT_ENABLE; // Retoma preempção
        //printf("\tSEM_UP-Quantum"); fflush(stdout);
        task_yield();
    }
    #endif

    return 0;
}

int sem_destroy(semaphore_t* s) {
    if (s == NULL || !(s->active)) {
        return -1;
    }
    
    PPOS_PREEMPT_DISABLE; // Impede preempção

    before_sem_destroy(s);

    s->active = 0;
    while (s->queue != NULL) {
        task_resume(s->queue);
    }

    after_sem_destroy(s);

    PPOS_PREEMPT_ENABLE; // Retoma preempção

    return 0;
}