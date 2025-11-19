extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavcodec/bsf.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <portaudio.h>
}
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <sys/time.h>
#include <atomic>
#include <map>
#include "shader.h"

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

class MediaPlayer {
  using PacketQueue = std::queue<AVPacket*>;
  using FrameQueue = std::queue<Frame>;
public:
  MediaPlayer(const std::vector<std::string>&urls);
  ~MediaPlayer();
  void start();
private:
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
    GLuint textures[3]{0, 0, 0};
    double clock{0.0};
  };
  void readData(int idx);
  void decodeThread(int idx);
  int packet_queue_put(AVPacket *packet);
  double synchronize_video(int idx, AVFrame *frame, double pts);
 
  void allocFrame();
  void gl_init();
  void showFrame();
  void processInput(GLFWwindow *window);
  static void framebuffer_size_callback(GLFWwindow* window, int width, int height);

  const std::vector<std::string> urls;
  std::vector<Channel> ch{3};

  AVFrame *pFrame[3]{nullptr};
  AVFrame *pFrameYUV[3]{nullptr};
  AVPacket *packet[3]{nullptr};

  GLFWwindow *window{NULL};
  Shader shader;
  GLuint vao, vbo;

  std::atomic_bool is_close{false};

  std::vector<std::thread> th;
  std::mutex mtx_frame[3];
  std::condition_variable cond_frame[3];

};
