cmake_minimum_required(VERSION 3.16)
project(holovisualize-preview CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies ──────────────────────────────────────────────────────────────
# vcpkg install glfw3 opengl
find_package(glfw3   REQUIRED)
find_package(OpenGL  REQUIRED)

# ikcp — same as server, pulled via FetchContent
include(FetchContent)
FetchContent_Declare(kcp
    GIT_REPOSITORY https://github.com/skywind3000/kcp.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(kcp)

add_library(ikcp STATIC ${kcp_SOURCE_DIR}/ikcp.c)
target_include_directories(ikcp PUBLIC ${kcp_SOURCE_DIR})
target_compile_definitions(ikcp PRIVATE _CRT_SECURE_NO_WARNINGS)

# ── Preview executable ────────────────────────────────────────────────────────
add_executable(preview src/main.cpp)

target_include_directories(preview PRIVATE
    ../server/include   # MeshFrame.h
)

target_link_libraries(preview PRIVATE
    ikcp
    glfw
    OpenGL::GL
)

if(WIN32)
    target_link_libraries(preview PRIVATE ws2_32 opengl32)
endif()
