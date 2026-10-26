cmake_minimum_required(VERSION 3.25)
project(CppDetector LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
find_package(httplib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Threads REQUIRED)

# Native core snapshot 885a79ed plus reference-cache fix bfd9b54a; see src/production/SOURCE.md.
add_library(production_core STATIC
  src/production/core/reference.cpp src/production/core/settings.cpp
  src/production/core/inspector.cpp src/production/core/seam.cpp src/production/core/performance_log.cpp)
target_include_directories(production_core PUBLIC src/production)
target_link_libraries(production_core PUBLIC opencv_core opencv_imgproc opencv_imgcodecs
  nlohmann_json::nlohmann_json Threads::Threads)

add_library(local_inspection STATIC src/inspect/inspector.cpp src/inspect/auto_roi.cpp
  src/app/app.cpp src/app/decision_log.cpp)
target_include_directories(local_inspection PUBLIC src)
target_link_libraries(local_inspection PUBLIC production_core)

add_executable(cppdetector src/main.cpp src/app/bench.cpp src/web/routes.cpp)
target_compile_definitions(cppdetector PRIVATE CD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(cppdetector PRIVATE local_inspection httplib::httplib)
if(MSVC)
  foreach(target production_core local_inspection cppdetector)
    target_compile_options(${target} PRIVATE /utf-8 /W3)
  endforeach()
endif()
include(CTest)
if(BUILD_TESTING)
  add_executable(shift_tolerance_test tests/shift_tolerance_test.cpp)
  target_link_libraries(shift_tolerance_test PRIVATE local_inspection)
  add_test(NAME shift_tolerance_test COMMAND shift_tolerance_test)
  add_executable(production_parity_test tests/production_parity_test.cpp)
  target_link_libraries(production_parity_test PRIVATE local_inspection)
  add_test(NAME production_parity_test COMMAND production_parity_test)
endif()
