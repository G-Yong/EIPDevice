/*******************************************************************************
 * my_application.h - EIP Target application definitions
 ******************************************************************************/
#ifndef MY_APPLICATION_H_
#define MY_APPLICATION_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "opener_api.h"

/* Assembly instance numbers (must match EDS file) */
#define MY_INPUT_ASSEMBLY_NUM                100
#define MY_OUTPUT_ASSEMBLY_NUM               150
#define MY_CONFIG_ASSEMBLY_NUM               151
#define MY_HEARTBEAT_INPUT_ONLY_ASSEMBLY_NUM 152
#define MY_HEARTBEAT_LISTEN_ONLY_ASSEMBLY_NUM 153
#define MY_EXPLICIT_ASSEMBLY_NUM             154

/* Maximum I/O data buffer size */
#define MY_MAX_IO_DATA_SIZE  512
#define MY_CONFIG_DATA_SIZE  10
#define MY_EXPLICIT_DATA_SIZE 32

/* Runtime-configurable I/O data sizes (set before CipStackInit) */
extern int g_my_input_data_size;
extern int g_my_output_data_size;

/* Global I/O data buffers */
extern EipUint8 g_input_data[MY_MAX_IO_DATA_SIZE];
extern EipUint8 g_output_data[MY_MAX_IO_DATA_SIZE];
extern EipUint8 g_config_data[MY_CONFIG_DATA_SIZE];
extern EipUint8 g_explicit_data[MY_EXPLICIT_DATA_SIZE];

/* Set I/O sizes before starting the stack */
void MyApp_SetIoSizes(int input_size, int output_size);

#ifdef __cplusplus
}
#endif

#endif /* MY_APPLICATION_H_ */
