extern "C" {
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include <iostream>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>

class MediaPlayer {
  using PacketQueue = std::queue<AVPacket*>;
  using FrameQueue = std::queue<AVFrame*>;
public:
  // 打开媒体文件并初始化
  MediaPlayer(const char *url);
  ~MediaPlayer();
  void start();
  // 读取数据,从视频流读取数据包packet并解码到frame中,并且转换成对应的格式存储起来
  void readData();
  int decode_packet(AVCodecContext* codecCtx, AVPacket* packet);
  int packet_queue_put();

  //音频sdl回调函数
  void audioDataRead(void *userdata, Uint8 *stream, int len);
  static void audioCallback(void *userdata, Uint8 *stream, int len);

  //解码线程
  void video_thread();
  void audio_thread();
  


private:
  // 开辟空间存储数据
  void allocFrame();
  void sdl_init();
  void showFrame();

  // 初始化部分
  const char *url_;
  AVFormatContext *pFormatCtx{NULL};
  // 一路流
  AVStream *vStream{NULL};
  AVStream *aStream{NULL};
  int videoStreamIndex{-1};
  int audioStreamIndex{-1};
  // 编解码器
  const AVCodec *pCodec{NULL};
  const AVCodec *aCodec{NULL};
  // 编解码器上下文
  AVCodecContext *pCodecCtx{NULL};
  AVCodecContext *aCodecCtx{NULL};
  // 存储视频帧
  AVFrame *pFrame{NULL};
  AVFrame *pFrameRGB{NULL};
  AVFrame *pFrameYUV{NULL};
  // 暂存视频帧原始数据
  int numBytes{0};
  uint8_t *buffer{NULL};
  // 数据包
  AVPacket *packet;
  struct SwsContext *sws_ctx{NULL};

  // sdl部分
  SDL_Surface *screen{NULL};
  // 窗口
  SDL_Window *window{NULL};
  // 渲染器
  SDL_Renderer *render{NULL};
  // 纹理
  SDL_Texture *texture{NULL};
  // 矩形区域
  SDL_Rect *rect{NULL};
  // 事件
  SDL_Event event;
  //结束符号
  bool is_close{false};

  PacketQueue vPacket_queue;
  PacketQueue aPacket_queue;
  FrameQueue vFrame_queue;
  FrameQueue aFrame_queue;

  // sdl音频部分
  SDL_AudioSpec wanted_spec;
  SDL_AudioSpec spec;
  SDL_AudioCallback audio_callback{NULL};

  //线程
  std::vector<std::thread> th;
  //读取得到原始Packet锁和条件变量
  std::mutex video_Packet_mtx;
  std::mutex audio_Packet_mtx;
  std::condition_variable video_Packet_cond;
  std::condition_variable audio_Packet_cond;
  //解码得到的帧锁和条件变量
  std::mutex video_Frame_mtx;
  std::mutex audio_Frame_mtx;
  std::condition_variable video_Frame_cond;
  std::condition_variable audio_Frame_cond;
};
 

