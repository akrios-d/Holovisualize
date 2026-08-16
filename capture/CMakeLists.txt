cmake_minimum_required(VERSION 3.16)
project(capture CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Feature flags ─────────────────────────────────────────────────────────────
# Set OFF to skip OpenCV (~40 min build). Background subtraction and ArUco
# calibration will be unavailable; everything else builds normally.
option(HOLOVISUALIZE_OPENCV "Enable OpenCV (background subtraction + ArUco calibration)" ON)

# ── Dependencies ──────────────────────────────────────────────────────────────
# Use the official Kinect SDK 2.0 instead of libfreenect2 when available/requested.
# Set -DHOLOVISUALIZE_KINECT_SDK=ON (or OFF to use libfreenect2).
option(HOLOVISUALIZE_KINECT_SDK "Use official Kinect for Windows SDK 2.0" OFF)
option(HOLOVISUALIZE_KINECT_V1  "Use Kinect v1 via libfreenect"          OFF)
if(NOT DEFINED HOLOVISUALIZE_KINECT_SDK AND DEFINED ENV{KINECTSDK20_ROOT})
    set(HOLOVISUALIZE_KINECT_SDK ON)
endif()

if(HOLOVISUALIZE_KINECT_V1)
    set(_freenect_root "C:/libfreenect/install")
    find_library(FREENECT_LIB freenect PATHS "${_freenect_root}/lib" NO_DEFAULT_PATH REQUIRED)
    find_path(FREENECT_INCLUDE libfreenect/libfreenect.h PATHS "${_freenect_root}/include" NO_DEFAULT_PATH REQUIRED)
    message(STATUS "Using libfreenect from ${_freenect_root}")
elseif(HOLOVISUALIZE_KINECT_SDK)
    # Resolve SDK root: explicit CMake var > env var > default install path
    if(DEFINED KINECT_SDK_ROOT)
        set(_kinect_root "${KINECT_SDK_ROOT}")
    elseif(DEFINED ENV{KINECTSDK20_ROOT})
        set(_kinect_root "$ENV{KINECTSDK20_ROOT}")
    elseif(EXISTS "C:/Program Files/Microsoft SDKs/Kinect/v2.0_1409")
        set(_kinect_root "C:/Program Files/Microsoft SDKs/Kinect/v2.0_1409")
    else()
        message(FATAL_ERROR "HOLOVISUALIZE_KINECT_SDK=ON but could not find the SDK. "
                            "Pass -DKINECT_SDK_ROOT=<path> or install Kinect for Windows SDK 2.0.")
    endif()
    message(STATUS "Using Kinect SDK 2.0 from ${_kinect_root}")
else()
    find_package(freenect2 REQUIRED)
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
    src/CaptureApp.cpp)

if(HOLOVISUALIZE_KINECT_V1)
    list(APPEND CAPTURE_SOURCES src/sensors/KinectV1Sensor.cpp)
else()
    list(APPEND CAPTURE_SOURCES src/sensors/KinectV2Sensor.cpp)
endif()

list(APPEND CAPTURE_SOURCES
    src/Filter.cpp
    src/Segmentation.cpp
    src/PointCloud.cpp
    src/Pipeline.cpp
    src/Sender.cpp
    src/filters/BodyFilter.cpp
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
    $<$<BOOL:${HOLOVISUALIZE_KINECT_SDK}>:HOLOVISUALIZE_KINECT_SDK>
    $<$<BOOL:${HOLOVISUALIZE_KINECT_V1}>:HOLOVISUALIZE_KINECT_V1>
)

if(HOLOVISUALIZE_KINECT_V1)
    find_library(LIBUSB_LIB NAMES libusb-1.0 usb-1.0 REQUIRED)
    target_include_directories(capture PRIVATE "${FREENECT_INCLUDE}")
    target_link_libraries(capture PRIVATE "${FREENECT_LIB}" "${LIBUSB_LIB}")
elseif(HOLOVISUALIZE_KINECT_SDK)
    target_include_directories(capture PRIVATE "${_kinect_root}/inc")
    target_link_libraries(capture PRIVATE "${_kinect_root}/Lib/x64/Kinect20.lib")
else()
    target_include_directories(capture PRIVATE ${freenect2_INCLUDE_DIRS})
    target_link_libraries(capture PRIVATE ${freenect2_LIBRARIES})
endif()

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
endif()
