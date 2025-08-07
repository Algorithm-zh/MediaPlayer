#include "player.h"

MediaPlayer::MediaPlayer(const char* url, AV_SYNC_TYPE av_sync_type)
:av_sync_type(av_sync_type)
{
  th.resize(5);
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
  
  //初始化音频同步设置
  //这个写法的意思是构造一个指数平均系数，影响范围约为NB帧，累计的误差约为0.01s
  audio_diff_avg_coef = exp(log(0.01 / AUDIO_DIFF_AVG_NB));//exp和log抵消了，主要是显示表达意图
  audio_diff_threshold = 2.0 * SDL_AUDIO_BUFFER_SIZE / aCodecCtx->sample_rate;


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
  flush_pkt = av_packet_alloc();
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
  //开始从视频流中读取数据包
  while(!is_close)
  {
    //seek操作
    if(seek_req.load())
    {
      std::cout << "seek_req:" << seek_req << std::endl;
      seek();
    }
    if(av_read_frame(pFormatCtx, packet) < 0)
    {
      break;
    }
    packet_queue_put(packet);
    //释放掉packet指向的内存,以方便读下一个包
    av_packet_unref(packet);
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
  double delay, ref_clock, diff, sync_threshold, actual_delay;
  //初始化sdl, 这个函数需要和showFrame在一个线程里
  sdl_init();
  //获取开始显示帧的时间
  gettimeofday(&start_time, NULL);
  while(true)
  {
    std::unique_lock<std::mutex> lock(video_Frame_mtx);
    video_Frame_cond.wait_for(lock, std::chrono::milliseconds(1000), [&](){
      return !vFrame_queue.empty();
    });

    if(is_close && vFrame_queue.empty())break;
    else if(vFrame_queue.empty())continue;

    AVFrame *frame = vFrame_queue.front().frame;
    double pts = vFrame_queue.front().pts;
    video_current_pts = pts;
    gettimeofday(&video_current_pts_time, NULL);
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
    /*
     音视频同步逻辑详解(音频为主)：
      音频为主即让视频去凑近音频
      获取当前帧的pts,与上一帧的pts相减得到delay（pts是每一帧显示完成的时间点）
      那么正常情况下该帧就应该显示delay时间
      但是由于解码速度、pts错误、以及pb帧等原因的影响，我们需要对其进行调整
      获取音频时钟ref（也就是当前正在播放的音频位置的pts）
      比较视频时钟和音频时钟的差距diff = pts - ref
      然后设定一个阈值，阈值过大无法同步(本代码没有进行同步)
      如果音频比视频快，减小delay时间,让视频帧快速显示完成
      如果视频比音频快，将delay翻两倍,让视频帧多停留一会
      设置frame_timer叠加每次的delay
      然后将frame_timer和系统时钟time进行对比
      那么该帧实际需要停留的时间就是actual_delay = frame_timer-time（让系统时钟到达我们设定的显示时间）
      actual_delay如果过小设置一个最小显示时间(因为需要让一帧有停留时间，不然就相当于丢帧了)
    */
    //pts是指这一帧显示完的时间点
    delay = pts - frame_last_pts; 
    if(delay <= 0 || delay >= 1.0)
    {
      delay = frame_last_delay;
    }
    //为下个time保存
    frame_last_delay = delay;
    frame_last_pts = pts;

    //确保当视频始终作为参考时钟时不进行视频同步操作
    if(av_sync_type != AV_SYNC_TYPE::AV_SYNC_VIDEO_MASTER)
    {
      //获取音频时间⏰
      ref_clock = get_audio_clock();
      
      //计算视频时间戳和音频时间之差
      diff = pts - ref_clock;
      //计算同步阈值
      sync_threshold = (delay > AV_SYNC_THRESHOLD) ? delay : AV_SYNC_THRESHOLD;
      if(std::fabs(diff) < AV_NOSYNC_THRESHOLD)
      {
        //音频比视频快，选择不延迟
        if(diff <= -sync_threshold)
        {
          delay = 0;
        }
        //视频比音频快，延迟2倍
        else if(diff >= sync_threshold)
        {
          delay = 2 * delay;
        }
      }
    }
    frame_timer += delay;
    //计算一下真实时间
    gettimeofday(&cur_time, NULL);
    //计算从开始读包到现在过去了多长时间
    double time = (cur_time.tv_sec - start_time.tv_sec) + (cur_time.tv_usec - start_time.tv_usec) / 1000000.0;
    //计算实际延迟
    actual_delay = frame_timer - time;
    if(actual_delay < 0.010)
    {
      actual_delay = 0.010;
    }
    SDL_Delay(actual_delay * 1000 + 0.5);//+0.5是为了四舍五入
    
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
  AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;
  audio_buf_index = 0;
  audio_buf_size = 0;
  int len1 = 0;
  //为何写个循环，因为要往len里填充数据，一帧音频帧可能不够,需要填充满
  while(len > 0)
  {
    if(audio_buf_index >= audio_buf_size)
    {
      std::unique_lock<std::mutex> lock(audio_Frame_mtx);
      //注意点：这里选择先填充静音数据而不是直接返回，因为直接返回sdl会继续使用stream里的数据会形成杂音
      if(aFrame_queue.empty())
      {
        memset(stream, 0, len);
        return;
      }
      AVFrame *frame = aFrame_queue.front().frame;
      //得到音频帧大小(注意这里没有很规范，因为下面转换格式了，正常应该下面转换格式后再获取数据大小，这里为了省事直接用之前的)
      int audio_size = aFrame_queue.front().data_bytes;
      //得到pts
      audio_clock = aFrame_queue.front().pts;
      aFrame_queue.pop();
      //分配临时缓冲区
      if(!audio_buf)
      {
        audio_buf = (uint8_t*)av_malloc(192000);
      }
      //音频格式转换
      int out_samples = swr_convert(swr_ctx, (uint8_t * const *)&audio_buf, frame->nb_samples, frame->extended_data, frame->nb_samples);
      //同步音频时钟
      audio_size = synchronize_audio((int16_t *)audio_buf, audio_size, audio_clock);
      if(audio_size < 0)
      {
        audio_buf_size = 1024;
        memset(audio_buf, 0, audio_buf_size);
      }
      else
      {
        audio_buf_size = audio_size;
      }
      audio_buf_index = 0;
      av_frame_free(&frame);
    }
    len1 = audio_buf_size - audio_buf_index;
    if(len1 > len)
    {
      len1 = len;
    }
    //将音频数据拷贝到sdl要读取的缓冲区里
    memcpy(stream, audio_buf + audio_buf_index, len1);
    stream += len1;
    audio_buf_index += len1;
    len -= len1;
  }
}

 

int MediaPlayer::packet_queue_put(AVPacket *packet) {
  AVPacket* pkt = av_packet_alloc();
  //设置不是flush_packet
  if (packet != flush_pkt && av_packet_ref(pkt, packet) < 0) {
    std::cerr << "packet 拷贝失败" << std::endl;
    av_packet_free(&pkt);  // 避免内存泄漏
    return -1;
  }

  //这里为了将重复代码简化用了个lambda
  auto enqueue_packet = [](std::queue<AVPacket*>& queue, std::mutex& mtx, std::condition_variable& cond, AVPacket* pkt) {
    std::lock_guard<std::mutex> lock(mtx);
    if (queue.size() >= MAX_QUEUE_SIZE) {
      AVPacket* old = queue.front();
      queue.pop();
      av_packet_free(&old);
    }
    queue.push(pkt);
    cond.notify_all();
  };

  if (pkt->stream_index == audioStreamIndex) 
  {
    enqueue_packet(aPacket_queue, audio_Packet_mtx, audio_Packet_cond, pkt);
  } 
  else if (pkt->stream_index == videoStreamIndex) 
  {
    enqueue_packet(vPacket_queue, video_Packet_mtx, video_Packet_cond, pkt);
  } 
  else 
  {
    av_packet_free(&pkt);  // 不属于音视频流，直接释放
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
    double pts = 0;
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
        AVFrame *old = vFrame_queue.front().frame;
        vFrame_queue.pop();
        av_frame_free(&old);
      }
      //获取pts,如果dts不存在但是opaque里有则用opaque里的值。不然就是dts,都没有就为0
      if(packet->dts == AV_NOPTS_VALUE && frame->opaque && (int64_t)frame->opaque != AV_NOPTS_VALUE)
      {
        //将opaqueue强转为int64_t类型的指针然后取值
        pts = *(int64_t*)frame->opaque; 
      }
      else if(packet->dts != AV_NOPTS_VALUE)
      {
        pts = packet->dts;
      }
      else
      {
        pts = 0;
      }
      pts *= av_q2d(vStream->time_base);
      pts = synchronize_video(frame, pts);//处理一下pts
      vFrame_queue.push({frame, pts});
      video_Frame_cond.notify_all();
    }
    else if(codecCtx->codec->type == AVMEDIA_TYPE_AUDIO)
    {
      std::lock_guard<std::mutex> lock(audio_Frame_mtx);
      if(aFrame_queue.size() > MAX_QUEUE_SIZE)
      {
        AVFrame *old = aFrame_queue.front().frame;
        aFrame_queue.pop();
        av_frame_free(&old);
      }
      if(packet->pts != AV_NOPTS_VALUE)
      {
        pts = packet->pts * av_q2d(aStream->time_base);
      }
      //获取音频帧大小
      int data_size = av_samples_get_buffer_size(frame->linesize, frame->ch_layout.nb_channels, frame->nb_samples, aCodecCtx->sample_fmt, 1);
      aFrame_queue.push({frame,pts, data_size});
      audio_Frame_cond.notify_all();
    }
  }
  return 0;
}
 
