#include "player.h"
#include <SDL2/SDL_events.h>
#include <chrono>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/rational.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <sys/select.h>
namespace
{
  const int MAX_QUEUE_SIZE = 1024;
}
MediaPlayer::MediaPlayer(const char* url)
{
  th.resize(4);
  /*
  * AVFormatContext 包含了媒体信息有关的成员
  * struct AVInputFormat *iformat //封装格式的信息
  * unsigned int nb_streams //该url有几路流
  * AVStream **streams //每个对象几路一路流的详细信息
  * int64_t start_time //第一帧的时间戳
  * int64_t duration //码流的总时长
  * int64_t bit_rate //码流的总码流
  * AVDictionary *metadata //文件信息头 key/value字符串
  *
  * 这些信息经过avformat_find_stream_info就可以拿到了
  */
  /*
  * AVStream
  * AVCodecContext *codec //记录了该码流的编码信息
  * int64_t start_time //第一帧的时间戳
  * int64_t duration //该码流的时长
  * int64_t bit_rate //该码流的码流
  * AVDictionary *metadata //文件信息头 key/value字符串
  * AVRational avg_frame_rate //平均帧率
  */
  /*
  * AVCodecContext 记录了一路流的具体编码信息
  * const struct AVCodec *codec //编码的详细信息
  * enum AVcodecID codec_id //编码类型
  * int bit_rate //平均码率
  * video only
  *   int width, height //码流里不一定存在
  *   enum AVPixelFormat pix_fmt //原始图像的格式 码流里不一定存在
  * audio only
  *   int sample_rate //音频采样率
  *   int channels  //通道数
  *   enum AVSampleFormat sample_fmt //音频的格式,位宽
  *   int frame_size //每个音频帧的sample个数
  */
  url_ = url;

  //1.该函数负责服务器的连接和码流头部信息的拉取
  //第三个参数指定媒体文件格式,第四个指定文件格式相关选项,如果为null,那么avformat则自动探测文件格式
  if(avformat_open_input(&pFormatCtx, url_, NULL, NULL) != 0)
  {
    std::cerr << "打开媒体文件失败:" << stderr << std::endl;
    return ;
  }
  //2.媒体信息的探测和分析函数,填充pFormatCtx->streams对应的信息
  if(avformat_find_stream_info(pFormatCtx, NULL) < 0)
  {
    std::cerr << "探测文件信息失败:" << stderr << std::endl;
    return ;
  }
  //打印pFormatCtx信息
  av_dump_format(pFormatCtx, 0, url_, 0);

  //3.找到视频流的下标
  videoStreamIndex = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  if (videoStreamIndex < 0) {
    std::cerr << "未找到视频流:" << stderr << std::endl;
    return ;
  }
  //找到音频流的下标
  audioStreamIndex = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
  if(audioStreamIndex < 0)
  {
    std::cerr << "未找到音频流:" << stderr << std::endl;
    return;
  }

  //4.得到视频流
  vStream = pFormatCtx->streams[videoStreamIndex];
  aStream = pFormatCtx->streams[audioStreamIndex];


  //5.流信息中关于codec的部分存储在了AVCodecParameters里,通过它里面的codec_id打开解码器
  //也可以直接自己指定解码器
  pCodec = avcodec_find_decoder(vStream->codecpar->codec_id);
  if(pCodec == NULL)
  {
    std::cerr << "打开视频解码器失败:" << stderr << std::endl;
    return ;
  }
  aCodec = avcodec_find_decoder(aStream->codecpar->codec_id);
  if(aCodec == NULL)
  {
    std::cerr << "打开音频解码器失败:" << stderr << std::endl;
    return ;
  }
  //6.给解码器的上下文alloc
  pCodecCtx = avcodec_alloc_context3(pCodec);
  aCodecCtx = avcodec_alloc_context3(aCodec);

  //7.把输入流里的AVCodecParameters的参数拷贝到解码器的上下文里
  //注意:旧版本使用的是avcodec_copy_context函数,新版本不要使用了
  if(avcodec_parameters_to_context(pCodecCtx, vStream->codecpar) < 0)
  {
    std::cerr << "输入流参数拷贝到视频解码器上下文中失败:" << stderr << std::endl;
    return ;
  }
  if(avcodec_parameters_to_context(aCodecCtx, aStream->codecpar) < 0)
  {
    std::cerr << "输入流参数拷贝到音频解码器上下文中失败:" << stderr << std::endl;
    return ;
  }

  //8.初始化解码器上下文
  if(avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
  {
    std::cerr << "初始化视频解码器上下文失败" << stderr << std::endl;
    return;
  }
  if(avcodec_open2(aCodecCtx, aCodec, NULL) < 0)
  {
    std::cerr << "初始化音频解码器上下文失败" << stderr << std::endl;
    return;
  }
  allocFrame();
  //初始化sdl
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
  {
    std::cerr << "初始化sdl失败:" << SDL_GetError();
    return;
  }
  //初始化sdl音频设置
  wanted_spec.freq = aCodecCtx->sample_rate;//采样率
  wanted_spec.format = AUDIO_S16SYS;//音频数据格式, singned 16bits 大小端和系统保持一致
  //wanted_spec.format = AUDIO_F32SYS;
  wanted_spec.channels = aCodecCtx->ch_layout.nb_channels;//声道数
  wanted_spec.samples = 1024;//采样大小(用多少位来记录振幅)
  wanted_spec.silence = 0;//是否静音
  audio_callback = audioCallback;
  wanted_spec.callback = audio_callback;//sdl会持续调用这个回调函数来填充固定数量的字节到音频缓冲区
  wanted_spec.userdata = this;//将this传入方便回调audioDateRead

  //初始化滤镜上下文
  sws_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height,
                           pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height,
                           AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);
  //初始化SwrContext
  int ret = swr_alloc_set_opts2(&swr_ctx, &aCodecCtx->ch_layout, 
                                AV_SAMPLE_FMT_S16, aCodecCtx->sample_rate,
                                &aCodecCtx->ch_layout, aCodecCtx->sample_fmt,
                                aCodecCtx->sample_rate, 0, NULL);
  if(ret < 0)
  {
    std::cerr << "初始化SwrContext失败" << std::endl;
    return;
  }
  swr_init(swr_ctx);

  //打开音频
  if(SDL_OpenAudio(&wanted_spec, &spec) < 0)
  {
    std::cerr << "sdl打开音频失败:" << SDL_GetError() << std::endl;
    return;
  }

  //开始播放音频
  SDL_PauseAudio(0);

  std::cout << "初始化完毕" << std::endl;
}
 
