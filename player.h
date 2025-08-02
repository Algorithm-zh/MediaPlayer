extern "C" {
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
}
#include <iostream>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <sys/time.h>
#include <atomic>
namespace
{
  const int MAX_QUEUE_SIZE = 1024;
  const double AV_SYNC_THRESHOLD = 0.01;//音视频误差超过该阈值需要同步处理
  const double AV_NOSYNC_THRESHOLD = 10.0;//差距超过该值就放弃同步直接播放
  const double MAX_FRAME_DELAY = 100;
  const int AUDIO_DIFF_AVG_NB = 10;
  const int SAMPLE_CORRECTION_PERCENT_MAX = 10;
  const int SDL_AUDIO_BUFFER_SIZE = 1024;
}
struct Frame
{
  AVFrame *frame;
  double pts;//为什么要特意写pts,因为有可能原视频的pts丢失或者找不到，需要我们自己写
  int data_bytes = 0;//每帧的字节数，方便音频同步时使用
};
enum class AV_SYNC_TYPE
{
  AV_SYNC_AUDIO_MASTER,
  AV_SYNC_VIDEO_MASTER,
  AV_SYNC_EXTERNAL_MASTER,
};
#define DEFAULT_AV_SYNC_TYPE AV_SYNC_TYPE::AV_SYNC_VIDEO_MASTER

class MediaPlayer {
  using PacketQueue = std::queue<AVPacket*>;
  using FrameQueue = std::queue<Frame>;
public:
  // 打开媒体文件并初始化
  MediaPlayer(const char *url, AV_SYNC_TYPE av_sync_type = DEFAULT_AV_SYNC_TYPE);
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
  
  //音视频同步
  double synchronize_video(AVFrame *frame, double pts);
  double get_audio_clock();
  double get_video_clock();
  double get_master_clock();
  double get_external_clock();
  int synchronize_audio(short *samples, int samples_size, double pts);

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
  //音频格式转换部分
  SwrContext *swr_ctx{NULL};
  //音频格式转换时的缓冲区
  uint8_t *audio_buf = nullptr;
  //当前播放音频帧的位置
  std::atomic_int audio_buf_index{0};//配合buf使用
  //当前播放音频帧的大小
  std::atomic_int audio_buf_size{0};//配合buf使用
  

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


  AV_SYNC_TYPE av_sync_type;
  //音频为主的视频同步
  struct timeval start_time;
  struct timeval cur_time;
  double video_clock{0.0};//上一帧的pts/预测下一帧的pts
  std::atomic<double> audio_clock{0.0};//音频已经播放出去的时间
  //记录上一帧的pts和延迟
  double frame_last_pts{0.0};
  double frame_last_delay{40e-3};
  //frame_timer会一直累加在播放过程中计算的延时，和系统时间做比较
  double frame_timer{0.0};

  //视频为主的视频同步
  double video_current_pts{0.0};
  struct timeval video_current_pts_time;
  double audio_diff_cum{0.0};//加权平均值
  int audio_diff_avg_count{0};
  double audio_diff_avg_coef{0.0};//权重系数.越高表示过去的数据权重越高
  double audio_diff_threshold{0.1};

  //seek操作
  int seek_req;
  int seek_flags;
  int64_t seek_pos;
  double incr, pos;
};
 

