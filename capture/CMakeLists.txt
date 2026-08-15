cmake_minimum_required(VERSION 3.16)
project(capture CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies ──────────────────────────────────────────────────────────────
find_package(freenect2  REQUIRED)
find_package(ixwebsocket REQUIRED)
find_package(OpenCV     REQUIRED COMPONENTS core imgproc highgui calib3d objdetect)
find_package(glfw3      REQUIRED)
find_package(GLEW       REQUIRED)
find_package(OpenGL     REQUIRED)
find_package(imgui      REQUIRED)

# ── Sources ───────────────────────────────────────────────────────────────────
add_executable(capture
    src/main.cpp
    src/CaptureApp.cpp
    src/sensors/KinectV2Sensor.cpp
    src/Filter.cpp
    src/Segmentation.cpp
    src/PointCloud.cpp
    src/Pipeline.cpp
    src/Sender.cpp
    src/Calibration.cpp
    src/filters/BodyFilter.cpp
    src/filters/BackgroundSubtractorFilter.cpp
)

target_include_directories(capture PRIVATE
    include
    ${freenect2_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(capture PRIVATE
    ${freenect2_LIBRARIES}
    ixwebsocket::ixwebsocket
    ${OpenCV_LIBS}
    glfw
    GLEW::GLEW
    OpenGL::GL
    imgui::imgui
)

if(WIN32)
    target_link_libraries(capture PRIVATE bcrypt ws2_32)
endif()
