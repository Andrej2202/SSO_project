cmake_minimum_required(VERSION 3.10)
project(diploma_generator)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(Freetype REQUIRED)

include_directories(${OpenCV_INCLUDE_DIRS})
include_directories(${YAML_CPP_INCLUDE_DIR})
include_directories(${FREETYPE_INCLUDE_DIRS})

add_executable(diploma_generator src/main.cpp)

target_link_libraries(diploma_generator
    ${OpenCV_LIBS}
    yaml-cpp
    ${FREETYPE_LIBRARIES}
    stdc++fs
)

target_include_directories(diploma_generator PRIVATE /opt/cpp-httplib)