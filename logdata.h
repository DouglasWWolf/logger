//==========================================================================================================
// logdata.h - Defines a thread-safe structure that maintains a queue of log-data
//==========================================================================================================
#pragma once
#include <time.h>
#include <deque>
#include <string>
#include <time.h>
#include "cthread.h"

using namespace std;

struct log_data_t
{
    time_t  timestamp;
    int     port;
    string  data;
};


//==========================================================================================================
// CLogData - A thread-safe double-ended queue
//==========================================================================================================
class CLogData
{
public:
    CLogData() {m_max_entries = 1000;}

    // Determine the maximum number of data entries
    void    set_max_entries(int count) {m_max_entries = count;}

    // Append a data item to the queue
    void    append(int port, const string& data);

    // Prevent other threads from accessing m_data
    void    lock() {m_mutex.lock();}

    // Allow other threads to access m_data
    void    unlock() {m_mutex.unlock();}

    // Get a reference to the queue data
    deque<log_data_t>& get_data() {return m_data;}    

protected:

    // Maximum number of entries in our queue
    int m_max_entries;

    // All access to the queue is protected by this
    CMutex m_mutex;

    // A double-ended queue of our log data
    deque<log_data_t> m_data;

};
//==========================================================================================================
