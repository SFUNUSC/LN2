#include "msgtool.h"


void MsgQ::send_message(int qid, struct mymsgbuf *qbuf, long type, char *text)
{
        /* Send a message to the queue */
        printf("Sending message: %s\n", text);
        qbuf->mtype = type;
        strcpy(qbuf->mtext, text);

        if((msgsnd(qid, (struct msgbuf *)qbuf,
                strlen(qbuf->mtext)+1, 0)) ==-1)
        {
                perror("msgsnd");
                exit(1);
        }
}

int MsgQ::read_message(int qid, struct mymsgbuf *qbuf, long type, char* message)
{
  int retval=0;
        /* Read a message from the queue */
  //printf("Reading a message ...\n");
  qbuf->mtype = type;
  retval=msgrcv(qid, (struct msgbuf *)qbuf, MAX_SEND_SIZE, type, IPC_NOWAIT);
  if(retval!=-1)
    {
      strcpy(message,qbuf->mtext);
      return 1;
    }
  else
    return -1;
  // if(retval!=-1)
  //  printf("Type: %ld Text: %s\n", qbuf->mtype, qbuf->mtext);
  // else
  //  printf("no message\n");
}

void MsgQ::remove_queue(int qid)
{
        /* Remove the queue */
        msgctl(qid, IPC_RMID, 0);
}

void MsgQ::flush(int qid)
{
  int retval=1;
  char *command=new char[4096];
  while(retval==1)
    retval=read(command);
}
