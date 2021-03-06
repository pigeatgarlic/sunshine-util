/**
 * @file capturer.h
 * @author {Do Huy Hoang} ({huyhoangdo0205@gmail.com})
 * @brief 
 * @version 1.0
 * @date 2022-07-06
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#ifndef __SUNSHINE_CAPTURER_H__
#define __SUNSHINE_CAPTURER_H__


#include <sunshine_util.h>
#include <sunshine_config.h>


namespace session
{
    typedef struct _Session
    {
        util::Broadcaster* shutdown_event;

        util::QueueArray* packet_queue;
    }Session;
    

    void        init_session        (Session* session);

    void        start_session       (Session* session);

        
} // namespace singleton


#endif