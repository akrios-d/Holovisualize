cmake_minimum_required(VERSION 3.16)
project(capture CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Feature flags ─────────────────────────────────────────────────────────────
# Set OFF to skip OpenCV (~40 min build). Background subtraction and ArUco
# calibration will be unavailable; everything else builds normally.
option(HOLOVISUALIZE_OPENCV "Enable OpenCV (background subtraction + ArUco calibration)" ON)

# ── Dependencies ──────────────────────────────────────────────────────────────
find_package(freenect2   REQUIRED)
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
    src/CaptureApp.cpp
    src/sensors/KinectV2Sensor.cpp
    src/Filter.cpp
    src/Segmentation.cpp
    src/PointCloud.cpp
    src/Pipeline.cpp
    src/Sender.cpp
    src/filters/BodyFilter.cpp
)

if(HOLOVISUALIZE_OPENCV)
    list(APPEND CAPTURE_SOURCES
        src/Calibration.cpp
        src/filters/BackgroundSubtractorFilter.cpp
    )
endif()

add_executable(capture ${CAPTURE_SOURCES})

target_include_directories(capture PRIVATE
    include
    ${freenect2_INCLUDE_DIRS}
)

target_compile_definitions(capture PRIVATE
    $<$<BOOL:${HOLOVISUALIZE_OPENCV}>:HOLOVISUALIZE_OPENCV>
)

target_link_libraries(capture PRIVATE
    ${freenect2_LIBRARIES}
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
endif()
