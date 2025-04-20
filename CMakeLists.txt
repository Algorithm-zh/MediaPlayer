project(player)

cmake_minimum_required(VERSION 3.0)
set(CMAKE_CXX_STANDARD 11)
file(GLOB SRC ${PROJECT_SOURCE_DIR}/*.cc)
add_executable(player ${SRC})

target_link_libraries(player avcodec avformat swscale avutil)


