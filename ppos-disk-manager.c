#include<signal.h>

#include "ppos.h"
#include "ppos-disk-manager.h"
#include "ppos-core-globals.h"
#include "ppos-data.h"
#include "disk-driver.h"

// #define DISK_BLOCK_SIZE 64
// #define DISK_SIZE 256

//============================= STRUCTS =================================== // 


//============================= GLOBALS =================================== // 

task_t* disk_suspended_queue;
diskrequest_t* disk_task_queue;
diskrequest_t* current_disk_task;

semaphore_t disk_mgr_sem;
semaphore_t disk_task_sem;

task_t disk_mgr_task;

disk_t disk;

struct sigaction disk_sig;
int disk_sig_flag;

//============================= FUNCTION DEFINITIONS =================================== // 

void append_disk_task(diskrequest_t* task);

void disk_append_ready_queue(task_t* task);

void task_suspend_disk(task_t* task);

task_t* pop_suspend_queue();

diskrequest_t* pop_disk_queue();

task_t* remove_node_suspended(task_t* task);

diskrequest_t* remove_node_disk(diskrequest_t* task);

diskrequest_t* sstf_disk_scheduler();

diskrequest_t* cscan_disk_scheduler();

diskrequest_t* fcfs_disk_scheduler();

//============================= FUNCTION IMPLEMENTATION =================================== // 

void disk_manager(void* args){

  while(1){
    //take exclusive control over disk task queue semaphore 
    sem_down(&disk_task_sem);
    
    //if a disk operation was completed
    if(disk_sig_flag){
      //remove the current_task_owner from the disk_suspended_queue 
      task_t* ready_task = remove_node_suspended(current_disk_task->task);

      //wake up task
      disk_append_ready_queue(ready_task);
      disk_sig_flag = 0;

      //get stats 
      disk.total_steps += abs(disk.head_position - current_disk_task->block);
      disk.total_time += systime() - current_disk_task->start_time;
      disk.head_position = current_disk_task->block;
      
      //dealocate current_disk_task 
      free(current_disk_task);
    }
    
    int disk_idle = disk_cmd(DISK_CMD_STATUS, 0 ,0) == DISK_STATUS_IDLE;
    if(disk_idle && disk_task_queue != NULL){
      diskrequest_t* next_task = cscan_disk_scheduler();

      //launch next disk task
      if(disk_cmd(next_task->op, next_task->block, next_task->buffer) >= 0){
        //if task was launched, remove task from queue
        //and assign it to the current_disk_task
        current_disk_task = remove_node_disk(next_task);
        current_disk_task->start_time = systime();
      }
    }
    
    //release control over disk
    sem_up(&disk_task_sem);

    //suspend disk_manager until a task raises the semaphore
    sem_down(&disk_mgr_sem);
   
  }
}


int disk_mgr_init(int *numblocks, int *blockSize){

  //initiate disk manager
  //1- disk_task_queue
  //2- disk_semaphore
  //3- disk_manager_semaphore
  //4- init disk
  
  PPOS_PREEMPT_DISABLE;

  //disk_suspended_queue
  disk_suspended_queue = NULL;
  disk_task_queue = NULL;
  disk_sig_flag = 0;

  //create disk_mgr_semaphore
  if (sem_create(&disk_mgr_sem, 0) < 0){return -1;}
  
  //create disk_task_sem
  if(sem_create(&disk_task_sem, 1) < 0){return -1;}
  
  //setup signal handler
  if(disk_sig_handler_setup() < 0){ return -1;} 
  
  //launch disk_manager task
  if(task_create(&disk_mgr_task, disk_manager, NULL) < 0){return -1;}
  //set disk_manager as a system task so it cannot be preempted
  disk_mgr_task.sys_task = 1;

  //init disk 
  if (disk_cmd(DISK_CMD_INIT, 0, 0) < 0){return -1;};

  //Intialize disk struct
  disk.num_blocks = disk_cmd(DISK_CMD_DISKSIZE, 0, 0);
  disk.block_size = disk_cmd(DISK_CMD_BLOCKSIZE, 0, 0);
  disk.total_time = 0;
  disk.total_steps = 0;
  disk.head_position = 0;
   
  *numblocks = disk.num_blocks;
  *blockSize = disk.block_size;
  
  PPOS_PREEMPT_ENABLE;
  
  //return operation status
  return 0;
}

int disk_block_read(int block, void *buffer){
  //create disk_task
  diskrequest_t* d_task = (diskrequest_t*) malloc(sizeof(diskrequest_t));
  
  //set disk task values
  d_task->task = taskExec;
  d_task->buffer = buffer;
  d_task->op = DISK_CMD_READ;
  d_task->launch_time = systime();
  d_task->block = block;
  d_task->next = NULL;
  d_task->prev = NULL;
  
  //obtains disk task queue semaphore 
  sem_down(&disk_task_sem);

  //Appends disk read task to disk task queue 
  append_disk_task(d_task);
  
  //suspends task until disk block is read
  task_suspend_disk(taskExec);
  
  //realeses disk queue semaphore 
  sem_up(&disk_task_sem);

  //call disk_manager task
  sem_up(&disk_mgr_sem); 
  
  task_switch(taskDisp);

  //return status of operation
  return 0;
}

