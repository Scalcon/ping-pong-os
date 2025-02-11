#include <signal.h>
#include <errno.h>
#include <string.h>
#include "ppos.h"
#include "ppos-core-globals.h"
#include "disk-driver.h"
#include "ppos-disk-manager.h"


// adicione todas as variaveis globais necessarias para implementar o gerenciado do disco

semaphore_t disk_mgr_sem;
semaphore_t disk_task_sem;

task_t* disk_suspended_queue;
disk_task_t* disk_task_queue;
disk_task_t* current_disk_task;

task_t disk_mgr_task;
disk_t disk;

int sig_comp_flag;

diskrequest_t* fcfs_disk_scheduler(diskrequest_t* queue);
disk_task_t* remove_node(next_task);
void bodyDiskManager(void* arg);
void diskSignalHandler();

// função para o tratamento de erros dos sinais - usada em disk_mgr_init()
void clean_exit_on_sig(int sig_num) {
    printf ("\n ERROR[Signal = %d]: %d \"%s\"", sig_num, errno, strerror(errno));
    exit(errno);
}

int disk_mgr_init (int *numBlocks, int *blockSize) {
    if (*numBlocks <= 0 || *blockSize <= 0) {
        return -1;
    }

    PPOS_PREEMPT_DISABLE;

    if (sem_create(&disk_mgr_sem, 0) != 0) {
        return -1;
    }
    if (sem_create(&disk_task_sem, 0) != 0) {
        return -1;
    }    

    disk_suspended_queue = NULL;
    disk_task_queue = NULL;
    // start disk manager task
    if(task_create(&disk_mgr_task, bodyDiskManager, NULL) < 0){
        return -1;
    }
    disk_mgr_task.sys_task = 1; // system task, so it will not be preempted

    //Intialize disk struct
     disk.num_blocks = disk_cmd(DISK_CMD_DISKSIZE, 0, 0);
     disk.block_size = disk_cmd(DISK_CMD_BLOCKSIZE, 0, 0);
     disk.total_time = 0;
     disk.total_steps = 0;
     disk.head_position = 0;
   
    *numBlocks = disk.num_blocks;
    *blockSize = disk.block_size;
    // As proximas linhas dessa função não devem ser modificadas
    signal(SIGSEGV, clean_exit_on_sig);

    return 0;
}

void bodyDiskManager(void* arg) {
    while (1) {
        sem_down(&disk_mgr_sem);

        //todo: implementar disk operation completion
        // The completed task is moved from disk_suspended_queue to the ready queue.
        // Disk statistics (head movement, time) are updated.


        // check if disk is idle
        int disk_idle = disk_cmd(DISK_CMD_STATUS, 0 ,0) == DISK_STATUS_IDLE;
        // start next task
        if (disk_idle && disk_task_queue != NULL) {
            disk_task_t* next_task = fcfs_disk_scheduler(&disk_task_queue);

            if(disk_cmd(next_task->operation, next_task->block, next_task->buffer) >= 0){
                current_disk_task = remove_node(next_task);
                current_disk_task->start_time = systime();
              }

        }

        //release control over disk
        sem_up(&disk_task_sem);

        //suspend disk_manager until a task raises the semaphore
        sem_down(&disk_mgr_sem);
    }
}

int disk_block_read(int block, void* buffer) {
    

    return 0;
}

int disk_block_write(int block, void* buffer) {
    

    return 0;
}


// Essa função implemeneta o escalonador de requisicoes de 
// leitura/scrita do disco usado pelo gerenciador do disco
// A função implementa a política FCFS.
diskrequest_t* fcfs_disk_scheduler(diskrequest_t* queue) {
     // FCFS scheduler
    if ( queue != NULL ) {
        PPOS_PREEMPT_DISABLE
        diskrequest_t* request = queue;
        PPOS_PREEMPT_ENABLE
        return request;
    }
    return NULL;
}

disk_task_t* remove_node(disk_task_t* task){
    if(task == NULL){
      return NULL;
    }
  
    disk_task_t* next = task->next;
    disk_task_t* prev = task->prev;
  
    if(task == disk_task_queue){
      disk_task_queue = next;
    }
  
    if(prev != NULL){
      prev->next = next;
    }
    if(next != NULL){
      next->prev = prev;
    }
    
    task->next = NULL;
    task->prev = NULL;
  
    return task;
  }