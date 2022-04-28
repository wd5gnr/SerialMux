#ifndef __CMDParam_H
#define __CMDParam_H

/* Command processor parsing class using C++ Strings
   Public domain -- Williams

   See cmdparm.cpp for more info 
*/

#include <string>

  class CmdParam
{
    protected:
  std::string cmdname;  // command name
  std::string cmddoc;   // help string
  unsigned int id;      // ID
  void *arg;            // argument to callback
  // the callback function
  void (*fp)(unsigned int id, void *arg, const char *cmdline);
  // current command we are processing
    static std::string current;
  // seperators (default " \t\r\n")
    static std::string sep;
  // Our current position in the current line
    static unsigned int index;
    // By making these pointers we can change them in a derived constructor
    static void (*notfoundfunc)(const char *cmdline, const char *cmd);
    static void (*printfunc)(const char *msg);
    public:
    // constructor (usually in a literal array; see demo 
 CmdParam(unsigned int iid, const char *name,const char *doc,void (*func)(unsigned int, void *,const char *),void *farg) : cmdname(name), cmddoc(doc), fp(func), id(iid), arg(farg) {};
    // Process a table and a command line
    // You can have different tables for different command lines
    static void process(CmdParam *table, const char *cmdline);
    // you can override these two for better control by setting
    // printfunc and notfoundfunc
    static void print(const char *msg);
    static void notfound(const char *cmdline, const char *cmd);
    // Get the doc string
  std::string &getDoc(void) { return cmddoc; };

  // Helper so you can use help directly
  static void help(unsigned id, void *arg, const char *cmdline)
  {
    help((CmdParam *)arg);
  }
  
  // Built-in help command
  static void help(CmdParam *table) 
    {
        unsigned i;
        for (i=0;table[i].cmdname.length()!=0;i++)
	  {
	    printfunc(table[i].cmdname.c_str());
	    printfunc(" - ");
	    printfunc(table[i].cmddoc.c_str());
	    printfunc("\r\n");
	  }
    };
  // Separators for everything
  static void setseperator(const char *set) { sep=set; }
  // Get token/float/int/uint or all the way to end of string (eos)
  static std::string gettoken(bool *valid=NULL);
  static float getfloat(bool *valid=NULL);
  static int getint(bool *valid=NULL);
  static unsigned int getuint(bool *valid=NULL);
  static std::string geteos(void) { return std::string(current,index); }
  
};

#endif
