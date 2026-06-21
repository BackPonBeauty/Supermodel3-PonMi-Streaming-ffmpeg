#include "FfmpegEncoder.h"
#include <GL/glew.h>
#include <cstdio>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

static int64_t s_frameCount = 0;

FfmpegEncoder::FfmpegEncoder()
{
}

bool FfmpegEncoder::Init(int width, int height, int fps, int port, const std::string &codec, OnEncodedCallback cb)
{
    m_width = width;
    m_height = height;
    m_fps = fps;
    m_codec = codec;
    m_callback = cb;

    s_frameCount = 0;

    // 1. Find encoder
    bool useH265 = (codec == "H265");
    const char* codecName = useH265 ? "libx265" : "libx264";
    m_ffmpegCodec = avcodec_find_encoder_by_name(codecName);
    if (!m_ffmpegCodec)
    {
        printf("[FFmpeg] Encoder %s not found. Falling back to default...\n", codecName);
        m_ffmpegCodec = avcodec_find_encoder(useH265 ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264);
        if (!m_ffmpegCodec)
        {
            printf("[FFmpeg] Failed to find H264/H265 encoder.\n");
            return false;
        }
    }

    // 2. Allocate context
    m_codecContext = avcodec_alloc_context3(m_ffmpegCodec);
    if (!m_codecContext)
    {
        printf("[FFmpeg] Failed to allocate codec context.\n");
        return false;
    }

    // 3. Set parameters
    m_codecContext->width = width;
    m_codecContext->height = height;
    m_codecContext->time_base = { 1, fps };
    m_codecContext->framerate = { fps, 1 };
    m_codecContext->pix_fmt = AV_PIX_FMT_YUV420P;
    m_codecContext->gop_size = 60;
    m_codecContext->max_b_frames = 0;
    
    // Default bitrate 2.5 Mbps
    m_codecContext->bit_rate = 2500000;
    m_codecContext->rc_max_rate = 3000000;
    m_codecContext->rc_buffer_size = 1000000;

    // Set low-latency options for libx264 / libx265
    if (strcmp(m_ffmpegCodec->name, "libx264") == 0 || strcmp(m_ffmpegCodec->name, "libx265") == 0)
    {
        av_opt_set(m_codecContext->priv_data, "preset", "ultrafast", 0);
        av_opt_set(m_codecContext->priv_data, "tune", "zerolatency", 0);
    }

    // Open codec
    if (avcodec_open2(m_codecContext, m_ffmpegCodec, nullptr) < 0)
    {
        printf("[FFmpeg] Failed to open codec.\n");
        avcodec_free_context(&m_codecContext);
        return false;
    }

    // 4. Allocate frames
    m_srcFrame = av_frame_alloc();
    m_dstFrame = av_frame_alloc();
    if (!m_srcFrame || !m_dstFrame)
    {
        printf("[FFmpeg] Failed to allocate frames.\n");
        Shutdown();
        return false;
    }

    // Allocate YUV420P buffer for destination frame
    if (av_image_alloc(m_dstFrame->data, m_dstFrame->linesize, width, height, AV_PIX_FMT_YUV420P, 32) < 0)
    {
        printf("[FFmpeg] Failed to allocate destination image buffer.\n");
        Shutdown();
        return false;
    }
    m_dstFrame->format = AV_PIX_FMT_YUV420P;
    m_dstFrame->width = width;
    m_dstFrame->height = height;

    // 5. Initialize scaling context (RGBA to YUV420P)
    m_swsContext = sws_getContext(width, height, AV_PIX_FMT_RGBA,
                                  width, height, AV_PIX_FMT_YUV420P,
                                  SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_swsContext)
    {
        printf("[FFmpeg] Failed to create sws context.\n");
        Shutdown();
        return false;
    }

    // Allocate host buffer for OpenGL texture readback
    m_hostBuffer.resize(width * height * 4);

    // Initialize packet
    m_packet = av_packet_alloc();
    if (!m_packet)
    {
        printf("[FFmpeg] Failed to allocate packet.\n");
        Shutdown();
        return false;
    }

    // Initialize RTP sender
    m_rtpEnabled = m_rtpSender.Init("127.0.0.1", port, useH265);

    printf("[FFmpeg] Encoder initialized successfully (%s) %dx%d @%dfps (RTP Port: %d)\n",
           m_ffmpegCodec->name, width, height, fps, port);

    return true;
}

void FfmpegEncoder::EncodeFrame(unsigned int glTextureID)
{
    if (!m_codecContext)
        return;

    // 1. Readback OpenGL texture to host buffer (RGBA)
    glBindTexture(GL_TEXTURE_2D, glTextureID);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_hostBuffer.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    uint8_t* srcData[4] = { m_hostBuffer.data(), nullptr, nullptr, nullptr };
    int srcLinesize[4] = { m_width * 4, 0, 0, 0 };

    sws_scale(m_swsContext, srcData, srcLinesize, 0, m_height, m_dstFrame->data, m_dstFrame->linesize);

    m_dstFrame->pts = s_frameCount++;

    // 3. Send frame to encoder
    int ret = avcodec_send_frame(m_codecContext, m_dstFrame);
    if (ret < 0)
    {
        printf("[FFmpeg] Error sending frame to encoder: %d\n", ret);
        return;
    }

    // 4. Retrieve encoded packets
    while (ret >= 0)
    {
        ret = avcodec_receive_packet(m_codecContext, m_packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            break;
        }
        else if (ret < 0)
        {
            printf("[FFmpeg] Error receiving packet: %d\n", ret);
            break;
        }

        // Send over RTP
        if (m_rtpEnabled)
        {
            m_rtpSender.Send(m_packet->data, m_packet->size);
        }

        // Trigger callback
        if (m_callback)
        {
            bool isKeyframe = (m_packet->flags & AV_PKT_FLAG_KEY) != 0;
            m_callback(m_packet->data, m_packet->size, isKeyframe);
        }

        av_packet_unref(m_packet);
    }
}

bool FfmpegEncoder::ReconfigureBitrate(int avgBitrate, int maxBitrate)
{
    if (!m_codecContext)
        return false;

    m_codecContext->bit_rate = avgBitrate;
    m_codecContext->rc_max_rate = maxBitrate;
    return true;
}

void FfmpegEncoder::Shutdown()
{
    if (m_swsContext)
    {
        sws_freeContext(m_swsContext);
        m_swsContext = nullptr;
    }
    if (m_dstFrame)
    {
        av_freep(&m_dstFrame->data[0]);
        av_frame_free(&m_dstFrame);
    }
    if (m_srcFrame)
    {
        av_frame_free(&m_srcFrame);
    }
    if (m_packet)
    {
        av_packet_free(&m_packet);
    }
    if (m_codecContext)
    {
        avcodec_free_context(&m_codecContext);
    }
    m_ffmpegCodec = nullptr;
    m_rtpEnabled = false;
}

void FfmpegEncoder::SetDestIP(const std::string &ip)
{
    m_rtpSender.SetDestIP(ip);
}

void FfmpegEncoder::SetDestIPs(const std::vector<std::string> &ips)
{
    m_rtpSender.SetDestIPs(ips);
}

void FfmpegEncoder::SetDestEndpoints(const std::vector<std::pair<std::string, int>> &endpoints)
{
    m_rtpSender.SetDestEndpoints(endpoints);
}
