#include "player.h"
#include "OpenglRender.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <libavcodec/codec.h>
#include <libavutil/frame.h>

MediaPlayer::MediaPlayer(const std::vector<std::string>& urls, const std::string &push_url)
  :push_url(push_url)
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

    pFrameYUV[i] = av_frame_alloc();
    //重要操作：不然无法使用
    av_image_alloc(pFrameYUV[i]->data, pFrameYUV[i]->linesize, 
                   ch[i].width, ch[i].height, AV_PIX_FMT_YUV420P, 1);
    pFrameYUV[i]->width  = c.width;
    pFrameYUV[i]->height = c.height;
  }

}


MediaPlayer::~MediaPlayer()  {
  is_close = true;
  for (auto& t : th) if (t.joinable()) t.join();

  for(int i = 0; i < 3; i ++){
    av_frame_free(&pFrameYUV[i]);
    avcodec_free_context(&ch[i].codec_ctx);
    avformat_close_input(&ch[i].fmt_ctx);
  }
}



void MediaPlayer::showFrame() {
  if(!renderer)return;

  // 获取当前时间（秒，双精度）
  auto get_current_time = []() -> double {
      struct timeval tv;
      gettimeofday(&tv, nullptr);
      return tv.tv_sec + tv.tv_usec / 1000000.0;
  };

  while (!renderer->ShouldClose() && !is_close) {
    renderer->PollEvents();

    int win_w = 0, win_h = 0;
    renderer->GetFramebufferSize(win_w, win_h);

    //初始化推流
    if(!outStream.push_enabled && win_w && win_h){
      if(!outStream.init(push_url, win_w, win_h, 25)){
        std::cerr << "推流初始化失败，关闭推流" << std::endl;
        outStream.push_enabled = false;
      }
    }

    bool has_frame = false;
    for (int i = 0; i < 3; ++i) {
      std::unique_lock<std::mutex> lk(mtx_frame[i]);
      cond_frame[i].wait_for(lk, std::chrono::milliseconds(10), [&]{
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

        // 每路独立平滑睡眠
        static double smoothed[3] = {0};
        smoothed[i] = smoothed[i] * 0.8 + sleep_sec * 0.2;

        if (smoothed[i] > 0.002 && smoothed[i] < 0.1) {
            std::this_thread::sleep_for(std::chrono::duration<double>(std::min(smoothed[i], 0.2)));
        }

        sws_scale(ch[i].sws_ctx, frame->data, frame->linesize, 0, ch[i].height,
                  pFrameYUV[i]->data, pFrameYUV[i]->linesize);

        //将yuv原始视频里上传到纹理
        renderer->UpLoadTexture(i, ch[i].width, ch[i].height,
                        pFrameYUV[i]->data[0], 
                        pFrameYUV[i]->data[1], 
                        pFrameYUV[i]->data[2]);

        av_frame_free(&frame);
        has_frame = true;
      }
    }
    renderer->BeginFrame();
    //开始渲染
    renderer->RenderScene();
    //推流
    int w = outStream.width;
    int h = outStream.height;
    if(outStream.push_enabled && has_frame && win_w >= w && win_h >= h)
    {
      int read_w = std::min(w, win_w);
      int read_h = std::min(h, win_h);
      
      if (read_w > 0 && read_h > 0) {
        float scale_w = static_cast<float>(read_w) / static_cast<float>(w);
        float scale_h = static_cast<float>(read_h) / static_cast<float>(h);
        
        if (scale_w >= 0.9f && scale_h >= 0.9f) {
          if (av_frame_make_writable(outStream.frame) >= 0) {
            // 读取像素
            if (renderer->ReadPixels(read_w, read_h, outStream.rgb_buffer.data())) {
              // 翻转 Y 轴
              std::fill(outStream.rgb_flipped.begin(), outStream.rgb_flipped.end(), 0);
              int full_stride = w * 3;
              int read_stride = read_w * 3;
              for (int y = 0; y < read_h; ++y) {
                std::memcpy(&outStream.rgb_flipped[y * full_stride],
                            &outStream.rgb_buffer[(read_h - 1 - y) * read_stride],
                            read_stride);
              }
              
              // 转换并编码
              uint8_t* src_data[1] = { outStream.rgb_flipped.data() };
              int src_linesize[1] = { full_stride };
              sws_scale(outStream.sws_out, src_data, src_linesize, 0, h,
                        outStream.frame->data, outStream.frame->linesize);
              
              outStream.frame->pts = outStream.next_pts++;
              outStream.encode_and_write();
            }
          }
        }
      }

    }
    
    renderer->EndFrame();

  }

  is_close = true;
}

void MediaPlayer::processInput(GLFWwindow *window)
{
  if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
    glfwSetWindowShouldClose(window, true);
}

void MediaPlayer::readData(int idx) {
  Channel& c = ch[idx];
  AVPacket pkt;  // 注意：不要提前 alloc，用栈上临时变量

  while (!is_close) {
    int ret = av_read_frame(c.fmt_ctx, &pkt);
    if (ret < 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
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
      cond_frame[idx].wait_for(lk, std::chrono::milliseconds(30), [&]{
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

  renderer = new og::OpenglRender();
  int max_w = 0, max_h = 0;
  for(auto &c : ch){
    max_w = std::max(max_w, c.width);
    max_h = std::max(max_h, c.height);
  }
  if(!renderer->Init(max_w * 2, max_h * 1.5, "播放器")){
    std::cerr << "渲染器初始化失败" << std::endl;
    delete renderer;
    is_close = true;
    for(auto& t : th) if (t.joinable()) t.join();
    return;
  }

  showFrame();

  if(renderer){
    delete renderer;
  }

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

