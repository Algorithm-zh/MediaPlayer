cmake_minimum_required(VERSION 3.10)
project(player)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)

set(FFMPEG_LIBRARY_DIR "/usr/local/lib")
set(FFMPEG_INCLUDE_DIR "/usr/local/include")
# 定义着色器目录
set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
# 将shaders文件夹复制到构建目录
file(COPY ${SHADER_DIR} DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

add_library(GLAD src/glad.c)
file(GLOB SRC ${PROJECT_SOURCE_DIR}/*.cc ${PROJECT_SOURCE_DIR}/src/*.cc )
add_executable(player ${SRC})

# For headers in /usr/local/include
include_directories(${FFMPEG_INCLUDE_DIR} include glsl)

target_link_libraries(player
                      GLAD     
                      avcodec
                      avformat
                      swscale
                      avutil
                      swresample
                      x264
                      glfw
                      OpenGL
                      pthread)
