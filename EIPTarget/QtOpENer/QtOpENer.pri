INCLUDEPATH += $$PWD

# ---- MSVC: compile source files as UTF-8 ----
win32-msvc*: QMAKE_CXXFLAGS += /utf-8
win32-msvc*: QMAKE_CFLAGS   += /utf-8

# ---- OpENer root ----
OPENER_ROOT = $$PWD/../../OpENer/source

# ---- Common defines ----
DEFINES += PC_OPENER_ETHERNET_BUFFER_SIZE=512
DEFINES += OPENER_WITH_TRACES
DEFINES += RESTRICT=__restrict
DEFINES += OPENER_CONSUMED_DATA_HAS_RUN_IDLE_HEADER

# ---- Include paths ----
INCLUDEPATH += \
    $$OPENER_ROOT/src \
    $$OPENER_ROOT/src/cip \
    $$OPENER_ROOT/src/enet_encap \
    $$OPENER_ROOT/src/ports \
    $$OPENER_ROOT/src/ports/nvdata \
    $$OPENER_ROOT/src/utils

# ---- Platform-specific ----
win32 {
    DEFINES += WIN32 _WIN32_WINNT=_WIN32_WINNT_VISTA
    LIBS += -lws2_32 -liphlpapi

    INCLUDEPATH += \
        $$OPENER_ROOT/src/ports/WIN32 \
        $$OPENER_ROOT/src/ports/WIN32/sample_application

    # WIN32 platform sources
    SOURCES += \
        $$OPENER_ROOT/src/ports/WIN32/networkhandler.c \
        $$OPENER_ROOT/src/ports/WIN32/opener_error.c \
        $$OPENER_ROOT/src/ports/WIN32/networkconfig.c
}

unix {
    INCLUDEPATH += \
        $$OPENER_ROOT/src/ports/POSIX \
        $$OPENER_ROOT/src/ports/POSIX/sample_application

    # POSIX platform sources
    SOURCES += \
        $$OPENER_ROOT/src/ports/POSIX/networkhandler.c \
        $$OPENER_ROOT/src/ports/POSIX/opener_error.c \
        $$OPENER_ROOT/src/ports/POSIX/networkconfig.c

    LIBS += -lpthread
}

# ---- Generic platform sources ----
SOURCES += \
    $$OPENER_ROOT/src/ports/generic_networkhandler.c \
    $$OPENER_ROOT/src/ports/socket_timer.c

# ---- CIP sources ----
SOURCES += \
    $$OPENER_ROOT/src/cip/appcontype.c \
    $$OPENER_ROOT/src/cip/cipassembly.c \
    $$OPENER_ROOT/src/cip/cipclass3connection.c \
    $$OPENER_ROOT/src/cip/cipcommon.c \
    $$OPENER_ROOT/src/cip/cipconnectionmanager.c \
    $$OPENER_ROOT/src/cip/cipconnectionobject.c \
    $$OPENER_ROOT/src/cip/cipdlr.c \
    $$OPENER_ROOT/src/cip/cipelectronickey.c \
    $$OPENER_ROOT/src/cip/cipepath.c \
    $$OPENER_ROOT/src/cip/cipethernetlink.c \
    $$OPENER_ROOT/src/cip/cipidentity.c \
    $$OPENER_ROOT/src/cip/cipioconnection.c \
    $$OPENER_ROOT/src/cip/cipmessagerouter.c \
    $$OPENER_ROOT/src/cip/cipqos.c \
    $$OPENER_ROOT/src/cip/cipstring.c \
    $$OPENER_ROOT/src/cip/cipstringi.c \
    $$OPENER_ROOT/src/cip/ciptcpipinterface.c \
    $$OPENER_ROOT/src/cip/ciptypes.c

# ---- ENET_ENCAP sources ----
SOURCES += \
    $$OPENER_ROOT/src/enet_encap/cpf.c \
    $$OPENER_ROOT/src/enet_encap/encap.c \
    $$OPENER_ROOT/src/enet_encap/endianconv.c

# ---- Utils sources ----
SOURCES += \
    $$OPENER_ROOT/src/utils/doublylinkedlist.c \
    $$OPENER_ROOT/src/utils/enipmessage.c \
    $$OPENER_ROOT/src/utils/random.c \
    $$OPENER_ROOT/src/utils/xorshiftrandom.c

# ---- NVDATA sources ----
SOURCES += \
    $$OPENER_ROOT/src/ports/nvdata/conffile.c \
    $$OPENER_ROOT/src/ports/nvdata/nvdata.c \
    $$OPENER_ROOT/src/ports/nvdata/nvqos.c \
    $$OPENER_ROOT/src/ports/nvdata/nvtcpip.c

# ---- Application sources ----
SOURCES += \
    $$PWD/eiptargetservice.cpp \
    $$PWD/eiptargetworker.cpp \
    $$PWD/my_application.c

HEADERS += \
    $$PWD/eiptargetservice.h \
    $$PWD/eiptargetworker.h \
    $$PWD/my_application.h \
    $$PWD/devicedata.h \
    $$PWD/opener_user_conf.h