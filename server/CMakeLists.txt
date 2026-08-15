cmake_minimum_required(VERSION 3.16)
project(holovisualize-server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies ──────────────────────────────────────────────────────────────
# vcpkg install ixwebsocket
find_package(ixwebsocket REQUIRED)

# ikcp — pulled from GitHub, compiled directly (bypasses kcp's CMakeLists.txt
# which recently bumped cmake_minimum_required to 4.0).
include(FetchContent)
FetchContent_Declare(kcp
    GIT_REPOSITORY https://github.com/skywind3000/kcp.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE)
# Populate source only — do NOT call FetchContent_MakeAvailable which would
# process kcp's own CMakeLists.txt and fail on the version requirement.
FetchContent_Populate(kcp)

add_library(ikcp STATIC ${kcp_SOURCE_DIR}/ikcp.c)
target_include_directories(ikcp PUBLIC ${kcp_SOURCE_DIR})
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
