//==========================================================================================================
// logger - ASCII string logger
//==========================================================================================================
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
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
class Listener : public CThread
{
public:
    void    spawn(int port);

protected:

    void    main();

    int     m_port;
};
//==========================================================================================================


void fetch_specs();
void dump_log_data();
void show_help();
struct
{
    map<int,string> port_map;
    int             server_port;
    bool            use_section;
    string          section;
    int             max_entries;
} conf;

CLogData    DataLog;
NetSock     Server;
CCmdLine    CmdLine;
CMgmtServer Manager;

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

    // Loop through each port we need to launch a listener on
    for (it = conf.port_map.begin(); it != conf.port_map.end(); ++it)
    {
        // Fetch the UDP port number
        int port = it->first;

        // Create a new thread object
        Listener *p_thread = new Listener();

        // And spawn it, telling it what port to listen on
        p_thread->spawn(port);
    }

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

        // Lock the datalog so we can read the whole thing
        DataLog.lock();

        // Send all of the log data to the connected client
        dump_log_data();

        // Allow other threads to access the log
        DataLog.unlock();

        // And close the server to indicate we've output the entire log
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
    CConfigScript cs;

    // Open the config file and bail if we can't
    if (!cf.read(config_file)) exit(1);

    // If the user wants us to look in a specific section, make it so
    if (conf.use_section) cf.set_current_section(conf.section);

    try
    {
        // Fetch the list of ports we're going to create servers on
        cf.get("ports",       &cs);
        cf.get("server_port", &conf.server_port);
        cf.get("max_entries", &conf.max_entries);
    }
    catch(const std::exception& e)
    {
        fprintf(stderr, "%s\n", e.what());
        exit(1);
    }


    // Build the dictionary that maps port number to tag string
    while (cs.get_next_line())
    {
        string tag  = cs.get_next_token();
        int    port = cs.get_next_int();
        conf.port_map[port] = tag;
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
// get_longest_tag() - Returns the length of the longest tag in the port_map
//==========================================================================================================
int get_longest_tag()
{
    int longest = -1;
    map<int,string>::iterator it;

    for (it = conf.port_map.begin(); it != conf.port_map.end(); ++it)
    {
        int tag_length = it->second.length();
        if (tag_length > longest) longest = tag_length;
    }

    return longest;
}
//==========================================================================================================


//==========================================================================================================
// dump_log_data() - Sends the entire log to the client connected to the server
//==========================================================================================================
void dump_log_data()
{
    deque<log_data_t>::iterator it;
    char line[1024];
    struct tm tm;

    // Find out how long the longest tag is
    int longest_tag = get_longest_tag();

    // Get a reference to the log data
    deque<log_data_t>& log_data = DataLog.get_data();
    
    // Loop through every item of log data
    for (it = log_data.begin(); it != log_data.end(); ++it)
    {
        // Get a convenient reference to this entry
        log_data_t& entry = *it;

        // Look up the tag that corresponds to the UDP port that recevied the message
        const char* tag = conf.port_map[entry.port].c_str();

        // Get a const char* to the line of data
        const char* data = entry.data.c_str();

        // Break the timestamp out into components
        localtime_r(&entry.timestamp, &tm);

        // Format the time, tag, and data
        sprintf(line, "%02d:%02d:%02d (%-*s): %s\n", tm.tm_hour, tm.tm_min, tm.tm_sec, 
                       longest_tag, tag, data);

        // And send this line to the client
        Server.send(line, strlen(line));
    }

    // Send the "End of file" marker
    Server.send("EOF\n");
}
//==========================================================================================================




//==========================================================================================================
// spawn() - Spawns the thread
//==========================================================================================================
void Listener::spawn(int port)
{
    m_port = port;
    CThread::spawn();
}
//==========================================================================================================


//==========================================================================================================
// main() - This thread listens for incoming UPD messages and logs them
//==========================================================================================================
void Listener::main()
{
    UDPSock udp;
    char    message[1024], *p;

    // Create the server port
    if (!udp.create_server(m_port, "", AF_INET))
    {
        fprintf(stderr, "Can't create listener on UDP port %i\n", m_port);
        exit(1);
    }

    // Every time a message is received...
    while (udp.receive(message, sizeof message)) 
    {
        // Chomp any carriage return or linefeed at the end of the message
        p = strchr(message, 10); if (p) *p = 0;
        p = strchr(message, 13); if (p) *p = 0;
        
        // Stuff the message into our queue
        DataLog.append(m_port, message);
    }
}
//==========================================================================================================
