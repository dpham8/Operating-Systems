#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

void ctrlc_handler(int sig);

int status;
pid_t cpid, parent; /* PID is 0 if in child process, PID is 1 if in parent */ 

int pipefd[2];

int main(int argc, char *argv[]) {

    /* block all signals except SIGINT */
    sigset_t mask;
    sigfillset(&mask);          /* initially set all the flags */
    sigdelset(&mask, SIGINT);   /* unset SIGINT so it WON't be blocked */

 
    /* allocate a structure for the signal handler */
    struct sigaction sa;

    /* setup handler before the fork */ 
    sa.sa_handler = ctrlc_handler; /* Name of the signal handler function */
    sa.sa_flags = SA_RESTART;    /* restart system calls if interrupted */ 
    sigfillset(&sa.sa_mask);     /* mask all signals while in the handler */


    /* register the handler  */
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction: ");
        exit(1);
    }  

    parent = getpid();
    printf("Parent PID is %d \n", parent);
    
    while(1) /* while true */
    {      
        if(pipe(pipefd) < 0) {  /* call pipe() */
            perror("pipe: ");
            exit(EXIT_FAILURE);
        }   
        
        cpid = fork();
        
        if (cpid < 0) {
            perror("fork: ");
        }
        /* CHILD */
        else if (cpid == 0) {
            close(pipefd[1]);
            
            char buf[20];
            memset(buf, 0, sizeof(buf));
            read(pipefd[0], buf, sizeof(buf));
            printf("Recieved command '%s' \n",buf);
                
            if(strcmp(buf, "quit") == 0) {
                printf("Child exiting and sending special exit code to parent \n");
                exit(2);
            }
            else if (strcmp(buf, "list") == 0) {
                char *newargv[] = {"/bin/ls", "-al", ".", NULL};
                execve(newargv[0],newargv, NULL);
            }
            else if (strcmp(buf, "cat") == 0) {
                char buf[20];
                printf("Enter filename: ");
                scanf("%s", buf);
             
                char *newargv[] = {"/bin/cat", NULL, NULL};
                newargv[1] = buf;
                execve("/bin/cat", newargv, NULL);  
            }
            else {
                printf("Unrecognized command. \n");
                exit(1);
            }
        }   
        /* PARENT */
        else {   
            close(pipefd[0]); /* close read end */
            printf("Child PID is %d \n", cpid);
            
            char buf[20]; 
            printf("Enter command: ");
            scanf("%s", buf);
            /* memset(buf, 0, sizeof(buf)); */ 
            write(pipefd[1], buf, sizeof(buf));

            wait(&status);
            close(pipefd[1]);

            if (WIFSIGNALED(status)) { 
                printf("Child terminated by signal %d\n",WTERMSIG(status));
            }
            else {
                printf("Child exited with exit code %d\n",WEXITSTATUS(status));
            }
               
            if(WEXITSTATUS(status) == 2) {
                printf("Parent exiting. \n"); 
                break;
                exit(EXIT_SUCCESS);
            }   
        }    
    } 
}

/* HANDLER */
void ctrlc_handler(int sig) {
    /* CHILD */   
    if (cpid == 0) {
        printf("Child process exiting... \n");
        close(pipefd[0]); /* close the read end of pipe */
    }
    /* PARENT */
    else {
        printf("Parent process exiting. \n");
        /* Close the write end will send EOF to the child process */
        close(pipefd[1]); 
    }
    exit(0);
}

