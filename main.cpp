//==========================================================================================================
// logger - ASCII string logger
//==========================================================================================================
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <signal.h>
#include "config_file.h"
#include "cmd_line.h"
#include "cthread.h"
#include "udpsock.h"
#include "netsock.h"
#include "logdata.h"
#include "mgmt_server.h"

using namespace std;


//==========================================================================================================
// Listener() - A thread that listens for incoming UDP messages to be logged
//==========================================================================================================
class CListener : public CThread
{
public:
    void    spawn(int port);

protected:

    void    main();

    int     m_port;
};
//==========================================================================================================


//==========================================================================================================
// CLiveLog - A thread that outputs logging messages in real time
//==========================================================================================================
class CLiveLog : public CThread
{
public:

    // Called by another thread to spawn this server
    void    spawn(int port);

    // Called by other threads to write messages to the live-log
    void    send(const char* tag, const char* message);

protected:

    void    main();
    int     m_port;
    bool    m_has_client;
    CMutex  m_mutex;
    NetSock m_server;
};
//==========================================================================================================


void fetch_specs();
void dump_log_data(NetSock& server);
void show_help();

struct conf_t
{
    int             log_port;
    int             server_port;
    int             live_log_port;
    int             max_entries;
    int             id_length;
    bool            use_section;
    string          section;
} conf;

CLogData    DataLog;
CLiveLog    LiveLog;
NetSock     Server;
CCmdLine    CmdLine;
CMgmtServer Manager;
CListener   Listener;

// This is the name of the configuration file
string   config_file = "logger.conf";

//==========================================================================================================
// main() - Execution starts here
//==========================================================================================================
int main(int argc, char** argv)
{
    string s;
    int mport;
    map<int, string>::iterator it;

    // Ignore SIGPIPE so that writes to closed sockets don't crash us
    signal(SIGPIPE, SIG_IGN);

    // Declare the valid command-line switches
    CmdLine.declare_switch("-config",  CLP_REQUIRED);
    CmdLine.declare_switch("-section", CLP_REQUIRED);
    CmdLine.declare_switch("-mport",   CLP_REQUIRED);

    // Parse the command line
    if (!CmdLine.parse(argc, argv)) show_help();

    // Did the user over-ride the name of our config file?
    if (CmdLine.has_switch("-config", &s)) config_file = s;

    // Did the user declare a configuration section?
    conf.use_section = CmdLine.has_switch("-section", &conf.section);

    // Fetch the configuration specs
    fetch_specs();

    // Tell the data-log the maximum number of entries it should keep
    DataLog.set_max_entries(conf.max_entries);

    // Spin up the thread that listens for incoming log messages
    Listener.spawn(conf.log_port);

    // Spin up the live-log thread
    LiveLog.spawn(conf.live_log_port);

    // If there was an "-mport" switch on the command line, spawn the process manager
    if (CmdLine.has_switch("-mport", &mport)) Manager.spawn(&mport);

    // Tell the user who we are and what we're doing
    printf("System logger listening on port %i\n", conf.server_port);

    // Sit in a loop forever, waiting for a client to connect
    while(true)
    {
        // Create a TCP server
        if (!Server.create_server(conf.server_port, "", AF_INET))
        {
            fprintf(stderr, "Logger can't create server on port %i\n", conf.server_port);
            exit(1);
        }
    
        // Wait for someone to connect to our TCP server
        Server.listen_and_accept();

        // Send all of the log data to the connected client
        dump_log_data(Server);

        // We're done.  Send an End-of-File message and close the port.
        Server.send("EOF\n");
        Server.close();
    }
}
//==========================================================================================================




//==========================================================================================================
// fetch_specs() - Reads and parses the configuration file
//
// On Exit: conf.port_map    = Maps a port number to a tag string
//          conf.server_port = The TCP port to listen on for dumping the log
//==========================================================================================================
void fetch_specs()
{
    CConfigFile cf;

    // Open the config file and bail if we can't
    if (!cf.read(config_file)) exit(1);

    // If the user wants us to look in a specific section, make it so
    if (conf.use_section) cf.set_current_section(conf.section);

    try
    {
        cf.get("server_port",   &conf.server_port);
        cf.get("live_log_port", &conf.live_log_port);
        cf.get("max_entries",   &conf.max_entries);
        cf.get("log_port",      &conf.log_port);
        cf.get("id_length",     &conf.id_length);

    }
    catch(const std::exception& e)
    {
        fprintf(stderr, "%s\n", e.what());
        exit(1);
    }
}
//==========================================================================================================


//==========================================================================================================
// show_help() - Show the user the valid switches, then exit
//==========================================================================================================
void show_help()
{
    printf("usage: logger \n");
    printf("  -config <filename>\n");
    printf("  -section <section_name\n");
    exit(1);
}
//==========================================================================================================




