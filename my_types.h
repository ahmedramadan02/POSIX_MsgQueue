#ifndef __MY_TYPES_HEADER
#define __MY_TYPES_HEADER

#ifdef SYSV_IPC
#include <sys/ipc.h> 
#include <sys/msg.h> 
#endif 

#define MAX_TEXT_SIZE 100
#define PERMISSIONS 0644

//REQ_GEN_004
typedef struct msgBuffer {
    long mtype;
    struct msgData{
        pid_t currentProcessID;
        char msgText[MAX_TEXT_SIZE];
        int lifeCounter;
    };
    struct msgData data;
} msg_t;

typedef struct msgMetaData {
    key_t key;
    int msgQueueId;
} msg_meta_t;

#endif