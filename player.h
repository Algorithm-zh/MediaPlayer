extern "C"
{
  #include <libavformat/avformat.h>
  #include <libavcodec/avcodec.h>
  #include <libswscale/swscale.h>
  #include <libavutil/imgutils.h>
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

  //初始化部分
  const char* url_;
  AVFormatContext *pFormatCtx{nullptr};
  //一路流
  AVStream *vStream{nullptr};
  int videoStreamIndex{-1};
  //编解码器
  AVCodec *pCodec{nullptr};
  //编解码器上下文
  AVCodecContext *pCodecCtx{nullptr};
  //存储视频帧
  AVFrame* pFrame{nullptr};
  AVFrame* pFrameRGB{nullptr};
  //暂存视频帧原始数据
  int numBytes{0};
  uint8_t* buffer{nullptr};
  //数据包
  AVPacket* packet;
  struct SwsContext *sws_ctx{nullptr};
};
