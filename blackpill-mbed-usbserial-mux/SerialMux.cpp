#include "mbed.h"
#include "SerialMux.h"

// This implements the Williams mux serial protocol
// FF [FF...] FE => actual FF character
// FF [FF...] NN => Swtich to channel N (0-FC)
// FF [FF...] FD => Ask other side to retransmit FF NN

Stream *SerialMux::tty=NULL;   // our main tty
Thread SerialMux::rthread(osPriorityNormal,OS_STACK_SIZE*3/2,NULL,"Mux Rcv");   // threads
Thread SerialMux::wthread(osPriorityNormal,OS_STACK_SIZE*3/2,NULL,"Mux Xmit");
SerialMux *SerialMux::head=NULL;  // linked list head
short SerialMux::coutput=-1;  // current output and input in case we are asked (v2 protocol)
short SerialMux::cinput=-1;
bool SerialMux::sync=false;  // should we wait for a handshake (v2 protocol)
Mutex SerialMux::ttymtx;     // mutex to protect writing to tty

// constructor bsize<8 vttyid between 0 and 0xFD (but see note at top)
// After construction if mask=0 something was wrong
SerialMux::SerialMux(int vttyid, buffsize bsize) 
{
    blocking=1;
    cinput=-1;
    coutput=-1;
    next=head;
    head=this;
    id=vttyid;
    ihead=itail=ohead=otail=0;
    if (bsize<=8&&bsize!=0) 
    {
        unsigned short m=1<<bsize; // make sure this is a short
        ibuffer=new char[m];
        obuffer=new char[m];
        mask=m-1;
    }
    else 
      mask=0 ;  // we should throw an exception here but check mask=0 instead;
}

// clean up 
SerialMux::~SerialMux() 
    {
        // if these are null it doesn't matter
        SerialMux *i;
        for (i=head;i;i=i->next)
        {
            if (i->next==this) i->next=next;  // remove me from chain
        }
        delete [] ibuffer;
        delete [] obuffer;
    }


// Start our servers. Must have a base tty for this and only call this once!
void SerialMux::start(Stream *basetty, bool syncflag)
{
    sync=syncflag;
    if (basetty) tty=basetty;
    // launch threads
    if (rthread.get_state()!=rtos::Thread::Running) rthread.start(readthread);
    if (wthread.get_state()!=rtos::Thread::Running) wthread.start(writethread);

}

// Any characters waiting in our queue?
bool SerialMux::readable(void)
{
    bool rv;
    muxlock();
    rv=ihead!=itail;
    muxunlock();
    return rv;
}

// Always writable (maybe that shouldn't be, but USBSerial does the same thing)
bool SerialMux::writable(void)
{
   return 1;
}

// Raw read, not the same as C lib read, but close
// Using the stream lock seems to mess this up 
ssize_t SerialMux::_read(void *buffer, size_t size)
{
    int rv;
    char *buf=(char *)buffer;
    size_t ct=0;
    muxlock();
    while (size)
    {
        if (itail==ihead)    // no characters? 
           while (blocking && itail==ihead)   // if blocking wait
           {
              muxunlock();
               ThisThread::yield();
               muxlock();
           }
        if (ihead==itail && !blocking) break;  // or quit
        *buf++=ibuffer[ihead];  // read
        ihead=incr(ihead);
        size--;
        ct++;
    }
    muxunlock(); 
    return ct;

}

// Raw write, not the same as C lib write, but close
ssize_t SerialMux::_write(const void *buffer, size_t size)
{
    size_t ct=0;
    char *buf=(char *)buffer;
   muxlock();
// if blocking=0 repeat until buffer is full or size is 0
// if blocking=1 then repeat until size is 0 (but we need to unlock when full)
   while ((blocking || incr(otail)!=ohead) && (size--)>0)
   {

       if (blocking && incr(otail)==ohead)  // sure, we don't really need to test blocking here
       {
            while (incr(otail)==ohead)  // wait
            {
               muxunlock();
               ThisThread::yield();
               muxlock();
            }

       }
      
       obuffer[otail]=*buf++;  // write
       otail=incr(otail);
       ct++;

   }
   muxunlock();
   return ct;
}

