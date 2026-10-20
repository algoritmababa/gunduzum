# Link against the static analysis core library (core/).

CORE_BUILD_DIR = $$OUT_PWD/../core
win32 {
    CONFIG(debug, debug|release): CORE_BUILD_DIR = $$CORE_BUILD_DIR/debug
    else: CORE_BUILD_DIR = $$CORE_BUILD_DIR/release
}

LIBS += -L$$CORE_BUILD_DIR -lturbcore
win32-msvc*: PRE_TARGETDEPS += $$CORE_BUILD_DIR/turbcore.lib
else: PRE_TARGETDEPS += $$CORE_BUILD_DIR/libturbcore.a

include($$PWD/common.pri)
