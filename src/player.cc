#include "player.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <libavcodec/codec.h>
namespace{

  static bool first_frame = true;
  static double pts_base = 0.0;
  static double time_base = 0.0;
}
MediaPlayer::MediaPlayer(const std::vector<std::string>& urls)
{
  if (urls.size() != 3) {
    std::cerr << "必须提供 3 个视频路径" << std::endl;
    return;
  }

  for (int i = 0; i < 3; ++i) {
    Channel& c = ch[i];
    if (avformat_open_input(&c.fmt_ctx, urls[i].c_str(), nullptr, nullptr) < 0) {
      std::cerr << "第" << i << "路打开失败" << std::endl;
      continue;
    }
    if (avformat_find_stream_info(c.fmt_ctx, nullptr) < 0) continue;

    c.stream_index = av_find_best_stream(c.fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (c.stream_index < 0) continue;

    c.stream = c.fmt_ctx->streams[c.stream_index];
    //const AVCodec* decoder = avcodec_find_decoder(c.stream->codecpar->codec_id);
    const AVCodec* decoder = avcodec_find_decoder_by_name("h264_cuvid");
    c.codec_ctx = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(c.codec_ctx, c.stream->codecpar);
    avcodec_open2(c.codec_ctx, decoder, nullptr);

    c.width = c.codec_ctx->width;
    c.height = c.codec_ctx->height;

    c.sws_ctx = sws_getContext(c.width, c.height, c.codec_ctx->pix_fmt,
                               c.width, c.height, AV_PIX_FMT_YUV420P,
                               SWS_BILINEAR, nullptr, nullptr, nullptr);


    // 分配 YUV 缓冲
    pFrameYUV[i] = av_frame_alloc();
    av_image_alloc(pFrameYUV[i]->data, pFrameYUV[i]->linesize,
                   c.width, c.height, AV_PIX_FMT_YUV420P, 1);
    pFrameYUV[i]->width = c.width;
    pFrameYUV[i]->height = c.height;
  }
}


MediaPlayer::~MediaPlayer()  {
  is_close = true;
  for (auto& t : th) if (t.joinable()) t.join();

  glDeleteVertexArrays(1, &vao);
  glDeleteBuffers(1, &vbo);
  for (auto& c : ch) glDeleteTextures(3, c.textures);

  if (window) glfwDestroyWindow(window);
  glfwTerminate();

  for(int i = 0; i < 3; i ++){
    if(pFrameYUV[i])av_frame_free(&pFrameYUV[i]);
    av_frame_free(&pFrameYUV[i]); 
    av_frame_free(&pFrame[i]);
    av_packet_free(&packet[i]);
    avcodec_free_context(&ch[i].codec_ctx);
    avformat_close_input(&ch[i].fmt_ctx);
  }
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

  int max_w = 0, max_h = 0;
  for (auto& c : ch) {
    max_w = std::max(max_w, c.width);
    max_h = std::max(max_h, c.height);
  }
  // 窗口宽高按最大分辨率，保持16:9左右比例
  window = glfwCreateWindow(max_w * 2, max_h, "三屏C形包围沉浸播放器", nullptr, nullptr);
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
    1.0f,  1.0f, 0.0f,  1.0f, 0.0f,
    1.0f, -1.0f, 0.0f,  1.0f, 1.0f,
    -1.0f, -1.0f, 0.0f,  0.0f, 1.0f,
    -1.0f,  1.0f, 0.0f,  0.0f, 0.0f
  };
  unsigned int indices[] = { 0, 1, 3, 1, 2, 3 };

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

  for(int i = 0; i < 3; i ++){
    glGenTextures(3, ch[i].textures);
    for(int j = 0; j < 3; j ++){
      glActiveTexture(GL_TEXTURE0 + j);
      glBindTexture(GL_TEXTURE_2D, ch[i].textures[j]);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RED,
                   (j == 0 ? ch[i].width : ch[i].width / 2),
                   (j == 0 ? ch[i].height : ch[i].height / 2),
                   0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
  }

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  shader.use();
  shader.setInt("texY", 0);
  shader.setInt("texU", 1);
  shader.setInt("texV", 2);
}

void MediaPlayer::showFrame() {
  gl_init();

  // 计算统一屏幕尺寸（按最大宽度，保持比例）
  float max_w = 0, max_h = 0;
  for (auto& c : ch) {
    max_w = std::max(max_w, (float)c.width);
    max_h = std::max(max_h, (float)c.height);
  }
  float aspect = max_h / max_w;
  float screenWidth = 2.5f;                   // 3D空间中每块屏宽度
  float screenHeight = screenWidth * aspect;

  // 获取当前时间（秒，双精度）
  auto get_current_time = []() -> double {
      struct timeval tv;
      gettimeofday(&tv, nullptr);
      return tv.tv_sec + tv.tv_usec / 1000000.0;
  };

  while (!glfwWindowShouldClose(window)) {
    processInput(window);

    // === 解码 + 上传纹理（你原来的代码完整复制）===
    bool has_frame = false;
    for (int i = 0; i < 3; ++i) {
      std::unique_lock<std::mutex> lk(mtx_frame[i]);
      cond_frame[i].wait_for(lk, std::chrono::milliseconds(1), [&]{
        return !ch[i].frame_queue.empty();
      });

      if (!ch[i].frame_queue.empty()) {
        auto [frame, pts] = ch[i].frame_queue.front();
        ch[i].frame_queue.pop();
        ch[i].clock = pts;

        if(ch[i].first_frame){
          ch[i].pts_base = pts;
          ch[i].time_base = get_current_time();
          ch[i].first_frame = false;
        }
        double video_elapsed = pts - ch[i].pts_base;
        double expected = ch[i].time_base + video_elapsed;
        double now = get_current_time();
        double sleep_sec = std::max(expected - now, sleep_sec) / 3;
        std::cout << sleep_sec << std::endl;

        // 每路独立平滑睡眠
        static double smoothed[3] = {0};
        smoothed[i] = smoothed[i] * 0.8 + sleep_sec * 0.2;

        if (smoothed[i] > 0.002 && smoothed[i] < 0.1) {
            std::this_thread::sleep_for(std::chrono::duration<double>(std::min(smoothed[i], 0.2)));
        }

        sws_scale(ch[i].sws_ctx, frame->data, frame->linesize, 0, ch[i].height,
                  pFrameYUV[i]->data, pFrameYUV[i]->linesize);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ch[i].textures[0]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ch[i].width, ch[i].height, GL_RED, GL_UNSIGNED_BYTE, pFrameYUV[i]->data[0]);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, ch[i].textures[1]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ch[i].width/2, ch[i].height/2, GL_RED, GL_UNSIGNED_BYTE, pFrameYUV[i]->data[1]);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, ch[i].textures[2]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ch[i].width/2, ch[i].height/2, GL_RED, GL_UNSIGNED_BYTE, pFrameYUV[i]->data[2]);

        av_frame_free(&frame);
        has_frame = true;
      }
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // === 统一的相机和投影 ===
    glm::mat4 view = glm::lookAt(
      glm::vec3(0.0f, 0.0f, 0.5f),    // 相机位置（稍微后退一点）
      glm::vec3(0.0f, 0.0f, 0.0f),    // 看向原点
      glm::vec3(0.0f, 1.0f, 0.0f)
    );
    int win_w, win_h;
    glfwGetFramebufferSize(window, &win_w, &win_h);
    glm::mat4 projection = glm::perspective(glm::radians(65.0f), (float)win_w / win_h, 0.1f, 100.0f);

    shader.setMat4("view", view);
    shader.setMat4("projection", projection);

    //关键的四个参数，通过调整这四个参数让视频达到完美融合
    float foldAngle = 25.0f;
    float screenDistance = 3.0f;
    float blendWidth = 0.10f;
    float overlap = 0.25f;
    shader.setFloat("blendWidth", blendWidth);

    glDepthMask(GL_FALSE);

    int draw_order[] = {1, 0, 2}; // 1 = Center, 0 = Left, 2 = Right

    for (int i : draw_order) {
        // 绑定对应通道的纹理
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, ch[i].textures[0]);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, ch[i].textures[1]);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, ch[i].textures[2]);

        shader.setInt("screenIndex", i);

        glm::mat4 model = glm::mat4(1.0f);
        float cosFactor = cos(glm::radians(foldAngle));
        float effectiveWidth = screenWidth * cosFactor;
        float xPos = (i - 1) * (effectiveWidth - overlap);
        model = glm::translate(model, glm::vec3(xPos, 0.0f, -screenDistance));

        if (i != 1) {
            float angle = (i == 0 ? foldAngle : -foldAngle);
            model = glm::rotate(model, glm::radians(angle), glm::vec3(0.0f, 1.0f, 0.0f));
        }
        if (i == 1) {
            model = glm::scale(model, glm::vec3(cosFactor + 0.3f, cosFactor, 1.0f));
        }

        shader.setMat4("model", model);
        shader.setVec2("uvOffset", glm::vec2(0.0f, 0.0f));
        shader.setVec2("uvScale", glm::vec2(1.0f, 1.0f));

        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    }
    // --- 恢复渲染状态 ---
    // 循环结束后，恢复到 gl_init 中设置的默认状态
    glDepthMask(GL_TRUE);
    glfwSwapBuffers(window);
    glfwPollEvents();
  }

  is_close = true;
}