// Stream read and write call _putc/_getc and we revector them to our _read and _write
int SerialMux::_putc(int c) 
{
   return (_write(&c,1)==1)?c:-1;
}

int SerialMux::_getc(void)
{
    char c;
    return (_read(&c,1)==1)?c:-1;
}



// characters available?
uint8_t SerialMux::available(void)
{
    unsigned short t;
    uint8_t rv;
    muxlock();
    t=itail;
    if (t>ihead) t+=mask+1;  // adjust for backwards situtation
    rv=t-ihead-1;
    muxunlock();
    return rv;
}

void SerialMux::iflush(void)
{
    muxlock();
    ihead=itail;
    muxunlock();
}
void SerialMux::oflush(void)
{
    muxlock();
    ohead=otail;
    muxunlock();
}


// We need to write the code out here, but to do that we should own the tty
void SerialMux::muxsync(void)
{
    char cc[4];
    ttylock();
    cc[2]=cc[0]='\xff';
    cc[1]='\xfd';
    cc[3]=coutput;   // answer with our current output channel
    tty->write(cc,4);
    ttyunlock();

}

// Threads for dealing with the main tty
// read from UART to buffers
void SerialMux::readthread(void)
{
    int state=0;
    int synced=0;
    SerialMux *current=NULL;
    while (1)
    {
        if (!current) 
            {
                current=head;   // initialize to first vtty when it is available
                cinput=current->id;
            }
        if (!current)
        {
            ThisThread::yield();    // no VTTYs yet, so just snooze
            continue; // should sleep!
        } 
        char c;
        if (tty->read(&c,1)!=1)   // get any waiting characters (could block)
        {
            ThisThread::yield();
            continue;    // if nothing on the UART, loop
        }
        if (c=='\xff')   // is this an escape code?
        {
            state=1;  // any number of FFs in a row are OK
            continue;
        }
        if (state!=0 && c=='\xfe')   // FF*FE is a real FF
        {
            // real FF
            c='\xff';
            state=0;   // end escape code
        }
        if (state!=0 && c=='\xfd')  // v2protocol, answer with our current output
        {
            char cc[2];
            cc[0]='\xff';
            cc[1]=coutput;
            ttylock();
            tty->write(cc,2);  // resend last output code
            ttyunlock();
            state=0;
            continue;
        }
        if (state==1 )  // if state==1 here, we must change channels
        {
// find correct object on chain, set current, and 
            for (current=head;current;current=current->next) if (current->id==c) break;
            if (!current) current=head;  // oops! No object with that ID found
            cinput=current->id;
            state=0;  // end escape code
            synced=1;
            continue;  // no real characters received yet
        }
        if (sync && !synced) continue;  // ignore until we got one channel change at least (if sync set)
        // normal character
        if (current->writable()) 
        {
            current->muxlock();
            current->ibuffer[current->itail]=c;
            current->itail=current->incr(current->itail);
            current->muxunlock();
        }

    }
}


//write from buffers to UART
void SerialMux::writethread(void)
{
    int channel=-1;
    char c;
    SerialMux *current=NULL;
    coutput=-1;
    while (1)
    {
        for (current=head;current;current=current->next)  // for each vtty
        {
            bool go=false;
            current->muxlock();
            if (current->ohead!=current->otail) go=true;    // something is there
            if (!go)
            {
                current->muxunlock();    // nothing here, so try the next one
                continue;
            }
            if (channel!=current->id)   // do we need to switch channels?
            {
                char cc[2];
                cc[0]='\xff';
                channel=cc[1]=current->id;
                coutput=channel;
                // send escape code
                ttylock();
                tty->write(cc,2);
                ttyunlock();
            }

            while (current->ohead!=current->otail)   // send characters until buffer empty
            {
                c=current->obuffer[current->ohead];
                current->ohead=current->incr(current->ohead);
                char cc[2];
                cc[0]=c;
                cc[1]='\xFE';
                // send escape code or normal character
                ttylock();
                tty->write(cc,(c=='\xff')?2:1);
                ttyunlock();
            }
            current->muxunlock();
        }
        ThisThread::yield();
  
    }
}


