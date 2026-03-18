cmake_minimum_required(VERSION 3.5)
project(SonarTracker715 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==========================================
# 【核心提速区】：强制开启 Release 模式和硬件加速
# ==========================================
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

# 开启 O3 极限优化 和 本地 CPU AVX/SSE 指令集加速
# -march=native 会让编译器自动榨干你当前 CPU 的全部潜能（含 AVX2/FMA 等）
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -DNDEBUG")

# 寻找 OpenMP (标记为 REQUIRED，找不到会直接在 CMake 阶段报错，防止你白白编译)
find_package(OpenMP REQUIRED)
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
    src/core/SelfValidator.h
    src/core/SelfValidator.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

# ==========================================
# 【关键修复】：在此处显式链接 OpenMP
# ==========================================
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt5::Widgets
    Qt5::PrintSupport
    Qt5::Core
    Eigen3::Eigen
    fftw3
    OpenMP::OpenMP_CXX   # <--- 现代 CMake 开启 OpenMP 的标准姿势
)

target_compile_definitions(${PROJECT_NAME} PRIVATE QCUSTOMPLOT_USE_LIBRARY)
