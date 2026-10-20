# Analysis core: pure C++11 + OpenCV (no Qt dependency), shared by the GUI, CLI and tests.
TEMPLATE = lib
CONFIG += staticlib
CONFIG -= qt
TARGET = turbcore

include(../common.pri)

HEADERS += \
    Types.h \
    Statistics.h \
    FrameSource.h \
    Preprocessor.h \
    Registration.h \
    ReferenceFrame.h \
    Algorithm.h \
    AlgorithmRegistry.h \
    AnalysisEngine.h \
    ResultExporter.h \
    algorithms/Algorithms.h

SOURCES += \
    Types.cpp \
    Statistics.cpp \
    FrameSource.cpp \
    Preprocessor.cpp \
    Registration.cpp \
    ReferenceFrame.cpp \
    AlgorithmRegistry.cpp \
    AnalysisEngine.cpp \
    ResultExporter.cpp \
    algorithms/IntensityAlgorithms.cpp \
    algorithms/StructureAlgorithms.cpp \
    algorithms/MotionAlgorithms.cpp
