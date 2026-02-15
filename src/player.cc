#include "player.h"

MediaPlayer::MediaPlayer(const char* url)
{
    th.resize(4);
    url_ = url;

    if(avformat_open_input(&pFormatCtx, url_, NULL, NULL) != 0)
    {
        std::cerr << "打开媒体文件失败:" << stderr << std::endl;
        return ;
    }
    if(avformat_find_stream_info(pFormatCtx, NULL) < 0)
    {
        std::cerr << "探测文件信息失败:" << stderr << std::endl;
        return ;
    }
    av_dump_format(pFormatCtx, 0, url_, 0);

    videoStreamIndex = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    audioStreamIndex = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);

    if (videoStreamIndex < 0 || audioStreamIndex < 0) {
        std::cerr << "未找到视频或音频流" << std::endl;
        return;
    }
    // Video
    vStream = pFormatCtx->streams[videoStreamIndex];
    pCodec = avcodec_find_decoder(vStream->codecpar->codec_id);
    pCodecCtx = avcodec_alloc_context3(pCodec);
    avcodec_parameters_to_context(pCodecCtx, vStream->codecpar);
    avcodec_open2(pCodecCtx, pCodec, NULL);

    // Audio
    aStream = pFormatCtx->streams[audioStreamIndex];
    aCodec = avcodec_find_decoder(aStream->codecpar->codec_id);
    aCodecCtx = avcodec_alloc_context3(aCodec);
    avcodec_parameters_to_context(aCodecCtx, aStream->codecpar);
    avcodec_open2(aCodecCtx, aCodec, NULL);

    allocFrame();

    sws_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height,
                           pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height,
                           AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);

    // Bitstream filter
    const AVBitStreamFilter *bsf = av_bsf_get_by_name("h264_mp4toannexb");
    av_bsf_alloc(bsf, &bsf_ctx);
    avcodec_parameters_copy(bsf_ctx->par_in, vStream->codecpar);
    av_bsf_init(bsf_ctx);

    // Audio Resampling
    swr_ctx = swr_alloc();
    av_opt_set_chlayout(swr_ctx, "in_chlayout", &aCodecCtx->ch_layout, 0);
    av_opt_set_chlayout(swr_ctx, "out_chlayout", &aCodecCtx->ch_layout, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate", aCodecCtx->sample_rate, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", aCodecCtx->sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", aCodecCtx->sample_fmt, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
    swr_init(swr_ctx);

    // PortAudio
    Pa_Initialize();
    
    int numDevices = Pa_GetDeviceCount();
    int defaultOutputDevice = Pa_GetDefaultOutputDevice();
    PaStreamParameters outputParameters;
    outputParameters.device = defaultOutputDevice;
    outputParameters.channelCount = aCodecCtx->ch_layout.nb_channels;
    outputParameters.sampleFormat = paInt16;
    outputParameters.suggestedLatency = Pa_GetDeviceInfo(defaultOutputDevice)->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;

    Pa_OpenStream(
        &audio_stream,
        NULL,
        &outputParameters,
        aCodecCtx->sample_rate,
        256,
        paNoFlag,
        paCallback,
        this
    );
    Pa_StartStream(audio_stream);

    std::cout << "初始化完毕" << std::endl;
}

MediaPlayer::~MediaPlayer()  {
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteTextures(3, textures);
    
    if(window)
        glfwDestroyWindow(window);
    glfwTerminate();

    if(bsf_ctx)
        av_bsf_free(&bsf_ctx);
    
    if (audio_stream) {
        Pa_StopStream(audio_stream);
        Pa_CloseStream(audio_stream);
    }
    Pa_Terminate();

    if(swr_ctx) swr_free(&swr_ctx);

    av_frame_free(&pFrameYUV); 
    av_frame_free(&pFrame);
    av_packet_free(&packet);
    avcodec_free_context(&pCodecCtx);
    avcodec_free_context(&aCodecCtx);
    avformat_close_input(&pFormatCtx);

    if(audio_buf) av_free(audio_buf);
}

