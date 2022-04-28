#ifndef __SERIALMUX_H
#define __SERIALMUX_H

// This implements the Williams mux serial protocol
// FF [FF...] FE => actual FF character
// FF [FF...] NN => Swtich to channel N (0-FD)
// Note: plan to make FF FD a code to retransmit your current channel back to us
// But not implemented yet

class SerialMux : public Stream
{
private:
    unsigned short incr(unsigned short v) { return (v+1)&mask; }  // increment circular buffer pointer
    unsigned short mask;    // circular buffer mask
protected:
    static Stream *tty;       // base tty for all instances
    static void readthread(void);  // threads for reading and writing the UART
    static void writethread(void);
    static Thread rthread;        // Thread objects for above
    static Thread wthread;
    static SerialMux *head;  // linked list of all SerialMux objects
    SerialMux *next;         // next item on list
    bool blocking;           // true if blocking (default)
    uint8_t ihead, itail, ohead, otail;  // buffers for input/output
    char *ibuffer;
    char *obuffer;
    short id;                 // our channel ID or tag (00-FD)
    Mutex mtx;   // Stream's mutex behaves oddly so we use our own
    static Mutex ttymtx;   // lock up the real port
    // These functions lock before manipulating ihead/ohead/itail/otail per object
    void muxlock() {  mtx.lock(); }
    void muxunlock() {  mtx.unlock(); }
    // These functions lock the real serial port for the object
    static void ttylock(void) {   ttymtx.lock(); }
    static void ttyunlock(void) { ttymtx.unlock(); }
    // remember current input/output
    static short cinput, coutput;
    // sync option - true if you should pitch input until you see a handshake (v2)
    static bool sync;
public:
// buffer size constants
    enum buffsize { BUFFER_SIZE4=2, BUFFER_SIZE8=3, BUFFER_SIZE16=4, BUFFER_SIZE32=5, BUFFER_SIZE64=6,
       BUFFER_SIZE128=7, BUFFER_SIZE256=8 };
    static void start(Stream *basetty, bool syncflag=true);   // start threads
    // constructor & destructor
    SerialMux(int vttyid,buffsize bsize=BUFFER_SIZE16);   // 4=2^4 = 16
    ~SerialMux();
// warning: these enable and disable I/O for everyone -- probably shouldn't use them
    int enable_input(bool e) { return tty?tty->enable_input(e):-1; }
    int enable_output(bool e) { return tty?tty->enable_output(e):-1; }
    // blocking behavior is the default
    int set_blocking(bool blocking) { this->blocking=blocking; return 0; }
    bool is_blocking() { return blocking; }
    // returning 1 from isatty breaks the C library I/O!
   // int isatty() { return 1; }
    bool readable();   // characters available?
    uint8_t available();  // how many characters available?
    bool writable();    // writable? Always return 1 (like USBSerial does)
    // stream needs these to do all the other things it does
    int _putc(int c);  
    int _getc(void);

   // internal read and write (probably should use the normal versions if you can)
    ssize_t _read(void *buffer, size_t size);
    ssize_t _write(const void *buffer, size_t size);

// Find out the current input/output channels
    static short get_current_input() { return cinput; }
    static short get_current_output() { return coutput; }
    // flush things
    void iflush();
    void oflush();
    void flush() { oflush(); iflush(); }
    // Ask the other side to resend its output select packet (handshake) V2 protocol
    static void muxsync(void); 

};

#endif