//==========================================================================================================
// transmit_log_entry() - Formats a log entry and writes it to the specified server
//
// Passed:   entry  = timestamp, port, and message to be logged
//           server = A reference to the NetSock where the formatted message should be written
//
// Returns:  true if the message was succesfully written, false if the server port was closed
//==========================================================================================================
bool transmit_log_entry(log_data_t& entry, NetSock& server)
{
    char line[1024];
    struct tm tm;

    // Handy one-letter references to the "struct tm" fields we care about
    int& h = tm.tm_hour;
    int& m = tm.tm_min;
    int& s = tm.tm_sec;

    // Look up the tag that corresponds to the UDP port that recevied the message
    const char* tag = entry.tag.c_str();

    // Get a const char* to the line of data
    const char* message = entry.data.c_str();

    // Break the timestamp out into components
    localtime_r(&entry.timestamp, &tm);

    // Format the time, tag, and data
    sprintf(line, "%02d:%02d:%02d (%-*s): %s\n", h, m, s, conf.id_length, tag, message);

    // And send this line to the client
    int bytes_sent = server.send(line, strlen(line));

    // Tell the caller whether or not this worked
    return (bytes_sent > 0);
}
//==========================================================================================================


//==========================================================================================================
// dump_log_data() - Sends the entire log to the client connected to the server
//==========================================================================================================
void dump_log_data(NetSock& server)
{
    deque<log_data_t>::iterator it;

    // Prevent other threads from altering the deque
    DataLog.lock();

    // Get a reference to the log data
    deque<log_data_t>& log_data = DataLog.get_data();
    
    // Loop through every item of log data, and transmit it.
    for (it = log_data.begin(); it != log_data.end(); ++it)
    {
        transmit_log_entry(*it, server);
    }

    // Allow other threads to have access to the deque
    DataLog.unlock();
}
//==========================================================================================================




//==========================================================================================================
// spawn() - Spawns the thread
//==========================================================================================================
void CListener::spawn(int port)
{
    m_port = port;
    CThread::spawn();
}
//==========================================================================================================


//==========================================================================================================
// main() - This thread listens for incoming UPD messages and logs them
//==========================================================================================================
void CListener::main()
{
    UDPSock udp;
    char    buffer[1024], *p;
    const char  *pmsg, *ptag;

    // Create the server port
    if (!udp.create_server(m_port, "", AF_INET))
    {
        fprintf(stderr, "Can't create listener on UDP port %i\n", m_port);
        exit(1);
    }


    // Every time a message is received...
    while (udp.receive(buffer, sizeof buffer)) 
    {
        // Chomp any carriage return or linefeed at the end of the message
        p = strchr(buffer, 10); if (p) *p = 0;
        p = strchr(buffer, 13); if (p) *p = 0;
        
        // Does the message contain the '$' delimeter that divides the tag from the message?
        p = strchr(buffer, '$');

        // If that delimieter exists, divide the buffer into a tag and a message
        if (p)
        {
            *p = 0;
            pmsg = p+1;
            ptag = buffer;
        }

        // Otherwise, the entire buffer is the message and the tag is an empty string
        else
        {
            pmsg = buffer;
            ptag = "";            
        }


        // Stuff the message into our queue
        DataLog.append(ptag, pmsg);
        
        // And send the message to the live-log TCP port
        LiveLog.send(ptag, pmsg);
    }
}
//==========================================================================================================



//==========================================================================================================
// spawn() - Spawns the thread
//==========================================================================================================
void CLiveLog::spawn(int port)
{
    m_has_client = false;
    m_port = port;
    CThread::spawn();
}
//==========================================================================================================


//==========================================================================================================
// main() - Manages the TCP port
//==========================================================================================================
void CLiveLog::main()
{
    char    c;

again:

    // We do not currently have a client connected
    m_mutex.lock();
    m_has_client = false;
    m_mutex.unlock();

    // Create the server and wait for a connection
    if (!m_server.create_server(m_port, "", AF_INET))
    {
        fprintf(stderr, "can't create server on TCP port %i\n", m_port);
        exit(1);
    }

    // Wait for someone to connect to us
    m_server.listen_and_accept();

    // We have a client connected
    m_mutex.lock();
    m_has_client = true;
    m_mutex.unlock();

    // Send a message so that the client knows there is someone here
    dump_log_data(m_server);

    // If the client closes the socket, start over
    while (true) if (m_server.receive(&c, 1) < 1) goto again;
}
//==========================================================================================================



//==========================================================================================================
// send() - Sends data to the live-log output stream
//==========================================================================================================
void CLiveLog::send(const char* tag, const char* message)
{
    // Ensure thread-synchronized access to both "m_has_client" and "m_server"
    UniqueLock lock(m_mutex);

    // If there's no client connected, do nothing
    if (!m_has_client) return;

    // Turn the data that describes our message into a log_data_t
    log_data_t entry = {time(NULL), tag, message};

    // Transmit the formatted message via the our TCP server
    transmit_log_entry(entry, m_server);
}
//==========================================================================================================