int disk_block_write(int block, void *buffer){
  //create disk_task
  diskrequest_t* d_task = (diskrequest_t*) malloc(sizeof(diskrequest_t));
  
  //set disk task values
  d_task->task = taskExec;
  d_task->buffer = buffer;
  d_task->op = DISK_CMD_WRITE;
  d_task->launch_time = systime();
  d_task->block = block;
  d_task->next = NULL;
  d_task->prev = NULL;

  //obtains disk task queue semaphore 
  sem_down(&disk_task_sem);
  
  //Appends disk read task to disk task queue 
  append_disk_task(d_task);
  
  //suspends task until disk block is read
  task_suspend_disk(taskExec);
  
  //realeses disk queue semaphore 
  sem_up(&disk_task_sem);
  
  //call disk_manager task
  sem_up(&disk_mgr_sem); 

  task_switch(taskDisp);
  
  //return operation status
  return 0;
}

int disk_sig_handler_setup(){
  disk_sig.sa_handler = disk_sig_handler;
  sigemptyset(&disk_sig.sa_mask);
  disk_sig.sa_flags = 0;
  
  if(sigaction(SIGUSR1, &disk_sig, 0) < 0){return -1;}
  return 0;
}

void disk_sig_handler(){
  //raise signal flag
  disk_sig_flag = 1;

  //wake disk_manager
  sem_up(&disk_mgr_sem);
}

void append_disk_task(diskrequest_t* task){
  if (disk_task_queue == NULL){
    disk_task_queue = task;
    return;
  }
  
  diskrequest_t* it = disk_task_queue;
  while(it->next != NULL){
    it = it->next;
  }

  it->next = task;
  task->prev = it;
}

void task_suspend_disk(task_t* task){
  if(disk_suspended_queue == NULL){
    disk_suspended_queue = task;
    return;
  }
  
  task_t* it = disk_suspended_queue;
  while(it->next != NULL){
    it = it->next;
  }

  it->next = task;
  task->prev = it;
}

task_t* pop_suspend_queue(){
  if(disk_suspended_queue == NULL){
    return NULL;
  }

  task_t* popped = disk_suspended_queue;
  disk_suspended_queue = disk_suspended_queue->next;
  
  if(disk_suspended_queue != NULL){
    disk_suspended_queue->prev = NULL;
  }
  
  return popped;
}

diskrequest_t* pop_disk_queue(){
  if(disk_task_queue == NULL){
    return NULL;
  }

  diskrequest_t* popped = disk_task_queue;
  disk_task_queue = disk_task_queue->next;
  
  if (disk_task_queue != NULL){
    disk_task_queue->prev = NULL;
  }

  return popped;
}

void disk_append_ready_queue(task_t* task){
  if(readyQueue == NULL){
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


//============================= SCHEDULERS IMPLEMENTATION =================================== //

diskrequest_t* fcfs_disk_scheduler(){
  return disk_task_queue;
}

diskrequest_t* sstf_disk_scheduler(){
  if (disk_task_queue == NULL){
    return NULL;
  }

  diskrequest_t* selected = disk_task_queue;
  diskrequest_t* it = disk_task_queue->next;

  int distance = abs(selected->block - disk.head_position);
 
  //find the request with the closest block
  while(it != NULL){
    if(abs(it->block - disk.head_position) < distance){
      selected = it;
      distance = abs(it->block - disk.head_position);
    }
    it = it->next;
  }

  return selected;
}

diskrequest_t* cscan_disk_scheduler(){
  if(disk_task_queue == NULL){
    return NULL;
  }
  
  diskrequest_t* selected = NULL;
  int distance = disk.num_blocks + 100;

  diskrequest_t* it = disk_task_queue;
  
  //search for the request closest to the disk's head such that
  // request->block > disk.head_position
  while(it != NULL){
    if(it->block - disk.head_position < distance && it->block > disk.head_position){
      selected = it;
      distance = it->block - disk.head_position;
    }
    it = it->next;
  }
  
  //if there is such a request, return it...
  if (selected != NULL){
    return selected;
  }

  //else, find the request whose block is the closest to the disk's beginning
  selected = disk_task_queue;
  it = disk_task_queue->next;
  while(it != NULL){
    if(it->block < selected->block){
      selected = it;
      distance = it->block;
    }
    it = it->next;
  }
  
  return selected;
}

//============================= AUX FUNCTIONS =================================== //

task_t* remove_node_suspended(task_t* task){
  if(task == NULL){
    return NULL;
  }

  task_t* next = task->next;
  task_t* prev = task->prev;

  //task is the list's head
  if(task == disk_suspended_queue){
    disk_suspended_queue = next;
  }

  if(prev != NULL){
    prev->next = next;
  }
  if(next != NULL){
    next->prev = prev;
  }
  
  //avoid dangling pointers
  task->next = NULL;
  task->prev = NULL;

  return task;
}

diskrequest_t* remove_node_disk(diskrequest_t* task){
  if(task == NULL){
    return NULL;
  }

  diskrequest_t* next = task->next;
  diskrequest_t* prev = task->prev;

  //task is the list's head
  if(task == disk_task_queue){
    disk_task_queue = next;
  }

  if(prev != NULL){
    prev->next = next;
  }
  if(next != NULL){
    next->prev = prev;
  }
  
  //avoid dangling pointers
  task->next = NULL;
  task->prev = NULL;

  return task;
}


