#pragma once
#include <cstdint>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <vector>
#include "shader.h"

namespace og{
  class OpenglRender{
  private:

    static void framebuffer_size_callback(GLFWwindow* window, int width, int height);
    static void processInput(GLFWwindow* window);

  public:
    OpenglRender() = default;
    ~OpenglRender();
    bool Init(int width, int height, const char* title);
    bool ShouldClose() const;
    void PollEvents() const;
    void UpLoadTexture(int channel, int width, int height, uint8_t* y_data, uint8_t* u_data, uint8_t* v_data);
    void BeginFrame() const;
    void EndFrame() const;
    void RenderScene();
    void GetFramebufferSize(int &width, int &height);
    bool ReadPixels(int width, int height, uint8_t *data);

  private:
    GLFWwindow *window{NULL};
    Shader shader{};
    GLuint vao{}, vbo{};
    GLuint textures[3][3]{};
    float channel_width[3]{};
    float channel_height[3]{};

    glm::mat4 view;
    glm::mat4 projection;
  };
}