void MediaPlayer::allocFrame()  {
  //为frame开辟空间
  pFrame = av_frame_alloc(); 
  pFrameYUV = av_frame_alloc();
  packet = av_packet_alloc(); 
  if(pFrame == NULL || pFrameYUV == NULL)
  {
    std::cerr << "分配视频帧空间失败" << stderr << std::endl;
    return ;
  }

  //分配内存空间方式一:
  //转换格式时需要一块地方来存储视频帧的原始数据,所以需要获取内存大小
  //最后一个参数是内存对齐,设置为1就是无对齐
  //numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height, 1);
  //av_malloc就是封装了malloc并做了一些内存对齐的事情
  //buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));
  //关联frame和刚才分配的内存,即格式化申请的内存
  //第一个参数存储每个平面的起始地址,比如yuv三个平面,他们会指向buffer这块内存的不同位置
  //linesize的作用:格式化后的行字节数(宽度),解码后的数据可能是经过对齐的,那么要显示视频数据肯定要先把对齐的数据删除
  //有了这玩意就可一行一行的把为了对齐而补的字节删除,还原真实数据
  //av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, buffer, AV_PIX_FMT_RGB24, 
  //                     pCodecCtx->width, pCodecCtx->height, 1);
  //方式二:
  //既能申请内存又能格式化数据 相当于上面三个函数
  av_image_alloc(pFrameYUV->data, pFrameYUV->linesize, 
                 pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, 1);
  //上面这个函数并不会设置下面这两个值
  pFrameYUV->width  = pCodecCtx->width;
  pFrameYUV->height = pCodecCtx->height;
}
 
