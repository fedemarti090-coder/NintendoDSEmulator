cmake_minimum_required(VERSION 3.16)
project(nds_emu LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(nds_emu
    main.cpp
)

if(MSVC)
    target_compile_options(nds_emu PRIVATE /W4)
else()
    target_compile_options(nds_emu PRIVATE -Wall -Wextra -Wpedantic)
endif()