void MediaPlayer::gl_init() {
    glfwInitHint(GLFW_WAYLAND_LIBDECOR, GLFW_FALSE);
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    std::cout << pCodecCtx->width << ' ' << pCodecCtx->height << std::endl;
    window = glfwCreateWindow(pCodecCtx->width, pCodecCtx->height, "MediaPlayer", NULL, NULL);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)){
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return;
    }

    shader.init("shaders/vertex.vs", "shaders/fragment.fs");
    float vertices[] = {
        // positions         // texture coords
        1.0f,  1.0f, 0.0f,   1.0f, 0.0f,
        1.0f, -1.0f, 0.0f,   1.0f, 1.0f,
       -1.0f, -1.0f, 0.0f,   0.0f, 1.0f,
       -1.0f,  1.0f, 0.0f,   0.0f, 0.0f
    };
    unsigned int indices[] = {
        0, 1, 3,
        1, 2, 3
    };

    GLuint ebo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Textures
    glGenTextures(3, textures);
    shader.use();
    shader.setInt("texY", 0);
    shader.setInt("texU", 1);
    shader.setInt("texV", 2);
    
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    for (int i = 0; i < 3; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
}

void MediaPlayer::showFrame()  {
    gl_init();
    
    struct timeval start_time, cur_time;
    gettimeofday(&start_time, NULL);
    frame_timer = (double)start_time.tv_sec + (double)start_time.tv_usec / 1000000.0;
    
    while(!glfwWindowShouldClose(window))
    {
        processInput(window);
        if(is_seeking) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if(is_paused) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            glfwPollEvents();
            continue;
        }
        std::unique_lock<std::mutex> lock(video_Frame_mtx);
        video_Frame_cond.wait_for(lock, std::chrono::milliseconds(1), [&](){
            return !vFrame_queue.empty();
        });

        if(is_close && vFrame_queue.empty()) break;
        else if(vFrame_queue.empty()) continue;

        AVFrame *frame = vFrame_queue.front().frame;
        double pts = vFrame_queue.front().pts;
        vFrame_queue.pop();
        
        // -- AV Sync --
        //解决暂停后继续播放时视频播放速度过快而音频正常播放的问题
        //因为暂停之后再继续这个frame_timer的差距就和current_time差距太小了,sleep的时间就短了
        //所以需要更新一下frame_timer
        if (needs_video_timer_reset_on_resume) {
            struct timeval cur_time;
            gettimeofday(&cur_time, NULL);
            double current_real_time = (double)cur_time.tv_sec + (double)cur_time.tv_usec / 1000000.0;
            double ref_clock = get_audio_clock(); // Get current audio clock
            frame_timer = current_real_time + (pts - ref_clock); // Reset frame_timer
            needs_video_timer_reset_on_resume = false;
        }

        double delay = pts - frame_last_pts;
        if(delay <= 0 || delay >= 1.0) {
            delay = frame_last_delay;
        }
        frame_last_delay = delay;
        frame_last_pts = pts;

        double ref_clock = get_audio_clock();
        double diff = pts - ref_clock;
        
        if (diff <= -AV_SYNC_THRESHOLD) {
            delay = 0;
        } else if (diff >= AV_SYNC_THRESHOLD) {
            delay = 2 * delay;
        }

        frame_timer += delay;

        gettimeofday(&cur_time, NULL);
        double current_time = (double)cur_time.tv_sec + (double)cur_time.tv_usec / 1000000.0;
        double actual_delay = frame_timer - current_time;

        if(actual_delay < 0.010) {
            actual_delay = 0.010;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds((long)(actual_delay * 1000)));

        // -- Rendering --
        sws_scale(sws_ctx, frame->data, frame->linesize, 0,
                  frame->height, pFrameYUV->data, pFrameYUV->linesize);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textures[0]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, pFrameYUV->width, pFrameYUV->height, 0, GL_RED, GL_UNSIGNED_BYTE, pFrameYUV->data[0]);
        
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, textures[1]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, pFrameYUV->width / 2, pFrameYUV->height / 2, 0, GL_RED, GL_UNSIGNED_BYTE, pFrameYUV->data[1]);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, textures[2]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, pFrameYUV->width / 2, pFrameYUV->height / 2, 0, GL_RED, GL_UNSIGNED_BYTE, pFrameYUV->data[2]);

        shader.use();
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        glfwSwapBuffers(window);
        glfwPollEvents();
        
        av_frame_free(&frame);
    }
    is_close = true;
    std::cout << "视频播放结束" << std::endl;
}

void MediaPlayer::processInput(GLFWwindow *window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    struct timeval tv;
    gettimeofday(&tv, NULL);
    double current_time = tv.tv_sec + tv.tv_usec / 1000000.0;

    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
        if (current_time - key_last_pressed[GLFW_KEY_SPACE] > 0.2) {
            toggle_pause();
            key_last_pressed[GLFW_KEY_SPACE] = current_time;
        }
    }
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
        if (current_time - key_last_pressed[GLFW_KEY_RIGHT] > 0.2) {
            seek(0.5);
            key_last_pressed[GLFW_KEY_RIGHT] = current_time;
        }
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
        if (current_time - key_last_pressed[GLFW_KEY_LEFT] > 0.2) {
            seek(-0.5);
            key_last_pressed[GLFW_KEY_LEFT] = current_time;
        }
    }
}

