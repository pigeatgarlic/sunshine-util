/**
 * @file windows_helper.h
 * @author {Do Huy Hoang} ({huyhoangdo0205@gmail.com})
 * @brief 
 * @version 1.0
 * @date 2022-07-07
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#ifndef __WINDOWS_HELPER_H__
#define __WINDOWS_HELPER_H__

#include <d3d11_datatype.h>

namespace helper
{
    directx::d3d11::Buffer make_buffer          (directx::d3d11::Device device, 
                                                 char* t);

    directx::d3d11::BlendState make_blend       (directx::d3d11::Device device, 
                                                 bool enable) 

    byte*                   make_cursor_image   (byte* img_data, 
                                                 DXGI_OUTDUPL_POINTER_SHAPE_INFO shape_info);
        


    int                     init_render_target_b(directx::d3d11::Device device, 
                                                 directx::d3d11::ShaderResourceView shader_res, 
                                                 directx::d3d11::RenderTargetView render_target, 
                                                 int width, int height, 
                                                 DXGI_FORMAT format);
} // namespace helper



#endif