#include <signal.h>
#include <errno.h>
#include <string.h>
#include "ppos.h"
#include "ppos-core-globals.h"
#include "disk-driver.h"
#include "ppos-disk-manager.h"

// adicione todas as variaveis globais necessarias para implementar o gerenciado do disco



void bodyDiskManager(void* arg);
void diskSignalHandler();

// função para o tratamento de erros dos sinais - usada em disk_mgr_init()
void 
clean_exit_on_sig(int sig_num)
{
    printf ("\n ERROR[Signal = %d]: %d \"%s\"", sig_num, errno, strerror(errno));
    exit(errno);
}

int disk_mgr_init (int *numBlocks, int *blockSize) {

    // coloque o codigo para inicializar o disco aqui

    // o seu codigo deve terminar ate aqui. 
    // As proximas linhas dessa função não devem ser modificadas
    signal(SIGSEGV, clean_exit_on_sig);

    return 0;
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
diskrequest_t* disk_scheduler(diskrequest_t* queue) {
     // FCFS scheduler
    if ( queue != NULL ) {
        PPOS_PREEMPT_DISABLE
        diskrequest_t* request = queue;
        PPOS_PREEMPT_ENABLE
        return request;
    }
    return NULL;
}