cmake_minimum_required(VERSION 3.30)

project(seahugh)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(RE2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(RE2_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(third/abseil-cpp EXCLUDE_FROM_ALL)
add_subdirectory(third/re2 EXCLUDE_FROM_ALL)
add_subdirectory(third/fmt EXCLUDE_FROM_ALL)

set(PYBIND11_FINDPYTHON NEW)

add_subdirectory(third/pybind11 EXCLUDE_FROM_ALL)

include(FetchContent)


FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG        v1.16.0
)
FetchContent_MakeAvailable(spdlog)

FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)
FetchContent_MakeAvailable(json)


file(GLOB EVENT_SOURCE "src/events/*.cpp")

add_library(events ${EVENT_SOURCE} "src/registry.cpp")
target_link_libraries(events PRIVATE nlohmann_json::nlohmann_json)
add_executable(seahugh src/reader.cpp src/watch.cpp src/process.cpp src/main.cpp)
target_include_directories(seahugh PRIVATE src/)
target_link_libraries(seahugh PRIVATE events nlohmann_json::nlohmann_json re2::re2 spdlog::spdlog fmt::fmt)


pybind11_add_module(_core src/python.cpp src/reader.cpp src/watch.cpp src/process.cpp)
target_link_libraries(_core PRIVATE events nlohmann_json::nlohmann_json re2::re2 spdlog::spdlog fmt::fmt)
