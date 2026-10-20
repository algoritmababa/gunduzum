TEMPLATE = app
QT += core gui widgets
TARGET = TurbulenceAnalysis

include(../core_link.pri)

HEADERS += \
    MainWindow.h \
    AnalysisWorker.h \
    ParameterEditor.h \
    RoiPreviewWidget.h \
    DatasetConfig.h

SOURCES += \
    main.cpp \
    MainWindow.cpp \
    AnalysisWorker.cpp \
    ParameterEditor.cpp \
    RoiPreviewWidget.cpp \
    DatasetConfig.cpp
