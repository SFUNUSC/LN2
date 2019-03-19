/*****************************************************************************
 Based on  "Linux Programmer's Guide - Chapter 6"
 (C)opyright 1994-1995, Scott Burkett
 ***************************************************************************** 
 MODULE: msgtool.c
 *****************************************************************************
 A command line tool for tinkering with SysV style Message Queues
 *****************************************************************************/

#ifndef __MSGTOOL
#define __MSGTOOL

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
#include <mqueue.h>
#include <iostream>


#define MAX_SEND_SIZE 80
///we use ftok to generate a key, from the argument:
#define KEY_SOURCE "/usr/bin"


struct mymsgbuf 
{
    long mtype;
    char mtext[MAX_SEND_SIZE];
};


/// MsgQ class handless both types of connection to a 
/// message queue: client and server
class MsgQ
{
public:
  MsgQ(void)
  {
    /* Create unique key via call to ftok() */
    key = ftok(KEY_SOURCE, 'm');
    
    /* Open the queue - create if necessary */
    if((msgqueue_id = msgget(key, IPC_CREAT|0660|O_NONBLOCK)) == -1) 
      {
	perror("msgget");
	exit(1);
      }
    //flush the queue
    char *buf=new char[MAX_SEND_SIZE];
    int counter=0;
    while(1)
      {
	int retval=read(buf);
	if(retval!=1)
	  break;
	counter++;
	if(counter>100)
	  {
	    //	error message
	    std::cout<<"funny msg queue !\n";
	    exit(1);
	  }
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
  
  int remove(void)
    {
      remove_queue(msgqueue_id); 
      return 1;
    };
  
  void usage(void);
private:
  
  key_t key;
  int   msgqueue_id;
  struct mymsgbuf qbuf;

  void send_message(int qid, struct mymsgbuf *qbuf, long type, char *text);
  int read_message(int qid, struct mymsgbuf *qbuf, long type, char *message);
  void remove_queue(int qid);

 
};


#endif
