/* Serial mux class demo

This opens up 4 "consoles" over the USB serial port

1) An analog console that reads an analog channel periodically and prints the value
2) A digital console that reads the built in switch and also displays a text tag
3) A debug console with informational messages
4) A command console that lets you change some timings and other parameters

You need the Linux server running:


Linux server command line:
ttymux -s -c 1:analogport.virtual -c 2:digitalport.virtual -c 100:debugport.virtual -c 10:cmdport.virtual

You then connect to the ports using something like picocom. For the command port you may want:

picocom -c --omap crlf cmdport.virtual

Note that you probably won't be able to backspace or anything unless you write code to buffer lines yourself.

You can pause the analog or digital consoles by entering a character othan than a space. Use a space character to resume.

Commands in the command window

blink 250  - set blink rate to 250ms
arate 150  - analog sample every 150ms
drate 500  - digital sample every 500ms
note Hello - Put the word hello in the digital output console from now on
help - Get this list of commands


-- Williams
 */

#include "mbed.h"
#include "USBSerial.h"
#include "SerialMux.h"


#include "cmds.h"

char cmdstr[32]="None";



    // Note: init to false requires connect to enumerate
    // If true, you must connect to the board to start
USBSerial usbSerial(false);



// If you want to use printf etc with USB Serial...
// HOWEVER. USBSerial must be connected prior to elaboration or 
// you need to call clearerr on the standard file handles after 
// a connect
namespace mbed
{
    FileHandle *mbed_override_console(int fd)
    {
        return &usbSerial;
    }

}

// Create the virtual serial ports
SerialMux analogConsole(1,SerialMux::BUFFER_SIZE16),
          digitalConsole(2,SerialMux::BUFFER_SIZE16),
          debugConsole(100,SerialMux::BUFFER_SIZE16),
          cmdConsole(10,SerialMux::BUFFER_SIZE64);


DigitalOut led(LED1);

// Writing to one port from multiple threads is OK, but it is possible to mix up output
// So all writes to debugConsole come through here

void debugLog(const char*s)
{
    static Mutex Logmtx;
    Logmtx.lock();
    debugConsole.puts(s);
    Logmtx.unlock();
}





void commandThread()
{
    cmdloop(&cmdConsole);  // never returns
}

void digitalThread()
{
    unsigned int n=0;
    DigitalIn btn(USER_BUTTON);
    btn.mode(PullUp);


    while (1)
    {
        int v=btn;
        debugLog(":Enter digital loop\r\n");
        // press anything but a space on the digital console and it will pause
        // until you press a space
        if (digitalConsole.readable())
        {
            while (digitalConsole._getc()!=' ') ThisThread::sleep_for(100ms);
        }
      digitalConsole.printf(":%d Switch=%d %s\r\n",n++,v,cmdstr);
        ThisThread::sleep_for(std::chrono::milliseconds(drate));
    }
 
}

void analogThread()
{
    unsigned int n=0;
    AnalogIn ain(PB_1);

    while (1)
    {
        debugLog(":Enter analog loop\r\n");
        // press anything but a space on the analog console and it will pause
        // until you press a space
        if (analogConsole.readable())
        {
            while (analogConsole._getc()!=' ') ThisThread::sleep_for(100ms);
        }
        float f=ain;
       analogConsole.printf(":%d Analog=%d.%d\r\n",n++,(int)(f*33/10.0),((int)(f*33))%10);
        ThisThread::sleep_for(std::chrono::milliseconds(arate));
    }
}


// Main thread
int main()

{
    int connected, lastconnected=0;
    usbSerial.connect();
    SerialMux::start(&usbSerial);
    Thread analog(osPriorityNormal,OS_STACK_SIZE,NULL,"Analog"), digital(osPriorityNormal,OS_STACK_SIZE,NULL,"Digital");
    Thread command(osPriorityNormal,OS_STACK_SIZE,NULL,"Command");
    analog.start(analogThread);
    digital.start(digitalThread);
    command.start(commandThread);

    while (1)
    {


        connected=usbSerial.connected();
        if (connected && ! lastconnected)
        {

           // if so, clear the standard streams
            clearerr(stdout);
            clearerr(stderr);
            clearerr(stdin);
            clearerr(analogConsole);
            clearerr(digitalConsole);
            clearerr(debugConsole);
            clearerr(cmdConsole);

        }
        lastconnected=connected;  // remember for next time#endif
        if (!connected) continue;

 

        led = !led;   // flip LED if output is true
        ThisThread::sleep_for(std::chrono::milliseconds(blinkrate));  // sleepy time
    }
}
