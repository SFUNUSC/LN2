/*****************************************************************************
 Based on  "Linux Programmer's Guide - Chapter 6"
 (C)opyright 1994-1995, Scott Burkett
 ***************************************************************************** 
 MODULE: msgtool.c
 *****************************************************************************
 A command line tool for tinkering with SysV style Message Queues
 *****************************************************************************/





#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>

#define MAX_SEND_SIZE 80

struct mymsgbuf 
{
    long mtype;
    char mtext[MAX_SEND_SIZE];
};

class MsgQ
{

public:
  MsgQ(void)
  {
    /* Create unique key via call to ftok() */
    key = ftok("/usr/bin", 'm');
    
    /* Open the queue - create if necessary */
    /*    if((msgqueue_id = msgget(key, IPC_CREAT|S_IRUSR|S_IWUSR|S_IRGRP|S_IWGR
P|S_IROTH|S_IWOTH|O_NONBLOCK)) == -1) */
    if((msgqueue_id = msgget(key, IPC_CREAT|0660|O_NONBLOCK)) == -1)
      {
	perror("msgget");
	exit(1);
      } 
  };
  void send(char *text)
  {
    send_message(msgqueue_id, (struct mymsgbuf *)&qbuf,
                                       1, text);
  };


  int read(char *message)
  {
    return read_message(msgqueue_id, &qbuf, 1, message); 
  }; 


  ~MsgQ(void)
  {
    remove_queue(msgqueue_id); 
  };
  
  void usage(void);
private:
  
  key_t key;
  int   msgqueue_id;
  struct mymsgbuf qbuf;


  void send_message(int qid, struct mymsgbuf *qbuf, long type, char *text);
  int read_message(int qid, struct mymsgbuf *qbuf, long type, char *message);
  void remove_queue(int qid);
  void flush(int qid);
 
};