void MediaPlayer::allocFrame()  {
  pFrame = av_frame_alloc(); 
  pFrameYUV = av_frame_alloc();
  packet = av_packet_alloc(); 
  if(pFrame == NULL || pFrameYUV == NULL) {
    std::cerr << "分配视频帧空间失败" << stderr << std::endl;
    return ;
  }
  av_image_alloc(pFrameYUV->data, pFrameYUV->linesize, 
                 pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, 1);
  pFrameYUV->width  = pCodecCtx->width;
  pFrameYUV->height = pCodecCtx->height;

  audio_buf = (uint8_t *)av_malloc(192000 * sizeof(uint8_t));
}
 
void MediaPlayer::readData()  {
  while(!is_close)
  {
    // Explicitly wait if seeking
    while(is_seeking) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (is_close) return; // If player is closing while seeking, exit
    }
    // If paused, just sleep and continue
    if(is_paused) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
    }

    int ret = av_read_frame(pFormatCtx, packet);
    if(ret < 0)
    {
      if (ret == AVERROR_EOF) {
          std::cerr << "readData: End of file reached." << std::endl;
          if (is_close) { // If player is explicitly closing, then break
              break;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Wait a bit before trying again
          continue; // Continue the loop to try reading again
      } else {
          char errbuf[AV_ERROR_MAX_STRING_SIZE];
          av_make_error_string(errbuf, AV_ERROR_MAX_STRING_SIZE, ret);
          std::cerr << "readData: Error reading frame: " << errbuf << std::endl;
          break;
      }
    }
    
    if (packet->stream_index == videoStreamIndex) {
        if (av_bsf_send_packet(bsf_ctx, packet) < 0) {
            av_packet_unref(packet);
            continue;
        }
        while (av_bsf_receive_packet(bsf_ctx, packet) == 0) {
            packet_queue_put(packet);
        }
    } else if (packet->stream_index == audioStreamIndex) {
        packet_queue_put(packet);
    }
    else {
        av_packet_unref(packet);
    }
  }
  is_close = true;
  std::cout << "读取数据结束" << std::endl;
}

int MediaPlayer::packet_queue_put(AVPacket *packet) {
  AVPacket* pkt = av_packet_alloc();
  if (av_packet_ref(pkt, packet) < 0) {
    av_packet_free(&pkt);
    return -1;
  }
  
  if (pkt->stream_index == videoStreamIndex) {
      std::lock_guard<std::mutex> lock(video_Packet_mtx);
      if (vPacket_queue.size() >= MAX_QUEUE_SIZE) {
          AVPacket* old = vPacket_queue.front();
          vPacket_queue.pop();
          av_packet_free(&old);
      }
      vPacket_queue.push(pkt);
      video_Packet_cond.notify_all();
  } else if (pkt->stream_index == audioStreamIndex) {
      std::lock_guard<std::mutex> lock(audio_Packet_mtx);
      if (aPacket_queue.size() >= MAX_QUEUE_SIZE) {
          AVPacket* old = aPacket_queue.front();
          aPacket_queue.pop();
          av_packet_free(&old);
      }
      aPacket_queue.push(pkt);
      audio_Packet_cond.notify_all();
  }
  
  return 0;
}

