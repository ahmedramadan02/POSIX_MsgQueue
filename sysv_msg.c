#include "sysv_msg.h"
#include <fcntl.h>
#include <sys/stat.h>

static void ErrorReport(const char*);

//TODO: use POSIX APIs instead of kernel space SYSV IPC
//It's there for backword compatibility

static key_t key; 

//TODO: Paramter to new message id
int initMsgQueue(const char* filepath, int proj_id){
    int _msgid;
    //Create file if not exists
    //system("touch projfile01"); //not POSIX
    mode_t mode = S_IRUSR | S_IWUSR;
    creat(filepath, mode);
    //According to man pages it's equivlent to 
    //open(filepath, O_WRONLY|O_CREAT|O_TRUNC,mode);

    // ftok to generate unique key 
   if ((key = ftok(filepath, proj_id)) == -1) {
      ErrorReport("ftok");
   }

   //temp print
   printf("IPC Current Key: 0x%08x\n",key);

    //connect to the queue
    if ((_msgid = msgget(key, PERMISSIONS | IPC_CREAT)) == -1) {
      ErrorReport("msgget");
   }

   return _msgid;
}

int sendMsg(msg_t* pmsg, int _msgid){
    if (msgsnd(_msgid, (void*) pmsg, sizeof(*pmsg), 0) == -1){
         ErrorReport("msgsnd");
    }
}

int recvMsg(msg_t* pmsg, int _msgid){
    if (msgrcv(_msgid, (void*) pmsg, sizeof(*pmsg), 0, 0) == -1) {
         ErrorReport("msgrcv");
    }

    return 0;
}

int rmMsgQueue(int _msgid){
    if (msgctl(_msgid, IPC_RMID, NULL) == -1) {
      ErrorReport("msgctl");
    }
}

static void ErrorReport(const char* _msg){
    perror(_msg);
    exit(EXIT_FAILURE);
}