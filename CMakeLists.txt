cmake_minimum_required(VERSION 3.25)
project(CppDetector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs video)
find_package(httplib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

find_package(Threads REQUIRED)
add_library(inspection_core
  src/inspect/production/core/settings.cpp
  src/inspect/production/core/reference.cpp
  src/inspect/production/core/inspector.cpp
  src/inspect/production/core/learning_matcher.cpp
  src/inspect/production/core/performance_log.cpp)
target_include_directories(inspection_core PUBLIC src/inspect/production ${OpenCV_INCLUDE_DIRS})
target_link_libraries(inspection_core PUBLIC opencv_core opencv_imgproc opencv_video nlohmann_json::nlohmann_json Threads::Threads)
if(MSVC)
  target_compile_options(inspection_core PRIVATE /utf-8 /W3)
endif()

add_executable(cppdetector
  src/main.cpp
  src/inspect/inspector.cpp
  src/inspect/auto_roi.cpp
  src/inspect/seam.cpp
  src/app/app.cpp
  src/app/decision_log.cpp
  src/app/bench.cpp
  src/web/routes.cpp)
target_compile_definitions(cppdetector PRIVATE CD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
if(MSVC)
  target_compile_options(cppdetector PRIVATE /utf-8 /W3)
endif()
target_link_libraries(cppdetector PRIVATE
  inspection_core opencv_core opencv_imgproc opencv_imgcodecs
  httplib::httplib nlohmann_json::nlohmann_json)

include(CTest)
if(BUILD_TESTING)
  foreach(name shift_accuracy alignment_preprocessing shift_tolerance)
    add_executable(${name}_test tests/production/${name}_test.cpp)
    target_link_libraries(${name}_test PRIVATE inspection_core)
    add_test(NAME ${name}_test COMMAND ${name}_test)
  endforeach()
  add_executable(inspection_adapter_test tests/inspection_adapter_test.cpp src/inspect/inspector.cpp)
  target_link_libraries(inspection_adapter_test PRIVATE inspection_core opencv_imgcodecs)
  target_include_directories(inspection_adapter_test PRIVATE src)
  add_test(NAME inspection_adapter_test COMMAND inspection_adapter_test)
  add_executable(app_inspection_test tests/app_inspection_test.cpp
    src/app/app.cpp src/app/decision_log.cpp src/inspect/inspector.cpp
    src/inspect/auto_roi.cpp src/inspect/seam.cpp)
  target_include_directories(app_inspection_test PRIVATE src)
  target_link_libraries(app_inspection_test PRIVATE inspection_core opencv_imgcodecs)
  if(MSVC)
    target_compile_options(app_inspection_test PRIVATE /utf-8 /W3)
  endif()
  add_test(NAME app_inspection_test COMMAND app_inspection_test)
endif()