void MediaPlayer::video_thread()  {
 stream_thread(vPacket_queue, video_Packet_mtx, video_Packet_cond, pCodecCtx);
 std::cout << "视频解码结束" << std::endl;
}

void MediaPlayer::audio_thread()  {
 stream_thread(aPacket_queue, audio_Packet_mtx, audio_Packet_cond, aCodecCtx);
 std::cout << "音频解码结束" << std::endl;
}
 
void MediaPlayer::stream_thread(std::queue<AVPacket*>& queue, std::mutex& mtx, std::condition_variable& cond, AVCodecContext* codecCtx)
{
  std::unique_lock<std::mutex> lock(mtx);
  while (true) 
  {
    //条件变量阻塞直到有新数据进来(最多等待1000ms)
    cond.wait_for(lock, std::chrono::milliseconds(1000), [&]() {
        return !queue.empty();
    });
    //等待1000ms还没有数据且设置关闭状态则认为程序已经结束
    if (is_close && queue.empty()) break;
    else if (queue.empty()) continue;//如果状态没有设置为已经关闭则继续

    AVPacket* pkt = queue.front();
    queue.pop();

    //如果是刷新包则刷新avcodec缓冲区
    if (pkt->data == flush_pkt->data) 
    {
      avcodec_flush_buffers(codecCtx);
      continue;
    }
    decode_packet(codecCtx, pkt);
    av_packet_free(&pkt);
  }
}

