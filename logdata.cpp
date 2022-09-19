//==========================================================================================================
// logdata.cpp - Implements a thread-safe structure that maintains a queue of log-data 
//==========================================================================================================
#include "logdata.h"



//==========================================================================================================
// append() - Appends an entry to the queue of logged data
//==========================================================================================================
void CLogData::append(int port, const string& data)
{
    // Build a queue entry from our input data
    struct log_data_t entry = {time(NULL), port, data};

    // Ensure thread-safe access to m_data
    lock();

    // If our queue is full, delete the oldest entry
    if (m_data.size() >= m_max_entries) m_data.pop_front();

    // Add the current entry to the back of the queue
    m_data.push_back(entry);

    // Allow other threads to access m_data;
    unlock();
}
//==========================================================================================================


