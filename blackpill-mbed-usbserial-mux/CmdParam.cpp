// Command processor parsing class using C++ strings
// Public domain -- Williams

// For demo:
// g++ -DDEMO=1 -o cmd cmd.cpp

/*

Create a table of commands with the following format:

CmdParam commands[] = {
   { 1, "help", "Get help", help, NULL },
   { 2, "exit", "Exit program", commands, (void *)1 },
   { 3, "blahblah", "Blah blah blah", setget, &somevar },
   . . .
   { 0, "", "", NULL, NULL }
};

The number is an ID and can be anything but by convention 0 is the end
(the code doesn't look at that, though -- it uses the command name NULL or "\0")

The first string is what the user has to type to trigger the command 
(help, exit, etc.)

The second string is what the built in help processor prints for help.

The 4th argument is a function name and the 5th is a void pointer that will be
sent to the function. Functions look like:

void help(unsigned int id, void *arg, const char *p);

Here, id will be 1, arg will be NULL, and p will be whatever is left
on the command line after eating the help. 

A few ideas:
You can pass anything that will fit in a void pointer and cast it.
So you can set arbitrary data to go to a function (for example
in the demo, look at cmd_val which can set one of several variables).

You can also swtich based on the command id if you want one function
to handle several things.

You can subclass to change error handling, help messages, etc.



 */
#include <mbed.h>
#include <cstdio>
#include <stdlib.h>

#include "CmdParam.h"
#include "cmds.h"  // we need cmdtty

// Test for empty entry
#define ISEMPTY(x) (x.length()==0)

// Static variables here
std::string CmdParam::current;
unsigned int CmdParam::index;
std::string CmdParam::sep=" \t\r\n" ;
void (*CmdParam::notfoundfunc)(const char *,const char *)=CmdParam::notfound;
void (*CmdParam::printfunc)(const char *)=CmdParam::print;


// Get float from current command line
// *valid==false if not present and safe to set valid to NULL (default)
float CmdParam::getfloat(bool *valid)
{
  float f=0.0;
  bool tvalid;
  std::string token=gettoken(&tvalid);
  if (valid) *valid=tvalid;
  if (tvalid) f=std::stof(token);
  return f;
}

// Get int from current command line
// *valid==false if not present and safe to set valid to NULL (default)
int CmdParam::getint(bool *valid)
{
  int f=0;
  bool tvalid;
  std::string token=gettoken(&tvalid);
  if (valid) *valid=tvalid;
  if (tvalid) f=std::stoi(token,NULL,0);
  return f;
}
// Get uint from current command line
// *valid==false if not present and safe to set valid to NULL (default)
unsigned int CmdParam::getuint(bool *valid)
{
  unsigned int f=0;
  bool tvalid;
  std::string token=gettoken(&tvalid);
  if (valid) *valid=tvalid;
  if (tvalid) f=std::stoul(token,NULL,0);
  return f;
}

// Get token from current command line
// *valid==false if not present and safe to set valid to NULL (default)
std::string CmdParam::gettoken(bool *valid)
{
  std::string token;
  size_t n1;
  if (valid) *valid=false;
  n1=current.find_first_not_of(sep,index);
  if (n1==std::string::npos) return token;  // all separators or end of string
  if (valid) *valid=true;
  index=current.find_first_of(sep,n1);  // find end of token
  token=current.substr(n1,(index!=std::string::npos)?index-n1:std::string::npos);
  return token;
}

// Take a table and a command line and make it happen
// Note you could have a command that sets a mode that makes
// a different table active, for example
void CmdParam::process(CmdParam *table, const char *cmdline)
{
  std::string ccmd;
    bool valid;
    current=cmdline;   // current line
    index=0;
    ccmd=gettoken(&valid);  // get the command
    if (!valid)
      {
	printfunc("Unknown error:");
	printfunc(cmdline);
	return;
      }
    // search table
    for (int i=0;table[i].cmdname.length()!=0;i++)
    {
      if (ccmd==table[i].cmdname)
        {
            // found
	    current=cmdline;
            table[i].fp(i,table[i].arg,current.substr(index).c_str());
            return;
        }
    }
    // not found
    notfoundfunc(cmdline, ccmd.c_str());
}

void CmdParam::notfound(const char *cmdline, const char *cmd)
    {
      printfunc("Not found: ");
      printfunc(cmd);
      printfunc("\r\n"); 
    };


void CmdParam::print(const char *msg)
    {
        cmdtty->puts(msg);
    };


