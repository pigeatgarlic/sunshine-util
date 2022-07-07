/**
 * @file encoder.cpp
 * @author {Do Huy Hoang} ({huyhoangdo0205@gmail.com})
 * @brief 
 * @version 1.0
 * @date 2022-07-06
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#include <thread>


#include <sunshine_queue.h>
#include <sunshine_log.h>
#include <sunshine_object.h>
#include <sunshine_bitstream.h>
#include <sunshine_event.h>

#include <encoder_datatype.h>
#include <encoder_packet.h>

#include <encoder_d3d11_device.h>
#include <sunshine_array.h>
#include <common.h>
#include <display.h>



namespace encoder {

    void do_nothing(pointer data){};

    typedef enum _Status {
        ok,
        reinit,
        timeout,
        error
    }Status;

    /**
     * @brief 
     * 
     */
    typedef struct _SyncSessionContext{
        event::Broadcaster* shutdown_event;
        event::Broadcaster* idr_event;
        event::Broadcaster* join_event;

        util::QueueArray* packet_queue;

        int frame_nr;
        Config config;
        Encoder* encoder;
        platf::Display* encoder;
        pointer channel_data;
    }SyncSessionContext;

    typedef struct _SyncSession{
        SyncSessionContext* ctx;
        platf::Image* img_tmp;
        Session session;
    }SyncSession;

    typedef struct _Session {
        libav::CodecContext* context;
        platf::HWDevice* device;

        /**
         * @brief 
         * Replace
         */
        ArrayObject* replacement_array;

        bitstream::NAL sps;
        bitstream::NAL vps;

        int inject;
    }Session;

    typedef struct _CaptureThreadSyncContext {
        object::Object base_object;

        /**
         * @brief 
         * SyncedSessionContext queue
         */
        util::QueueArray* sync_session_queue;
    }CaptureThreadSyncContext;


    PacketClass*
    packet_class_init()
    {
        static PacketClass klass;
        return &klass;
    }


    /**
     * @brief 
     * 
     * @param ctx 
     * @param hwdevice_ctx 
     * @param format 
     * @return int 
     */
    int 
    hwframe_ctx(libav::CodecContext ctx, 
                libav::BufferRef* hwdevice, 
                libav::PixelFormat format) 
    {
        libav::BufferRef* frame_ref = av_hwframe_ctx_alloc(hwdevice);

        auto frame_ctx               = (AVHWFramesContext *)frame_ref->data;
        frame_ctx->format            = ctx->pix_fmt;
        frame_ctx->sw_format         = format;
        frame_ctx->height            = ctx->height;
        frame_ctx->width             = ctx->width;
        frame_ctx->initial_pool_size = 0;

        int err = av_hwframe_ctx_init(frame_ref);
        if( err < 0) 
            return err;
        
        ctx->hw_frames_ctx = av_buffer_ref(frame_ref);
        return 0;
    }

    /**
     * @brief 
     * 
     * @param frame_nr 
     * @param session 
     * @param frame 
     * @param packets 
     * @param channel_data 
     * @return int 
     */
    int 
    encode(int frame_nr, 
           Session* session, 
           libav::Frame* frame, 
           util::QueueArray* packets, 
           void *channel_data) 
    {
        frame->pts = (int64_t)frame_nr;

        libav::CodecContext* ctx = session->ctx;

        bitstream::NAL sps = session->sps;
        bitstream::NAL vps = session->vps;

        /* send the frame to the encoder */
        auto ret = avcodec_send_frame(ctx, frame);
        if(ret < 0) {
            char err_str[AV_ERROR_MAX_STRING_SIZE] { 0 };
            // BOOST_LOG(error) << "Could not send a frame for encoding: "sv << av_make_error_string(err_str, AV_ERROR_MAX_STRING_SIZE, ret);
            return -1;
        }

        while(ret >= 0) {
            Packet* packet    = packet_class_init()->init();
            libav::Packet* av_packet = packet->packet;

            ret = avcodec_receive_packet(ctx, av_packet);

            if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) 
                return 0;
            else if(ret < 0) 
                return ret;

            if(session->inject) {
                if(session->inject == 1) {
                    bitstream::H264 h264 = bitstream::make_sps_h264(ctx, av_packet);
                    sps = h264.sps;
                } else {
                    bitstream::HEVC hevc = bitstream::make_sps_hevc(ctx, av_packet);

                    sps = hevc.sps;
                    vps = hevc.vps;

                    array_object_emplace_back(
                    std::string_view((char *)std::begin(vps.old), vps.old.size()),
                    std::string_view((char *)std::begin(vps._new), vps._new.size()));
                }

                session->inject = 0;


                Replace* replace = malloc(sizeof(Replace));
                replace->old = sps.old;
                replace->_new = sps._new;
                array_object_emplace_back(session->replacement_array,replace);
            }

            packet->replacement_array = session->replacement_array;
            packet->channel_data = channel_data;

            object::Object* object = OBJECT_CLASS->init(packet,DO_NOTHING);
            QUEUE_ARRAY_CLASS->push(packets,object);
        }

        return 0;
    }


    Session*
    make_session(const Encoder* encoder, 
                 const Config config, 
                 int width, int height, 
                 platf::HWDevice* hwdevice) 
    {
        bool hardware = encoder->dev_type != AV_HWDEVICE_TYPE_NONE;

        Profile video_format = config.videoFormat == 0 ? encoder->h264 : encoder->hevc;
        if(!video_format[FrameFlags::PASSED]) {
            // BOOST_LOG(error) << encoder->name << ": "sv << video_format.name << " mode not supported"sv;
            return NULL;
        }

        if(config.dynamicRange && !video_format[FrameFlags::DYNAMIC_RANGE]) {
            // BOOST_LOG(error) << video_format.name << ": dynamic range not supported"sv;
            return NULL;
        }

        AVCodec* codec = avcodec_find_encoder_by_name(video_format.name);
        if(!codec) {
            // BOOST_LOG(error) << "Couldn't open ["sv << video_format.name << ']';
            return NULL;
        }

        libav::CodecContext* ctx = avcodec_alloc_context3(codec);
        ctx->width     = config.width;
        ctx->height    = config.height;
        ctx->time_base = AVRational { 1, config.framerate };
        ctx->framerate = AVRational { config.framerate, 1 };

        if(config.videoFormat == 0) {
            ctx->profile = encoder->profile.h264_high;
        }
        else if(config.dynamicRange == 0) {
            ctx->profile = encoder->profile.hevc_main;
        }
        else {
            ctx->profile = encoder->profile.hevc_main_10;
        }

        // B-frames delay decoder output, so never use them
        ctx->max_b_frames = 0;

        // Use an infinite GOP length since I-frames are generated on demand
        ctx->gop_size = encoder->flags & LIMITED_GOP_SIZE ? INT16_MAX : INT_MAX;
        ctx->keyint_min = INT_MAX;

        if(config.numRefFrames == 0) {
            ctx->refs = video_format[FrameFlags::REF_FRAMES_AUTOSELECT] ? 0 : 16;
        }
        else {
            // Some client decoders have limits on the number of reference frames
            ctx->refs = video_format[FrameFlags::REF_FRAMES_RESTRICT] ? config.numRefFrames : 0;
        }

        ctx->flags |= (AV_CODEC_FLAG_CLOSED_GOP | AV_CODEC_FLAG_LOW_DELAY);
        ctx->flags2 |= AV_CODEC_FLAG2_FAST;

        ctx->color_range = (config.encoderCscMode & 0x1) ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;

        int sws_color_space;
        switch(config.encoderCscMode >> 1) {
        case 0:
        default:
            // Rec. 601
            // BOOST_LOG(info) << "Color coding [Rec. 601]"sv;
            ctx->color_primaries = AVCOL_PRI_SMPTE170M;
            ctx->color_trc       = AVCOL_TRC_SMPTE170M;
            ctx->colorspace      = AVCOL_SPC_SMPTE170M;
            sws_color_space      = SWS_CS_SMPTE170M;
            break;

        case 1:
            // Rec. 709
            // BOOST_LOG(info) << "Color coding [Rec. 709]"sv;
            ctx->color_primaries = AVCOL_PRI_BT709;
            ctx->color_trc       = AVCOL_TRC_BT709;
            ctx->colorspace      = AVCOL_SPC_BT709;
            sws_color_space      = SWS_CS_ITU709;
            break;

        case 2:
            // Rec. 2020
            // BOOST_LOG(info) << "Color coding [Rec. 2020]"sv;
            ctx->color_primaries = AVCOL_PRI_BT2020;
            ctx->color_trc       = AVCOL_TRC_BT2020_10;
            ctx->colorspace      = AVCOL_SPC_BT2020_NCL;
            sws_color_space      = SWS_CS_BT2020;
            break;
        }
        // BOOST_LOG(info) << "Color range: ["sv << ((config.encoderCscMode & 0x1) ? "JPEG"sv : "MPEG"sv) << ']';

        AVPixelFormat sw_fmt;
        if(config.dynamicRange == 0) {
            sw_fmt = encoder->static_pix_fmt;
        }
        else {
            sw_fmt = encoder->dynamic_pix_fmt;
        }

        // Used by cbs::make_sps_hevc
        ctx->sw_pix_fmt = sw_fmt;

        libav::BufferRef* hwdevice_ctx;
        if(hardware) {
            ctx->pix_fmt = encoder->dev_pix_fmt;

            libav::BufferRef* buf_or_error = encoder->make_hw_ctx_func(hwdevice);

            // check datatype
            // if(buf_or_error.has_right()) {
            //     return NULL;
            // }

            hwdevice_ctx = buf_or_error;
            if(hwframe_ctx(ctx, hwdevice_ctx, sw_fmt)) 
                return NULL;
            
            ctx->slices = config.slicesPerFrame;
        } else /* software */ {
            ctx->pix_fmt = sw_fmt;

            // Clients will request for the fewest slices per frame to get the
            // most efficient encode, but we may want to provide more slices than
            // requested to ensure we have enough parallelism for good performance.
            ctx->slices = std::max(config.slicesPerFrame, config::video.min_threads);
        }

        if(!video_format[FrameFlags::SLICE]) {
            ctx->slices = 1;
        }

        ctx->thread_type  = FF_THREAD_SLICE;
        ctx->thread_count = ctx->slices;

        /**
         * @brief 
         * map from config to option here
         */
        AVDictionary *options { nullptr };
        for(auto &option : video_format.options) {
            av_dict_set_int(options);
            av_dict_set_int(options);
            av_dict_set_int(options);
            av_dict_set(options);
            av_dict_set(options);
        }

        if(video_format[FrameFlags::CBR]) {
            auto bitrate        = config.bitrate * (hardware ? 1000 : 800); // software bitrate overshoots by ~20%
            ctx->rc_max_rate    = bitrate;
            ctx->rc_buffer_size = bitrate / 10;
            ctx->bit_rate       = bitrate;
            ctx->rc_min_rate    = bitrate;
        }
        else if(video_format.qp) {
            handle_option(*video_format.qp);
        }
        else {
            BOOST_LOG(error) << "Couldn't set video quality: encoder "sv << encoder->name << " doesn't support qp"sv;
            return NULL;
        }

        if(auto status = avcodec_open2(ctx, codec, &options)) {
            char err_str[AV_ERROR_MAX_STRING_SIZE] { 0 };
            // BOOST_LOG(error)
            // << "Could not open codec ["sv
            // << video_format.name << "]: "sv
            // << av_make_error_string(err_str, AV_ERROR_MAX_STRING_SIZE, status);
            return NULL;
        }

        libav::Frame* frame = av_frame_alloc();
        frame->format = ctx->pix_fmt;
        frame->width  = ctx->width;
        frame->height = ctx->height;


        if(hardware) 
            frame->hw_frames_ctx = av_buffer_ref(ctx->hw_frames_ctx);
        

        platf::HWDevice* device;
        if(!hwdevice->data) {
            // TODO software encoder
            // auto device_tmp = std::make_unique<swdevice_t>();
            // if(device_tmp->init(width, height, frame.get(), sw_fmt)) 
            // {
            //     return NULL;
            // }

            // device = std::move(device_tmp);
        }
        else 
        {
            device = hwdevice;
        }

        if(device->klass.set_frame(frame)) 
            return NULL;
        

        device->set_colorspace(sws_color_space, ctx->color_range);

        Session* session = malloc(sizeof(Session));
        session->ctx = ctx;
        session->device = device;
        // 0 ==> don't inject, 1 ==> inject for h264, 2 ==> inject for hevc
        // session->inject = (1 - (int)video_format[FrameFlags::VUI_PARAMETERS]) * (1 + config.videoFormat),

        // TODO
        // if(!video_format[FrameFlags::NALU_PREFIX_5b]) 
        {
            char* hevc_nalu = "\000\000\000\001(";
            char* h264_nalu = "\000\000\000\001e";
            char* nalu_prefix = config.videoFormat ? hevc_nalu : h264_nalu;
            Replace* temp = malloc(sizeof(Replace));
            temp->old = nalu_prefix.substr(1);
            temp->new = nalu_prefix;
            array_object_emplace_back(session.replacements);
        }

        return std::make_optional(std::move(session));
    }


    /**
     * @brief 
     * 
     * @param disp 
     * @param encoder 
     * @param img 
     * @param ctx 
     * @return SyncSession* 
     */
    SyncSession* 
    make_synced_session(platf::Display* disp, 
                        const Encoder* encoder, 
                        platf::Image *img, 
                        SyncSessionContext *ctx) 
    {
        SyncSession* encode_session = (SyncSession*)malloc(sizeof(SyncSession));

        platf::PixelFormat pix_fmt = ctx->config.dynamicRange == 0 ? map_pix_fmt(encoder->static_pix_fmt) : map_pix_fmt(encoder->dynamic_pix_fmt);
        platf::HWDevice* hwdevice = disp->klass->make_hwdevice(disp,pix_fmt);

        if(!hwdevice) 
            return NULL;
        
        encoder::Session* session = make_session(encoder, ctx->config, img.width, img.height, hwdevice);

        if(!session) 
            return NULL;

        encode_session->ctx = ctx;
        encode_session->session = session;
        return encode_session;
    }



    /**
     * @brief 
     * 
     * @param img 
     * @param synced_session_ctxs 
     * @param encode_session_ctx_queue 
     */
    platf::Capture
    on_image_snapshoot (platf::Image* img,
                        platf::Display* disp,
                        Encoder* encoder,
                        ArrayObject* synced_sessions,
                        ArrayObject* synced_session_ctxs,
                        util::QueueArray* encode_session_ctx_queue)
    {
        while(QUEUE_ARRAY_CLASS->peek(encode_session_ctx_queue)) {
            object::Object* obj = OBJECT_CLASS->init(NULL,DO_NOTHING);
            QUEUE_ARRAY_CLASS->pop(encode_session_ctx_queue,obj);
            SyncSessionContext* encode_session_ctx = obj->data;

            if(!encode_session_ctx) 
                return platf::Capture::error;
            SyncSession* encode_session = make_synced_session(disp, encoder, *img, encode_session_ctx);
            if(!encode_session) 
                return platf::Capture::error;

            array_object_emplace_back(synced_sessions,encode_session);
            array_object_emplace_back(synced_session_ctxs,encode_session_ctx);
        }


        int index = 0;
        while (array_object_has_data(synced_sessions,index))
        {
            SyncSession* pos = (SyncSession*)array_object_get_data(synced_sessions,index);

            // get frame from device
            libav::Frame* frame = pos->session.device->frame;

            // get avcodec context from synced session
            SyncSessionContext* ctx   = pos->ctx;

            // shutdown while loop whenever shutdown event happen
            if(WAIT_EVENT(ctx->shutdown_event)) {
                // Let waiting thread know it can delete shutdown_event
                RAISE_EVENT(ctx->join_event);
                array_object_finalize(synced_sessions);
                return platf::Capture::error;
            }

            // peek idr event (keyframe)
            if(WAIT_EVENT(ctx->idr_event)) {
                frame->pict_type = AV_PICTURE_TYPE_I;
                frame->key_frame = 1;
            }

            // convert image
            if(pos->session.device->klass.convert(*img)) {
                LOG_ERROR("Could not convert image");
                RAISE_EVENT(ctx->shutdown_event);
                continue;
            }

            // encode
            if(encode(ctx->frame_nr++, pos->session, frame, ctx->packets, ctx->channel_data)) {
                LOG_ERROR("Could not encode video packet");
                RAISE_EVENT(ctx->shutdown_event);
                continue;
            }

            // reset keyframe attribute
            frame->pict_type = AV_PICTURE_TYPE_NONE;
            frame->key_frame = 0;

            pos++;
        };
        return img;
    }


    platf::MemoryType
    map_dev_type(AVHWDeviceType type) {
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

    void 
    reset_display(platf::Display* &disp, 
                  AVHWDeviceType type, 
                  char* display_name, 
                  int framerate) 
    {
        // We try this twice, in case we still get an error on reinitialization
        for(int x = 0; x < 2; ++x) {
            disp = platf::display(map_dev_type(type), display_name, framerate);

            if(disp) 
                break;

            std::this_thread::sleep_for(200ms);
        }
    }

    // TODO add encoder and display to context
    Status
    encode_run_sync(ArrayObject* synced_session_ctxs,
                    util::QueueArray* encode_session_ctx_queue) 
    {
        int count;
        const Encoder* encoder = NVENC;
        char* display_names  = platf::display_names(map_dev_type(encoder->dev_type));
        int display_p       = 0;

        // fail to get display names
        if(!display_names) 
            display_names = config::video.output_name;

        // display selection
        count = 0;
        while (*(display_names+count))
        {
            if(*(display_names+count) == config::video.output_name) {
                display_p = count;
                break;
            }
            count++;
        }

        // create one session for easch synced session context
        count = 0;
        SyncSessionContext* synced_sessions;
        while(array_object_has_data(synced_session_ctxs,count)) {
            SyncSessionContext* ctx = array_object_get_data(synced_session_ctxs,count);
            auto synced_session = make_synced_session(disp, encoder, *img, *ctx);

            if(!synced_session) 
                return Status::error;

            array_object_emplace_back(synced_sessions,synced_session);
            count++;
        }



        // pop context from context queue and move them to synced session queue
        if(!array_object_length(synced_session_ctxs)) 
        {
            object::Object* obj = OBJECT_CLASS->init(NULL,DO_NOTHING);
            QUEUE_ARRAY_CLASS->pop(encode_session_ctx_queue,obj);
            SyncSessionContext* ctx = (SyncSessionContext*) obj->data;
            if(!ctx) 
                return Status::ok;

            array_object_emplace_back(synced_session_ctxs,ctx);
        }

        // read framerate from synced session 
        int framerate = ((SyncSessionContext*)array_object_get_data(synced_session_ctxs,1))->config.framerate;

        // reset display every 200ms until display is ready
        platf::Display* disp;
        while(encode_session_ctx_queue.running()) {
            reset_display(disp, encoder->dev_type, display_names[display_p], framerate);
            if(disp) 
                break;

            std::this_thread::sleep_for(200ms);
        }

        if(!disp) 
            return encoder::error;
        
        // allocate display image and intialize with dummy data
        platf::Image* img = disp->klass->alloc_img(disp);
        if(!img || disp->klass->dummy_img(disp,img)) {
            return Status::error;
        }




        // return status
        Status ec = Status::ok;
        while(TRUE) {
            // cursor
            // run image capture in while loop, 
            auto status = disp->klass->capture(disp,on_image_snapshoot, img, TRUE);
            // return for timeout status
            switch(status) {
                case Status::reinit:
                case Status::error:
                case Status::ok:
                case Status::timeout:
                return ec != Status::ok ? ec : status;
            }
        }
        return Status::ok;
    }

    void captureThreadSync(CaptureThreadSyncContext* ctx) {
        // start capture thread sync thread and create a reference to its context
        SyncSessionContext* synced_session_ctxs;


        while(encode_run_sync(synced_session_ctxs, ctx->sync_session_queue) == Status::reinit) {}
        QUEUE_ARRAY_CLASS->stop(ctx->sync_session_queue);
        
        int i = 0;
        while ((synced_session_ctxs + i) != NULL)
        {
            SyncSessionContext ss_ctx = *(synced_session_ctxs + i);
            RAISE_EVENT(ss_ctx->shutdown_event);
            RAISE_EVENT(ss_ctx->join_event);
            i++;
        }

        while (QUEUE_ARRAY_CLASS->peek(ctx->sync_session_queue))
        {
            SyncSessionContext* ss_ctx = (SyncSessionContext*) obj->data;
            RAISE_EVENT(ss_ctx->shutdown_event);
            RAISE_EVENT(ss_ctx->join_event);
            i++;
        }
    }

    /**
     * @brief 
     * 
     * @param shutdown_event 
     * @param packet_queue 
     * @param config 
     * @param data 
     */
    void 
    capture( event::Broadcaster* shutdown_event,
             util::QueueArray* packet_queue,
             Config config,
             pointer data) 
    {
        event::Broadcaster* join_event = NEW_EVENT;
        event::Broadcaster* idr_event = NEW_EVENT;
        RAISE_EVENT(idr_event);

        // start capture thread sync and let it manages its own context
        CaptureThreadSyncContext ctx = {0};
        captureThreadSync(&ctx);

        // push new session context to concode queue
        SyncSessionContext ss_ctx = {
            shutdown_event,
            idr_event,
            join_event,
            
            packet_queue,
            1,

            &config,
            data
        };

        object::Object* obj = OBJECT_CLASS->init(&ss_ctx,DO_NOTHING);
        QUEUE_ARRAY_CLASS->push(ctx.sync_session_queue,obj);

        // Wait for join signal
        while(!WAIT_EVENT(join_event)){ }
    }


    platf::PixelFormat
    map_pix_fmt(libav::PixelFormat fmt) 
    {
        switch(fmt) {
            case AV_PIX_FMT_YUV420P10:
                return platf::PixelFormat::yuv420p10;
            case AV_PIX_FMT_YUV420P:
                return platf::PixelFormat::yuv420p;
            case AV_PIX_FMT_NV12:
                return platf::PixelFormat::nv12;
            case AV_PIX_FMT_P010:
                return platf::PixelFormat::p010;
            default:
                return platf::PixelFormat::unknown;
        }

        return platf::PixelFormat::unknown;
    }
}