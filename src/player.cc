#include "player.h"

const char *vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
out vec2 TexCoord;
void main()
{
    gl_Position = vec4(aPos, 1.0);
    TexCoord = aTexCoord;
}
)";

const char *fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D texY;
uniform sampler2D texU;
uniform sampler2D texV;
void main()
{
    float y = texture(texY, TexCoord).r;
    float u = texture(texU, TexCoord).r - 0.5;
    float v = texture(texV, TexCoord).r - 0.5;
    float r = y + 1.402 * v;
    float g = y - 0.344 * u - 0.714 * v;
    float b = y + 1.772 * u;
    FragColor = vec4(r, g, b, 1.0);
}
)";

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
    glDeleteProgram(shaderProgram);
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

    // Shaders
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

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

    glUseProgram(shaderProgram);
    glUniform1i(glGetUniformLocation(shaderProgram, "texY"), 0);
    glUniform1i(glGetUniformLocation(shaderProgram, "texU"), 1);
    glUniform1i(glGetUniformLocation(shaderProgram, "texV"), 2);
    
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

        glUseProgram(shaderProgram);
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        glfwSwapBuffers(window);
        glfwPollEvents();
        
        av_frame_free(&frame);
    }
    is_close = true;
    std::cout << "视频播放结束" << std::endl;
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
    if(av_read_frame(pFormatCtx, packet) < 0)
    {
      break;
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
        return !queue.empty();
    });
    if (is_close && queue.empty()) break;
    else if (queue.empty()) continue;

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
    // make sure the viewport matches the new window dimensions; note that width and 
    // height will be significantly larger than specified on retina displays.
    glViewport(0, 0, width, height);
}