void MediaPlayer::start()  {
  th[0] = std::thread(&MediaPlayer::readData, this); 
  th[1] = std::thread(&MediaPlayer::video_thread, this); 
  th[2] = std::thread(&MediaPlayer::audio_thread, this); 
  th[3] = std::thread(&MediaPlayer::showFrame, this);
  th[4] = std::thread(&MediaPlayer::control_thread, this);

  th[0].join();
  th[1].join();
  th[2].join();
  th[3].join();
  th[4].join();
  std::cout << "执行完毕" << std::endl;
}
 
//如果帧存在pts,直接返回即可，如果缺失，则通过video_clock来得到
double MediaPlayer::synchronize_video(AVFrame *frame, double pts)  {
  double frame_delay = 0;
  if(pts != 0)
  {
    video_clock = pts;
  }
  else 
  {
    pts = video_clock;
  }
  //更新video_clock
  frame_delay = av_q2d(vStream->time_base);
  //处理重复帧的情况
  frame_delay += frame->repeat_pict * (frame_delay * 0.5);
  video_clock += frame_delay;//更新video_clock，存储下一帧的显示时间
  return pts;
}
 
//获得音频时间钟
//audio_clock记录的是一整个音频帧播放完的时间，但是声卡驱动填充播放还需要时间，所以需要通过计算得到实际正在播放的音频时钟
double MediaPlayer::get_audio_clock()  {

  double pts;
  int hw_buf_size, bytes_per_sec, n;//还没有填充到sdl音频播放驱动里的数据量，每秒播放的字节数
  
  pts = audio_clock;
  hw_buf_size = audio_buf_size - audio_buf_index;
  bytes_per_sec = 0;
  n = aCodecCtx->ch_layout.nb_channels * AV_SAMPLE_FMT_S16;//声道数✖️每个声道使用的位深(这里固定为了AV_SAMPLE_FMT_S16)
  bytes_per_sec = aCodecCtx->sample_rate * n;
  double tmp = (double)hw_buf_size / bytes_per_sec;
  if(pts >= tmp)
  {
    pts -= tmp;
  }
  return pts;//实际播放的时间
}
 
