cmake_minimum_required(VERSION 3.16)
project(Claude_Usage)

set(CMAKE_CXX_STANDARD 20)

add_executable(Claude_Usage WIN32 main.cpp)
target_link_libraries(Claude_Usage PRIVATE user32 gdi32)
