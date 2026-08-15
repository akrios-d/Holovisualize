cmake_minimum_required(VERSION 3.16)
project(capture CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Dependencies (install via vcpkg) ---
# vcpkg install libfreenect2 ixwebsocket opencv4[contrib]
find_package(freenect2  REQUIRED)
find_package(ixwebsocket REQUIRED)
find_package(OpenCV     REQUIRED COMPONENTS core imgproc calib3d objdetect)

add_executable(capture
    src/main.cpp
    src/sensors/KinectV2Sensor.cpp
    src/Filter.cpp
    src/Segmentation.cpp
    src/PointCloud.cpp
    src/Pipeline.cpp
    src/Sender.cpp
    src/Calibration.cpp
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
)

if(WIN32)
    # BCryptGenRandom is needed by mbedtls (pulled in by ixwebsocket)
    target_link_libraries(capture PRIVATE bcrypt ws2_32)
endif()
