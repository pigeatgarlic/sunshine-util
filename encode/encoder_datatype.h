/**
 * @file encoder_datatype.h
 * @author {Do Huy Hoang} ({huyhoangdo0205@gmail.com})
 * @brief 
 * @version 1.0
 * @date 2022-07-23
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#ifndef __ENCODER_DATATYPE_H__
#define __ENCODER_DATATYPE_H__

#include <sunshine_util.h>
#include <platform_common.h>

namespace encoder
{    
    /**
     * @brief 
     * 
     */
    struct _EncodeContext{
        libav::CodecContext* context;
        libav::Codec* codec;

        platf::Device* device;
    };


    struct _Session {
        int64 pts;

        libav::Packet* packet;

        encoder::EncodeContext encode;

        rtp::RtpContext* rtp;
    };

} // namespace encoder


#endif