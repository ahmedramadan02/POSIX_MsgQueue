/********************************************************************************
Main.c is the main file to implement the application 
The main purpose is to use SYSV IPC Msg Queues, pthreads, POSIX timers and signals 
Current Version: V2.0
History:
V1.0: Initial application version
V2.0: 
1. Resolved BUG_01: parent is not working
2. Resolved BUG_02: Ctrl-C interrupt handling for safe termination
3. little refactoring
*********************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <error.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <fcntl.h>

#define SYSV_IPC
#include "my_types.h"
#include "sysv_msg.h"

//Set by the signal handler for safe terminaton
uint8_t terminationflag = 0; 
static void terminationHanlder(int, siginfo_t*, void*);

static pid_t childPid, parentPid;
static timer_t timerId;
static unsigned int CurrentLifeTime = 0;
static unsigned int ChildAge = 0;

//File stuff
static const char* logfile = "parentLogs.txt";
static int fd;//log file descriptor
static int WriteLogToFile(const char* log, int _fd);
static int openfile();

//two messages and for all !
int msgid; 
int msgid_ack;

//Processes
void ChildProcess();
void ParentProcess();

//Parent stuff
//the timer expiry notification function
void ptimer_handler(int, siginfo_t*, void*, timer_t);
timer_t setupTimer(int,int,int);
void setAction(int, struct sigaction);
static void CleanUp();

//Child stuff
void cthread_handler(void*);

int main(int argc, char** argv){
    
    //BUG_02: Global signal action for terminating and cleanup
    //Note: Don't use signal() it might not be portable
    //One exit point
    struct sigaction tAction;
    sigemptyset(&tAction.sa_mask);
	tAction.sa_flags = SA_SIGINFO;
	tAction.sa_sigaction = terminationHanlder;
    setAction(SIGINT, tAction);

    //Init msg queues
    //REQ_GEN_003
    msgid = initMsgQueue("projfile01", 65);
    msgid_ack = initMsgQueue("projfile02", 66);

    //REQ_GEN_001
    switch (fork()) {
        case -1:
            printf("Error with fork()");
        case 0:
            //REQ_GEN_002
            ChildProcess();

        default:
            //open log file
            fd = openfile();

            //REQ_GEN_002
            //Process
            ParentProcess();

            //close
            close(fd);
            printf("File closed");
    }

    rmMsgQueue(msgid);
    rmMsgQueue(msgid_ack);
    return 0;
}

void ChildProcess(){
    msg_t message, message_ack;
    childPid = getpid();

    printf("The child process msgid id: %d\n", childPid);
    
    pthread_t cthreadId;

    //No thread args as for the moment
    //REQ_CHI_001
    pthread_create(&cthreadId,NULL,cthread_handler, NULL);

    //Prepare the message
    while(!terminationflag){
        // wait for messages
        //REQ_CHI_004
        recvMsg(&message, msgid);
        printf("child recvd: %s with global age: %d coming from pid: %d\n", 
                    message.data.msgText
                    ,message.data.lifeCounter
                    ,message.data.currentProcessID);

        //ACK 
        //TODO: Check if one message queue only with msgtype changed
        message_ack.mtype = 2;
        message_ack.data.lifeCounter = ChildAge;
        message_ack.data.currentProcessID = getpid();
        strcpy(message_ack.data.msgText, "Greetings back from the child! I’m getting older!");
        //BUG_01 Was here !: Child was sending to itself, making the parent stuck !
        //Previous: sendMsg(&message_ack, msgid);
        sendMsg(&message_ack, msgid_ack);

        sleep(1);
    }

    //Terminate
    printf("Terminating child\n");
    pthread_join(cthreadId, NULL);
    exit(EXIT_SUCCESS);
}

void ParentProcess(){
    parentPid = getpid();

    printf("The parent process id: %d\n", parentPid);

    //timer thread firing setup
    struct sigaction timerAction;

	sigemptyset(&timerAction.sa_mask);
	timerAction.sa_flags = SA_SIGINFO;
	timerAction.sa_sigaction = ptimer_handler;
    setAction(SIGRTMAX, timerAction);

    //REQ_PAR_001 - Periodic timer every 1 sec
    timerId = setupTimer(SIGRTMAX, 1000, 1);

    while(!terminationflag);

    //Terminate
    printf("Terminating parent\n");
	clear_timer(timerId);
    close(fd);
    exit(EXIT_SUCCESS);
}

void cthread_handler(void* tparams){
    pthread_t tid = pthread_self();

    while(!terminationflag){
        //REQ_CHI_002
        //printf might cause problems while printing, not thread safe as it references stdout FILE* each time w/o sync
        //as per posix, use flockfile(stdout) funlockfile(stdout) can be used only from one process
        //TODO: use POSIX semaphores if needed
        printf("%u: Child is still alive!\n", pthread_self());

        //REQ_CHI_003
        ChildAge++;
        sleep(2);
    }
}

//REQ_PAR_002
void ptimer_handler(int _signalType, siginfo_t* info, void* context, timer_t _timerId){
    if (_signalType == SIGRTMAX) {
		//Send the greating msg to the child
        msg_t message;

        message.mtype = 1; //per the man pages, this should be > 0
        strcpy(message.data.msgText, "Greetings from parent");   
        message.data.lifeCounter = CurrentLifeTime;
        message.data.currentProcessID = getpid();
        //REQ_PAR_004
        sendMsg(&message, msgid);
        //REQ_PAR_003
        CurrentLifeTime++;

        //Read ACK if there, if there are messages
        //In SYSV IPC, no notify functions ?, check again
        //We can implement with using mq_* posix apis
        // wait for messages
        msg_t msg_ack;

        //REQ_PAR_005
        recvMsg(&msg_ack, msgid_ack);
        printf("parent recvd: %s with global age: %d coming from pid: %d\n", 
                    msg_ack.data.msgText
                    ,msg_ack.data.lifeCounter
                    ,msg_ack.data.currentProcessID);

        //Log to file, file is written in parent only, no sync needed
        WriteLogToFile(msg_ack.data.msgText,fd);

	}
}

timer_t setupTimer(int signo, int msec, int mode){
    struct sigevent sigev;
	timer_t timerid;
	struct itimerspec itval;
	struct itimerspec oitval;

	// Create the POSIX timer to generate signo
	sigev.sigev_notify = SIGEV_SIGNAL;
	sigev.sigev_signo = signo;
	sigev.sigev_value.sival_ptr = &timerid;

	if (timer_create(CLOCK_REALTIME, &sigev, &timerid) == 0) {
		itval.it_value.tv_sec = msec / 1000;
		itval.it_value.tv_nsec = (long)(msec % 1000) * (1000000L);

        //if continous mode or not
		if (mode == 1) {
			itval.it_interval.tv_sec = itval.it_value.tv_sec;
			itval.it_interval.tv_nsec = itval.it_value.tv_nsec;
		} else {
			itval.it_interval.tv_sec = 0;
			itval.it_interval.tv_nsec = 0;
		}

		if (timer_settime(timerid, 0, &itval, &oitval) != 0) {
			perror("time_settime error!");
		}
	} else {
		perror("timer_create error!");
		return (timer_t)-1;
	}

	return timerid;
}

void setAction(int _signalType, struct sigaction _action){
	// set action to catch a signal (timer, interrupt)
	if (sigaction(_signalType, &_action, NULL) == -1) {
		perror("sigaction failed");
		return -1;
	}
}

void clear_timer(timer_t _timerid){
    timer_delete(_timerid);
}

static int openfile(){
    int fd = open(logfile, O_RDWR | O_CREAT, 0777);
    if(fd == -1){
        perror("file open failure");
        exit(EXIT_FAILURE);
    }else{
        printf("File opened with fd: %d\n", fd);
    }

    return fd;
}

static int WriteLogToFile(const char* log, int _fd){
    ssize_t sz = write(fd, log, strlen(log));
    //close(fd);
    if (sz == -1){
        perror("file write failure");
        exit(EXIT_FAILURE);
    }else{
        
    }

    return sz;
}

static void terminationHanlder(int signo, siginfo_t* info, void* args){
    printf("Received termination interrupt\n");
    CleanUp();//Cleanup hook
    // Broadcast termination: You can kill the processes, 
    //using the flag for every process to release its resources
    terminationflag = 1;
}

static void CleanUp(){

}