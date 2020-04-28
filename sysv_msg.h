#ifndef __SYSV_IPC_HEADER
#define __SYSV_IPC_HEADER

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <error.h>
#include <sys/ipc.h> 
#include <sys/msg.h> 

#include "my_types.h"

int initMsgQueue(const char*,int);
int rmMsgQueue(int);
int sendMsg(msg_t*, int);
int recvMsg(msg_t*, int);

static void ErrorReport(const char*);

#endif