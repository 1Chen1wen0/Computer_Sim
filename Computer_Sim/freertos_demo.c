#include "freertos_demo.h"
/* freertos相关的头文件，必须的 */
#include "FreeRTOS.h"
#include "task.h"
/* 需要用到的其他头文件 */
#include "main.h"
#include "usart.h"
#include "gpio.h"
#include "lcd.h"
#include "usart.h"
#include "sys.h"
#include "delay.h"
#include "string.h"
#include "usbh_usr.h" 
#include "sram.h"
#include "lvgl.h"
#include "SYS/DEBUG/debug.h"
#include "sdio_sdcard.h"
#include "led.h"
#include "key.h"
#include "w25qxx.h"
#include "ff.h"
#include "exfuns.h"
#include "string.h"
#include "rtc.h"
#include "sram.h"
#include "malloc.h"
#include "Password.h"
#include "tpad.h"
#include "SystemSettings.h"
#include "AudioPlayer.h"
volatile RuntimeFaultInfo g_runtime_fault;

static void runtime_fault_capture_status(void)
{
    g_runtime_fault.cfsr = SCB->CFSR;
    g_runtime_fault.hfsr = SCB->HFSR;
    g_runtime_fault.bfar = SCB->BFAR;
    g_runtime_fault.mmfar = SCB->MMFAR;
}

void RuntimeFault_CaptureHardFault(u32 * frame, u32 exc_return)
{
    u32 address = (u32)frame;
    __disable_irq();
    g_runtime_fault.task_handle = (u32)xTaskGetCurrentTaskHandle();
    g_runtime_fault.exc_return = exc_return;
    g_runtime_fault.stack_pointer = address;
    runtime_fault_capture_status();
    /* Core registers are first in both basic and extended exception frames.
     * Skip dereferencing an invalid stack or a failed exception stacking. */
    if((g_runtime_fault.cfsr & ((1UL << 4) | (1UL << 12))) == 0U &&
       ((address >= 0x20000000UL && address <= 0x2001FFE0UL) ||
        (address >= 0x10000000UL && address <= 0x1000FFE0UL) ||
        (address >= 0x68000000UL && address <= 0x680FFFE0UL)) &&
       (address & 3U) == 0U) {
        g_runtime_fault.pc = frame[6];
        g_runtime_fault.lr = frame[5];
        g_runtime_fault.xpsr = frame[7];
    }
    g_runtime_fault.kind = 1U;
    for(;;) __NOP();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char * name)
{
    __disable_irq();
    g_runtime_fault.task_handle = (u32)task;
    g_runtime_fault.task_name = name;
    g_runtime_fault.stack_pointer = __get_PSP();
    runtime_fault_capture_status();
    g_runtime_fault.kind = 2U;
    for(;;) __NOP();
}

extern volatile int32_t g_mouse_x;
extern volatile int32_t g_mouse_y;
extern volatile uint8_t g_mouse_pressed;
extern volatile uint8_t g_tpad_mouse_pressed;
/* 启动任务的配置 */
#define START_TASK_STACK 1024
#define START_TASK_PRIORITY 4
TaskHandle_t start_task_handle;
StackType_t start_task_stack[START_TASK_STACK]; // 静态任务的任务栈，以数组形式存储
StaticTask_t start_task_tcb;                    // 静态任务的TCB结构体类型
void start_task(void *pvParameters);

/* 任务1的配置 */
#define TASK1_STACK 512
#define TASK1_PRIORITY 2
TaskHandle_t task1_handle;
StackType_t task1_stack[TASK1_STACK]; // 静态任务的任务栈，以数组形式存储
StaticTask_t task1_tcb;               // 静态任务的TCB结构体类型
void task1(void *pvParameters);

/* 任务2的配置 */
#define TASK2_STACK 3072
#define TASK2_PRIORITY 2
TaskHandle_t task2_handle;
StackType_t task2_stack[TASK2_STACK]; // 静态任务的任务栈，以数组形式存储
StaticTask_t task2_tcb;               // 静态任务的TCB结构体类型
void task2(void *pvParameters);

