#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <getopt.h>
#include <cstring>
#include <signal.h>


// This runs forever until you break
// Note that creating symlinks doesn't really check much
// And will delete anything it creates on normal exit
// So be careful if you run this as root (which you probably shouldn't)


// The class maintains a list of all virtual ttys (vttys)

#include "ttymux.h"

int ttychan::tty=-1;   // the real serial port/tty
ttychan *ttychan::ttyhead=NULL;  // head of the list of ptys
// the threads that manage the real tty
pthread_t ttychan::readthread=(pthread_t)NULL;   
pthread_t ttychan::writethread=(pthread_t)NULL;
int ttychan::autodelete=0;
int ttychan::nottysetup=0;
int ttychan::v2proto=1;
int ttychan::coutput=0;
int ttychan::cinput=0;
bool ttychan::sync=false;

// Clean up on destruct or explicit request
void ttychan::cleanup(void)
{
  close(pty);
  if (link && autodelete)
    {
      unlink(link);
    }
}

// Walk the list and clean up everyone
void ttychan::cleanupAll(void)
{
  ttychan *p;
  for (p=ttyhead;p;p=p->next)
    {
      p->cleanup();
    }
}

// Destructor -- not always called (e.g., on exit()).
ttychan::~ttychan()
{
  ttychan *b4,*after=next;
  // find my parent
  for (b4=ttyhead;b4->next!=this;b4=b4->next);
  // unlink myself
  if (b4)
    {
      b4->next=after;
    }
  cleanup();
  
}


ttychan::ttychan()
{
  link=NULL;
  next=ttyhead;
  ttyhead=this;
  pty=-1;
}

// Construct with a filename. Again, only once (default after that)
int ttychan::run(const char *fn)
{
  int ftty=open(fn,O_RDWR|O_NOCTTY|O_SYNC|  O_NONBLOCK);
  if (ftty<0)
    {
      perror(fn);
      return -1;
    }
  else
    return run(ftty);
}

// user override to tweak handle settings
// Note type==1 for tty, 0 for vttys
void ttychan::adjustfile(int type,struct termios *info)
{
  return;
}

// Prep a file handle
int ttychan::prepfhandle(int handle)
{
  struct termios info;
  tcgetattr(handle,&info);
  if ((handle==tty && nottysetup==0)||handle!=tty)
      {
      cfmakeraw(&info);
      info.c_cflag&=~CRTSCTS;
      info.c_cflag|=(CLOCAL|CREAD);
      info.c_cflag&= ~CSIZE;
      info.c_oflag&=~OPOST;
      info.c_cc[VTIME]=0;
      info.c_cc[VMIN]=0;
      }
  adjustfile(handle==tty,&info);
  return tcsetattr(handle,TCSANOW,&info);

}

// Run the server. If you haven't already passed a base tty
// you must do so now
int ttychan::run(int basetty)
{
  int rv=0;
  if (basetty>0) tty=basetty;
  if (readthread || writethread) return 2; // don't call me more than once!
  tcflush(tty,TCIOFLUSH);
  // this should be in an override and check errors?
  if (prepfhandle(tty)) perror("TTY set attribute");
  // check threads and if needed start them up
  rv=pthread_create(&ttychan::readthread, NULL, ttychan::rthread,NULL);
  if (rv==0)
      rv=pthread_create(&ttychan::writethread,NULL,ttychan::wthread,NULL);
  return rv;
}


// Start a vtty with the given id
int ttychan::start(int id)
  {
    int rv=0;
    this->id=id;  // set id
    // allocate pty
    pty=posix_openpt(O_RDWR|O_NOCTTY|O_NONBLOCK);
    if (pty==-1) return -1;
    grantpt(pty);
    unlockpt(pty);
    if ((rv=prepfhandle(pty))) perror("PTY set attribute");
    // set link if requested
    if (link)
      {
	// just in case
	unlink(link);  // force creation
	if (symlink(ptsname(pty),link))
	  {
	    perror(link);
	    link=NULL;  // on error don't try to delete later
	  }
      }
    return rv;
  }

// This is the receive thread from real tty
void *ttychan::rthread(void *arg)
{
  ttychan *current=ttyhead;
  int state=0; // 0 = normal, 1 = escaped
  int synced=0;
  while (1)
    {
      int c;
      // get character
      if (!current) continue;  // wait until someone is listening
      if (read(tty,&c,1)!=1) continue;
      // need to determine if this is a switch
      if (c==0xFF)
	{
	  state=1;
	  continue;
	}
      if (state==1 &&c<(v2proto?0xFD:0xFE))  //(c!=0xFE && (c!=0xFD||v2proto==0)))
	{
	  ttychan *i;
	  state=0;
	  if (current->id==c && synced)
	    {
	      continue;  // we are alredy on this channel so nevermind
	    }
	  // we need to change channels here to id c
	  for (i=ttyhead;i;i=i->next)
	    {
	      if (i->id==c)
		{
		  current=i;
		  cinput=current->id;
		  synced=1;
		  break; // break out of for loop
		}
	    }
	  // here we either broke out of the for loop or we fell out in which case nothing happens and we eat the escape
	  continue;
	}
      if (state==1 && c== 0xFE)
	{
	  state=0;
	  c=0xFF;
	}
      if (state==1 && c==0xFD)  // can't get here if v2proto==0
	{
	  char cc[2];
	  if (coutput!=-1)
	    {
	      cc[0]='\xff';
	      cc[1]=coutput;
	  // handle request for response to current
	      write(tty,cc,2);
	    }
	  state=0;  // eat escape either way
	  continue;
	}
      if (sync && !synced) continue;  // don't do anything until we get a start sync
      if (current && current->pty>0)
	{
	  int rv;
	  // this is sort of a hack. If the pty is not ready we should retry
	  // but if it isn't ready for a bunch of times in a row, we eventually kill everything spinning in this loop
	  // then the transmitter overflows and well... so we set a "resonable number" for retries 
	  int retrycount=10000;   // if we have a disconnected PTY this eventually gets crazy
	  do {
	    rv=write(current->pty,&c,1); 
	  } while (rv==-1 && errno==EAGAIN && --retrycount);
	  if (rv<0) perror("Write 2");
	}
    }
  return NULL;
}

