# Turbulence measurement tool - GUI application (minimal backbone version).
#
# Single, self-contained qmake project: open this file in Qt Creator and press Run.
# All core and GUI sources are compiled directly into the application.
#
# OpenCV discovery (OpenCV 3.x is the target; 4.x also builds), in this order:
#   1. Explicit paths:  qmake OPENCV_INCLUDE_DIR=... OPENCV_LIB_DIR=... [OPENCV_LIB_SUFFIX=...]
#      (Qt Creator: Projects > Build > qmake > Additional arguments.
#       OPENCV_LIB_SUFFIX is e.g. "341" / "341d" on Windows: opencv_core341.lib)
#   2. pkg-config "opencv" (3.x) or "opencv4" (4.x).
#   3. Standard install locations (/usr/local, /usr, /opt/opencv*).

TEMPLATE = app
TARGET = TurbulenceAnalysis
QT += core gui widgets
CONFIG += c++11 thread
QMAKE_CXXFLAGS_WARN_ON += -Wall

# ---------------------------------------------------------------------------------
# OpenCV
# ---------------------------------------------------------------------------------
OPENCV_MODULES = opencv_core opencv_imgproc opencv_imgcodecs opencv_videoio opencv_video

isEmpty(OPENCV_INCLUDE_DIR) {
    CONFIG += link_pkgconfig
    packagesExist(opencv) {
        PKGCONFIG += opencv
        OPENCV_FOUND_BY = pkg-config opencv
    } else: packagesExist(opencv4) {
        PKGCONFIG += opencv4
        OPENCV_FOUND_BY = pkg-config opencv4
    } else {
        CONFIG -= link_pkgconfig
        for(prefix, $$list(/usr/local /usr /opt/opencv /opt/opencv3 /opt/opencv4)) {
            isEmpty(OPENCV_INCLUDE_DIR) {
                exists($$prefix/include/opencv4/opencv2/core.hpp): OPENCV_INCLUDE_DIR = $$prefix/include/opencv4
                else: exists($$prefix/include/opencv2/core.hpp): OPENCV_INCLUDE_DIR = $$prefix/include
                !isEmpty(OPENCV_INCLUDE_DIR) {
                    for(libdir, $$list($$prefix/lib $$prefix/lib64 $$prefix/lib/x86_64-linux-gnu $$prefix/lib/aarch64-linux-gnu)) {
                        isEmpty(OPENCV_LIB_DIR) {
                            exists($$libdir/libopencv_core.so*)|exists($$libdir/libopencv_core.a)|exists($$libdir/libopencv_core.dylib): OPENCV_LIB_DIR = $$libdir
                        }
                    }
                }
            }
        }
        isEmpty(OPENCV_INCLUDE_DIR) {
            error("OpenCV not found. Install libopencv-dev and pkg-config, or pass OPENCV_INCLUDE_DIR=... OPENCV_LIB_DIR=... to qmake (Qt Creator: Projects > Build > qmake > Additional arguments).")
        }
        OPENCV_FOUND_BY = search in standard locations
    }
} else {
    OPENCV_FOUND_BY = OPENCV_INCLUDE_DIR argument
}

!isEmpty(OPENCV_INCLUDE_DIR) {
    INCLUDEPATH += $$OPENCV_INCLUDE_DIR
    !isEmpty(OPENCV_LIB_DIR) {
        LIBS += -L$$OPENCV_LIB_DIR
        unix: QMAKE_RPATHDIR += $$OPENCV_LIB_DIR
    }
    for(m, OPENCV_MODULES): LIBS += -l$${m}$${OPENCV_LIB_SUFFIX}
}

!build_pass: message("OpenCV: $$OPENCV_FOUND_BY  include=$$OPENCV_INCLUDE_DIR  lib=$$OPENCV_LIB_DIR")

# ---------------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------------
INCLUDEPATH += $$PWD/core $$PWD/app

# Analysis core (pure C++11 + OpenCV, no Qt)
HEADERS += \
    core/FrameSource.h \
    core/Stats.h \
    core/Metrics.h \
    core/Measurement.h \
    core/ResultTable.h

SOURCES += \
    core/FrameSource.cpp \
    core/Stats.cpp \
    core/Metrics.cpp \
    core/Measurement.cpp \
    core/ResultTable.cpp

# Qt Widgets GUI
HEADERS += \
    app/MainWindow.h \
    app/MeasureWorker.h \
    app/RoiView.h

SOURCES += \
    app/main.cpp \
    app/MainWindow.cpp \
    app/MeasureWorker.cpp \
    app/RoiView.cpp
