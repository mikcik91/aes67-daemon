cmake_minimum_required(VERSION 3.16)
project(aes67_daemon VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Wymuszenie flagi -pthread
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Zależności
find_package(Boost REQUIRED COMPONENTS system)
find_package(nlohmann_json 3.2.0 REQUIRED)

# Executable
add_executable(aes67_daemon src/main.cpp)

# Target linking & include directories
target_link_libraries(aes67_daemon 
    PRIVATE 
        Boost::system
        nlohmann_json::nlohmann_json
        Threads::Threads
)

target_compile_options(aes67_daemon PRIVATE -Wall -Wextra -Wpedantic)