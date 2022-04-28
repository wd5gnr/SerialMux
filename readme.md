
This code implements a serial multiplexer and demultiplexer protocol for Linux and a microcontroller. In this case, the microcontroller is an STM32F411 but since it uses Mbed, probably anything with Mbed would work. In addition, the protocol is very simple, so it would be easy to work out almost any sort of microcontroller program (e.g., Arduino) to do the same thing although the Mbed code is "fancy" and relies on RTOS and CLib features, this isn't really necessary.

Linux Side
-------------
The Linux software opens a terminal port (e.g. /dev/ttyUSB0 or /dev/ttyACM0 etc.) and then produces multiple psuedoterminals that most terminal software can use. For USB devices, the baud rate is probably unimportant. However, for a real serial device, you probably need to match up baudrates (untested). The code current does not change the baudrate so you'd need to set it externally or modify the code if you need to.
The ttymux program takes a few options. The only one that is critical is the -c option which defines a virtual port. Each port has an ID number from 0-253. You can also ask for a symlink. So, for example look at this command:

    ttymux -c 10 -c 33:virtualportA -c 50:/tmp/portB /dev/ttyACM0
Here we are creating three ports. Port #10 has no name. Port 33 will be virtualportA in the current directory and port 50 will be in /tmp/portB.

Other options:
* -d - Autodelete symlinks on exit
* -n - Do not set attributes on serial port
* -s - Do not send data to a virtual port until expressly selected (by default, some data on start can go to the wrong port; see protocol, below)
* -1 - Omit protocol v2 extensions (see protocol, below)

When the program runs you'll see a list of channels and their associated psuedoterminals (probably /dev/pts/X where X is some number). If you don't provide a symlink, that's how you connect to the virtual port. If you provide a symlink, you can use either. Note that the ID number is not the same as the pts number. So channel 10 in the above example probably won't be /dev/pts/10. If it is, that's just a coincidence.

To compile, you need pthreads:

    g++ -o ttymux ttymux.cpp -lpthread

MBED Side
---------------
The MBED code creates a list of SerialMux objects and launches two threads to manage the real serial port which can be any MBED stream.

You need to create your channels and then start the threads. So something like this:

    SerialMux channelA(1);
    SerialMux channelB(2);
    SerialMux::start(usbSerialPort);

This code uses the default buffer size for each channel. The channelA and B objects are proper streams so you can do things like:

    channelA.printf("Hello %d\n",n++);
Notice, however, that while SerialMux is threadsafe, writing to one stream from multiple threads may give you mixed up results. Also, some of the oddness of dealing with ports under MBED still apply.
In the example code, several threads write to debugConsole. To do that, a function debugLog uses a Mutex to make sure all the output from one thread stays together.

The USB port will cause the stdio library to fail if it is not connected. The example code shows one way to deal with that by calling clearerr when you first get a connection. This has nothing to do with SerialMux and is just an oddity of MBED.

Protocol
-----------
Despite the complex code to make things a proper stream under MBED the actual protocol is simple.
The transmitter sends most data bytes directly over the port. When there is a desire to change virtual ports, the transmitter sends an FF byte followed by the channel number. There are a few issues about doing this.

First, the transmitter has to be aware that if it really wants to send an FF, it can't. So it sends FF FE instead. If you were sending nothing but FF characters, that would double the amount of data sent.

Second, there is a robustness problem if the transmitter sends an FF and then dies. The next byte could be considered a channel number. However, the next byte should be an FF (the initial transmit selector). To combat this, the protocol accepts any number of FF bytes as a legitimate prefix. So all of the following will select channel 4:
FF 04
FF FF 04
FF FF FF FF FF FF 04

Third, there is the case where the receiver dies and starts  again midstream. This can cause an issue where some data goes to the wrong virtual terminal. There are two features to help with this. 

First, if the transmitter sends FF FD to the receiver, the receiver should retransmit its current channel selector. You could also set a transmitter to periodically send the channel selector since it is harmless to send the same selector more than one time.

Another way to combat this partially is to use the -s switch on the server to prevent any data from flowing to the virtual terminals until one is explicitly selected. Note that this only applies to the start of the server. Once a channel is selected it stays selected until another one is selected.

It is important to realize that each side is both a transmitter and a receiver and the current channel for each is unrelated.  That is, the microcontroller might be sending data for channel 20 while the PC is sending for channel 25. There's no relationship between the sending and receiving channels.

Porting for Microcontrollers
----------------------------------
While the MBED code is complex to make it easy to use, you could easily encode this protocol into anything with a serial port.
Being a transmitter is especially easy. Suppose you had channels 65 (ASCII A) and 66 (ASCII B). You could do something like:

    serial.printf("\xFFASend data to channel A %d\n",n++);
    serial.printf("\xFFBAnd don't forget B! %f\n", f2);

Receiving is a bit more work, but only a little. It would also be possible to use the protocol between two microcontrollers and have no PC involved at all.
 

What about Windows?
----------------------------
Naturally, someone is going to want to use this under Windows. WSL might work and I'd be interested to hear if it does. However, com0com and its related projects can probably do something similar and/or work with a server similar to the Linux one. Probably.


