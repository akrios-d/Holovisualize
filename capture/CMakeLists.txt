cmake_minimum_required(VERSION 3.16)
project(capture CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Feature flags ─────────────────────────────────────────────────────────────
# Set OFF to skip OpenCV (~40 min build). Background subtraction and ArUco
# calibration will be unavailable; everything else builds normally.
option(HOLOVISUALIZE_OPENCV "Enable OpenCV (background subtraction + ArUco calibration)" ON)

# ── Dependencies ──────────────────────────────────────────────────────────────
# Kinect v2 via libfreenect2. The copy fetched by vcpkg's port (v0.2.1)
# detects GLFW3 via pkg-config, which silently fails on Windows, so it always
# builds without OpenGL support (CPU-only depth decoding, ~10-12 fps instead
# of ~60-100). If a local OpenGL-enabled build is present (see
# C:/libfreenect2/build — built from source, GLFW3 found via find_package
# instead of pkg-config), use that instead. Otherwise fall back to vcpkg's
# copy with CpuPacketPipeline, so machines/CI without that local build (and
# without OpenGL depth decode) still compile and run.
set(_freenect2_local_root "C:/libfreenect2")
if(EXISTS "${_freenect2_local_root}/build/lib/Release/freenect2.lib")
    set(freenect2_INCLUDE_DIRS "${_freenect2_local_root}/include" "${_freenect2_local_root}/build")
    find_library(freenect2_LIBRARIES freenect2
        PATHS "${_freenect2_local_root}/build/lib/Release" NO_DEFAULT_PATH REQUIRED)
    set(_freenect2_dll_dir "${_freenect2_local_root}/build/bin/Release")
    set(HOLOVISUALIZE_FREENECT2_OPENGL ON)
    message(STATUS "Using local OpenGL-enabled libfreenect2 from ${_freenect2_local_root}")
else()
    find_package(freenect2 REQUIRED)
    set(HOLOVISUALIZE_FREENECT2_OPENGL OFF)
    message(STATUS "Using vcpkg's libfreenect2 (CPU depth decode — no local OpenGL build found)")
endif()

find_package(ixwebsocket REQUIRED)
find_package(glfw3       REQUIRED)
find_package(GLEW        REQUIRED)
find_package(OpenGL      REQUIRED)
find_package(imgui       REQUIRED)

if(HOLOVISUALIZE_OPENCV)
    find_package(OpenCV REQUIRED COMPONENTS core imgproc highgui calib3d objdetect)
endif()

# ── Sources ───────────────────────────────────────────────────────────────────
set(CAPTURE_SOURCES
    src/main.cpp
    src/CaptureModelView.cpp
    src/CaptureViewController.cpp
    src/sensors/KinectV2Sensor.cpp
    src/Filter.cpp
    src/Segmentation.cpp
    src/PointCloud.cpp
    src/Pipeline.cpp
    src/Sender.cpp
    src/GestureClient.cpp
    src/filters/BackgroundSubtractorFilter.cpp
)

if(HOLOVISUALIZE_OPENCV)
    list(APPEND CAPTURE_SOURCES
        src/Calibration.cpp
    )
endif()

add_executable(capture ${CAPTURE_SOURCES})

target_include_directories(capture PRIVATE include)
target_compile_definitions(capture PRIVATE
    $<$<BOOL:${HOLOVISUALIZE_OPENCV}>:HOLOVISUALIZE_OPENCV>
    $<$<BOOL:${HOLOVISUALIZE_FREENECT2_OPENGL}>:HOLOVISUALIZE_FREENECT2_OPENGL>
)

target_include_directories(capture PRIVATE ${freenect2_INCLUDE_DIRS})
target_link_libraries(capture PRIVATE ${freenect2_LIBRARIES})

target_link_libraries(capture PRIVATE
    ixwebsocket::ixwebsocket
    glfw
    GLEW::GLEW
    OpenGL::GL
    imgui::imgui
)

if(HOLOVISUALIZE_OPENCV)
    target_include_directories(capture PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(capture PRIVATE ${OpenCV_LIBS})
endif()

if(WIN32)
    target_link_libraries(capture PRIVATE bcrypt ws2_32)

    # Copy vcpkg DLLs to output directory so capture.exe runs without PATH changes.
    set(_vcpkg_bin "${CMAKE_PREFIX_PATH}/bin")
    if(EXISTS "${_vcpkg_bin}")
        file(GLOB _vcpkg_dlls "${_vcpkg_bin}/*.dll")
        foreach(_dll ${_vcpkg_dlls})
            add_custom_command(TARGET capture POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_dll}" "$<TARGET_FILE_DIR:capture>"
                COMMENT "Copying ${_dll}")
        endforeach()
    endif()

    # Runtime DLLs for the local OpenGL-enabled libfreenect2 build (freenect2,
    # libusb-1.0, turbojpeg) — not provided by vcpkg since that copy lacks
    # OpenGL support. See the freenect2 find_library block above.
    if(_freenect2_dll_dir AND EXISTS "${_freenect2_dll_dir}")
        file(GLOB _freenect2_dlls "${_freenect2_dll_dir}/*.dll")
        foreach(_dll ${_freenect2_dlls})
            add_custom_command(TARGET capture POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_dll}" "$<TARGET_FILE_DIR:capture>"
                COMMENT "Copying ${_dll}")
        endforeach()
    endif()
endif()
