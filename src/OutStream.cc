#include "OutStream.h"
#include <iostream>

bool OutStream::init(const std::string &url, int w, int h, int fps)  {
 if (url.empty()) return false;
 if(!fmt_ctx)return true;

  width  = w;
  height = h;
  fps    = fps;

  const AVOutputFormat *ofmt = nullptr;
  int ret = avformat_alloc_output_context2(&fmt_ctx, nullptr, "flv", url.c_str());
  if (ret < 0 || !fmt_ctx) {
    std::cerr << "创建输出上下文失败" << std::endl;
    return false;
  }
  ofmt = fmt_ctx->oformat;

  const AVCodec *encoder = avcodec_find_encoder_by_name("h264_nvenc");
  if (!encoder) {
    std::cerr << "未找到 h264_nvenc，改用软件 H.264 编码器" << std::endl;
    encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
  }
  if (!encoder) {
    std::cerr << "未找到 H.264 编码器" << std::endl;
    return false;
  }

  enc_ctx = avcodec_alloc_context3(encoder);
  enc_ctx->width     = width;
  enc_ctx->height    = height;
  enc_ctx->pix_fmt   = AV_PIX_FMT_YUV420P;
  enc_ctx->time_base = AVRational{1, fps};
  enc_ctx->framerate = AVRational{fps, 1};
  enc_ctx->gop_size  = fps * 2;
  enc_ctx->max_b_frames = 2;
  enc_ctx->bit_rate  = 4 * 1000 * 1000; // 4Mbps


  ret = avcodec_open2(enc_ctx, encoder, nullptr);
  if (ret < 0) {
    std::cerr << "打开编码器失败" << std::endl;
    return false;
  }

  stream = avformat_new_stream(fmt_ctx, encoder);
  if (!stream) {
    std::cerr << "创建输出流失败" << std::endl;
    return false;
  }
  stream->time_base = enc_ctx->time_base;
  ret = avcodec_parameters_from_context(stream->codecpar, enc_ctx);
  if (ret < 0) {
    std::cerr << "拷贝编码器参数失败" << std::endl;
    return false;
  }

  if (!(ofmt->flags & AVFMT_NOFILE)) {
    ret = avio_open(&fmt_ctx->pb, url.c_str(), AVIO_FLAG_WRITE);
    if (ret < 0) {
      std::cerr << "打开输出 URL 失败: " << url << std::endl;
      return false;
    }
  }

  ret = avformat_write_header(fmt_ctx, nullptr);
  if (ret < 0) {
    std::cerr << "写输出头失败" << std::endl;
    return false;
  }

  frame = av_frame_alloc();
  if (!frame) {
    std::cerr << "分配输出帧失败" << std::endl;
    return false;
  }
  frame->format = enc_ctx->pix_fmt;
  frame->width  = width;
  frame->height = height;
  ret = av_frame_get_buffer(frame, 32);
  if (ret < 0) {
    std::cerr << "分配输出帧缓冲失败" << std::endl;
    return false;
  }

  sws_out = sws_getContext(width, height, AV_PIX_FMT_RGB24,
                           width, height, AV_PIX_FMT_YUV420P,
                           SWS_BILINEAR, nullptr, nullptr, nullptr);
  if (!sws_out) {
    std::cerr << "创建 sws 输出上下文失败" << std::endl;
    return false;
  }

  rgb_buffer.resize(static_cast<size_t>(width) * height * 3);
  rgb_flipped.resize(rgb_buffer.size());
  next_pts = 0;

  std::cout << "推流初始化成功: " << url << std::endl;
  return true;
}


void OutStream::encode_and_write()
{
  if (!enc_ctx || !fmt_ctx || !stream) return;

  int ret = avcodec_send_frame(enc_ctx, frame);
  if (ret < 0) {
    std::cerr << "发送帧到编码器失败" << std::endl;
    return;
  }

  // 使用已初始化且引用计数安全的 AVPacket
  AVPacket *pkt = av_packet_alloc();
  if (!pkt) {
    std::cerr << "分配编码包失败" << std::endl;
    return;
  }

  while (true) {
    ret = avcodec_receive_packet(enc_ctx, pkt);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      break;
    if (ret < 0) {
      std::cerr << "从编码器接收数据失败" << std::endl;
      break;
    }

    av_packet_rescale_ts(pkt, enc_ctx->time_base, stream->time_base);
    pkt->stream_index = stream->index;
    av_interleaved_write_frame(fmt_ctx, pkt);
    av_packet_unref(pkt);
  }

  av_packet_free(&pkt);
}
 

