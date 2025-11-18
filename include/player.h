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
  int data_bytes = 0;
};

class MediaPlayer {
  using PacketQueue = std::queue<AVPacket*>;
  using FrameQueue = std::queue<Frame>;
public:
  MediaPlayer(const char *url);
  ~MediaPlayer();
  void start();
  void toggle_pause();
  void seek(double offset);
  void readData();
  int decode_packet(AVCodecContext* codecCtx, AVPacket* packet);
  int packet_queue_put(AVPacket *packet);

  void video_thread();
  void audio_thread();
  void stream_thread(std::queue<AVPacket*>& queue, std::mutex& mtx, std::condition_variable& cond, AVCodecContext* codecCtx);
  
  double synchronize_video(AVFrame *frame, double pts);
  double get_audio_clock();

private:
  void allocFrame();
  void gl_init();
  void showFrame();
  int audio_decode_frame(uint8_t *audio_buf, int buf_size);
  static int paCallback( const void *inputBuffer, void *outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData );
  int portAudioCallback(void *outputBuffer, unsigned long framesPerBuffer);
  static void framebuffer_size_callback(GLFWwindow* window, int width, int height);
  void processInput(GLFWwindow *window);


  const char *url_;
  AVFormatContext *pFormatCtx{NULL};
  AVStream *vStream{NULL};
  AVStream *aStream{NULL};
  int videoStreamIndex{-1};
  int audioStreamIndex{-1};

  const AVCodec *pCodec{NULL};
  const AVCodec *aCodec{NULL};

  AVCodecContext *pCodecCtx{NULL};
  AVCodecContext *aCodecCtx{NULL};

  AVFrame *pFrame{NULL};
  AVFrame *pFrameYUV{NULL};
  AVPacket *packet;
  struct SwsContext *sws_ctx{NULL};
  AVBSFContext *bsf_ctx{NULL};
  struct SwrContext *swr_ctx{NULL};

  GLFWwindow *window{NULL};
  Shader shader;
  GLuint vao, vbo;
  GLuint textures[3];

  std::atomic_bool is_close{false};
  std::atomic_bool is_paused{false};
  std::atomic_bool is_seeking{false};

  PacketQueue vPacket_queue;
  PacketQueue aPacket_queue;
  FrameQueue vFrame_queue;
  FrameQueue aFrame_queue;

  std::vector<std::thread> th;
  std::mutex video_Packet_mtx;
  std::mutex audio_Packet_mtx;
  std::condition_variable video_Packet_cond;
  std::condition_variable audio_Packet_cond;
  std::mutex video_Frame_mtx;
  std::mutex audio_Frame_mtx;
  std::condition_variable video_Frame_cond;
  std::condition_variable audio_Frame_cond;

  double video_clock{0.0};
  double frame_last_pts{0.0};
  double frame_last_delay{40e-3};
  double frame_timer{0.0};
  
  PaStream *audio_stream{NULL};
  uint8_t *audio_buf{NULL};
  unsigned int audio_buf_size{0};
  unsigned int audio_buf_index{0};
  std::atomic<double> audio_clock{0.0};
  std::map<int, double> key_last_pressed;
};