void MediaPlayer::processInput(GLFWwindow *window)
{
  if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
    glfwSetWindowShouldClose(window, true);
}

void MediaPlayer::allocFrame()  {
  for(int i = 0; i < 3; i ++){
    pFrame[i] = av_frame_alloc(); 
    pFrameYUV[i] = av_frame_alloc();
    if(pFrame[i] == NULL || pFrameYUV[i] == NULL) {
      std::cerr << "分配视频帧空间失败" << stderr << std::endl;
      return ;
    }  
    packet[i] = av_packet_alloc(); 
    av_image_alloc(pFrameYUV[i]->data, pFrameYUV[i]->linesize, 
                   ch[i].width, ch[i].height, AV_PIX_FMT_YUV420P, 1);
    pFrameYUV[i]->width  = ch[i].width;
    pFrameYUV[i]->height = ch[i].height;
  }
}
void MediaPlayer::readData(int idx) {
  Channel& c = ch[idx];
  AVPacket pkt;  // 注意：不要提前 alloc，用栈上临时变量

  while (!is_close) {
    int ret = av_read_frame(c.fmt_ctx, &pkt);
    if (ret < 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    if (pkt.stream_index == c.stream_index) {
      if (c.bsf_ctx) {
        // 先送原包
        ret = av_bsf_send_packet(c.bsf_ctx, &pkt);
        if (ret < 0) {
          av_packet_unref(&pkt);
          continue;
        }

        // 循环收所有过滤后的包
        while (av_bsf_receive_packet(c.bsf_ctx, &pkt) == 0) {
          AVPacket* copy = av_packet_clone(&pkt);
          if (copy) {
            std::lock_guard<std::mutex> lk(mtx_frame[idx]);
            if (c.packet_queue.size() >= MAX_QUEUE_SIZE) {
              av_packet_free(&c.packet_queue.front());
              c.packet_queue.pop();
            }
            c.packet_queue.push(copy);
            cond_frame[idx].notify_one();
          }
        }
      } else {
        // 无 bsf，直接 clone
        AVPacket* copy = av_packet_clone(&pkt);
        if (copy) {
          std::lock_guard<std::mutex> lk(mtx_frame[idx]);
          c.packet_queue.push(copy);
          cond_frame[idx].notify_one();
        }
      }
    }
    av_packet_unref(&pkt);  // 关键！每次都 unref 原始包
  }
}

void MediaPlayer::decodeThread(int idx) {

  Channel& c = ch[idx];
  while (!is_close) {
    AVPacket* pkt = nullptr;
    {
      std::unique_lock<std::mutex> lk(mtx_frame[idx]);
      cond_frame[idx].wait_for(lk, std::chrono::milliseconds(10), [&]{
        return !c.packet_queue.empty() || is_close;
      });
      if (c.packet_queue.empty()) continue;
      pkt = c.packet_queue.front();
      c.packet_queue.pop();
    }

    avcodec_send_packet(c.codec_ctx, pkt);
    while (true) {
      AVFrame* frame = av_frame_alloc();
      int ret = avcodec_receive_frame(c.codec_ctx, frame);
      if (ret < 0) { av_frame_free(&frame); break; }

      double pts = 0.0;
      if (pkt->pts != AV_NOPTS_VALUE)
        pts = pkt->pts * av_q2d(c.stream->time_base);
      pts = synchronize_video(idx, frame, pts);

      {
        std::lock_guard<std::mutex> lk(mtx_frame[idx]);
        if (c.frame_queue.size() > MAX_QUEUE_SIZE) {
          av_frame_free(&c.frame_queue.front().frame);
          c.frame_queue.pop();
        }
        Frame f{frame, pts};
        c.frame_queue.emplace(f);
      }
      cond_frame[idx].notify_one();
    }
    av_packet_free(&pkt);
  }
  }



void MediaPlayer::start()  {
  for(int i = 0; i < 3; i ++){
    th.emplace_back(&MediaPlayer::readData, this, i);
    th.emplace_back(&MediaPlayer::decodeThread, this, i);
  }
  showFrame();
  for(auto& t : th)t.join();
  std::cout << "执行完毕" << std::endl;
}

double MediaPlayer::synchronize_video(int idx, AVFrame *frame, double pts)  {
  double frame_delay;
  if(pts != 0) {
    ch[idx].clock = pts;
  } else {
    pts = ch[idx].clock;
  }
  frame_delay = av_q2d(ch[idx].stream->time_base);
  frame_delay += frame->repeat_pict * (frame_delay * 0.5);
  ch[idx].clock += frame_delay;
  return pts;
}

void MediaPlayer::framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
  glViewport(0, 0, width, height);
}
