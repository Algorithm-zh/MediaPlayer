#include <SDL2/SDL_render.h>
#include <SDL2/SDL_surface.h>
#include <SDL2/SDL_video.h>
extern "C"
{
  #include <libavformat/avformat.h>
  #include <libavcodec/avcodec.h>
  #include <libswscale/swscale.h>
  #include <libavutil/imgutils.h>
  #include <SDL2/SDL.h>
  #include <SDL2/SDL_thread.h>
}
#include <iostream>
class MediaPlayer
{
public:
  //打开媒体文件并初始化
  MediaPlayer(const char* url);
  ~MediaPlayer();
  //读取数据,从视频流读取数据包packet并解码到frame中,并且转换成对应的格式存储起来
  void readData();

private:
  //开辟空间存储数据
  void allocFrame();
  void saveFrame(int iFrame);  
  void sdl_init();
  void showFrame();  

  //初始化部分
  const char* url_;
  AVFormatContext *pFormatCtx{NULL};
  //一路流
  AVStream *vStream{NULL};
  int videoStreamIndex{-1};
  //编解码器
  const AVCodec *pCodec{NULL};
  //编解码器上下文
  AVCodecContext *pCodecCtx{NULL};
  //存储视频帧
  AVFrame* pFrame{NULL};
  AVFrame* pFrameRGB{NULL};
  AVFrame* pFrameYUV{NULL};
  //暂存视频帧原始数据
  int numBytes{0};
  uint8_t* buffer{NULL};
  //数据包
  AVPacket* packet;
  struct SwsContext *sws_ctx{NULL};

  //sdl部分
  SDL_Surface *screen{NULL};
  //窗口
  SDL_Window *window{NULL};
  //渲染器
  SDL_Renderer *render{NULL};
  //纹理
  SDL_Texture *texture{NULL};
  //矩形区域
  SDL_Rect *rect{NULL};
  //事件
  SDL_Event event;
};
 

