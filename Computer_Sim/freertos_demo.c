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
/* 启动任务的配置 */
#define START_TASK_STACK 128
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
#define TASK2_STACK 512
#define TASK2_PRIORITY 3
TaskHandle_t task2_handle;
StackType_t task2_stack[TASK2_STACK]; // 静态任务的任务栈，以数组形式存储
StaticTask_t task2_tcb;               // 静态任务的TCB结构体类型
void task2(void *pvParameters);

/* 任务3的配置 */
#define TASK3_STACK 256
#define TASK3_PRIORITY 1
TaskHandle_t task3_handle;
StackType_t task3_stack[TASK3_STACK]; // 静态任务的任务栈，以数组形式存储
StaticTask_t task3_tcb;               // 静态任务的TCB结构体类型
void task3(void *pvParameters);

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
			LCD_ShowString(30,130,200,16,16,"USB Connecting...");	
			LCD_Fill(0,150,lcddev.width,lcddev.height,WHITE);
			break;
		case 1:	//USB键盘
			LCD_ShowString(30,130,200,16,16,"USB Connected    ");	
			LCD_ShowString(30,150,200,16,16,"USB KeyBoard");	 
			LCD_ShowString(30,180,210,16,16,"KEYVAL:");	
			LCD_ShowString(30,200,210,16,16,"INPUT STRING:");	
			break;
		case 2:	//USB鼠标
			LCD_ShowString(30,130,200,16,16,"USB Connected    ");	
			LCD_ShowString(30,150,200,16,16,"USB Mouse");	 
			LCD_ShowString(30,180,210,16,16,"BUTTON:");	
			LCD_ShowString(30,200,210,16,16,"X POS:");	
			LCD_ShowString(30,220,210,16,16,"Y POS:");	
			LCD_ShowString(30,240,210,16,16,"Z POS:");	
			break; 		
		case 3:	//不支持的USB设备
			LCD_ShowString(30,130,200,16,16,"USB Connected    ");	
			LCD_ShowString(30,150,200,16,16,"Unknow Device");	 
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
	extern void lv_port_disp_init(void);
	lv_port_disp_init();
	extern void lv_port_indev_init(void);
	lv_port_indev_init();

	
POINT_COLOR=RED;
LCD_ShowString(30,50,200,16,16,"Explorer STM32F4");	
//LCD_ShowString(30,70,200,16,16,"USB MOUSE/KEYBOARD TEST");	
//LCD_ShowString(30,90,200,16,16,"ATOM@ALIENTEK");
//LCD_ShowString(30,110,200,16,16,"2017/5/16");	 
//LCD_ShowString(30,130,200,16,16,"USB Connecting...");	  
USBH_Init(&USB_OTG_Core_dev,USB_OTG_FS_CORE_ID,&USB_Host,&HID_cb,&USR_Callbacks); 

	
    /* 进入临界区:保护临界区里的代码不会被打断 */
    taskENTER_CRITICAL();

    /* 使用静态创建3个任务 */
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


        vTaskDelay(1);
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
    while (1)
    {

        vTaskDelay(50);
        // HAL_Delay(500); //这种delay不会让任务进入阻塞态，不会让出cpu使用权
    }
}
