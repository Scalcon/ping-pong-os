#define _POSIX_C_SOURCE 199309L
#define SA_RESTART 0x10000000

#include "ppos.h"
#include "ppos-core-globals.h"
#include "disk-driver.h"
#include "ppos-disk-manager.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>

// Variáveis globais para gerenciamento do disco
task_t* disk_suspended_queue = NULL;
diskrequest_t* disk_task_queue = NULL;
diskrequest_t* current_disk_task = NULL;

semaphore_t disk_mgr_sem;
semaphore_t disk_task_sem;

task_t disk_mgr_task;

disk_t disk;

struct sigaction disk_sig;
int disk_sig_flag = 0;

// Protótipos de funções
void append_disk_task(diskrequest_t* task);
void disk_append_ready_queue(task_t* task);
void task_suspend_disk(task_t* task);
int disk_sig_handler_setup();
void disk_sig_handler();
task_t* pop_suspend_queue();
diskrequest_t* pop_disk_queue();
task_t* remove_node_suspended(task_t* task);
diskrequest_t* remove_node_disk(diskrequest_t* task);
diskrequest_t* sstf_disk_scheduler(diskrequest_t* disk_task_queue);
diskrequest_t* cscan_disk_scheduler(diskrequest_t* disk_task_queue);
diskrequest_t* fcfs_disk_scheduler(diskrequest_t* disk_task_queue);

// Função para tratamento de sinais de erro
void clean_exit_on_sig(int sig_num) {
    printf("\n ERROR[Signal = %d]: %d \"%s\"", sig_num, errno, strerror(errno));
    exit(errno);
}

// Função principal do gerenciador de disco
void disk_manager(void* args) {
    while (1) {
        sem_down(&disk_task_sem);

        if (disk_sig_flag) {
            task_t* ready_task = remove_node_suspended(current_disk_task->task);
            disk_append_ready_queue(ready_task);
            disk_sig_flag = 0;

            disk.total_steps += abs(disk.head_position - current_disk_task->block);
            disk.total_time += systime() - current_disk_task->start_time;
            disk.head_position = current_disk_task->block;

            free(current_disk_task);
        }

        int disk_idle = disk_cmd(DISK_CMD_STATUS, 0, 0) == DISK_STATUS_IDLE;
        if (disk_idle && disk_task_queue != NULL) {
            diskrequest_t* next_task = fcfs_disk_scheduler(disk_task_queue);

            if (disk_cmd(next_task->op, next_task->block, next_task->buffer) >= 0) {
                current_disk_task = remove_node_disk(next_task);
                current_disk_task->start_time = systime();
            }
        }

        sem_up(&disk_task_sem);
        sem_down(&disk_mgr_sem);
    }
}

// Inicialização do gerenciador de disco
int disk_mgr_init(int *numblocks, int *blockSize) {
    PPOS_PREEMPT_DISABLE;

    disk_suspended_queue = NULL;
    disk_task_queue = NULL;
    disk_sig_flag = 0;

    if (sem_create(&disk_mgr_sem, 0) < 0) return -1;
    if (sem_create(&disk_task_sem, 1) < 0) return -1;
    if (disk_sig_handler_setup() < 0) return -1;

    if (task_create(&disk_mgr_task, disk_manager, NULL) < 0) return -1;
    disk_mgr_task.sys_task = 1;

    if (disk_cmd(DISK_CMD_INIT, 0, 0) < 0) return -1;

    disk.num_blocks = disk_cmd(DISK_CMD_DISKSIZE, 0, 0);
    disk.block_size = disk_cmd(DISK_CMD_BLOCKSIZE, 0, 0);
    disk.total_time = 0;
    disk.total_steps = 0;
    disk.head_position = 0;

    *numblocks = disk.num_blocks;
    *blockSize = disk.block_size;

    PPOS_PREEMPT_ENABLE;

    signal(SIGSEGV, clean_exit_on_sig);
    return 0;
}

// Funções para leitura e escrita de blocos no disco
int disk_block_read(int block, void *buffer) {
    diskrequest_t* d_task = (diskrequest_t*) malloc(sizeof(diskrequest_t));
    d_task->task = taskExec;
    d_task->buffer = buffer;
    d_task->op = DISK_CMD_READ;
    d_task->launch_time = systime();
    d_task->block = block;
    d_task->next = NULL;
    d_task->prev = NULL;

    sem_down(&disk_task_sem);
    append_disk_task(d_task);
    task_suspend_disk(taskExec);
    sem_up(&disk_task_sem);
    sem_up(&disk_mgr_sem);
    task_switch(taskDisp);

    return 0;
}

