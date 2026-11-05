#include "sys.h"

#ifndef __FREERTOS_DEMO_H
#define __FREERTOS_DEMO_H

/* Keil Watch: kind 1 = HardFault, 2 = task stack overflow.
 * All addresses and registers are retained until reset. */
typedef struct {
    u32 kind;
    u32 task_handle;
    const char * task_name;
    u32 exc_return;
    u32 stack_pointer;
    u32 pc;
    u32 lr;
    u32 xpsr;
    u32 cfsr;
    u32 hfsr;
    u32 bfar;
    u32 mmfar;
} RuntimeFaultInfo;

extern volatile RuntimeFaultInfo g_runtime_fault;
void RuntimeFault_CaptureHardFault(u32 * frame, u32 exc_return);

void freertos_start(void);
void USBH_HID_Reconnect(void);
void USBH_Msg_Show(u8 msgx);
#endif
