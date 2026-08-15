cmake_minimum_required(VERSION 3.16)
project(holovisualize-server C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies ──────────────────────────────────────────────────────────────
# vcpkg install ixwebsocket
find_package(ixwebsocket REQUIRED)

# ikcp — vendored in third_party/kcp (avoids FetchContent + kcp's CMake 4.0 req)
add_library(ikcp STATIC ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/kcp/ikcp.c)
target_include_directories(ikcp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/kcp)
target_compile_definitions(ikcp PRIVATE _CRT_SECURE_NO_WARNINGS)

# ── Server executable ─────────────────────────────────────────────────────────
add_executable(server
    src/main.cpp
    src/SessionModelView.cpp
    src/SessionViewController.cpp
    src/Hub.cpp
    src/VoxelGrid.cpp
    src/MarchingCubes.cpp
)

target_include_directories(server PRIVATE include)

target_link_libraries(server PRIVATE
    ixwebsocket::ixwebsocket
    ikcp
)

if(WIN32)
    target_link_libraries(server PRIVATE ws2_32)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(server PRIVATE -O3)
endif()
