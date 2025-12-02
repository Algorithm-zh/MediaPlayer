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
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <sys/time.h>
#include <atomic>
#include "OpenglRender.h"
#include "OutStream.h"

namespace
{
  const int MAX_QUEUE_SIZE = 1024;
  const double AV_SYNC_THRESHOLD = 0.01;
  const double AV_NOSYNC_THRESHOLD = 10.0;
}

struct Frame
{
  AVFrame *frame;
  double pts;
};

using PacketQueue = std::queue<AVPacket*>;
using FrameQueue = std::queue<Frame>;

struct Channel{
  AVFormatContext *fmt_ctx{NULL};
  AVCodecContext *codec_ctx{NULL};
  const AVCodec *codec{NULL};
  AVStream *stream{NULL};
  int stream_index{-1};
  struct SwsContext *sws_ctx{NULL};
  AVBSFContext *bsf_ctx{NULL};
  PacketQueue packet_queue;
  FrameQueue frame_queue;
  int width{0};
  int height{0};
  double clock{0.0};
  double last_clock{0.0};
  double last_delay{0.04};
  double pts_base{0.04};
  double time_base{0.04};
  bool first_frame{true};
};


class MediaPlayer {
public:
  MediaPlayer(const std::vector<std::string>&urls, const std::string& push_url);
  ~MediaPlayer();
  void start();
private:

  void readData(int idx);
  void decodeThread(int idx);
  int packet_queue_put(AVPacket *packet);
  double synchronize_video(int idx, AVFrame *frame, double pts);
 
  void allocFrame();
  void gl_init();
  void showFrame();
  void processInput(GLFWwindow *window);

private:
  const std::vector<std::string> urls;
  std::vector<Channel> ch{3};

  std::atomic_bool is_close{false};
  std::vector<std::thread> th;
  std::mutex mtx_frame[3];
  std::condition_variable cond_frame[3];
  AVFrame* pFrameYUV[3];

  og::OpenglRender* renderer;

  //推流
  OutStream outStream;  
  const std::string push_url;
};
