#ifndef __CMD_H
#define __CMD_H
#include "mbed.h"
#include "USBSerial.h"
// definitions between main.cpp and cmds.cpp


extern unsigned int blinkrate;  // led blink rate
extern unsigned int arate;      // analog sample rate
extern unsigned int drate;      // digital sample rate
extern char cmdstr[];           // text note on digital window
extern void cmdloop(Stream *s);  // the worker function for command processing
extern Stream *cmdtty;          // command console stream




#endif
