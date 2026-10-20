# Shared build settings for all sub-projects.
#
# OpenCV discovery (OpenCV 3.x is the target; 4.x also builds):
#   1. Explicit paths:  qmake OPENCV_INCLUDE_DIR=... OPENCV_LIB_DIR=... [OPENCV_LIB_SUFFIX=...]
#      (OPENCV_LIB_SUFFIX is e.g. "341" / "341d" on Windows builds: opencv_core341.lib)
#   2. pkg-config "opencv" (3.x) or "opencv4" (4.x).

CONFIG += c++11
QMAKE_CXXFLAGS_WARN_ON += -Wall

OPENCV_MODULES = opencv_core opencv_imgproc opencv_imgcodecs opencv_videoio opencv_video

!isEmpty(OPENCV_INCLUDE_DIR) {
    INCLUDEPATH += $$OPENCV_INCLUDE_DIR
    !isEmpty(OPENCV_LIB_DIR): LIBS += -L$$OPENCV_LIB_DIR
    for(m, OPENCV_MODULES): LIBS += -l$${m}$${OPENCV_LIB_SUFFIX}
} else {
    CONFIG += link_pkgconfig
    packagesExist(opencv) {
        PKGCONFIG += opencv
    } else: packagesExist(opencv4) {
        PKGCONFIG += opencv4
    } else {
        error("OpenCV not found. Pass OPENCV_INCLUDE_DIR / OPENCV_LIB_DIR to qmake or install pkg-config files.")
    }
}

INCLUDEPATH += $$PWD/core
