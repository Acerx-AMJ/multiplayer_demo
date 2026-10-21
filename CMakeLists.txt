cmake_minimum_required(VERSION 3.10)
project(multiplayer_demo)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wall)

include_directories(${PROJECT_SOURCE_DIR}/src)
add_executable(server src/server.cpp)
add_executable(client src/client.cpp)

find_package(cx REQUIRED)
find_package(SFML 2.6 COMPONENTS window system graphics network REQUIRED)
target_link_libraries(server PRIVATE sfml-window sfml-system sfml-graphics sfml-network cx)
target_link_libraries(client PRIVATE sfml-window sfml-system sfml-graphics sfml-network cx)