//获取视频时钟
//这样实现而不是简单的使用pts可以防止误差过大
double MediaPlayer::get_video_clock()  {
  double delta;
  gettimeofday(&cur_time, NULL);
  delta = (cur_time.tv_sec - video_current_pts_time.tv_sec) + (cur_time.tv_usec - video_current_pts_time.tv_usec) / 1000000.0;
	return video_current_pts + delta;
}
 
double MediaPlayer::get_master_clock()  {
  if(av_sync_type == AV_SYNC_TYPE::AV_SYNC_VIDEO_MASTER)
  {
    return get_video_clock();
  }
  else if(av_sync_type == AV_SYNC_TYPE::AV_SYNC_AUDIO_MASTER)
  {
    return get_audio_clock();
  }
  else
  {
    return get_external_clock();
  }
}
 
//同步音频时钟 
//通过调整采样大小
/*
 * 通过写音频同步视频这个函数可以了解的难点：
 * 我们如果单纯使用diff进行调整， 如果某次diff跳动过大，就会引起音频采样大幅度变化
 * 而这样就会严重影响用户观看
 * 采用的方式就是指数加权平均移动
 * 让diff平滑调整，选择一个稳定的diff进行调整，这样就不会使大幅度的diff变化影响整体
*/
int MediaPlayer::synchronize_audio(short *samples, int samples_size, double pts)  {

  int n;
  double ref_clock;
  n = 2 * aCodecCtx->ch_layout.nb_channels;

  if(av_sync_type != AV_SYNC_TYPE::AV_SYNC_AUDIO_MASTER)
  {
    double diff, avg_diff;//误差和平滑后的误差
    int wanted_size, min_size, max_size; //nb_samples

    //这里是视频时钟为主.所以返回的是视频时钟
    ref_clock = get_master_clock();
    //当前音频时钟和视频时钟的差值
    diff = get_audio_clock() - ref_clock;

    //较小时才去矫正
    if(diff < AV_NOSYNC_THRESHOLD)
    {
      //使用指数加权平均对差值进行平滑防止跳变引起错误的调整
      //差值平滑累计
      audio_diff_cum = diff + audio_diff_avg_coef * audio_diff_cum;//该式子取极限等价于diff*1/1-eof
      //enum
      if(audio_diff_avg_count < AUDIO_DIFF_AVG_NB)
      {
        audio_diff_avg_count ++;//用于确保前几次不生效直到稳定
      }
      else
      {
        //为何乘这个？因为要还原为diff,audio_diff_cum是“放大”后的值
        avg_diff = audio_diff_cum * (1.0 - audio_diff_avg_coef);   
        if(fabs(avg_diff) >= audio_diff_threshold)
        {
          
          //采样大小 = 采样率（1s采样多少次）* 差距时间 * 每个采样点的字节数 * 通道数
          //也就是让采样大小更大或者更小，更大点就能让视频更长，否则更短
          wanted_size = samples_size + (int)(diff * aCodecCtx->sample_rate) * n;
          min_size = samples_size * (100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100;
          max_size = samples_size * (100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100;
          
          if(wanted_size < min_size)
          {
            wanted_size = min_size;
          }
          else if(wanted_size > max_size)
          {
            wanted_size = wanted_size;
          }
          
          if(wanted_size < samples_size)
          {
            //如果是缩减直接删掉后面的
            samples_size = wanted_size;
          }
          else if(wanted_size > samples_size)
          {
            uint8_t *sample_end, *q;
            int nb;

            //通过复制后面的采样点来加长音频
            nb = wanted_size - samples_size;//需要增加的字节数
            sample_end = (uint8_t*)samples + samples_size - n;

            //分配新buffer(防止内存区域大小不够)
            uint8_t *new_samples = (uint8_t*)av_malloc(wanted_size);
            memcpy(new_samples, samples, samples_size);

            //填充新样本
            q = new_samples + samples_size;
            while(nb > 0)
            {
              memcpy(q, sample_end, n);
              q += n;
              nb -= n;
            }
            //释放原来的buffer并指向新buffer
            // BUG FIX: COMMENTED OUT PROBLEMATIC CODE - samples was not allocated with av_malloc()
            // av_free(samples);  // <- MISTAKE: Don't free samples that weren't allocated with av_malloc()
            
            // FIXED: Don't free the original samples buffer since it belongs to the audio frame
            // The original samples buffer will be freed when the audio frame is freed
            samples = (short*)new_samples;
            samples_size = wanted_size;
          }
        }
      }
    }
    else 
    {
      //差距过大，不再做渐进调节，而是直接重置状态，等待下一次稳定同步
      audio_diff_avg_count = 0;
      audio_diff_cum = 0;
    }
  }
  return samples_size;
}
 
double MediaPlayer::get_external_clock()  {
  gettimeofday(&cur_time, NULL);
	return cur_time.tv_sec + cur_time.tv_usec / 1000000.0;
}
 
void MediaPlayer::stream_seek(int64_t pos, int rel)  {
  if(!seek_req.load()) 
  {
    seek_pos = pos;  
    seek_flags = rel < 0 ? AVSEEK_FLAG_BACKWARD : 0;
    seek_req.store(true);
  }
}
 
void MediaPlayer::packet_queue_flush(PacketQueue& packet_queue, int stream_index, std::mutex &mtx)  {
  std::lock_guard<std::mutex> lock(mtx);
  while(!packet_queue.empty())
  {
    auto p = packet_queue.front();
    packet_queue.pop();
    av_packet_free(&p);
  }
  std::cout << "刷新完毕" << std::endl;
}


void MediaPlayer::frame_queue_flush(std::queue<Frame>& queue, std::mutex& mtx) {
    std::lock_guard<std::mutex> lock(mtx);
    while (!queue.empty()) {
        AVFrame* frame = queue.front().frame;
        queue.pop();
        av_frame_free(&frame);
    }
}
 
void MediaPlayer::seek()  {
  int64_t seek_target = seek_pos;
  int stream_index = -1;
  std::cout << "seek" << std::endl;
  if(videoStreamIndex >= 0)
  {
    stream_index = videoStreamIndex;
  }
  else if(audioStreamIndex >= 0)
  {
    stream_index = audioStreamIndex;
  }
  if(stream_index >= 0)
  {
    seek_target = av_rescale_q(seek_target, AV_TIME_BASE_Q, pFormatCtx->streams[stream_index]->time_base);
    std::cout << "seek_target:" << seek_target << std::endl;
    //跳转到seek_target位置，这个需要时间基正确，否则会出错
    if(av_seek_frame(pFormatCtx, stream_index, seek_target, seek_flags) < 0)
    {
      std::cerr << "跳转失败" << std::endl;
    }
    else
    {
      //刷新packet队列
      if(audioStreamIndex >= 0)
      {
        packet_queue_flush(aPacket_queue, audioStreamIndex, audio_Packet_mtx);
        frame_queue_flush(aFrame_queue, audio_Frame_mtx);
        //把刷新标志包放到队列里
        packet_queue_put(flush_pkt);
      }
      if(videoStreamIndex >= 0)
      {
        packet_queue_flush(vPacket_queue, videoStreamIndex, video_Packet_mtx);
        frame_queue_flush(vFrame_queue, video_Frame_mtx);
        packet_queue_put(flush_pkt);
      }
    }
    seek_req.store(false);
  }
}
 
void MediaPlayer::control_thread()  {

  while(true)
  {
    SDL_WaitEvent(&event);
    switch (event.type) {
      case SDL_QUIT:
        std::cout << "SDL_QUIT" << std::endl;
        is_close = true;
        break;
      case SDL_KEYDOWN:
        switch(event.key.keysym.sym)
        {
          case SDLK_LEFT:
            incr = -10.0;
            goto do_seek;
          case SDLK_RIGHT:
            incr = 10.0;
            goto do_seek;
          case SDLK_UP:
            incr = 60.0;
            goto do_seek;
          case SDLK_DOWN:
            incr = -60.0;
            goto do_seek;
          do_seek:
            
            pos = get_master_clock();  
            pos += incr;               
            //Convert seconds to AV_TIME_BASE units before seeking
            int64_t seek_pos_timebase = (int64_t)(pos * AV_TIME_BASE);
            stream_seek(seek_pos_timebase, incr);
            std::cout << "pos:" << pos << std::endl;
            break;
        }
    }
    if(is_close)break;
  }
}
