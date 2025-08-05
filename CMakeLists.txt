cmake_minimum_required(VERSION 3.10)
project(FuckYCursor_CPP)

# 设置C++标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置编译选项
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /MT")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/SUBSYSTEM:CONSOLE")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -s -static -static-libgcc -static-libstdc++")
endif()

# 添加可执行文件
add_executable(FuckYCursor_cpp main.cpp)

# 链接Windows库
if(WIN32)
    target_link_libraries(FuckYCursor_cpp psapi)
endif()

# 设置默认构建类型为Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()