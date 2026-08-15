cmake_minimum_required(VERSION 3.16)
project(Holovisualize)

# Sub-projects. The capture sub-project requires libfreenect2 (Kinect SDK /
# libfreenect2 installed).  Server and preview only need ixwebsocket.
# Build everything together:
#
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
#   cmake --build build --config Release
#
# Or build sub-projects individually from their own directories.

add_subdirectory(capture)
add_subdirectory(server)
add_subdirectory(preview)