/* 任务3的配置 */
#define TASK3_STACK 128
#define TASK3_PRIORITY 1
TaskHandle_t task3_handle;
StackType_t task3_stack[TASK3_STACK]; // 静态任务的任务栈，以数组形式存储
StaticTask_t task3_tcb;               // 静态任务的TCB结构体类型
void task3(void *pvParameters);

/* 任务4的配置 */
/* Words: allow margin above the linker's ~2.6 KB call chain and context. */
#define TASK4_STACK 1536
#define TASK4_PRIORITY 3
TaskHandle_t task4_handle;
StackType_t *task4_stack;            // Internal SRAM; allocated before task creation.
StaticTask_t task4_tcb;               // 静态任务的TCB结构体类型
void task4(void *pvParameters);

/* ======================= 静态创建方式，需要手动指定2个特殊任务的资源=============================== */
/* 空闲任务的配置 */
StackType_t idle_task_stack[configMINIMAL_STACK_SIZE]; // 静态任务的任务栈，以数组形式存储
StaticTask_t idle_task_tcb;                            // 静态任务的TCB结构体类型

/* 软件定时器任务的配置 */
StackType_t timer_task_stack[configTIMER_TASK_STACK_DEPTH]; // 静态任务的任务栈，以数组形式存储
StaticTask_t timer_task_tcb;                            // 静态任务的TCB结构体类型

/* 分配空闲任务的资源 */
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer = &idle_task_tcb;
    *ppxIdleTaskStackBuffer = idle_task_stack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

