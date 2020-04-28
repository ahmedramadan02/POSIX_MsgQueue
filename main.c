#include <stdio.h>
#include <stdlib.h>
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

//TODO: Refactoring, then refactoring, then refactoring

static pid_t childPid, parentPid;
static timer_t timerId;
static CurrentLifeTime = 0;
static ChildAge = 0;

//File stuff
static const char* logfile = "parentLogs.txt";
int fd;//log file descriptor
static int WriteLogToFile(const char* log, int _fd);
static int openfile();

//This is accesses in the timer handler context
//TODO: check if we need a design change, and if this will be a shared resource 
//No need for sync mechanism, as it's been set at init and used, and it's const for all
int msgid; 

//Processes
void ChildProcess();
void ParentProcess();

//Parent stuff
//The thread function - the expiry notification function
void ptimer_handler(int, siginfo_t*, void*, timer_t);
timer_t setupTimer(int,int,int);
void setAction(int, struct sigaction);

//Child stuff
void cthread_handler(void*);

int main(int argc, char** argv){
    
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
    }

    return 0;
}

void ChildProcess(){
    msg_t message, message_ack;
    childPid = getpid();

    printf("The child process id: %d\n", childPid);
    //REQ_GEN_003
    int msgid = initMsgQueue("projfile01", 65);
    int msgid_ack = initMsgQueue("projfile02", 66);
    
    pthread_t cthreadId;
    //TODO: custom thread attributes here
    //No args as for the moment
    //REQ_CHI_001
    pthread_create(&cthreadId,NULL,cthread_handler, NULL);

    //Prepare the message
    while(1){
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
        message.data.lifeCounter = ChildAge;
        message.data.currentProcessID = getpid();
        strcpy(message.data.msgText, "Greetings back from the child! I’m getting older!");
        sendMsg(&message, msgid);

        sleep(1);
    }

    //Terminate
    rmMsgQueue(msgid);
    pthread_join(cthreadId, NULL);
    exit(0);
}

void ParentProcess(){
    parentPid = getpid();

    printf("The parent process id: %d\n", parentPid);
    //int msgid = initMsgQueue("projfile01", 65); 
    //Make it global to be used in the timer, handler
    //TODO: check if we need a design change, and if this will be a shared resource 
    msgid = initMsgQueue("projfile01", 65);

    //timer setup
    struct sigaction actionProperty;

	sigemptyset(&actionProperty.sa_mask);
	actionProperty.sa_flags = SA_SIGINFO;
	actionProperty.sa_sigaction = ptimer_handler;
    setAction(SIGRTMAX, actionProperty);
    setAction(SIGINT, actionProperty); //Set action for the timer termination

    //REQ_PAR_001 - Periodic timer every 1 sec
    timerId = setupTimer(SIGRTMAX, 1000, 1);

    while(1);

    exit(0);
}

void cthread_handler(void* tparams){
    pthread_t tid = pthread_self();
    //int ktid = gettid(); //Kernel thread id, it's not the same as returned from pthread_self();

    while(1){
        //TODO: to remove printf
        //REQ_CHI_002
        printf("%u: Child is still alive!\n", pthread_self());
        //REQ_CHI_003
        ChildAge++;
        sleep(2);
    }
}

//REQ_PAR_002
void ptimer_handler(int _signalType, siginfo_t* info, void* context, timer_t _timerId){
    //NOTE: Don't call prinf() here, it's not thread-safe
    if (_signalType == SIGRTMAX) {
		//Send messages here
        msg_t message;
        message.mtype = 1; //Per the man pages, this should be > 0
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
        int msgid_ack = initMsgQueue("projfile02", 66);
        msg_t msg_ack;

        //REQ_PAR_005
        recvMsg(&msg_ack, msgid_ack);
        printf("parent recvd: %s with global age: %d coming from pid: %d\n", 
                    msg_ack.data.msgText
                    ,msg_ack.data.lifeCounter
                    ,msg_ack.data.currentProcessID);

        //Log to file
        WriteLogToFile(msg_ack.data.msgText,fd);

	}
	else if (_signalType == SIGINT) {
		clear_timer(_timerId);
        close(fd);//TODO: TO MOVE THIS
		perror("Exit, program interrupted");
		exit(EXIT_FAILURE);
	}
}

timer_t setupTimer(int signo, int usec, int mode){
    struct sigevent sigev;
	timer_t timerid;
	struct itimerspec itval;
	struct itimerspec oitval;

	// Create the POSIX timer to generate signo
	sigev.sigev_notify = SIGEV_SIGNAL;
	sigev.sigev_signo = signo;
	sigev.sigev_value.sival_ptr = &timerid;

	if (timer_create(CLOCK_REALTIME, &sigev, &timerid) == 0) {
		itval.it_value.tv_sec = usec / 1000;
		itval.it_value.tv_nsec = (long)(usec % 1000) * (1000000L);

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
    int fd = open(logfile, O_RDWR | O_CREAT);
    if(fd == -1){
        perror("fileopen");
        exit(EXIT_FAILURE);
    }

    return fd;
}
static int WriteLogToFile(const char* log, int _fd){
    ssize_t sz = write(fd, log, strlen(log));
    if (sz == -1){
        perror("filewrite");
        exit(EXIT_FAILURE);
    }
}