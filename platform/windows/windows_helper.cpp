/**
 * @file windows_helper.cpp
 * @author {Do Huy Hoang} ({huyhoangdo0205@gmail.com})
 * @brief 
 * @version 1.0
 * @date 2022-07-23
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#include <windows_helper.h>
#include <d3d11_datatype.h>
#include <display_vram.h>
#include <sunshine_util.h>

#include <d3dcompiler.h>
#include <directxmath.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext_d3d11va.h>
}

#include <platform_common.h>
#include <gpu_hw_device.h>
#include <thread>



// TODO: setup shader dir
#define SUNSHINE_ASSETS_DIR "C:/Users/developer/Desktop/sunshine/sunshine-util"
#define SUNSHINE_SHADERS_DIR SUNSHINE_ASSETS_DIR "/directx"

#define DISPLAY_RETRY   5
using namespace std::literals;

namespace helper
{
    d3d11::Buffer
    convert_to_d3d11_buffer(d3d11::Device device, 
                util::Buffer* buffer) 
    {
      int size;
      pointer ptr = BUFFER_CLASS->ref(buffer,&size);
      D3D11_BUFFER_DESC buffer_desc {
        size,
        D3D11_USAGE_IMMUTABLE,
        D3D11_BIND_CONSTANT_BUFFER
      };

      D3D11_SUBRESOURCE_DATA init_data {
        ptr
      };

      d3d11::Buffer buf_p;
      auto status = device->CreateBuffer(&buffer_desc, &init_data, &buf_p);
      if(status) {
        LOG_ERROR("Failed to create buffer");
        BUFFER_CLASS->unref(buffer);
        return NULL;
      }

      BUFFER_CLASS->unref(buffer);
      return buf_p;
    }

    d3d11::BlendState
    make_blend(d3d11::Device device, 
              bool enable) 
    {
      D3D11_BLEND_DESC bdesc {};
      auto &rt                 = bdesc.RenderTarget[0];
      rt.BlendEnable           = enable;
      rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

      if(enable) {
        rt.BlendOp      = D3D11_BLEND_OP_ADD;
        rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;

        rt.SrcBlend  = D3D11_BLEND_SRC_ALPHA;
        rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;

        rt.SrcBlendAlpha  = D3D11_BLEND_ZERO;
        rt.DestBlendAlpha = D3D11_BLEND_ZERO;
      }

      d3d11::BlendState blend;
      auto status = device->CreateBlendState(&bdesc, &blend);
      if(status) {
        LOG_ERROR("Failed to create blend state");
        return NULL;
      }

      return blend;
    }



    util::Buffer*
    make_cursor_image(util::Buffer* img_obj, 
                      DXGI_OUTDUPL_POINTER_SHAPE_INFO shape_info) 
    {
      int size;
      uint32* img_data = (uint32*)BUFFER_CLASS->ref(img_obj,&size);
      const uint32 black       = 0xFF000000;
      const uint32 white       = 0xFFFFFFFF;
      const uint32 transparent = 0;

      switch(shape_info.Type) {
        case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR:
          for (int i = 0; i < size; i++)
          {
            uint32 *pixel = (img_data + i);
            if(*pixel & 0xFF000000) {
              *pixel = transparent;
            }
          }
        case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR:
          BUFFER_CLASS->unref(img_obj);
          return BUFFER_CLASS->duplicate(img_obj);
        default:
          break;
      }

      shape_info.Height /= 2;

      BUFFER_MALLOC(ret,shape_info.Width * shape_info.Height * 4,cursor_img);

      auto bytes       = shape_info.Pitch * shape_info.Height;

      auto pixel_begin = (uint32 *)cursor_img;
      auto pixel_data  = pixel_begin;

      auto and_mask    = img_data;
      auto xor_mask    = img_data + bytes;

      for(auto x = 0; x < bytes; ++x) {
        for(auto c = 7; c >= 0; --c) {
          auto bit        = 1 << c;
          auto color_type = ((*and_mask & bit) ? 1 : 0) + ((*xor_mask & bit) ? 2 : 0);

          switch(color_type) {
          case 0: //black
            *pixel_data = black;
            break;
          case 2: //white
            *pixel_data = white;
            break;
          case 1: //transparent
          {
            *pixel_data = transparent;
            break;
          }
          case 3: //inverse
          {
            auto top_p    = pixel_data - shape_info.Width;
            auto left_p   = pixel_data - 1;
            auto right_p  = pixel_data + 1;
            auto bottom_p = pixel_data + shape_info.Width;

            // Get the x coordinate of the pixel
            auto column = (pixel_data - pixel_begin) % shape_info.Width != 0;

            if(top_p >= pixel_begin && *top_p == transparent) {
              *top_p = black;
            }

            if(column != 0 && left_p >= pixel_begin && *left_p == transparent) {
              *left_p = black;
            }

            if(bottom_p < (uint32*)(cursor_img)) {
              *bottom_p = black;
            }

            if(column != shape_info.Width - 1) {
              *right_p = black;
            }
            *pixel_data = white;
          }
          }

          ++pixel_data;
        }
        ++and_mask;
        ++xor_mask;
      }

      BUFFER_CLASS->unref(img_obj);
      return ret;
    }

    d3d::Blob 
    compile_shader(LPCSTR file, 
                   LPCSTR entrypoint, 
                   LPCSTR shader_model) 
    {
      d3d::Blob msg_p = NULL;
      d3d::Blob compiled_p;

      DWORD flags = D3DCOMPILE_ENABLE_STRICTNESS;

    // #ifndef NDEBUG
    //   flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    // #endif
      std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> converter;

      auto wFile  = converter.from_bytes(file);
      auto status = D3DCompileFromFile(wFile.c_str(), nullptr, nullptr, entrypoint, shader_model, flags, 0, &compiled_p, &msg_p);

      if(msg_p) {
        // BOOST_LOG(warning) << std::string_view { (const char *)msg_p->GetBufferPointer(), msg_p->GetBufferSize() - 1 };
        msg_p->Release();
      }

      if(status) {
        LOG_ERROR("Couldn't compile");
        return NULL;
      }

      return d3d::Blob { compiled_p };
    }

    d3d::Blob 
    compile_pixel_shader(LPCSTR file) {
      return compile_shader(file, "main_ps", "ps_5_0");
    }

    d3d::Blob 
    compile_vertex_shader(LPCSTR file) {
      return compile_shader(file, "main_vs", "vs_5_0");
    }




    platf::MemoryType
    map_dev_type(libav::HWDeviceType type) {
        switch(type) {
        case AV_HWDEVICE_TYPE_D3D11VA:
            return platf::MemoryType::dxgi;
        case AV_HWDEVICE_TYPE_VAAPI:
            return platf::MemoryType::vaapi;
        case AV_HWDEVICE_TYPE_CUDA:
            return platf::MemoryType::cuda;
        case AV_HWDEVICE_TYPE_NONE:
            return platf::MemoryType::system;
        default:
            return platf::MemoryType::unknown;
        }
        return platf::MemoryType::unknown;
    } 


    HLSL*
    init_hlsl() 
    {
      static bool initialize = false;
      static HLSL hlsl = {0};
      if (initialize)
        return &hlsl;
      


      LOG_INFO("Compiling shaders...");
      hlsl.scene_vs_hlsl = helper::compile_vertex_shader(SUNSHINE_SHADERS_DIR "/SceneVS.hlsl");
      if(!hlsl.scene_vs_hlsl) {
        return NULL;
      }

      hlsl.convert_Y_ps_hlsl = helper::compile_pixel_shader(SUNSHINE_SHADERS_DIR "/ConvertYPS.hlsl");
      if(!hlsl.convert_Y_ps_hlsl) {
        return NULL;
      }

      hlsl.convert_UV_ps_hlsl = helper::compile_pixel_shader(SUNSHINE_SHADERS_DIR "/ConvertUVPS.hlsl");
      if(!hlsl.convert_UV_ps_hlsl) {
        return NULL;
      }

      hlsl.convert_UV_vs_hlsl = helper::compile_vertex_shader(SUNSHINE_SHADERS_DIR "/ConvertUVVS.hlsl");
      if(!hlsl.convert_UV_vs_hlsl) {
        return NULL;
      }

      hlsl.scene_ps_hlsl = helper::compile_pixel_shader(SUNSHINE_SHADERS_DIR "/ScenePS.hlsl");
      if(!hlsl.scene_ps_hlsl) {
        return NULL;
      }
      LOG_INFO("Compiled shaders");
      initialize = true;
      return &hlsl;
    }    
} // namespace helper






namespace platf {



  
    Display* 
    get_display(MemoryType hwdevice_type, 
            char* display_name, 
            int framerate) 
    {
        if(hwdevice_type == MemoryType::dxgi) 
            return ((platf::DisplayClass*)DISPLAY_VRAM_CLASS)->init(framerate, display_name);
        
        if(hwdevice_type == MemoryType::system)
            // TODO display ram
        
        return NULL;
    }

    char**
    display_names(MemoryType type) 
    {
      char** display_names;

      HRESULT status;

      //  "Detecting monitors...";


      dxgi::Factory factory;
      status = CreateDXGIFactory1(IID_IDXGIFactory1, (void **)&factory);
      if(FAILED(status)) {
        LOG_ERROR("Failed to create DXGIFactory1");
        return {};
      }

      dxgi::Adapter adapter;
      for(int x = 0; factory->EnumAdapters1(x, &adapter) != DXGI_ERROR_NOT_FOUND; ++x) {
        DXGI_ADAPTER_DESC1 adapter_desc;
        adapter->GetDesc1(&adapter_desc);

        // BOOST_LOG(debug)
        //   << std::endl
        //   << "====== ADAPTER ====="sv << std::endl
        //   << "Device Name      : "sv << converter.to_bytes(adapter_desc.Description) << std::endl
        //   << "Device Vendor ID : 0x"sv << util::hex(adapter_desc.VendorId).to_string_view() << std::endl
        //   << "Device Device ID : 0x"sv << util::hex(adapter_desc.DeviceId).to_string_view() << std::endl
        //   << "Device Video Mem : "sv << adapter_desc.DedicatedVideoMemory / 1048576 << " MiB"sv << std::endl
        //   << "Device Sys Mem   : "sv << adapter_desc.DedicatedSystemMemory / 1048576 << " MiB"sv << std::endl
        //   << "Share Sys Mem    : "sv << adapter_desc.SharedSystemMemory / 1048576 << " MiB"sv << std::endl
        //   << std::endl
        //   << "    ====== OUTPUT ======"sv << std::endl;

        IDXGIOutput* output = NULL;
        for(int y = 0; adapter->EnumOutputs(y, &output) != DXGI_ERROR_NOT_FOUND; ++y) {
          DXGI_OUTPUT_DESC desc;
          output->GetDesc(&desc);

          char* name =  (char*)desc.DeviceName;
          long width  = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
          long height = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;



          // BOOST_LOG(debug)
          //   << "    Output Name       : "sv << device_name << std::endl
          //   << "    AttachedToDesktop : "sv << (desc.AttachedToDesktop ? "yes"sv : "no"sv) << std::endl
          //   << "    Resolution        : "sv << width << 'x' << height << std::endl
          //   << std::endl;
          *(display_names + y) = name;
        }
      }
      return display_names;
    }

    Display*
    tryget_display(libav::HWDeviceType type, 
                  char* display_name, 
                  int framerate) 
    {
        static Display* disp = NULL;
        // We try this twice, in case we still get an error on reinitialization
        for(int x = 0; x < DISPLAY_RETRY; ++x) {
            if (disp)
                break;
            disp = get_display(helper::map_dev_type(type), display_name, framerate);
            if (disp)
                break;
            std::this_thread::sleep_for(200ms);
        }
        return disp;
    }
} // namespace platf
