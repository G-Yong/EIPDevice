#-------------------------------------------------
# EIPTarget.pro - EtherNet/IP Adapter (Slave) Qt Project
#
# Compiles the OpENer C stack directly into a Qt GUI application.
# OpENer 中 WIN32/main.c 已重命名为 opener_main.c，
# 避免 nmake 推导规则与 src/main.cpp 冲突。
# Cross-platform: Windows (MSVC) / Linux (GCC).
#-------------------------------------------------

QT       += core gui widgets network
CONFIG   += c11

TARGET = EIPTarget
TEMPLATE = app

# ---- MSVC: compile source files as UTF-8 ----
win32-msvc*: QMAKE_CXXFLAGS += /utf-8
win32-msvc*: QMAKE_CFLAGS   += /utf-8

# ---- Application sources ----
SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp

HEADERS += \
    src/mainwindow.h

include($$PWD/QtOpENer/QtOpENer.pri)