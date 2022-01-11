#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/msg.h>

#define BLOCK_SIZE 80  /* Number of characters in one "block" */

int fib(int n); /* use fib to make one thread do some busy work to test synchronization */
void ctrlc_handler(int sig);

/* Create data structure for the message */
typedef struct { 
    long type; 
    char text[100];  
   } mq_msg;

union semun {
    int  val;
    struct semid_ds *buf;
    unsigned short  *array;
    struct seminfo  *__buf;
};

/* Structure to pass information to thread function */
typedef struct {
    int myid;   
} t_info;

/* thread functions */
void *consumer(void *); 
void *producer(void *); 

/* globals */
int semid;                /* semaphore ID */
int mqid;                 /* message queue ID */
int LIMIT = 5;            /* the number of loops in the functions */
int end = 0;              /* indicates if end of file has been reached */  
 
struct sembuf lockPSEM[1];     /* grabs producer */
struct sembuf unlockPSEM[1];  /* releases producer */
struct sembuf lockCSEM[5][1];     /* grabs consumer */
struct sembuf unlockCSEM[5][1];  /* releases consumer */

int main(int argc, char *argv[]) {     

    /* block all signals except SIGINT */
    sigset_t mask;
    sigfillset(&mask);          /* initially set all the flags */
    sigdelset(&mask, SIGINT);   /* unset SIGINT so it WON'T be blocked */

    /* allocate a structure for the signal handler */
    struct sigaction sa;

    sa.sa_handler = ctrlc_handler;  /* Name of the signal handler function */    
    sa.sa_flags = 0;    /* restart system calls if interrupted */ 
    sigfillset(&sa.sa_mask);     /* mask all signals while in the handler */
    
    /* register the handler  */
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction: ");
        exit(1);
    }

    /* allows user to change limit from the command line */
    if (argc == 2) {
        LIMIT = atoi(argv[1]);
    }

    /* get ipc key */
    char pathname[200];
    key_t ipckey;
    getcwd(pathname,200);
    strcat(pathname,"/foo");      
    ipckey = ftok(pathname, 10);   

    /* get a msg queue */
    int mqid = msgget(ipckey, IPC_CREAT | 0666);
    printf("Created message queue with ID: %d \n", mqid);
    if(mqid < 0) {
        perror("IPC msgget: ");
        exit(1); 
    }

    /* initialize semaphores */
    int nsems = 6;         
    semid = semget(ipckey, nsems, 0666 | IPC_CREAT); 
    printf("Created semaphore with ID: %d \n", semid);
    if (semid < 0) {
        perror("semget: ");
        exit(1);
    }
  
    /* initialize to release producer and block consumer */
    union semun init_pro;
    union semun init_con;

    init_pro.val = 1;
    if (semctl(semid, 5, SETVAL, init_pro) == -1) {
        perror("setting PSEM value: ");
        exit(1);
    }
    
    /* PSEM  */
    lockPSEM[0].sem_flg = SEM_UNDO;
    lockPSEM[0].sem_num = 5;  
    lockPSEM[0].sem_op = -1;  

    unlockPSEM[0].sem_flg = SEM_UNDO; 
    unlockPSEM[0].sem_num = 5;  
    unlockPSEM[0].sem_op = +1; 
    
    int a;
    a = semctl(semid, 5, GETVAL);
    printf("PSEM semaphore initial val:  %d \n", a);

    init_con.val = 0;
    int i;
    for (i=0; i < 5; i++) { 
        if (semctl(semid, i, SETVAL, init_con) == -1) {
            perror("setting CSEM value: ");
            exit(1);
        }
        
        /* CSEM */ 
        lockCSEM[i][0].sem_flg = SEM_UNDO;
        lockCSEM[i][0].sem_num = i;  
        lockCSEM[i][0].sem_op = -1; 

        unlockCSEM[i][0].sem_flg = SEM_UNDO; 
        unlockCSEM[i][0].sem_num = i;  
        unlockCSEM[i][0].sem_op = +1;  
    
        int b;
        b = semctl(semid, i, GETVAL);
        printf("CSEM semaphore %d initial val:  %d \n", i, b);
    }

    t_info data[5];
    for (i=0; i < 5; i++) { 
        data[i].myid = i;
    }
  
    int dummy = 0;   /* Threads need something passed to them, so use this */
 
    /* spawn producer thread */
    pthread_t pthr;  
    printf("Calling pthread_create for producer thread... \n");
    if (pthread_create( &pthr, NULL,  producer, (void *)&dummy) != 0) {
        perror("producer pthread_create: ");
        exit(1);
    }
    
    /* spawn consumer threads */
    pthread_t cthr[5];  
    for(i = 0; i < 5; i++) {
        printf("Calling pthread_create for consumer thread %d...\n", i); 
        if (pthread_create( &cthr[i], NULL,  consumer, (void *)&(data[i])) != 0) {
            perror("consumer pthread_create: ");
            exit(1);
        }
    } 
     
    /* join producer thread */
    if (pthread_join(pthr, NULL) < 0) {
        printf("Calling pthread_join for consummer thread %d... \n");
        perror("pthread_join:");
        exit(1);
    }

    /* join consumer threads */
    for(i = 0; i < 5; i++) {
        printf("Calling pthread_join for consumer thread %d... \n", i);
        if (pthread_join(cthr[i], NULL) < 0) {
            perror("pthread_join:");
            exit(1);
        }    
    }
  
    /* cleanup */   
    msgctl(mqid, IPC_RMID, NULL);
    semctl(semid, 0, IPC_RMID);

    return 0;
}

