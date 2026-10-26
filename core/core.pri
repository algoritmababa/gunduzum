# Analysis core for the optional console tools (cli/, tests/): OpenCV setup + core sources.
# The GUI project (TurbulenceAnalysis.pro) is self-contained and does not use this file.
#
# OpenCV discovery (OpenCV 3.x is the target; 4.x also builds), in this order:
#   1. Explicit paths:  qmake OPENCV_INCLUDE_DIR=... OPENCV_LIB_DIR=... [OPENCV_LIB_SUFFIX=...]
#      (OPENCV_LIB_SUFFIX is e.g. "341" / "341d" on Windows builds: opencv_core341.lib)
#   2. pkg-config "opencv" (3.x) or "opencv4" (4.x).
#   3. Standard install locations (/usr, /usr/local, /opt/opencv*).

CONFIG += c++11
QMAKE_CXXFLAGS_WARN_ON += -Wall

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
        # fall back to well-known install locations
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

INCLUDEPATH += $$PWD

HEADERS += \
    $$PWD/Types.h \
    $$PWD/Statistics.h \
    $$PWD/FrameSource.h \
    $$PWD/Preprocessor.h \
    $$PWD/Registration.h \
    $$PWD/ReferenceFrame.h \
    $$PWD/Algorithm.h \
    $$PWD/AlgorithmRegistry.h \
    $$PWD/AnalysisEngine.h \
    $$PWD/ResultExporter.h \
    $$PWD/algorithms/Algorithms.h

SOURCES += \
    $$PWD/Types.cpp \
    $$PWD/Statistics.cpp \
    $$PWD/FrameSource.cpp \
    $$PWD/Preprocessor.cpp \
    $$PWD/Registration.cpp \
    $$PWD/ReferenceFrame.cpp \
    $$PWD/AlgorithmRegistry.cpp \
    $$PWD/AnalysisEngine.cpp \
    $$PWD/ResultExporter.cpp \
    $$PWD/algorithms/IntensityAlgorithms.cpp \
    $$PWD/algorithms/StructureAlgorithms.cpp \
    $$PWD/algorithms/MotionAlgorithms.cpp
