/*******************************************************************************
 * opener_user_conf.h - OpENer user configuration for Qt EIP Target
 *
 * Cross-platform: Windows / Linux
 ******************************************************************************/
#ifndef OPENER_USER_CONF_H_
#define OPENER_USER_CONF_H_

#include <assert.h>
#include <stdio.h>

#ifdef _WIN32
  /* Windows */
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  typedef unsigned short in_port_t;
  #include "typedefs.h"
#else
  /* POSIX / Linux */
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <arpa/inet.h>
  #include <sys/select.h>
  #include "typedefs.h"
#endif

/* ---- DLR ---- */
#ifndef OPENER_IS_DLR_DEVICE
  #define OPENER_IS_DLR_DEVICE  0
#endif

/* ---- TCP/IP ---- */
#ifndef OPENER_TCPIP_IFACE_CFG_SETTABLE
  #define OPENER_TCPIP_IFACE_CFG_SETTABLE 0
#endif

/* ---- Ethernet Link ---- */
#ifndef OPENER_ETHLINK_INSTANCE_CNT
  #define OPENER_ETHLINK_INSTANCE_CNT 1
#endif

#ifndef OPENER_ETHLINK_LABEL_ENABLE
  #define OPENER_ETHLINK_LABEL_ENABLE 0
#endif

#ifndef OPENER_ETHLINK_CNTRS_ENABLE
  #define OPENER_ETHLINK_CNTRS_ENABLE 0
#endif

#ifndef OPENER_ETHLINK_IFACE_CTRL_ENABLE
  #define OPENER_ETHLINK_IFACE_CTRL_ENABLE 0
#endif

/* ---- Connection capacity ---- */
#define OPENER_CIP_NUM_APPLICATION_SPECIFIC_CONNECTABLE_OBJECTS 1
#define OPENER_CIP_NUM_EXPLICIT_CONNS 6
#define OPENER_CIP_NUM_EXLUSIVE_OWNER_CONNS 1
#define OPENER_CIP_NUM_INPUT_ONLY_CONNS 1
#define OPENER_CIP_NUM_INPUT_ONLY_CONNS_PER_CON_PATH 3
#define OPENER_CIP_NUM_LISTEN_ONLY_CONNS 1
#define OPENER_CIP_NUM_LISTEN_ONLY_CONNS_PER_CON_PATH 3

/* ---- Sessions ---- */
#define OPENER_NUMBER_OF_SUPPORTED_SESSIONS 20

/* ---- Timer tick ---- */
static const MilliSeconds kOpenerTimerTickInMilliSeconds = 10;

/* ---- Tracing / Assertion ---- */
#ifdef OPENER_WITH_TRACES
  #define LOG_TRACE(...)  fprintf(stderr, __VA_ARGS__)
  #define OPENER_ASSERT(assertion) assert(assertion)
#else
  #define OPENER_ASSERT(assertion) assert(assertion)
#endif

#endif /* OPENER_USER_CONF_H_ */