void ctrlc_handler(int sig) {
    printf("Cleaning up before terminating... \n");
    /* Delete message queue */
    if (mqid > 0) { 
        msgctl(mqid, IPC_RMID, NULL);
    }

    /* Delete semaphores */
    if (semid > 0) {
        semctl(semid, 0, IPC_RMID);
    }
    exit(1);
}

/*------------
 * CONSUMER  *
 *-----------*/
void *consumer(void *var) {
    int i, thrID;
    mq_msg mymsg; 
    FILE * outf;
 
    t_info *data;
    data = (t_info *)var;
    thrID = data->myid;

    pid_t tid = syscall(SYS_gettid);      
    printf("Consumer %d thread pid: %d tid: %d \n", thrID, getpid(), tid);

    char logf[256];
    printf("consumer %d opening log_offset%d for writing \n", thrID, thrID);
    sprintf(logf, "log_offset%d \n", thrID);
 
    outf = fopen("logf", "w");
    if (outf == NULL) {
        perror("output file: ");
        exit(1);
    }

    for (i = 0; i < LIMIT; i++) {   
        if (semop(semid, lockCSEM[thrID], 5) < 0) { 
            perror("semop lockCSEM:");       
        } 

        if (end == 1) {
            break;
        }
        
        printf("Consumer %d retrieving message from Producer. \n", thrID);
        printf("mymsg: %c \n", mymsg.text);
        if (msgrcv(mqid, &mymsg, sizeof(mymsg), 0, 0) < 0) {
            perror("IPC msgrcv: ");
        }     
        
        fib(20); /* busy wait */
        
        printf("Consumer %d signaling Producer. \n", thrID);  
        if (semop(semid, unlockPSEM, 1) < 0) { 
            perror("semop unlockPSEM:");  
        } 
            
        /* Write the data to the outfile */        
        fprintf(outf, "%s", mymsg.text);
        putc('\n',outf); 
    }
    fclose(outf); 
    pthread_exit(0);
}

/*-----------*
 * PRODUCER  *
 *-----------*/
void *producer(void *dummy) {
    int i, j, count, local_end = 0;
    char buf[BLOCK_SIZE];
    mq_msg mymsg;
    FILE * inf;
   
    /* Change the filename to read another file */
    printf("Producer opening poem for reading. \n");
    inf = fopen("poem", "r");
    if (inf == NULL ) {
        perror("input file: ");
        exit(1);
    }
  
    pid_t tid = syscall(SYS_gettid);      
    printf("Producer thread pid: %d tid: %d \n",getpid(),tid);

    for (i = 0; i < 5*LIMIT; i++) {
        if (local_end == 1) {
            end = 1;   
            for (j=0; j<5; j++) {
                printf("Signaling consumer %d to exit. \n", j);
                semop(semid, unlockCSEM[j], 5);
            }    
            break;
        }    
        
        count = 0;
        memset(buf, 0, BLOCK_SIZE);
    
        printf("Producer waiting on PSEM... \n");
        if (semop(semid, lockPSEM, 1) < 0) {  
            perror("semop lockPSEM:"); 
        }
        
        printf("Producer reading from input file. \n");     
        while (count < BLOCK_SIZE-1) {
            buf[count] = fgetc(inf);
            if( feof(inf) ) {
                local_end = 1;
                break;
            }
                
            if (buf[count] == '\n') {
                break;
            }    
            count++;
        }

        printf("count: Producer reads %d characters \n", count); 
        if (count > 1) {
            strncpy(mymsg.text, buf, count);
            mymsg.type = 1;
 
            if (msgsnd(mqid, &mymsg, sizeof(mymsg), 0) == -1) {
                perror("IPC msgsnd: ");
            }   
         
            fib(30); /* busy work */
         
            printf("Producer signaling Consumer %d to read line. \n", i%5);    
            if (semop(semid, unlockCSEM[i%5], 5) < 0) { 
                perror("semop unlockCSEM: "); 
            }       
        }     
    }
    printf("Ending Producer loop. \n");
    fclose(inf); 
    pthread_exit(0);
}

int fib(int n) {
    if (n < 2) return n;
    return fib(n-1) + fib(n-2);
}
