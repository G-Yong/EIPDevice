/*******************************************************************************
 * my_application.c - OpENer application callbacks for Qt EIP Target
 *
 * Implements all callbacks required by OpENer. The Qt layer calls
 * OpENer's CipStackInit() which internally invokes ApplicationInitialization().
 ******************************************************************************/

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "opener_api.h"
#include "appcontype.h"
#include "trace.h"
#include "ciptcpipinterface.h"
#include "cipqos.h"
#include "nvdata.h"
#include "my_application.h"

/* Global end-stack flag (normally in OpENer's platform main.c) */
volatile int g_end_stack = 0;

/* Runtime-configurable I/O data sizes */
int g_my_input_data_size = 32;
int g_my_output_data_size = 32;

/* I/O data buffers */
EipUint8 g_input_data[MY_MAX_IO_DATA_SIZE];
EipUint8 g_output_data[MY_MAX_IO_DATA_SIZE];
EipUint8 g_config_data[MY_CONFIG_DATA_SIZE];
EipUint8 g_explicit_data[MY_EXPLICIT_DATA_SIZE];

static EipUint32 s_cycle_counter = 0;

/* Snapshot of last-sent input data for Change-of-State detection */
static EipUint8 s_last_input_data[MY_MAX_IO_DATA_SIZE];
static int s_input_data_changed = 0;

/* ============================================================
 * Callback function pointer set by Qt wrapper (eiptargetworker.cpp)
 * ============================================================ */
typedef void (*IoEventCallback)(unsigned int, unsigned int, int);
typedef void (*OutputDataCallback)(const EipUint8*, int);

static IoEventCallback    s_io_event_cb    = NULL;
static OutputDataCallback s_output_data_cb = NULL;

void MyApp_SetIoEventCallback(IoEventCallback cb) {
    s_io_event_cb = cb;
}

void MyApp_SetOutputDataCallback(OutputDataCallback cb) {
    s_output_data_cb = cb;
}

void MyApp_SetIoSizes(int input_size, int output_size) {
    if (input_size < 1) input_size = 1;
    if (input_size > MY_MAX_IO_DATA_SIZE) input_size = MY_MAX_IO_DATA_SIZE;
    if (output_size < 1) output_size = 1;
    if (output_size > MY_MAX_IO_DATA_SIZE) output_size = MY_MAX_IO_DATA_SIZE;
    g_my_input_data_size = input_size;
    g_my_output_data_size = output_size;
}

EipStatus ApplicationInitialization(void) {
    memset(g_input_data, 0, g_my_input_data_size);
    memset(g_output_data, 0, g_my_output_data_size);
    memset(g_config_data, 0, sizeof(g_config_data));
    memset(g_explicit_data, 0, sizeof(g_explicit_data));

    CreateAssemblyObject(MY_INPUT_ASSEMBLY_NUM,
                         g_input_data, g_my_input_data_size);
    CreateAssemblyObject(MY_OUTPUT_ASSEMBLY_NUM,
                         g_output_data, g_my_output_data_size);
    CreateAssemblyObject(MY_CONFIG_ASSEMBLY_NUM,
                         g_config_data, sizeof(g_config_data));
    CreateAssemblyObject(MY_HEARTBEAT_INPUT_ONLY_ASSEMBLY_NUM, NULL, 0);
    CreateAssemblyObject(MY_HEARTBEAT_LISTEN_ONLY_ASSEMBLY_NUM, NULL, 0);
    CreateAssemblyObject(MY_EXPLICIT_ASSEMBLY_NUM,
                         g_explicit_data, sizeof(g_explicit_data));

    ConfigureExclusiveOwnerConnectionPoint(
        0, MY_OUTPUT_ASSEMBLY_NUM, MY_INPUT_ASSEMBLY_NUM, MY_CONFIG_ASSEMBLY_NUM);
    ConfigureInputOnlyConnectionPoint(
        0, MY_HEARTBEAT_INPUT_ONLY_ASSEMBLY_NUM,
        MY_INPUT_ASSEMBLY_NUM, MY_CONFIG_ASSEMBLY_NUM);
    ConfigureListenOnlyConnectionPoint(
        0, MY_HEARTBEAT_LISTEN_ONLY_ASSEMBLY_NUM,
        MY_INPUT_ASSEMBLY_NUM, MY_CONFIG_ASSEMBLY_NUM);

    InsertGetSetCallback(GetCipClass(kCipQoSClassCode),
                         NvQosSetCallback, kNvDataFunc);
    InsertGetSetCallback(GetCipClass(kCipTcpIpInterfaceClassCode),
                         NvTcpipSetCallback, kNvDataFunc);

    return kEipStatusOk;
}

void HandleApplication(void) {
    s_cycle_counter++;
    g_input_data[0] = (EipUint8)(s_cycle_counter & 0xFF);
    g_input_data[1] = (EipUint8)((s_cycle_counter >> 8) & 0xFF);
    g_input_data[2] = (EipUint8)((s_cycle_counter >> 16) & 0xFF);
    g_input_data[3] = (EipUint8)((s_cycle_counter >> 24) & 0xFF);
}

void CheckIoConnectionEvent(unsigned int output_assembly,
                            unsigned int input_assembly,
                            IoConnectionEvent event) {
    if (s_io_event_cb) {
        s_io_event_cb(output_assembly, input_assembly, (int)event);
    }
}

EipStatus AfterAssemblyDataReceived(CipInstance *instance) {
    if (instance->instance_number == MY_OUTPUT_ASSEMBLY_NUM) {
        if (s_output_data_cb) {
            s_output_data_cb(g_output_data, g_my_output_data_size);
        }
    }
    return kEipStatusOk;
}

EipBool8 BeforeAssemblyDataSend(CipInstance *instance) {
    if (instance->instance_number == MY_INPUT_ASSEMBLY_NUM) {
        /* Check if input assembly data changed since last send (for COS) */
        if (memcmp(g_input_data, s_last_input_data, g_my_input_data_size) != 0) {
            memcpy(s_last_input_data, g_input_data, g_my_input_data_size);
            s_input_data_changed = 1;
            return true;  /* data changed -> increment sequence count */
        }
        return false;  /* no change -> keep same sequence count */
    }
    (void)instance;
    return true;
}

EipStatus ResetDevice(void) {
    CloseAllConnections();
    CipQosUpdateUsedSetQosValues();
    return kEipStatusOk;
}

EipStatus ResetDeviceToInitialConfiguration(void) {
    g_tcpip.encapsulation_inactivity_timeout = 120;
    CipQosResetAttributesToDefaultValues();
    ResetDevice();
    return kEipStatusOk;
}

void *CipCalloc(size_t num, size_t size) {
    return calloc(num, size);
}

void CipFree(void *ptr) {
    free(ptr);
}

void RunIdleChanged(EipUint32 run_idle_value) {
    (void)run_idle_value;
}
