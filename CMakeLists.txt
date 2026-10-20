cmake_minimum_required(VERSION 3.25)
project(CppDetector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
find_package(httplib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(cppdetector src/main.cpp)
target_link_libraries(cppdetector PRIVATE
  opencv_core opencv_imgproc opencv_imgcodecs
  httplib::httplib nlohmann_json::nlohmann_json)