// Thread that manages writes to real tty
void *ttychan::wthread(void *arg)
{
  ttychan *current;
  int lastid=-1;
  coutput=-1;  // not really but if you ask now that's what we will answer
  char cc[2];
  while (1)
    {
      for (current=ttyhead;current;current=current->next)
	{
	  int c,n;
	  usleep(0); // this is not a very good answer but it does work... why???? Should just yield
	  n=read(current->pty,&c,1);  // need to be nonblock!

	  if (n<=0) continue; // could be no characters or no connection to pty
	  // if we are changing channels, send the codes
	  if (current->id!=lastid)
	    {
	      char sw[2];
	      // send escape
	      sw[0]='\xff';
	      sw[1]=current->id;
	      write(tty,sw,2);
	      lastid=current->id;  // remember for next time
	      coutput=lastid;
	    }
	  // send data
	  cc[0]=c;
	  n=1;
	  if (c=='\xff')
	    {
	      cc[1]='\xff';  // handle escaped ff
	      n=2;
	    }
	      
	  if (write(tty,cc,n)!=n) perror("Write error"); 
	}
    }
  return NULL;
}

void ttychan::muxsync(void)
{
  char cc[4];
  cc[2]=cc[0]='\xff';
  cc[1]='\xfd';
  cc[3]=coutput;
  write(tty,cc,4);
}

// generic error and help messages
static void Xerror(const char *msg, int rc=1)
{
  fprintf(stderr,"%s\n",msg);
  exit(rc);
}     

static void help(void)
{
  Xerror("Usage: ttymux -d -c id[:link] [-c id[:link]...] serial_port\n"
	 "   -c - Set up channel with ID and optional symlink (full path)\n"
         "   -d - Autodelete symlinks on exit\n"
	 "   -n - Do not set default terminal attributes on serial_port\n"
	 "   -s - Don't rececive until you get the first escape code\n"
	 "   -1 - Omit protocol v2 extensions (Allow channel 0xFD)\n"
	 ,1);

}

// Control C handler
static void sighandle(int notused)
{
  // clean up on signals
  fprintf(stderr,"Exiting on signal\n");
  ttychan::cleanupAll();
  exit(10);
}

// The server
int main(int argc, char *argv[])
{
  FILE *f;
  int opt, nchannels=0,i;
  // small waste of memory, but not much
  unsigned char channels[254];  // id for each channel
  char *links[254];  // link pointer for each channel
  signal(SIGINT,sighandle);  // catch Control+C
  // process command line
  while ((opt=getopt(argc,argv,"dc:hn1s"))!=-1)
    {
      switch (opt)
	{
	case 's':
	  ttychan::setsync();
	  break;
	  
	case '1':
	  ttychan::v2proto=0;  // No version 2 protocol
	  break;
        case 'n':
	  ttychan::nottysetup=1;  // don't set terminal options on tty
	  break;
	case 'd':
	  ttychan::autodelete=1;  // delete symlinks on exit
	  break;
	case 'c':
	  {
	    long nlong;
	    int n;
	    nlong=strtol(optarg,NULL,0);  // get id #
	    n=nlong;
	    // find link if ther
	    char *colon=strchr(optarg,':');
	    if (colon)
	      {
		// remember link name (we never free this)
		links[nchannels]=strdup(colon+1);
	      }
	    else
	      links[nchannels]=NULL;
	    if (n<0||n>254) Xerror("Channel ID must be 0-254");
	    channels[nchannels++]=n;
	  }
	  break;
	case 'h':
	default:
	  help();
	}
    }
  // sanity checks
  if (nchannels==0) Xerror("Must specify at least one channel (-c)",2);
  if (optind>=argc) Xerror("Must specify serial port or device",3);
  // we must have one object and we create it 
  ttychan chan0;
  if (chan0.run(argv[optind])) exit(1);   // and start the server
  if (links[0]) chan0.setLink(links[0]);  // and set its link
  // now we actually start the vtty
  if (chan0.start(channels[0])) fprintf(stderr,"Can't open PTY 0\n");
  printf("Connect %d = %s (%s)\n",channels[0],chan0.getptyname(),links[0]?links[0]:"");  
  // do 1 to N-1 for the rest
  for (i=1;i<nchannels;i++)
    {
      ttychan *chan=new ttychan();
      if (links[i]) chan->setLink(links[i]);
      // in theory, we are done with link so we could reclaim that memory
      if (chan->start(channels[i])) fprintf(stderr,"Can't open PTY %d\n",i);
      printf("Connect %d = %s (%s)\n",channels[i],chan->getptyname(),links[i]?links[i]:"");
    }
  while (1); 
  return 0;  // not reached
}
