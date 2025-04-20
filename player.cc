#include "player.h"

MediaPlayer::MediaPlayer(const char* url)  {
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
  if(avformat_open_input(&pFormatCtx, url_, nullptr, nullptr) != 0)
  {
    std::cerr << "打开媒体文件失败:" << stderr << std::endl;
    return ;
  }
  //2.媒体信息的探测和分析函数,填充pFormatCtx->streams对应的信息
  if(avformat_find_stream_info(pFormatCtx, nullptr) < 0)
  {
    std::cerr << "探测文件信息失败:" << stderr << std::endl;
    return ;
  }
  //打印pFormatCtx信息
  av_dump_format(pFormatCtx, 0, url_, 0);

  //3.找到视频流的下标
  videoStreamIndex = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (videoStreamIndex == -1) {
    std::cerr << "未找到视频流:" << stderr << std::endl;
    return ;
  }

  //4.得到视频流
  vStream = pFormatCtx->streams[videoStreamIndex];

  //5.流信息中关于codec的部分存储在了AVCodecParameters里,通过它里面的codec_id打开解码器
  //也可以直接自己指定解码器
  pCodec = avcodec_find_decoder(vStream->codecpar->codec_id);
  if(pCodec == nullptr)
  {
    std::cerr << "打开解码器失败:" << stderr << std::endl;
    return ;
  }

  //6.初始化解码器的上下文
  pCodecCtx = avcodec_alloc_context3(pCodec);

  //7.把输入流里的AVCodecParameters的参数拷贝到解码器的上下文里
  //注意:旧版本使用的是avcodec_copy_context函数,新版本不要使用了
  if(avcodec_parameters_to_context(pCodecCtx, vStream->codecpar) < 0)
  {
    std::cerr << "输入流参数拷贝到解码器上下文中失败:" << stderr << std::endl;
    return ;
  }

  //8.初始化解码器
  if(avcodec_open2(pCodecCtx, pCodec, nullptr) < 0)
  {
    std::cerr << "初始化解码器失败" << stderr << std::endl;
  }
  allocFrame();
}
 
void MediaPlayer::allocFrame()  {
  //为frame开辟空间
  pFrame = av_frame_alloc(); 
  pFrameRGB = av_frame_alloc();
  packet = av_packet_alloc(); 
  if(pFrame == nullptr || pFrameRGB == nullptr)
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
  av_image_alloc(pFrameRGB->data, pFrameRGB->linesize, 
                 pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_RGB24, 1);
  //上面这个函数并不会设置下面这两个值
  pFrameRGB->width  = pCodecCtx->width;
  pFrameRGB->height = pCodecCtx->height;
    // 分配数据缓冲区
//  if (av_frame_get_buffer(pFrameRGB, 1) < 0) {
//      std::cerr << "RGB Frame 缓冲区分配失败\n";
//      return;
//  }
}
 
void MediaPlayer::readData()  {
  int i = 0;
  //初始化滤镜上下文
  sws_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height,
                           pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height,
                           AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
  //开始从视频流中读取数据包
  while(av_read_frame(pFormatCtx, packet) >= 0)
  {
    if(packet->stream_index == videoStreamIndex)
    {
      //将数据包放入解码器解码
      int ret = avcodec_send_packet(pCodecCtx, packet);
      if(ret < 0)
      {
        std::cerr << "提交数据包到解码器失败:" << stderr << std::endl;
        return;
      }
      while(ret >= 0)
      {
        //解码后的数据从这个函数中读取,放入frame里(一帧一帧的读)
        ret = avcodec_receive_frame(pCodecCtx, pFrame);
        if(ret < 0)
        {
          //这两种情况不是发生了解码错误,所以不退出
          if(ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
          {
            //std::cerr << "没有可用的解码后的视频数据可读了" << std::endl;
            break;
          }
          //其它情况直接退出
          std::cerr << "解码过程出现错误" << stderr << std::endl;
          return;
        }
        //读取到解码后的frame了,将它转为我们想要的格式的frame
        //sws_scale函数能够做视频像素格式和分辨率转换
        //四五参数,int srcSliceY, int srcSliceH,定义在输入图像上处理区域，srcSliceY是起始位置，srcSliceH是处理多少行
        //这两个参数主要是为了多线程处理,可以创建两个线程,第一个处理0,h/2-1行,第二个处理h/2,h-1行
        //设置为0,height,就是全都处理
        std::cout << pFrame->height << " " << pFrameRGB->width << std::endl;
        int ret = sws_scale(sws_ctx, pFrame->data, pFrame->linesize, 0, pFrame->height, pFrameRGB->data, pFrameRGB->linesize);
        if(ret <= 0)
        {
          std::cerr << "转换格式失败" << std::endl;
          return ;
        }
        if(++ i < 80 && i > 75)
        {
          saveFrame(i); 
        }
      }
    }
    //释放掉packet指向的内存,以方便读下一个包
    av_packet_unref(packet);
  }
}

void MediaPlayer::saveFrame(int iFrame)  {
  FILE *pFile;
  char szFileName[32];
  int y;

  //open file
  sprintf(szFileName, "frame%d.ppm", iFrame);
  pFile = fopen(szFileName, "wb");
  if(pFile == nullptr)
  {
    return ;
  }
  //write header
  fprintf(pFile, "P6\n%d %d\n255\n", pCodecCtx->width, pCodecCtx->height);

  //write pixel data
  for(y = 0; y < pFrameRGB->height; y ++)
  {
    fwrite(pFrameRGB->data[0] + y * pFrameRGB->linesize[0], 1, pFrameRGB->width * 3, pFile);
  }
  fclose(pFile);
  std::cout << "saveFrame" << std::endl;
}

MediaPlayer::~MediaPlayer()  {
  av_frame_free(&pFrameRGB); 
  av_frame_free(&pFrame);
  av_packet_free(&packet);
  avcodec_free_context(&pCodecCtx);
  avformat_close_input(&pFormatCtx);
}