int disk_block_write(int block, void *buffer) {
    diskrequest_t* d_task = (diskrequest_t*) malloc(sizeof(diskrequest_t));
    d_task->task = taskExec;
    d_task->buffer = buffer;
    d_task->op = DISK_CMD_WRITE;
    d_task->launch_time = systime();
    d_task->block = block;
    d_task->next = NULL;
    d_task->prev = NULL;

    sem_down(&disk_task_sem);
    append_disk_task(d_task);
    task_suspend_disk(taskExec);
    sem_up(&disk_task_sem);
    sem_up(&disk_mgr_sem);
    task_switch(taskDisp);

    return 0;
}

// Configuração do manipulador de sinais
int disk_sig_handler_setup() {
    disk_sig.sa_handler = disk_sig_handler;
    sigemptyset(&disk_sig.sa_mask);
    disk_sig.sa_flags = 0;

    if (sigaction(SIGUSR1, &disk_sig, 0) < 0) return -1;
    return 0;
}

void disk_sig_handler() {
    disk_sig_flag = 1;
    sem_up(&disk_mgr_sem);
}

// Funções auxiliares para manipulação de filas
void append_disk_task(diskrequest_t* task) {
    if (disk_task_queue == NULL) {
        disk_task_queue = task;
        return;
    }

    diskrequest_t* it = disk_task_queue;
    while (it->next != NULL) it = it->next;
    it->next = task;
    task->prev = it;
}

void task_suspend_disk(task_t* task) {
    if (disk_suspended_queue == NULL) {
        disk_suspended_queue = task;
        return;
    }

    task_t* it = disk_suspended_queue;
    while (it->next != NULL) it = it->next;
    it->next = task;
    task->prev = it;
}

task_t* pop_suspend_queue() {
    if (disk_suspended_queue == NULL) return NULL;

    task_t* popped = disk_suspended_queue;
    disk_suspended_queue = disk_suspended_queue->next;
    if (disk_suspended_queue != NULL) disk_suspended_queue->prev = NULL;
    return popped;
}

diskrequest_t* pop_disk_queue() {
    if (disk_task_queue == NULL) return NULL;

    diskrequest_t* popped = disk_task_queue;
    disk_task_queue = disk_task_queue->next;
    if (disk_task_queue != NULL) disk_task_queue->prev = NULL;
    return popped;
}

void disk_append_ready_queue(task_t* task) {
    if (readyQueue == NULL) {
        readyQueue = task;
        readyQueue->next = readyQueue;
        readyQueue->prev = readyQueue;
        return;
    }

    task_t* last = readyQueue->prev;
    last->next = task;
    task->prev = last;
    readyQueue->prev = task;
    task->next = readyQueue;
}

// Implementações dos algoritmos de escalonamento
diskrequest_t* fcfs_disk_scheduler(diskrequest_t* disk_task_queue) {
    if (disk_task_queue != NULL) {
        PPOS_PREEMPT_DISABLE;
        diskrequest_t* request = disk_task_queue;
        PPOS_PREEMPT_ENABLE;
        return request;
    }
    return NULL;
}

diskrequest_t* sstf_disk_scheduler(diskrequest_t* disk_task_queue) {
    if (disk_task_queue == NULL) return NULL;

    diskrequest_t* selected = disk_task_queue;
    diskrequest_t* it = disk_task_queue->next;
    int distance = abs(selected->block - disk.head_position);

    while (it != NULL) {
        if (abs(it->block - disk.head_position) < distance) {
            selected = it;
            distance = abs(it->block - disk.head_position);
        }
        it = it->next;
    }

    return selected;
}

diskrequest_t* cscan_disk_scheduler(diskrequest_t* disk_task_queue) {
    if (disk_task_queue == NULL) return NULL;

    diskrequest_t* selected = NULL;
    int distance = disk.num_blocks + 100;
    diskrequest_t* it = disk_task_queue;

    while (it != NULL) {
        if (it->block - disk.head_position < distance && it->block > disk.head_position) {
            selected = it;
            distance = it->block - disk.head_position;
        }
        it = it->next;
    }

    if (selected != NULL) return selected;

    selected = disk_task_queue;
    it = disk_task_queue->next;
    while (it != NULL) {
        if (it->block < selected->block) selected = it;
        it = it->next;
    }

    return selected;
}

// Funções auxiliares para remoção de nós das filas
task_t* remove_node_suspended(task_t* task) {
    if (task == NULL) return NULL;

    task_t* next = task->next;
    task_t* prev = task->prev;

    if (task == disk_suspended_queue) disk_suspended_queue = next;
    if (prev != NULL) prev->next = next;
    if (next != NULL) next->prev = prev;

    task->next = NULL;
    task->prev = NULL;
    return task;
}

diskrequest_t* remove_node_disk(diskrequest_t* task) {
    if (task == NULL) return NULL;

    diskrequest_t* next = task->next;
    diskrequest_t* prev = task->prev;

    if (task == disk_task_queue) disk_task_queue = next;
    if (prev != NULL) prev->next = next;
    if (next != NULL) next->prev = prev;

    task->next = NULL;
    task->prev = NULL;
    return task;
}