void MediaPlayer::readData()  {
  //获取开始读取包的时间
  gettimeofday(&start_time, NULL);
  //开始从视频流中读取数据包
  while(!is_close && av_read_frame(pFormatCtx, packet) >= 0)
  {
    packet_queue_put();
    //释放掉packet指向的内存,以方便读下一个包
    av_packet_unref(packet);
    SDL_PollEvent(&event);
    switch (event.type) {
      case SDL_QUIT:
        std::cout << "SDL_QUIT" << std::endl;
        is_close = true;
        break;
    }
  }
  is_close = true;
  std::cout << "读取数据结束" << std::endl;
}


MediaPlayer::~MediaPlayer()  {

  SDL_CloseAudio(); // 先关闭音频播放（阻止后续回调）
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(render);
  SDL_DestroyWindow(window);
  SDL_Quit(); // SDL 清理

  av_frame_free(&pFrameYUV); 
  av_frame_free(&pFrame);
  av_packet_free(&packet);
  avcodec_free_context(&pCodecCtx);
  avcodec_free_context(&aCodecCtx);
  avformat_close_input(&pFormatCtx);
}

 
//使用sdl将yuv显示到屏幕上
void MediaPlayer::showFrame()  {
  
  //初始化sdl, 这个函数需要和showFrame在一个线程里
  sdl_init();
  std::unique_lock<std::mutex> lock(video_Frame_mtx);
  while(true)
  {
    video_Frame_cond.wait_for(lock, std::chrono::milliseconds(1000), [&](){
      return !vFrame_queue.empty();
    });
    if(is_close && vFrame_queue.empty())break;
    else if(vFrame_queue.empty())continue;
    AVFrame *frame = vFrame_queue.front();
    vFrame_queue.pop();

    //将像素格式转换为我们想要的
    auto ret = sws_scale(sws_ctx, frame->data, frame->linesize, 0,
                        frame->height,pFrameYUV->data, pFrameYUV->linesize);
    if(ret < 0)
    {
      std::cerr << "视频转换格式失败" << std::endl;
      return;
    }
    //1.更新纹理的像素数据
    auto ret1 = SDL_UpdateTexture(texture, rect, (const void *)pFrameYUV->data[0], pFrameYUV->linesize[0]);
    if(ret1 < 0)
    {
      std::cerr << "更新纹理失败" << std::endl;
      return;
    }
    SDL_RenderClear(render);
    //2.复制纹理到渲染目标
    auto ret2 = SDL_RenderCopy(render, texture, rect, rect);
    if(ret2 < 0)
    {
      std::cerr << "复制纹理失败" << std::endl;
      return;
    }
    
    //获取当前时间
    gettimeofday(&cur_time, NULL);
    //得到从开始读取数据到现在时间过去了多久
    double time = cur_time.tv_sec - start_time.tv_sec + (double)(cur_time.tv_usec - start_time.tv_usec) / 1000000.0;
    //得到该帧还有多久才是他播放的时间(注意：时间基只能从AVFormatContext里得到，frame里的是错误的)
    //av_q2d = return a.num / (double) a.den;
    double pts = frame->pts * av_q2d(pFormatCtx->streams[videoStreamIndex]->time_base);
    double duration = pts - time;
    //重要操作。延迟播放,让每一帧在他该显示的地方显示
    if(duration > 0)
      SDL_Delay(duration * 1000);    

    //3.显示画面
    SDL_RenderPresent(render);
    //记得回收内存
    av_frame_free(&frame);
  }
  std::cout << "视频播放结束" << std::endl;
}
 
