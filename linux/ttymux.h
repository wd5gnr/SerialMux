#ifndef __TTYMUX_H
#define __TTYMUX_H

/*
This code opens a serial port (basetty) and lets you create multiple ptys. The protocol is simple:

Any character except FF is itself.

An escape code is 1 or more FF bytes. Since you can have more than 1, a restart tends to be graceful because
you either see the first escape code or absorb it in an ongoing escape code -- the other side should start
with an escape code, of course.

FF [FF FF...] FE -> real FF
FF [FF FF...] NN -> Switch to channel NN (00-FD)

Bytes coming from a PTY (vtty) get sent to the main tty with the escape code to switch if necessary.
Bytes coming in get routed to the correct vtty based on the last escape

In case of a reset from the sender, you will synchronize
In the case of a reset for the receiver, it is possible that up to the next escape code could go to the default serial port. 
It is also possible that the receiver could miss the first FF and pick up a garabage byte and then send the rest to the default port until
another escape code occurs.

If this worries you, set up a fake default port and don't use it. Then ensure you periodically assert an escape code.

*/

class ttychan
{
private:
  void construct(int basetty);  // private constructor helper
  static int prepfhandle(int handle);  // prepare handle for I/O
protected:
  static int tty;   // main tty (serial port)
  static ttychan *ttyhead;  // first item in list of vttys
  ttychan *next;    // next vtty
  // need static read thread/write thread
  static pthread_t readthread, writethread;  // threads to manage port
  // the actual thread functions
  static void *rthread(void *arg);
  static void *wthread(void *arg);
  // vtty pty
  int pty;
  // name of symlink if any
  const char *link;
  int id;  // the ID that identifies this vtty
  // this is for subclasses if they just want to modify the termios for the tty (type=1) or ptys (type=0)
  // this runs even if notttysetup is set even on the tty
  static void adjustfile(int type, struct termios *info);
  static int cinput;
  static int coutput;
  static bool sync;  // if 1 wait for a channel escape before reading anything
public:
  // construct with file name or handle
  ttychan();
  ~ttychan();
  // start the server (do once)
  static int run(int basetty);
  static int run(const char *fn);

  // start a vtty with particular id
  int start(int id);
  // get pty name
  const char *getptyname(void) { return ptsname(pty); };
  // get symlink name or pty name if no link
  const char *getlink(void) { return link?link:getptyname(); }
  // get file descriptor for tty
  int getFD(void) { return tty; }
  // set link name
  void setLink(const char *link) { this->link=link; }
  // clean up this vtty
  void cleanup(void);
  // clean up all vttys
  static void cleanupAll(void);
  static void muxsync(void);
  static void setsync(bool state=true) { sync=state; }
  static int autodelete;  // set to 1 if delete symlinks when vtty closed or program exits
  static int nottysetup;  // set to 1 if you want to skip terminal setup on tty (still calls user routine)
  static int v2proto;
};

#endif
