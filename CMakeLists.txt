cmake_minimum_required(VERSION 3.10)
project(player)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()
set(CMAKE_CXX_STANDARD 17)
set(FFMPEG_LIBRARY_DIR "/usr/local/lib")
set(FFMPEG_INCLUDE_DIR "/usr/local/include")

file(GLOB SRC ${PROJECT_SOURCE_DIR}/*.cc)
add_executable(player ${SRC})
#指定ffmpeg库路径和头文件路径
#防止找不到或者系统里有多个ffmpeg
target_link_directories(player PUBLIC ${FFMPEG_LIBRARY_DIR})
include_directories(${FFMPEG_INCLUDE_DIR})

target_link_libraries(player avcodec avformat swscale avutil SDL2 pthread)


