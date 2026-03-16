cmake_minimum_required(VERSION 3.5)
project(SonarTrackerPro LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==========================================
# 【核心提速区】：强制开启 Release 模式和硬件加速
# ==========================================
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

# 开启 O3 极限优化 和 本地 CPU AVX/SSE 指令集加速
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -DNDEBUG")

# 寻找并链接 OpenMP (让 Eigen 自动多线程并行计算矩阵)
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
endif()
# ==========================================

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 COMPONENTS Core Gui Widgets PrintSupport REQUIRED)
find_package(Eigen3 3.3 REQUIRED NO_MODULE)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/qcustomplot
    ${EIGEN3_INCLUDE_DIRS}
)

set(SOURCES
    src/main.cpp
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/core/DspWorker.cpp
    src/core/DspWorker.h
    src/core/DataTypes.h
    src/core/RawReader.cpp
    src/core/RawReader.h
    src/core/CBFProcessor.cpp
    src/core/CBFProcessor.h
    src/core/TrackManager.cpp
    src/core/TrackManager.h
    src/core/Deconvolution.cpp
    src/core/Deconvolution.h
    src/core/detect_line_spectrum_from_lofar_change.h
    src/core/detect_line_spectrum_from_lofar_change.cpp
    src/core/fir2.h
    src/core/fir2.cpp
    3rdparty/qcustomplot/qcustomplot.cpp
    3rdparty/qcustomplot/qcustomplot.h
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt5::Widgets
    Qt5::PrintSupport
    Qt5::Core
    Eigen3::Eigen
    fftw3
)

target_compile_definitions(${PROJECT_NAME} PRIVATE QCUSTOMPLOT_USE_LIBRARY)
