/**
 * @file hw_device.cpp
 * @author {Do Huy Hoang} ({huyhoangdo0205@gmail.com})
 * @brief 
 * @version 1.0
 * @date 2022-07-23
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#include <platform_common.h>
#include <gpu_hw_device.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext_d3d11va.h>
}
#include <display_base.h>

#include <windows_helper.h>

namespace gpu
{    
    void d3d11_device_init_view_port(GpuDevice* self,
                                     float width, 
                                     float height);
    int 
    hw_device_set_frame(platf::Device* self,
                        libav::Frame* frame) 
    {
      GpuDevice* hw = (GpuDevice*)self;
      if(hw->hwframe)
        av_frame_free(&hw->hwframe);

      hw->hwframe    = frame;
      hw->base.frame = frame;

      d3d11::Device device = (d3d11::Device)hw->base.data;

      int out_width  = frame->width;
      int out_height = frame->height;

      float in_width  = hw->img.display->width;
      float in_height = hw->img.display->height;

      // // Ensure aspect ratio is maintained
      auto scalar       = std::fminf(out_width / in_width, out_height / in_height);
      auto out_width_f  = in_width * scalar;
      auto out_height_f = in_height * scalar;

      // result is always positive
      auto offsetX = (out_width - out_width_f) / 2;
      auto offsetY = (out_height - out_height_f) / 2;

      hw->outY_view  = D3D11_VIEWPORT 
      { 
        offsetX, 
        offsetY, 
        out_width_f, 
        out_height_f, 
        0.0f, 1.0f 
      };

      hw->outUV_view = D3D11_VIEWPORT 
      { 
        offsetX / 2, 
        offsetY / 2, 
        out_width_f / 2, 
        out_height_f / 2, 
        0.0f, 1.0f 
      };

      D3D11_TEXTURE2D_DESC t {};
      t.Width            = out_width;
      t.Height           = out_height;
      t.MipLevels        = 1;
      t.ArraySize        = 1;
      t.SampleDesc.Count = 1;
      t.Usage            = D3D11_USAGE_DEFAULT;
      t.Format           = hw->format;
      t.BindFlags        = D3D11_BIND_RENDER_TARGET;

      auto status = device->CreateTexture2D(&t, NULL, &hw->img.texture);
      if(FAILED(status)) {
        LOG_ERROR("Failed to create render target texture");
        return -1;
      }

      hw->img.base.width       = out_width;
      hw->img.base.height      = out_height;
      hw->img.base.data        = (uint8*)hw->img.texture;
      hw->img.base.row_pitch   = out_width * 4;
      hw->img.base.pixel_pitch = 4;

      int inf_size = 16 / sizeof(float);
      float info_in[inf_size] { 1.0f / (float)out_width }; //aligned to 16-byte
      util::Buffer* buf = BUFFER_CLASS->init(info_in,16,DO_NOTHING);
      hw->info_scene = helper::convert_to_d3d11_buffer(device, buf);

      if(!info_in) {
        LOG_ERROR("Failed to create info scene buffer");
        return -1;
      }

      D3D11_RENDER_TARGET_VIEW_DESC nv12_rt_desc {
        DXGI_FORMAT_R8_UNORM,
        D3D11_RTV_DIMENSION_TEXTURE2D
      };

      status = device->CreateRenderTargetView(hw->img.texture, &nv12_rt_desc, &hw->nv12_Y_rt);
      if(FAILED(status)) {
        LOG_ERROR("Failed to create render target view");
        return -1;
      }

      nv12_rt_desc.Format = DXGI_FORMAT_R8G8_UNORM;
      status = device->CreateRenderTargetView(hw->img.texture, &nv12_rt_desc, &hw->nv12_UV_rt);
      if(FAILED(status)) {
        LOG_ERROR("Failed to create render target view");
        return -1;
      }

      // Need to have something refcounted
      if(!frame->buf[0]) {
        frame->buf[0] = av_buffer_allocz(sizeof(AVD3D11FrameDescriptor));
      }

      auto desc     = (AVD3D11FrameDescriptor *)frame->buf[0]->data;
      desc->texture = (d3d11::Texture2D)hw->img.base.data;
      desc->index   = 0;

      frame->data[0] = hw->img.base.data;
      frame->data[1] = 0;

      frame->linesize[0] = hw->img.base.row_pitch;

      frame->height = hw->img.base.height;
      frame->width  = hw->img.base.width;

      return 0;
    }

    /**
     * @brief 
     * 
     * @param display 
     * @param device_p 
     * @param device_ctx_p 
     * @param pix_fmt 
     * @return int 
     */
    platf::Device*
    hw_device_init(platf::Display* display, 
                   d3d11::Device device_p, 
                   d3d11::DeviceContext device_ctx_p,
                   platf::PixelFormat pix_fmt) 
    {
      GpuDevice* self = (GpuDevice*)malloc(sizeof(GpuDevice));
      memset((pointer)self,0,sizeof(GpuDevice));

      self->base.klass = (platf::DeviceClass*)d3d11_device_class_init();
      helper::HLSL* hlsl = helper::init_hlsl();
      HRESULT status;

      device_p->AddRef();
      self->base.data = device_p;
      self->device_ctx = device_ctx_p;

      self->format = (pix_fmt == platf::PixelFormat::nv12 ? DXGI_FORMAT_NV12 : DXGI_FORMAT_P010);
      status = device_p->CreateVertexShader(hlsl->scene_vs_hlsl->GetBufferPointer(), hlsl->scene_vs_hlsl->GetBufferSize(), nullptr, &self->scene_vs);
      if(status) {
        LOG_ERROR("Failed to create scene vertex shader");
        return NULL;
      }

      status = device_p->CreatePixelShader(hlsl->convert_Y_ps_hlsl->GetBufferPointer(), hlsl->convert_Y_ps_hlsl->GetBufferSize(), nullptr, &self->convert_Y_ps);
      if(status) {
        LOG_ERROR("Failed to create convertY pixel shader");
        return NULL;
      }

      status = device_p->CreatePixelShader(hlsl->convert_UV_ps_hlsl->GetBufferPointer(), hlsl->convert_UV_ps_hlsl->GetBufferSize(), nullptr, &self->convert_UV_ps);
      if(status) {
        LOG_ERROR("Failed to create convertUV pixel shader");
        return NULL;
      }

      status = device_p->CreateVertexShader(hlsl->convert_UV_vs_hlsl->GetBufferPointer(), hlsl->convert_UV_vs_hlsl->GetBufferSize(), nullptr, &self->convert_UV_vs);
      if(status) {
        LOG_ERROR("Failed to create convertUV vertex shader");
        return NULL;
      }

      status = device_p->CreatePixelShader(hlsl->scene_ps_hlsl->GetBufferPointer(), hlsl->scene_ps_hlsl->GetBufferSize(), nullptr, &self->scene_ps);
      if(status) {
        LOG_ERROR("Failed to create scene pixel shader");
        return NULL;
      }


      platf::Color* color = platf::get_color();
      util::Buffer* buf = BUFFER_CLASS->init(color,sizeof(platf::Color[4]),DO_NOTHING);
      self->color_matrix = helper::convert_to_d3d11_buffer(device_p, buf);
      if(!self->color_matrix) {
        LOG_ERROR("Failed to create color matrix buffer");
        return NULL;
      }

      D3D11_INPUT_ELEMENT_DESC layout_desc {
        "SV_Position", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0
      };

      status = device_p->CreateInputLayout(
        &layout_desc, 1,
        hlsl->convert_UV_vs_hlsl->GetBufferPointer(), 
        hlsl->convert_UV_vs_hlsl->GetBufferSize(),
        &self->input_layout);

      self->img.display = display;

      // Color the background black, so that the padding for keeping the aspect ratio
      // is black
      if(self->img.display->klass->dummy_img(self->img.display,&self->back_img.base)) {
        LOG_WARNING("Couldn't create an image to set background color to black");
        return NULL;
      }

      D3D11_SHADER_RESOURCE_VIEW_DESC desc {
        DXGI_FORMAT_B8G8R8A8_UNORM,
        D3D11_SRV_DIMENSION_TEXTURE2D
      };
      desc.Texture2D.MipLevels = 1;

      status = device_p->CreateShaderResourceView(self->back_img.texture, &desc, &self->back_img.input_res);
      if(FAILED(status)) {
        LOG_ERROR("Failed to create input shader resource view");
        return NULL;
      }

      self->device_ctx->IASetInputLayout(self->input_layout);
      self->device_ctx->PSSetConstantBuffers(0, 1, &self->color_matrix);
      self->device_ctx->VSSetConstantBuffers(0, 1, &self->info_scene);

      return (platf::Device*)self;
    }

    void 
    hw_device_free(platf::Device* dev)
    {
        free((pointer)dev);
    }

    void 
    hw_device_set_colorspace(platf::Device* dev,
                             uint32 colorspace, 
                             uint32 color_range) 
    {
      GpuDevice* self = (GpuDevice*)dev;
      platf::Color* colors = platf::get_color();
      switch(colorspace) {
      case 5: // SWS_CS_SMPTE170M
        self->color_p = colors;
        break;
      case 1: // SWS_CS_ITU709
        self->color_p = colors + 2;
        break;
      case 9: // SWS_CS_BT2020
      default:
        LOG_WARNING("Colorspace: not yet supported: switching to default");
        self->color_p = colors;
      };

      if(color_range > 1) {
        // Full range
        ++self->color_p;
      }


      util::Buffer* buf = BUFFER_CLASS->init(colors,sizeof(platf::Color[4]),DO_NOTHING);
      d3d11::Buffer color_matrix = (d3d11::Buffer)helper::convert_to_d3d11_buffer((d3d11::Device)self->base.data, buf);
      if(!color_matrix) {
        LOG_WARNING("Failed to create color matrix");
        return;
      }

      self->device_ctx->PSSetConstantBuffers(0, 1, &color_matrix);
      self->color_matrix = color_matrix;
    }

    /**
     * @brief 
     * 
     * @param img_base 
     * @return int 
     */
    int 
    hw_device_convert(platf::Device* dev,
                      platf::Image* img_base) 
    {
        GpuDevice* self = (GpuDevice*)dev;
        ImageGpu* img = (ImageGpu*)img_base;

        self->device_ctx->IASetInputLayout(self->input_layout);

        d3d11_device_init_view_port(self,self->img.base.width, self->img.base.height);
        self->device_ctx->OMSetRenderTargets(1, &self->nv12_Y_rt, nullptr);
        self->device_ctx->VSSetShader(self->scene_vs, nullptr, 0);
        self->device_ctx->PSSetShader(self->convert_Y_ps, nullptr, 0);
        self->device_ctx->PSSetShaderResources(0, 1, &self->back_img.input_res);
        self->device_ctx->Draw(3, 0);

        self->device_ctx->RSSetViewports(1, &self->outY_view);
        self->device_ctx->PSSetShaderResources(0, 1, &img->input_res);
        self->device_ctx->Draw(3, 0);

        // Artifacts start appearing on the rendered image if Sunshine doesn't flush
        // before rendering on the UV part of the image.
        self->device_ctx->Flush();

        d3d11_device_init_view_port(self,self->img.base.width / 2, self->img.base.height / 2);
        self->device_ctx->OMSetRenderTargets(1, &self->nv12_UV_rt, nullptr);
        self->device_ctx->VSSetShader(self->convert_UV_vs, nullptr, 0);
        self->device_ctx->PSSetShader(self->convert_UV_ps, nullptr, 0);
        self->device_ctx->PSSetShaderResources(0, 1, &self->back_img.input_res);
        self->device_ctx->Draw(3, 0);

        self->device_ctx->RSSetViewports(1, &self->outUV_view);
        self->device_ctx->PSSetShaderResources(0, 1, &img->input_res);
        self->device_ctx->Draw(3, 0);
        self->device_ctx->Flush();

        return 0;
    }

    void 
    _init_view_port(GpuDevice* self,
                    float x, 
                    float y, 
                    float width, 
                    float height) 
    {
      D3D11_VIEWPORT view {
          x, y,
          width, height,
          0.0f, 1.0f
      };

      self->device_ctx->RSSetViewports(1, &view);
    }

    void 
    d3d11_device_init_view_port(GpuDevice* self,
                                float width, 
                                float height) 
    {
      _init_view_port((GpuDevice*)self,0.0f, 0.0f, width, height);
    }

    GpuDeviceClass*       
    d3d11_device_class_init()
    {
        static bool initialize = FALSE;
        static GpuDeviceClass klass = {0};
        if (initialize)
            return &klass;
        
        klass.base.convert            = hw_device_convert;
        klass.base.init               = hw_device_init;
        klass.base.finalize           = hw_device_free;
        klass.base.set_frame          = hw_device_set_frame;
        klass.base.set_colorspace     = hw_device_set_colorspace;
        klass.init_view_port          = d3d11_device_init_view_port;
        initialize = TRUE;
        return &klass;
    }
    
} // namespace gpu