//初始化sdl
void MediaPlayer::sdl_init()  {

  //2.创建窗口
  //创建一个标题为Video,窗口坐标在中间的宽高和视频一样的窗口
  window = SDL_CreateWindow("Video", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, pCodecCtx->width, pCodecCtx->height, SDL_WINDOW_SHOWN);
  if(!window)
  {
    std::cerr << "创建sdl窗口失败" << std::endl;
    return;
  }
  //3.创建渲染器
  //初始化默认的渲染设备，使用软件渲染,和显示器的刷新率同步
  render = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if(!render)
  {
    std::cerr << "创建sdl渲染器失败" << std::endl;
    return;
  }
  //4.创建纹理
  texture = SDL_CreateTexture(render, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);
  if(!texture)
  {
    std::cerr << "创建sdl纹理失败" << std::endl;
    return;
  }

}

void MediaPlayer::audioCallback(void *userdata, Uint8 *stream, int len) {
  MediaPlayer* m = (MediaPlayer *)userdata;
  m->audioDataRead(m->aCodecCtx, stream, len);
}

//音频回调函数
void MediaPlayer::audioDataRead(void *userdata, Uint8 *stream, int len) {

  //这个函数不需要写成循环，因为sdl会自己一直调用的
  AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;
  std::unique_lock<std::mutex> lock(audio_Frame_mtx);
  if(aFrame_queue.empty())return;
  AVFrame *frame = aFrame_queue.front();
  aFrame_queue.pop();

  //分配临时缓冲区
  if(!audio_buf)
  {
    std::cout << "未分配缓冲区" << std::endl;
    audio_buf = (uint8_t*)av_malloc(192000);
  }
  //音频格式转换
  int out_samples = swr_convert(swr_ctx, (uint8_t * const *)&audio_buf, frame->nb_samples, frame->extended_data, frame->nb_samples);
  //获取音频帧的大小
  int data_size = av_samples_get_buffer_size(frame->linesize, frame->ch_layout.nb_channels, frame->nb_samples, aCodecCtx->sample_fmt, 1);
  //len为sdl需要我们写入的缓冲区的大小
  if(data_size > len)
  {
    data_size = len;
  }
  //将音频数据拷贝到sdl要读取的缓冲区里
  //memcpy(stream, frame->data[0], data_size);
  memcpy(stream, audio_buf, data_size);
  stream += data_size;
  len -= data_size;
  av_frame_free(&frame);
}

 
int MediaPlayer::packet_queue_put()  {

  AVPacket *pkt = av_packet_alloc();
  if(av_packet_ref(pkt, packet) < 0)
  {
    std::cerr << "packet copy失败" << std::endl;
    return -1;
  }
  if(pkt->stream_index == audioStreamIndex)
  {
    std::lock_guard<std::mutex> lock(audio_Packet_mtx);
    //缓冲队列满了，选择丢包处理
    if(aPacket_queue.size() >= MAX_QUEUE_SIZE)
    {
      AVPacket *old = aPacket_queue.front();
      aPacket_queue.pop();
      av_packet_free(&old);
    }
    aPacket_queue.push(pkt);
    //缓冲队列新加了数据，唤醒条件变量
    audio_Packet_cond.notify_all();
  }
  else if(pkt->stream_index == videoStreamIndex)
  {
    std::lock_guard<std::mutex> lock(video_Packet_mtx);
    if(vPacket_queue.size() >= MAX_QUEUE_SIZE)
    {
      AVPacket *old = vPacket_queue.front();
      vPacket_queue.pop();
      av_packet_free(&old);
    }
    vPacket_queue.push(pkt); 
    video_Packet_cond.notify_all();
  }
  return 0;
}
 
