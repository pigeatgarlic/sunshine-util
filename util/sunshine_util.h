/**
 * @file sunshine_util.h
 * @author {Do Huy Hoang} ({huyhoangdo0205@gmail.com})
 * @brief 
 * @version 1.0
 * @date 2022-07-10
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#ifndef __SUNSHINE_UTIL_H___
#define __SUNSHINE_UTIL_H___

#include <avcodec_wrapper.h>
#include <sunshine_datatype.h>
#include <sunshine_object.h>
#include <sunshine_array.h>
#include <sunshine_queue.h>
#include <sunshine_macro.h>
#include <sunshine_log.h>
#include <sunshine_event.h>


namespace rtp {
typedef struct _RtpContext RtpContext;
}

namespace encoder {
typedef struct _EncodeContext EncodeContext;

typedef struct _Session Session;

typedef struct _EncodeThreadContext EncodeThreadContext;

typedef struct _Config  Config;

typedef struct _Encoder Encoder;
}


#endif