int MediaPlayer::decode_packet(AVCodecContext* codecCtx, AVPacket* packet)  {
  int ret = avcodec_send_packet(codecCtx, packet);
  if(ret < 0)
  {
    std::cerr << "提交数据包到解码器失败:" << av_err2str(ret) << std::endl;
    return -1;
  }
  while(ret >= 0)
  {
    AVFrame *frame = av_frame_alloc();
    ret = avcodec_receive_frame(codecCtx, frame);
    if(ret < 0)
    {
      if(ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
        av_frame_free(&frame);
        break;
      }
      std::cerr << "解码过程出现错误" << av_err2str(ret) << std::endl;
      av_frame_free(&frame);
      return -1;
    }
    
    if (codecCtx->codec_type == AVMEDIA_TYPE_VIDEO) {
        double pts = 0;
        if(packet->dts != AV_NOPTS_VALUE) {
            pts = packet->dts;
        }
        pts *= av_q2d(vStream->time_base);
        pts = synchronize_video(frame, pts);

        std::lock_guard<std::mutex> lock(video_Frame_mtx);
        if(vFrame_queue.size() > MAX_QUEUE_SIZE) {
            AVFrame *old = vFrame_queue.front().frame;
            vFrame_queue.pop();
            av_frame_free(&old);
        }
        vFrame_queue.push({frame, pts});
        video_Frame_cond.notify_all();

    } else if (codecCtx->codec_type == AVMEDIA_TYPE_AUDIO) {
        double pts = 0;
        if(packet->pts != AV_NOPTS_VALUE) {
            pts = packet->pts * av_q2d(aStream->time_base);
        }
        int data_size = av_samples_get_buffer_size(NULL, aCodecCtx->ch_layout.nb_channels, frame->nb_samples, AV_SAMPLE_FMT_S16, 1);

        std::lock_guard<std::mutex> lock(audio_Frame_mtx);
        if(aFrame_queue.size() > MAX_QUEUE_SIZE) {
            AVFrame *old = aFrame_queue.front().frame;
            aFrame_queue.pop();
            av_frame_free(&old);
        }
        aFrame_queue.push({frame, pts, data_size});
        audio_Frame_cond.notify_all();
    }
  }
  return 0;
}
 
void MediaPlayer::video_thread()  {
  stream_thread(vPacket_queue, video_Packet_mtx, video_Packet_cond, pCodecCtx);
  std::cout << "视频解码结束" << std::endl;
}

void MediaPlayer::audio_thread() {
  stream_thread(aPacket_queue, audio_Packet_mtx, audio_Packet_cond, aCodecCtx);
  std::cout << "音频解码结束" << std::endl;
}


void MediaPlayer::stream_thread(std::queue<AVPacket*>& queue, std::mutex& mtx, std::condition_variable& cond, AVCodecContext* codecCtx)
{
  std::unique_lock<std::mutex> lock(mtx);
  while (true) 
  {
    cond.wait_for(lock, std::chrono::milliseconds(100), [&]() {
        return !queue.empty() || is_seeking || is_close;
    });

    // Explicitly wait if seeking
    while(is_seeking) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (is_close) return; // If player is closing while seeking, exit
    }

    if (is_close && queue.empty()) {
        std::cout << "stream_thread: Exiting due to is_close and empty queue." << std::endl;
        break;
    }
    else if (queue.empty()) {
        continue;
    }

    AVPacket* pkt = queue.front();
    queue.pop();
    lock.unlock();

    decode_packet(codecCtx, pkt);
    av_packet_free(&pkt);

    lock.lock();
  }
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
 
double MediaPlayer::synchronize_video(AVFrame *frame, double pts)  {
  double frame_delay;
  if(pts != 0) {
    video_clock = pts;
  } else {
    pts = video_clock;
  }
  frame_delay = av_q2d(vStream->time_base);
  frame_delay += frame->repeat_pict * (frame_delay * 0.5);
  video_clock += frame_delay;
  return pts;
}

double MediaPlayer::get_audio_clock() {
    double pts = audio_clock;
    int hw_buf_size = audio_buf_size - audio_buf_index;
    int bytes_per_sec = aCodecCtx->sample_rate * aCodecCtx->ch_layout.nb_channels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
    if (bytes_per_sec > 0) {
        pts -= (double)hw_buf_size / bytes_per_sec;
    }
    if (pts < 0) { // Ensure clock doesn't go negative
        pts = 0;
    }
    std::cout << "get_audio_clock: audio_clock=" << audio_clock << ", hw_buf_size=" << hw_buf_size << ", bytes_per_sec=" << bytes_per_sec << ", returning pts=" << pts << std::endl;
    return pts;
}

int MediaPlayer::paCallback(const void *inputBuffer, void *outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData) {
    MediaPlayer *p = (MediaPlayer*)userData;
    return p->portAudioCallback(outputBuffer, framesPerBuffer);
}

