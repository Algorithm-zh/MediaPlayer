cmake_minimum_required(VERSION 3.10)
project(player)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)

set(FFMPEG_LIBRARY_DIR "/usr/local/lib")
set(FFMPEG_INCLUDE_DIR "/usr/local/include")
add_library(GLAD src/glad.c)
file(GLOB SRC ${PROJECT_SOURCE_DIR}/*.cc ${PROJECT_SOURCE_DIR}/src/*.cc)
add_executable(player ${SRC})

# For headers in /usr/local/include
include_directories(${FFMPEG_INCLUDE_DIR} include)

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
                      portaudio
                      pthread)
