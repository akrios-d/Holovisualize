cmake_minimum_required(VERSION 3.16)
project(holovisualize-preview C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies ──────────────────────────────────────────────────────────────
# vcpkg install glfw3 glew opengl
find_package(glfw3   REQUIRED)
find_package(GLEW    REQUIRED)
find_package(OpenGL  REQUIRED)

# ikcp — vendored in third_party/kcp (avoids FetchContent + kcp's CMake 4.0 req)
add_library(ikcp STATIC ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/kcp/ikcp.c)
target_include_directories(ikcp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/kcp)
target_compile_definitions(ikcp PRIVATE _CRT_SECURE_NO_WARNINGS)

# ── Preview executable ────────────────────────────────────────────────────────
add_executable(preview src/main.cpp)

target_include_directories(preview PRIVATE
    ../server/include   # MeshFrame.h
)

target_link_libraries(preview PRIVATE
    ikcp
    glfw
    GLEW::GLEW
    OpenGL::GL
)

if(WIN32)
    target_link_libraries(preview PRIVATE ws2_32 opengl32)
endif()