int MediaPlayer::portAudioCallback(void *outputBuffer, unsigned long framesPerBuffer) {
    if (is_paused || is_seeking) {
        memset(outputBuffer, 0, framesPerBuffer * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16) * aCodecCtx->ch_layout.nb_channels);
        return paContinue;
    }
    int len, audio_size;
    uint8_t *out = (uint8_t*)outputBuffer;
    unsigned long len_to_copy = framesPerBuffer * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16) * aCodecCtx->ch_layout.nb_channels;

    while(len_to_copy > 0) {
        if(audio_buf_index >= audio_buf_size) {
            audio_size = audio_decode_frame(audio_buf, sizeof(audio_buf));
            if(audio_size < 0) {
                audio_buf_size = 1024;
                memset(audio_buf, 0, audio_buf_size);
            } else {
                audio_buf_size = audio_size;
            }
            audio_buf_index = 0;
        }
        len = audio_buf_size - audio_buf_index;
        if(len > len_to_copy)
            len = len_to_copy;

        memcpy(out, (uint8_t *)audio_buf + audio_buf_index, len);
        len_to_copy -= len;
        out += len;
        audio_buf_index += len;
    }
    return paContinue;
}

int MediaPlayer::audio_decode_frame(uint8_t *audio_buf, int buf_size) {
    std::unique_lock<std::mutex> lock(audio_Frame_mtx);
    if(aFrame_queue.empty()) {
        lock.unlock();
        return -1;
    }

    AVFrame *frame = aFrame_queue.front().frame;
    audio_clock = aFrame_queue.front().pts;
    int data_size = aFrame_queue.front().data_bytes;
    aFrame_queue.pop();
    lock.unlock();

    int resampled_data_size = swr_convert(swr_ctx, &audio_buf, frame->nb_samples, (const uint8_t **)frame->extended_data, frame->nb_samples);
    
    int out_size = resampled_data_size * aCodecCtx->ch_layout.nb_channels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);

    av_frame_free(&frame);

    return out_size;
}


void MediaPlayer::framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}

void MediaPlayer::toggle_pause()
{
    is_paused = !is_paused;
    if (!is_paused) { // Resuming playback
        needs_video_timer_reset_on_resume = true;
    }
}

void MediaPlayer::seek(double offset)
{
    double current_pos = get_audio_clock();
    double target_pos = current_pos + offset;
    if (target_pos < 0) {
        target_pos = 0;
    }
    if (target_pos > (float)pFormatCtx->duration / AV_TIME_BASE) {
        target_pos = (float)pFormatCtx->duration / AV_TIME_BASE;
    }

    is_seeking = true;
    is_close = false; // Ensure is_close is false when seeking

    // Notify all threads to pause immediately
    video_Packet_cond.notify_all();
    audio_Packet_cond.notify_all();
    video_Frame_cond.notify_all();
    audio_Frame_cond.notify_all();

    int64_t target_ts = target_pos * AV_TIME_BASE;

    if (av_seek_frame(pFormatCtx, -1, target_ts, AVSEEK_FLAG_BACKWARD) < 0) {
        std::cerr << "Error seeking frame to " << target_pos << "s" << std::endl;
        is_seeking = false;
        return;
    } else {
        std::cout << "Successfully sought to " << target_pos << "s" << std::endl;
    }

    // Flush the codec buffers
    avcodec_flush_buffers(pCodecCtx);
    avcodec_flush_buffers(aCodecCtx);

    // Lock the queues and clear them
    {
        std::lock_guard<std::mutex> v_pkt_lock(video_Packet_mtx);
        while(!vPacket_queue.empty()) {
            av_packet_free(&vPacket_queue.front());
            vPacket_queue.pop();
        }
    }
    {
        std::lock_guard<std::mutex> a_pkt_lock(audio_Packet_mtx);
        while(!aPacket_queue.empty()) {
            av_packet_free(&aPacket_queue.front());
            aPacket_queue.pop();
        }
    }
    {
        std::lock_guard<std::mutex> v_frame_lock(video_Frame_mtx);
        while(!vFrame_queue.empty()) {
            av_frame_free(&vFrame_queue.front().frame);
            vFrame_queue.pop();
        }
    }
    {
        std::lock_guard<std::mutex> a_frame_lock(audio_Frame_mtx);
        while(!aFrame_queue.empty()) {
            av_frame_free(&aFrame_queue.front().frame);
            aFrame_queue.pop();
        }
    }

    // Reset audio buffer indices
    audio_buf_index = 0;
    audio_buf_size = 0;

    audio_clock = target_pos;
    video_clock = target_pos;
    frame_last_pts = target_pos;
    
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    frame_timer = (double)current_time.tv_sec + (double)current_time.tv_usec / 1000000.0;

    is_seeking = false;
    // Notify all threads to resume
    video_Packet_cond.notify_all();
    audio_Packet_cond.notify_all();
    video_Frame_cond.notify_all();
    audio_Frame_cond.notify_all();
}
