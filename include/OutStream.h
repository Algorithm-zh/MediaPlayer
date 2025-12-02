#pragma once
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavcodec/bsf.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}
#include <string>
#include <vector>

class OutStream{

public:
  bool init(const std::string &url, int w, int h, int fps);
  void encode_and_write();

public:
  std::string push_url;
  AVFormatContext *fmt_ctx{nullptr};
  AVCodecContext *enc_ctx{nullptr};
  AVStream *stream{nullptr};
  SwsContext *sws_out{nullptr};
  int width{0};
  int height{0};
  int fps{0};
  int64_t next_pts{0};

  AVFrame *frame{nullptr};
  std::vector<uint8_t> rgb_buffer;
  std::vector<uint8_t> rgb_flipped;

public:
  bool push_enabled{false};

};
