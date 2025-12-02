#include "OpenglRender.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <iostream>
using namespace og; 

void OpenglRender::framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
  glViewport(0, 0, width, height);
}

bool OpenglRender::Init(int width, int height, const char* title)  {

  glfwInitHint(GLFW_WAYLAND_LIBDECOR, GLFW_FALSE);
  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW" << std::endl;
    return false;
  }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  // 窗口宽高按最大分辨率，保持16:9左右比例
  window = glfwCreateWindow(width, height, title, nullptr, nullptr);
  if (!window) {
    std::cerr << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    return false;
  }

  glfwMakeContextCurrent(window);
  glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

  if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)){
    std::cerr << "Failed to initialize GLAD" << std::endl;
    return false;
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
    glGenTextures(3, textures[i]);
  }

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  shader.use();
  shader.setInt("texY", 0);
  shader.setInt("texU", 1);
  shader.setInt("texV", 2);

  return true;
}

bool OpenglRender::ShouldClose() const {
  return glfwWindowShouldClose(window);
}

void OpenglRender::PollEvents() const {
  processInput(window);
  glfwPollEvents();
}

void OpenglRender::processInput(GLFWwindow* window)  {
  if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
    glfwSetWindowShouldClose(window, true);
}

void OpenglRender::UpLoadTexture(int channel, int width, int height, uint8_t* y_data, uint8_t* u_data, uint8_t* v_data) {

  if(channel < 0 || channel >= 3)return;

  //也就是第一次上传
  if(channel_width[channel] == 0){
    channel_width[channel] = width; 
    channel_height[channel] = height;

    //初始化纹理
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textures[channel][0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, y_data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, textures[channel][1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width / 2, height / 2, 0, GL_RED, GL_UNSIGNED_BYTE, u_data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, textures[channel][2]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width / 2, height / 2, 0, GL_RED, GL_UNSIGNED_BYTE, v_data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  //上传纹理
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, textures[channel][0]);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RED, GL_UNSIGNED_BYTE, y_data);

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, textures[channel][1]);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width/2, height/2, GL_RED, GL_UNSIGNED_BYTE, u_data);

  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, textures[channel][2]);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width/2, height/2, GL_RED, GL_UNSIGNED_BYTE, v_data);
}

void OpenglRender::BeginFrame() const {
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenglRender::RenderScene()  {

  int win_w, win_h;
  glfwGetFramebufferSize(window, &win_w, &win_h);
  if(win_w <= 0 || win_h <= 0) return;

  //统一屏幕尺寸
  float max_w, max_h;
  for(int i = 0; i < 3; i ++){
    if(channel_width[i] > 0){
      max_w = std::max(max_w, channel_width[i]);
      max_h = std::max(max_h, channel_height[i]);
    }
  }
  if(max_w == 0 || max_h == 0)return;

  float aspect = max_h / max_w;
  float screenWidth = 2.5f;
  float screenHeight = screenWidth * aspect;

  //计算视图和投影矩阵
  view = glm::lookAt(
      glm::vec3(0.0f, 0.0f, 0.5f),    // 相机位置（稍微后退一点）
      glm::vec3(0.0f, 0.0f, 0.0f),    // 看向原点
      glm::vec3(0.0f, 1.0f, 0.0f)
      );
  projection = glm::perspective(glm::radians(65.0f), (float)win_w / win_h, 0.1f, 100.0f);

  shader.setMat4("view", view);
  shader.setMat4("projection", projection);

  // --- 关键参数 ---
  float foldAngle = 25.0f;      // 两侧屏幕弯折角度
  float screenDistance = 3.0f;  // 屏幕与相机的距离
  float overlap = 0.27f;        // 屏幕间物理重叠的宽度

  // 根据物理重叠宽度，动态计算着色器所需的混合区域比例
  float blendWidth = overlap / screenWidth; 

  shader.setFloat("blendWidth", blendWidth);

  glDepthMask(GL_FALSE);

  int draw_order[] = {1, 0, 2}; // 1 = Center, 0 = Left, 2 = Right

  for (int i : draw_order) {
    // 绑定对应通道的纹理
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, textures[i][0]);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, textures[i][1]);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, textures[i][2]);

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
      model = glm::scale(model, glm::vec3(cosFactor + 0.25f, cosFactor, 1.0f));
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
}
 
void OpenglRender::EndFrame() const {
  glfwSwapBuffers(window);
}
 
void OpenglRender::GetFramebufferSize(int &width, int &height)  {
  glfwGetFramebufferSize(window, &width, &height); 
}

bool OpenglRender::ReadPixels(int width, int height, uint8_t *data)  {
  glReadBuffer(GL_BACK);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, data);
  return true;
}
 
OpenglRender::~OpenglRender()  {
  glDeleteVertexArrays(1, &vao); 
  glDeleteBuffers(1, &vbo); 
  for(int i = 0; i < 3; i ++){
    glDeleteTextures(3, textures[i]);
  }
  if(window){
    glfwDestroyWindow(window);
  }
  glfwTerminate();
}
 

