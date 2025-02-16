#include "ppos.h"
#include "ppos-core-globals.h"
#include "disk-driver.h"
#include "ppos-disk-manager.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>

/* ============================================================
 * Variáveis Globais do Gerenciador de Disco
   ============================================================
 */

// Filas de requisições e tarefas suspensas 
static task_t *disk_suspended_queue = NULL;
static diskrequest_t *disk_task_queue = NULL;
static diskrequest_t *current_disk_task = NULL;

// Semáforos para sincronização 
static semaphore_t disk_mgr_sem;
static semaphore_t disk_task_sem;

// Tarefa do gerenciador de disco 
static task_t disk_mgr_task;

// Informações do disco 
static disk_t disk;

// Controle de sinais 
static struct sigaction disk_sig;
static volatile int disk_sig_flag = 0;  // Volatile pois é modificada no handler 

/* ============================================================
 * Protótipos de Funções Auxiliares
   ============================================================
 */

// Manipulação de filas 
static void append_disk_task(diskrequest_t *request);
static void add_task_to_suspended_queue(task_t *task);
static void add_task_to_ready_queue(task_t *task);
static task_t *remove_task_from_suspended_queue(task_t *task);
static diskrequest_t *remove_disk_request(diskrequest_t *request);

// Manipulação de sinais 
static int setup_disk_signal_handler(void);
static void disk_signal_handler(int signum);

// Função auxiliar para criar requisição de disco 
static diskrequest_t *create_disk_request(int op, int block, void *buffer, task_t *task);

// Escalonadores
static diskrequest_t *fcfs_scheduler(diskrequest_t *queue);
static diskrequest_t *sstf_scheduler(diskrequest_t *queue);
static diskrequest_t *cscan_scheduler(diskrequest_t *queue);

// Função comum para operações de leitura e escrita 
static int disk_block_operation(int op, int block, void *buffer);

/* ============================================================
 * Função de Tratamento de Sinais de Erro
   ============================================================
 */

void clean_exit_on_sig(int sig_num) {
    printf("\n ERROR[Signal = %d]: %d \"%s\"\n", sig_num, errno, strerror(errno));
    exit(errno);
}

/* ============================================================
 * Função Principal do Gerenciador de Disco
   ============================================================
 */

void disk_manager(void *args) {
    (void) args;  // Parâmetro não utilizado 

    while (1) {
        sem_down(&disk_task_sem);

        // Se o sinal do disco foi recebido, conclui a tarefa corrente 
        if (disk_sig_flag) {
            task_t *ready_task = remove_task_from_suspended_queue(current_disk_task->task);
            add_task_to_ready_queue(ready_task);

            disk_sig_flag = 0;

            disk.total_steps += abs(disk.head_position - current_disk_task->block);
            disk.total_time += systime() - current_disk_task->start_time;
            disk.head_position = current_disk_task->block;

            free(current_disk_task);
            current_disk_task = NULL;
        }

        // Se o disco está ocioso e há requisições pendentes 
        int disk_idle = (disk_cmd(DISK_CMD_STATUS, 0, 0) == DISK_STATUS_IDLE);
        if (disk_idle && disk_task_queue != NULL) {
            // * Pode-se alternar entre os escalonadores: fcfs, sstf, cscan ==================================================================// 
            diskrequest_t *next_request = fcfs_scheduler(disk_task_queue); 
            if (disk_cmd(next_request->op, next_request->block, next_request->buffer) >= 0) {
                current_disk_task = remove_disk_request(next_request);
                current_disk_task->start_time = systime();
            }
        }

        sem_up(&disk_task_sem);
        sem_down(&disk_mgr_sem);
    }
}

/* ============================================================
 * Inicialização do Gerenciador de Disco
   ============================================================
 */

