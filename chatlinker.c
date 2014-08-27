/*--------------------------------------------------------------------*/
/* functions to connect clients and server */

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h> 
#include <sys/socket.h> 
#include <netinet/in.h> 
#include <arpa/inet.h> 
#include <netdb.h>
#include <time.h> 
#include <errno.h>
#include <stdlib.h>

#include "common.h"
/*--------------------------------------------------------------------*/


/*----------------------------------------------------------------*/
/*
  prepares server to accept requests
  returns file descriptor of socket
  returns -1 on error
*/
int startserver()
{
  int     sd;      /* socket descriptor */
  int     myport;  /* server's port */
  char *  myname;  /* full name of local host */
  
  char 	  linktrgt[MAXNAMELEN];
  char 	  linkname[MAXNAMELEN];

  /*
    FILL HERE
    create a TCP socket using socket()
  */
  if((sd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
      fprintf(stderr, "Can't create socket.\n");
      return -1;
  }

  /*
    FILL HERE
    bind the socket to some port using bind()
    let the system choose a port
  */
  struct sockaddr_in sin;
  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = INADDR_ANY;
  sin.sin_port = 0;

  if(bind(sd, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
      fprintf(stderr, "Can't bind socket.\n");
      close(sd);
      return -1;
  }

  /* specify the accept queue length */
  listen(sd, 5);

  /*
    FILL HERE
    figure out the full local host name and server's port
    use getsockname(), gethostname() and gethostbyname()
  */
  char localhost[MAXNAMELEN];
  localhost[MAXNAMELEN-1] = '\0';
  gethostname(localhost, MAXNAMELEN-1);
  struct hostent *h;
  if((h = gethostbyname(localhost)) == NULL) {
      fprintf(stderr, "Can't get host by name.\n");
      close(sd);
      return -1;
  }
  else
      myname = h->h_name;

  struct sockaddr_in getPort;
  socklen_t getPortLen = sizeof(getPort);
  if(getsockname(sd, (struct sockaddr*)&getPort, &getPortLen) < 0) {
      fprintf(stderr, "Can't get servport.\n");
      close(sd);
      return -1;
  }
  else
      myport = ntohs(getPort.sin_port);

  /* create the '.chatport' link in the home directory */
  sprintf(linktrgt, "%s:%d", myname, myport);
  sprintf(linkname, "%s/%s", getenv("HOME"), PORTLINK);
  if (symlink(linktrgt, linkname) != 0) {
    fprintf(stderr, "error : server already exists\n");
    return(-1);
  }

  /* ready to accept requests */
  printf("admin: started server on '%s' at '%d'\n",
	 myname, myport);
  return(sd);
}
/*----------------------------------------------------------------*/

/*----------------------------------------------------------------*/
/*
  establishes connection with the server
  returns file descriptor of socket
  returns -1 on error
*/
int hooktoserver()
{
  int     sd;                    /* socket descriptor */

  char    linkname[MAXNAMELEN];
  char    linktrgt[MAXNAMELEN];
  char *  servhost;
  char *  servport;
  int     bytecnt;

  /* locate server */
  sprintf(linkname, "%s/%s", getenv("HOME"), PORTLINK);
  bytecnt = readlink(linkname, linktrgt, MAXNAMELEN);
  if (bytecnt == -1) {
    fprintf(stderr, "error : no active chat server\n");
    return(-1);
  }
  linktrgt[bytecnt] = '\0';

  /* split addr into host and port */
  servport = index(linktrgt, ':');
  *servport = '\0';
  servport++;
  servhost = linktrgt;

  /*
    FILL HERE
    create a TCP socket using socket()
  */
  if((sd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
      fprintf(stderr, "Can't create socket.\n");
      return -1;
  }

  /*
    FILL HERE
    connect to the server on 'servhost' at 'servport'
    need to use gethostbyname() and connect()
  */
  struct sockaddr_in sin;
  bzero(&sin, sizeof(struct sockaddr_in));
  sin.sin_family = AF_INET;
  sin.sin_port = htons(atoi(servport));
  
  struct hostent *hostEnt;
  if(hostEnt = gethostbyname(servhost))
      memcpy(&sin.sin_addr, hostEnt->h_addr, hostEnt->h_length);
  else {
      fprintf(stderr, "Can't get host by name.\n");
      close(sd);
      return -1;
  }

  if(connect(sd, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
      fprintf(stderr, "Can't connect to server.\n");
      close(sd);
      return -1;
  }

  /* succesful. return socket descriptor */
  printf("admin: connected to server on '%s' at '%s'\n",
	 servhost, servport);
  return(sd);
}
/*----------------------------------------------------------------*/

/*----------------------------------------------------------------*/
int readn(int sd, char *buf, int n)
{
  int     toberead;
  char *  ptr;

  toberead = n;
  ptr = buf;
  while (toberead > 0) {
    int byteread;

    byteread = read(sd, ptr, toberead);
    if (byteread <= 0) {
      if (byteread == -1)
	perror("read");
      return(0);
    }
    
    toberead -= byteread;
    ptr += byteread;
  }
  return(1);
}

Packet *recvpkt(int sd)
{
  Packet *pkt;
  
  /* allocate space for the pkt */
  pkt = (Packet *) calloc(1, sizeof(Packet));
  if (!pkt) {
    fprintf(stderr, "error : unable to calloc\n");
    return(NULL);
  }

  /* read the message type */
  if (!readn(sd, (char *) &pkt->type, sizeof(pkt->type))) {
    free(pkt);
    return(NULL);
  }

  /* read the message length */
  if (!readn(sd, (char *) &pkt->lent, sizeof(pkt->lent))) {
    free(pkt);
    return(NULL);
  }
  pkt->lent = ntohl(pkt->lent);

  /* allocate space for message text */
  if (pkt->lent > 0) {
    pkt->text = (char *) malloc(pkt->lent);
    if (!pkt) {
      fprintf(stderr, "error : unable to malloc\n");
      return(NULL);
    }

    /* read the message text */
    if (!readn(sd, pkt->text, pkt->lent)) {
      freepkt(pkt);
      return(NULL);
    }
  }
  
  /* done reading */
  return(pkt);
}

int sendpkt(int sd, char typ, long len, char *buf)
{
  char tmp[8];
  long siz;

  /* write type and lent */
  bcopy(&typ, tmp, sizeof(typ));
  siz = htonl(len);
  bcopy((char *) &siz, tmp+sizeof(typ), sizeof(len));
  write(sd, tmp, sizeof(typ) + sizeof(len));

  /* write message text */
  if (len > 0)
    write(sd, buf, len);
  return(1);
}

void freepkt(Packet *pkt)
{
  free(pkt->text);
  free(pkt);
}
/*----------------------------------------------------------------*/
