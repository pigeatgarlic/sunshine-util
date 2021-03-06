/**
 * @file encoder_d3d11_device.h
 * @author {Do Huy Hoang} ({huyhoangdo0205@gmail.com})
 * @brief 
 * @version 1.0
 * @date 2022-07-06
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#ifndef __ENCODER_D3D11_DEVICE_H__
#define __ENCODER_D3D11_DEVICE_H__
#include <sunshine_util.h>
#include <encoder_device.h>


#define NVENC encoder::make_d3d11_encoder()

namespace encoder
{
    Encoder* make_d3d11_encoder();

}


#endif