int disk_mgr_init(int *numblocks, int *blockSize) {
    PPOS_PREEMPT_DISABLE;

    disk_suspended_queue = NULL;
    disk_task_queue = NULL;
    disk_sig_flag = 0;

    if (sem_create(&disk_mgr_sem, 0) < 0) {
        PPOS_PREEMPT_ENABLE;
        return -1;
    }
    if (sem_create(&disk_task_sem, 1) < 0) {
        PPOS_PREEMPT_ENABLE;
        return -1;
    }
    if (setup_disk_signal_handler() < 0) {
        PPOS_PREEMPT_ENABLE;
        return -1;
    }

    if (task_create(&disk_mgr_task, disk_manager, NULL) < 0) {
        PPOS_PREEMPT_ENABLE;
        return -1;
    }

    if (disk_cmd(DISK_CMD_INIT, 0, 0) < 0) {
        PPOS_PREEMPT_ENABLE;
        return -1;
    }

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

/* ============================================================
 * Operações de Leitura e Escrita de Blocos no Disco
   ============================================================
 */

/* Função comum para operações de leitura e escrita */
static int disk_block_operation(int op, int block, void *buffer) {
    task_t *current_task = taskExec;  /* Tarefa corrente */

    diskrequest_t *request = create_disk_request(op, block, buffer, current_task);

    sem_down(&disk_task_sem);
    append_disk_task(request);
    add_task_to_suspended_queue(current_task);
    sem_up(&disk_task_sem);

    sem_up(&disk_mgr_sem);
    task_switch(taskDisp);

    return 0;
}

int disk_block_read(int block, void *buffer) {
    return disk_block_operation(DISK_CMD_READ, block, buffer);
}

int disk_block_write(int block, void *buffer) {
    return disk_block_operation(DISK_CMD_WRITE, block, buffer);
}

/* ============================================================
 * Configuração do Manipulador de Sinais
   ============================================================
 */

static int setup_disk_signal_handler(void) {
    disk_sig.sa_handler = disk_signal_handler;
    sigemptyset(&disk_sig.sa_mask);
    disk_sig.sa_flags = 0;

    if (sigaction(SIGUSR1, &disk_sig, NULL) < 0)
        return -1;
    return 0;
}

static void disk_signal_handler(int signum) {
    (void)signum;  // Evita warning de variável não utilizada 
    disk_sig_flag = 1;
    sem_up(&disk_mgr_sem);
}

/* ============================================================
 * Função Auxiliar para Criar Requisição de Disco
   ============================================================
 */

static diskrequest_t *create_disk_request(int op, int block, void *buffer, task_t *task) {
    diskrequest_t *request = (diskrequest_t *) malloc(sizeof(diskrequest_t));
    if (!request) {
        perror("Erro ao alocar memória para requisição de disco");
        exit(EXIT_FAILURE);
    }
    request->task = task;
    request->buffer = buffer;
    request->op = op;
    request->launch_time = systime();
    request->block = block;
    request->next = NULL;
    request->prev = NULL;
    return request;
}

/* ============================================================
 * Funções Auxiliares para Manipulação de Filas
   ============================================================
 */

// Adiciona uma requisição no final da fila de tarefas do disco 
static void append_disk_task(diskrequest_t *request) {
    if (disk_task_queue == NULL) {
        disk_task_queue = request;
    } else {
        diskrequest_t *curr = disk_task_queue;
        while (curr->next != NULL)
            curr = curr->next;
        curr->next = request;
        request->prev = curr;
    }
}

// Adiciona uma tarefa na fila de tarefas suspensas 
static void add_task_to_suspended_queue(task_t *task) {
    if (disk_suspended_queue == NULL) {
        disk_suspended_queue = task;
    } else {
        task_t *curr = disk_suspended_queue;
        while (curr->next != NULL)
            curr = curr->next;
        curr->next = task;
        task->prev = curr;
    }
}

// Adiciona uma tarefa na fila de prontos (readyQueue) 
static void add_task_to_ready_queue(task_t *task) {
    if (readyQueue == NULL) {
        readyQueue = task;
        readyQueue->next = readyQueue;
        readyQueue->prev = readyQueue;
    } else {
        task_t *last = readyQueue->prev;
        last->next = task;
        task->prev = last;
        task->next = readyQueue;
        readyQueue->prev = task;
    }
}

// Remove uma tarefa específica da fila de suspensas 
static task_t *remove_task_from_suspended_queue(task_t *task) {
    if (!task)
        return NULL;

    task_t *next = task->next;
    task_t *prev = task->prev;

    if (task == disk_suspended_queue)
        disk_suspended_queue = next;
    if (prev)
        prev->next = next;
    if (next)
        next->prev = prev;

    task->next = task->prev = NULL;
    return task;
}

// Remove uma requisição específica da fila de tarefas do disco 
static diskrequest_t *remove_disk_request(diskrequest_t *request) {
    if (!request)
        return NULL;

    diskrequest_t *next = request->next;
    diskrequest_t *prev = request->prev;

    if (request == disk_task_queue)
        disk_task_queue = next;
    if (prev)
        prev->next = next;
    if (next)
        next->prev = prev;

    request->next = request->prev = NULL;
    return request;
}

/* ============================================================
 * Escalonadores de Requisições para o Disco
   ============================================================
 */
static diskrequest_t* fcfs_scheduler(diskrequest_t* queue) {
    // FCFS scheduler
   if ( queue != NULL ) {
       PPOS_PREEMPT_DISABLE
       diskrequest_t* request = queue;
       PPOS_PREEMPT_ENABLE
       return request;
   }
   return NULL;
}

static diskrequest_t *sstf_scheduler(diskrequest_t *queue) {
    if (queue == NULL)
        return NULL;
    // SSTF scheduler
    diskrequest_t *selected = queue;
    int min_distance = abs(selected->block - disk.head_position);

    for (diskrequest_t *curr = queue->next; curr != NULL; curr = curr->next) {
        int distance = abs(curr->block - disk.head_position);
        if (distance < min_distance) {
            selected = curr;
            min_distance = distance;
        }
    }
    return selected;
}

static diskrequest_t *cscan_scheduler(diskrequest_t *queue) {
    if (queue == NULL)
        return NULL;
    //cscan scheduler
    diskrequest_t *selected = NULL;
    int min_distance = disk.num_blocks + 100; // Valor grande arbitrário

    for (diskrequest_t *curr = queue; curr != NULL; curr = curr->next) {
        if (curr->block > disk.head_position && (curr->block - disk.head_position) < min_distance) {
            selected = curr;
            min_distance = curr->block - disk.head_position;
        }
    }

    if (selected != NULL)
        return selected;

    /* Caso não haja requisição com bloco maior que a posição atual,
       seleciona a requisição com o menor bloco */
    selected = queue;
    for (diskrequest_t *curr = queue->next; curr != NULL; curr = curr->next) {
        if (curr->block < selected->block)
            selected = curr;
    }
    return selected;
}