int MediaPlayer::decode_packet(AVCodecContext* codecCtx, AVPacket* packet)  {
  //将数据包放入解码器解码
  int ret = avcodec_send_packet(codecCtx, packet);
  if(ret < 0)
  {
    std::cerr << "提交数据包到解码器失败:" << av_err2str(ret) << std::endl;
    return -1;
  }
  while(ret >= 0)
  {
    AVFrame *frame = av_frame_alloc();
    //解码后的数据从这个函数中读取,放入frame里(一帧一帧的读)
    ret = avcodec_receive_frame(codecCtx, frame);
    if(ret < 0)
    {
      //这两种情况不是发生了解码错误,所以不退出
      if(ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
      {
        //std::cerr << "没有可用的解码后的视频数据可读了" << std::endl;
        break;
      }
      //其它情况直接退出
      std::cerr << "解码过程出现错误" << av_err2str(ret) << std::endl;
      av_frame_free(&frame);
      return -1;
    }
    if(codecCtx->codec->type == AVMEDIA_TYPE_VIDEO)
    {
      std::lock_guard<std::mutex> lock(video_Frame_mtx);
      if(vFrame_queue.size() > MAX_QUEUE_SIZE)
      {
        //缓冲队列过大，丢帧处理
        AVFrame *old = vFrame_queue.front();
        vFrame_queue.pop();
        av_frame_free(&old);
      }
      vFrame_queue.push(frame);
      video_Frame_cond.notify_all();
    }
    else if(codecCtx->codec->type == AVMEDIA_TYPE_AUDIO)
    {
      std::lock_guard<std::mutex> lock(audio_Frame_mtx);
      if(aFrame_queue.size() > MAX_QUEUE_SIZE)
      {
        AVFrame *old = aFrame_queue.front();
        aFrame_queue.pop();
        av_frame_free(&old);
      }
      aFrame_queue.push(frame);
      audio_Frame_cond.notify_all();
    }
  }
  return 0;
}
 
void MediaPlayer::video_thread()  {

  std::unique_lock<std::mutex> lock(video_Packet_mtx);
  while(true)
  {
    //条件变量阻塞直到有新数据进来
    video_Packet_cond.wait_for(lock, std::chrono::milliseconds(1000), [&](){
      return !vPacket_queue.empty();
    });
    //等待1000ms还没有数据认为程序已经结束
    if(is_close && vPacket_queue.empty())break;
    else if(vPacket_queue.empty())continue;//如果状态没有设置为已经关闭则继续
    AVPacket *pkt = vPacket_queue.front();
    vPacket_queue.pop();
    //解码并放到帧队列
    decode_packet(pCodecCtx, pkt);
    av_packet_free(&pkt);
  }
  std::cout << "视频解码结束" << std::endl;
}

 
void MediaPlayer::audio_thread()  {

  std::unique_lock<std::mutex> lock(audio_Packet_mtx);
  while(true)
  {
    //条件变量阻塞直到有新数据进来(最多等待1000ms)
    audio_Packet_cond.wait_for(lock, std::chrono::milliseconds(1000), [&](){
        return !aPacket_queue.empty();
    });
    //等待1000ms还没有数据且设置关闭状态则认为程序已经结束
    if(is_close && aPacket_queue.empty())break;
    else if(aPacket_queue.empty())continue;
    AVPacket *pkt = aPacket_queue.front();
    aPacket_queue.pop();
    decode_packet(aCodecCtx, pkt);
    av_packet_free(&pkt);
  }
  std::cout << "音频解码结束" << std::endl;
}
 
void MediaPlayer::start()  {
  th[0] = std::thread(&MediaPlayer::readData, this); 
  th[1] = std::thread(&MediaPlayer::video_thread, this); 
  th[2] = std::thread(&MediaPlayer::audio_thread, this); 
  th[3] = std::thread(&MediaPlayer::showFrame, this);

  th[0].join();
  th[1].join();
  th[2].join();
  th[3].join();
  std::cout << "执行完毕" << std::endl;
}
