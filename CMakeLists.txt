cmake_minimum_required(VERSION 3.16)
project(QuranVideoMakerFFmpeg CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
  cxxopts
  GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
  GIT_TAG        v3.2.0
)
FetchContent_MakeAvailable(cxxopts)

FetchContent_Declare(
  cpr
  GIT_REPOSITORY https://github.com/libcpr/cpr.git
  GIT_TAG        1.10.5
)
FetchContent_MakeAvailable(cpr)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# Find required system libraries
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# Find FFmpeg libraries using PkgConfig
pkg_check_modules(AVFORMAT libavformat REQUIRED)
pkg_check_modules(AVCODEC libavcodec REQUIRED)
pkg_check_modules(AVFILTER libavfilter REQUIRED)
pkg_check_modules(AVUTIL libavutil REQUIRED)
pkg_check_modules(SWSCALE libswscale REQUIRED)
pkg_check_modules(FREETYPE freetype2 REQUIRED)
pkg_check_modules(HARFBUZZ harfbuzz REQUIRED)


add_executable(quran-video-maker
    src/main.cpp
    src/api.cpp src/api.h
    src/video_generator.cpp src/video_generator.h
    src/timing_parser.cpp src/timing_parser.h
    src/types.h
)

target_include_directories(quran-video-maker PRIVATE
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVCODEC_INCLUDE_DIRS}
    ${AVFILTER_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
    ${SWSCALE_INCLUDE_DIRS}
    ${FREETYPE_INCLUDE_DIRS}
    ${HARFBUZZ_INCLUDE_DIRS}
)

target_link_libraries(quran-video-maker PRIVATE
    ${AVFORMAT_LIBRARIES}
    ${AVCODEC_LIBRARIES}
    ${AVFILTER_LIBRARIES}
    ${AVUTIL_LIBRARIES}
    ${SWSCALE_LIBRARIES}
    ${FREETYPE_LIBRARIES}
    ${HARFBUZZ_LIBRARIES}
    cpr::cpr
    nlohmann_json::nlohmann_json
    Threads::Threads
    cxxopts::cxxopts
)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 8.0)
    target_link_libraries(quran-video-maker PRIVATE stdc++fs)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 7.0)
    target_link_libraries(quran-video-maker PRIVATE stdc++fs)
endif()