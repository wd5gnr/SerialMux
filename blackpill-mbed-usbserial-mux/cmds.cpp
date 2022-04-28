#include "cmds.h"
#include "CmdParam.h"

// This file has commands for the command window

Stream *cmdtty;  // the command window
// generic functions to set uint and string parameters
static void set(unsigned int n, void *arg, const char *p); // forward refs
static void setstr(unsigned int n, void *arg, const char *p); 

// sleep rates in ms and the string tag for the digital console
unsigned int blinkrate=500;
unsigned int arate=500;
unsigned int drate=500;
extern char cmdstr[];



// command table. See CmdParam.h/cpp for format
CmdParam commands[] = {
		       { 1, "blink", "Set blink rate in milliseconds", set, &blinkrate },
               { 2, "arate", "Set analog rate in milliseconds", set, &arate },
               { 3, "drate", "Set digtial rate in milliseconds", set, &drate },
               { 4, "note", "Set note field on digital output", setstr, &cmdstr },
		       { 6, "help", "This message", CmdParam::help, commands},

		       { 0, "", "", NULL, NULL }
};


// just a dumb function to print ok
static void ok()
{
    cmdtty->puts("OK\r\n");
}


static void set(unsigned int n, void *arg, const char *p)
{
  bool valid;
  unsigned int *v=(unsigned int *)arg;
  unsigned int tkn=CmdParam::getuint(&valid);
  if (valid) *v=tkn;
  ok();
}


static void setstr(unsigned int n, void *arg, const char *p)
{
  bool valid;
  char *v=(char *)arg;
  std::string tkn=CmdParam::gettoken(&valid);
  if (valid)
  {
      strcpy(v,tkn.c_str());  // up to you not to overflow!
  }
  ok();
}
// Simple main
// we need to wait for USB connection so we don't bog up the stdout system
// so we need to bring in the USBSerial from main :( )
#include "USBSerial.h"
extern USBSerial usbSerial;

// The thread calls this which never returns
void cmdloop(Stream *s)
  {
   char cmdline[257];
   cmdtty=s;
   while (!usbSerial.connected()) ThisThread::sleep_for(250ms);
   while (1)
     {
         // you will need your terminal set for echo and CR=>CRLF. Also, don't expect backspace, etc. in this mode
       s->putc('?');
       s->putc(' ');
       s->gets(cmdline,sizeof(cmdline));    // presumably you won't overflow this buffer
       CmdParam::process(commands,cmdline);   // do it!
     }
  }