/* 分配软件定时器任务的资源 */
void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    uint32_t *pulTimerTaskStackSize)
{
    *ppxTimerTaskTCBBuffer = &timer_task_tcb;
    *ppxTimerTaskStackBuffer = timer_task_stack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

USBH_HOST  USB_Host;
USB_OTG_CORE_HANDLE  USB_OTG_Core_dev;
extern HID_Machine_TypeDef HID_Machine;	

/*USB信息显示*/
//msgx:0,USB无连接
//     1,USB键盘
//     2,USB鼠标
//     3,不支持的USB设备
void USBH_Msg_Show(u8 msgx)
{
	POINT_COLOR=RED;
	switch(msgx)
	{
		case 0:	//USB无连接
			LCD_ShowString(30,400,200,16,16,"USB Lost");	
			break;
		case 1:	//USB键盘
//			LCD_ShowString(30,130,200,16,16,"USB Connected    ");	
//			LCD_ShowString(30,150,200,16,16,"USB KeyBoard");	 
//			LCD_ShowString(30,180,210,16,16,"KEYVAL:");	
//			LCD_ShowString(30,200,210,16,16,"INPUT STRING:");	
			break;
		case 2:	//USB鼠标
//			LCD_ShowString(30,130,200,16,16,"USB Connected    ");	
//			LCD_ShowString(30,150,200,16,16,"USB Mouse");	 
//			LCD_ShowString(30,180,210,16,16,"BUTTON:");	
//			LCD_ShowString(30,200,210,16,16,"X POS:");	
//			LCD_ShowString(30,220,210,16,16,"Y POS:");	
//			LCD_ShowString(30,240,210,16,16,"Z POS:");	
			break; 		
		case 3:	//不支持的USB设备
//			LCD_ShowString(30,130,200,16,16,"USB Connected    ");	
//			LCD_ShowString(30,150,200,16,16,"Unknow Device");	 
			break; 	 
	} 
}   
//HID重新连接
void USBH_HID_Reconnect(void)
{
	//关闭之前的连接
	USBH_DeInit(&USB_OTG_Core_dev,&USB_Host);	//复位USB HOST
	USB_OTG_StopHost(&USB_OTG_Core_dev);		//停止USBhost
	if(USB_Host.usr_cb->DeviceDisconnected)		//存在,才禁止
	{
		USB_Host.usr_cb->DeviceDisconnected(); 	//关闭USB连接
		USBH_DeInit(&USB_OTG_Core_dev, &USB_Host);
		USB_Host.usr_cb->DeInit();
		USB_Host.class_cb->DeInit(&USB_OTG_Core_dev,&USB_Host.device_prop);
	}
	USB_OTG_DisableGlobalInt(&USB_OTG_Core_dev);//关闭所有中断
	//重新复位USB
    __HAL_RCC_USB_OTG_FS_FORCE_RESET();//USB OTG FS 复位
	delay_ms(5);
    __HAL_RCC_USB_OTG_FS_RELEASE_RESET();//复位结束
	memset(&USB_OTG_Core_dev,0,sizeof(USB_OTG_CORE_HANDLE));
	memset(&USB_Host,0,sizeof(USB_Host));
	//重新连接USB HID设备
	USBH_Init(&USB_OTG_Core_dev,USB_OTG_FS_CORE_ID,&USB_Host,&HID_cb,&USR_Callbacks);  
}


/**
 * @description: 启动FreeRTOS
 * @return {*}
 */
void freertos_start(void)
{
    /* 1.创建一个启动任务 */
    start_task_handle = xTaskCreateStatic(
        (TaskFunction_t)start_task,       // 任务函数的地址
        (char *)"start_task",             // 任务名字符串
        (uint32_t)START_TASK_STACK,       // 任务栈大小，默认最小128，单位4字节
        (void *)NULL,                     // 传递给任务的参数
        (UBaseType_t)START_TASK_PRIORITY, // 任务的优先级
        (StackType_t *)start_task_stack,  // 我们创建的任务栈（数组存储）的地址
        (StaticTask_t *)&start_task_tcb   // 我们创建的tcb的地址
    );

    /* 2.启动调度器:会自动创建空闲任务和软件定时器（如果开启），
       静态创建的方式需要去实现2个分配资源的接口函数 */
    vTaskStartScheduler();
}

/**
 * @description: 启动任务：用来创建其他Task
 * @param {void} *pvParameters
 * @return {*}
 */
void start_task(void *pvParameters)
{	
	KEY_Init();
	TPAD_Init(6);

	extern void lv_port_disp_init(void);
	lv_port_disp_init();
	extern void lv_port_indev_init(void);
	lv_port_indev_init();
	Password_ui_create();
	
USBH_Init(&USB_OTG_Core_dev,USB_OTG_FS_CORE_ID,&USB_Host,&HID_cb,&USR_Callbacks); 

    /* Initialize audio queues before task4 can run. */
    /* Keep the playback return addresses and exception frames in internal
     * SRAM. The captured fault occurred on this task's external SRAM stack. */
    task4_stack = (StackType_t *)mymalloc(SRAMIN,
                                         TASK4_STACK * sizeof(StackType_t));
    if(task4_stack != NULL && !AudioPlayer_Init()) {
        myfree(SRAMIN, task4_stack);
        task4_stack = NULL;
    }
	
    /* 进入临界区:保护临界区里的代码不会被打断 */
    taskENTER_CRITICAL();

    /* 使用静态创建4个任务 */
    task1_handle = xTaskCreateStatic(
        (TaskFunction_t)task1,
        (char *)"task1",
        (configSTACK_DEPTH_TYPE)TASK1_STACK,
        (void *)NULL,
        (UBaseType_t)TASK1_PRIORITY,
        (StackType_t *)task1_stack, // 我们创建的任务栈（数组存储）的地址
        (StaticTask_t *)&task1_tcb  // 我们创建的tcb的地址
    );

    task2_handle = xTaskCreateStatic(
        (TaskFunction_t)task2,
        (char *)"task2",
        (configSTACK_DEPTH_TYPE)TASK2_STACK,
        (void *)NULL,
        (UBaseType_t)TASK2_PRIORITY,
        (StackType_t *)task2_stack, // 我们创建的任务栈（数组存储）的地址
        (StaticTask_t *)&task2_tcb  // 我们创建的tcb的地址
    );

    task3_handle = xTaskCreateStatic(
        (TaskFunction_t)task3,
        (char *)"task3",
        (configSTACK_DEPTH_TYPE)TASK3_STACK,
        (void *)NULL,
        (UBaseType_t)TASK3_PRIORITY,
        (StackType_t *)task3_stack, // 我们创建的任务栈（数组存储）的地址
        (StaticTask_t *)&task3_tcb  // 我们创建的tcb的地址
    );
    if(task4_stack != NULL) {
        task4_handle = xTaskCreateStatic(
            (TaskFunction_t)task4,
            (char *)"task4",
            (configSTACK_DEPTH_TYPE)TASK4_STACK,
            (void *)NULL,
            (UBaseType_t)TASK4_PRIORITY,
            task4_stack,
            &task4_tcb
        );
    }
	
    /* 启动任务只需要执行一次即可，用完就删除自己 */
    vTaskDelete(NULL);

    /* 退出临界区 */
    taskEXIT_CRITICAL();
}

/**
 * @description: 任务一：实现LED1每500ms闪烁一次
 * @param {void} *pvParameters
 * @return {*}
 */
void task1(void *pvParameters)
{

    while (1)
    {
       	USBH_Process(&USB_OTG_Core_dev, &USB_Host);
		if(bDeviceState==1)//连接建立了
		{ 
			if(USBH_Check_HIDCommDead(&USB_OTG_Core_dev,&HID_Machine))//检测USB HID通信,是否还正常? 
			{ 	    
				USBH_HID_Reconnect();//重连
			}				
			
		}else	//连接未建立的时候,检测
		{
				if(USBH_Check_EnumeDead(&USB_Host))	//检测USB HOST 枚举是否死机了?死机了,则重新初始化 
				{ 	    
				USBH_HID_Reconnect();//重连
			}			
		}

        vTaskDelay(5);
    }
}

/**
 * @description: 任务二：实现LED2每500ms闪烁一次
 * @param {void} *pvParameters
 * @return {*}
 */
void task2(void *pvParameters)
{
	
    while (1)
    {
	/*LVGL_demo*/
     lv_timer_handler();
     vTaskDelay(5); 
    }
}

/**
 * @description: 任务三：判断按键KEY1是否按下，按下则删掉task1
 * @param {void} *pvParameters
 * @return {*}
 */

void task3(void *pvParameters)
{
	u8 key_value;

    while (1)
    {
		key_value = KEY_Scan(1);
		if(key_value == KEY0_PRES)
		{
			g_mouse_x += SystemSettings_ScaleMouseDelta(5);
		}
		else if(key_value == KEY1_PRES)
		{
			g_mouse_y += SystemSettings_ScaleMouseDelta(7);
		}
		else if(key_value == KEY2_PRES)
		{
			g_mouse_x += SystemSettings_ScaleMouseDelta(-5);
		}
		else if(key_value == WKUP_PRES)
		{
			g_mouse_y += SystemSettings_ScaleMouseDelta(-7);
		}

		if(g_mouse_x < 0) g_mouse_x = 0;
		else if(g_mouse_x >= lcddev.width) g_mouse_x = lcddev.width - 1;
		if(g_mouse_y < 0) g_mouse_y = 0;
		else if(g_mouse_y >= lcddev.height) g_mouse_y = lcddev.height - 1;

		/* The capacitive touch key acts as the mouse left button. */
		g_tpad_mouse_pressed = TPAD_Scan(1) ? 1U : 0U;
		vTaskDelay(pdMS_TO_TICKS(5));
    }
}


void task4(void *pvParameters)
{
    /* Wait for a song, play it, then block again when playback ends. */
    AudioPlayer_Task(pvParameters);
}
