#include "sdio_sdcard.h"
#include "string.h"
#include "delay.h"
#include "FreeRTOS.h"
#include "task.h"
//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//ALIENTEK STM32F407开发板
//SD卡驱动代码	   
//正点原子@ALIENTEK
//技术论坛:www.openedv.com
//创建日期:2017/4/18
//版本：V1.0
//版权所有，盗版必究。
//Copyright(C) 广州市星翼电子科技有限公司 2014-2024
//All rights reserved									  
//////////////////////////////////////////////////////////////////////////////////

SD_HandleTypeDef        SDCARD_Handler;     //SD卡句柄
HAL_SD_CardInfoTypeDef  SDCardInfo;         //SD卡信息结构体
DMA_HandleTypeDef SDTxDMAHandler,SDRxDMAHandler;    //SD卡DMA发送和接收句柄

//SD_ReadDisk/SD_WriteDisk函数专用buf,当这两个函数的数据缓存区地址不是4字节对齐的时候,
//需要用到该数组,确保数据缓存区地址是4字节对齐的.
__align(4) u8 SDIO_DATA_BUFFER[512];

/* Watch g_sd_first_error after a failure. Clear valid, or reset, to re-arm.
 * Capture before diskio retries can replace the original HAL error. */
typedef struct {
    u32 valid;
    u32 stage;       /* 1: read, 2: read ready, 3: write, 4: write ready */
    u32 result;
    u32 hal_error;
    u32 sector;
    u32 count;
    u32 context;
    u32 status;
} SD_IO_ErrorInfo;

volatile SD_IO_ErrorInfo g_sd_first_error;

static void SD_RecordIOError(u32 stage, u32 result, u32 sector, u32 count)
{
    if(g_sd_first_error.valid != 0U) return;
    g_sd_first_error.stage = stage;
    g_sd_first_error.result = result;
    g_sd_first_error.hal_error = SDCARD_Handler.ErrorCode;
    g_sd_first_error.sector = sector;
    g_sd_first_error.count = count;
    g_sd_first_error.context = SDCARD_Handler.Context;
    g_sd_first_error.status = SDCARD_Handler.Instance->STA;
    g_sd_first_error.valid = 1U;
}

//SD卡初始化
//返回值:0 初始化正确；其他值，初始化错误
u8 SD_Init(void)
{
    u8 SD_Error;
    
    //初始化时的时钟不能大于400KHZ 
    SDCARD_Handler.Instance=SDIO;
    SDCARD_Handler.Init.ClockEdge=SDIO_CLOCK_EDGE_RISING;          			//上升沿     
    SDCARD_Handler.Init.ClockBypass=SDIO_CLOCK_BYPASS_DISABLE;     			//不使用bypass模式，直接用HCLK进行分频得到SDIO_CK
    SDCARD_Handler.Init.ClockPowerSave=SDIO_CLOCK_POWER_SAVE_DISABLE;    	//空闲时不关闭时钟电源
    SDCARD_Handler.Init.BusWide=SDIO_BUS_WIDE_1B;                        	//1位数据线
    SDCARD_Handler.Init.HardwareFlowControl=SDIO_HARDWARE_FLOW_CONTROL_DISABLE;//关闭硬件流控
    SDCARD_Handler.Init.ClockDiv=10U;//SDIO_TRANSFER_CLK_DIV;            			//SD传输时钟频率最大25MHZ
    
    SD_Error=HAL_SD_Init(&SDCARD_Handler);
    if(SD_Error!=HAL_OK) return 1;
	
	//获取SD卡信息
	HAL_SD_GetCardInfo(&SDCARD_Handler,&SDCardInfo);
    
    SD_Error=HAL_SD_ConfigWideBusOperation(&SDCARD_Handler,SDIO_BUS_WIDE_4B);//使能宽总线模式
    if(SD_Error!=HAL_OK) return 2;
    return 0;
}

//SDMMC底层驱动，时钟使能，引脚配置，DMA配置
//此函数会被HAL_SD_Init()调用
//hsd:SD卡句柄
void HAL_SD_MspInit(SD_HandleTypeDef *hsd)
{
    GPIO_InitTypeDef GPIO_Initure;
    
    __HAL_RCC_SDIO_CLK_ENABLE();    //使能SDIO时钟
    __HAL_RCC_DMA2_CLK_ENABLE();    //使能DMA2时钟 
    __HAL_RCC_GPIOC_CLK_ENABLE();   //使能GPIOC时钟
    __HAL_RCC_GPIOD_CLK_ENABLE();   //使能GPIOD时钟
    
    //PC8,9,10,11,12
    GPIO_Initure.Pin=GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12;
    GPIO_Initure.Mode=GPIO_MODE_AF_PP;      //推挽复用
    GPIO_Initure.Pull=GPIO_PULLUP;          //上拉
    GPIO_Initure.Speed=GPIO_SPEED_HIGH;     //高速
    GPIO_Initure.Alternate=GPIO_AF12_SDIO;  //复用为SDIO
    HAL_GPIO_Init(GPIOC,&GPIO_Initure);     //初始化
    
    //PD2
    GPIO_Initure.Pin=GPIO_PIN_2;            
    HAL_GPIO_Init(GPIOD,&GPIO_Initure);     //初始化

#if (SD_DMA_MODE==1)                        //使用DMA模式
    /* These handlers only call HAL, without FreeRTOS ISR APIs. */
    HAL_NVIC_SetPriority(SDIO_IRQn,5,0);
    HAL_NVIC_EnableIRQ(SDIO_IRQn);
    
    //配置发送DMA
    SDRxDMAHandler.Instance=DMA2_Stream3;
    SDRxDMAHandler.Init.Channel=DMA_CHANNEL_4;
    SDRxDMAHandler.Init.Direction=DMA_PERIPH_TO_MEMORY;
    SDRxDMAHandler.Init.PeriphInc=DMA_PINC_DISABLE;
    SDRxDMAHandler.Init.MemInc=DMA_MINC_ENABLE;
    SDRxDMAHandler.Init.PeriphDataAlignment=DMA_PDATAALIGN_WORD;
    SDRxDMAHandler.Init.MemDataAlignment=DMA_MDATAALIGN_WORD;
    SDRxDMAHandler.Init.Mode=DMA_PFCTRL;
    SDRxDMAHandler.Init.Priority=DMA_PRIORITY_VERY_HIGH;
    SDRxDMAHandler.Init.FIFOMode=DMA_FIFOMODE_ENABLE;
    SDRxDMAHandler.Init.FIFOThreshold=DMA_FIFO_THRESHOLD_FULL;
    SDRxDMAHandler.Init.MemBurst=DMA_MBURST_INC4;
    SDRxDMAHandler.Init.PeriphBurst=DMA_PBURST_INC4;

    __HAL_LINKDMA(hsd, hdmarx, SDRxDMAHandler); //将接收DMA和SD卡的发送DMA连接起来
    HAL_DMA_DeInit(&SDRxDMAHandler);
    HAL_DMA_Init(&SDRxDMAHandler);              //初始化接收DMA
    
    //配置接收DMA 
    SDTxDMAHandler.Instance=DMA2_Stream6;
    SDTxDMAHandler.Init.Channel=DMA_CHANNEL_4;
    SDTxDMAHandler.Init.Direction=DMA_MEMORY_TO_PERIPH;
    SDTxDMAHandler.Init.PeriphInc=DMA_PINC_DISABLE;
    SDTxDMAHandler.Init.MemInc=DMA_MINC_ENABLE;
    SDTxDMAHandler.Init.PeriphDataAlignment=DMA_PDATAALIGN_WORD;
    SDTxDMAHandler.Init.MemDataAlignment=DMA_MDATAALIGN_WORD;
    SDTxDMAHandler.Init.Mode=DMA_PFCTRL;
    SDTxDMAHandler.Init.Priority=DMA_PRIORITY_VERY_HIGH;
    SDTxDMAHandler.Init.FIFOMode=DMA_FIFOMODE_ENABLE;
    SDTxDMAHandler.Init.FIFOThreshold=DMA_FIFO_THRESHOLD_FULL;
    SDTxDMAHandler.Init.MemBurst=DMA_MBURST_INC4;
    SDTxDMAHandler.Init.PeriphBurst=DMA_PBURST_INC4;
    
    __HAL_LINKDMA(hsd, hdmatx, SDTxDMAHandler);//将发送DMA和SD卡的发送DMA连接起来
    HAL_DMA_DeInit(&SDTxDMAHandler);
    HAL_DMA_Init(&SDTxDMAHandler);              //初始化发送DMA 
  

    HAL_NVIC_SetPriority(DMA2_Stream3_IRQn, 5, 0);  //接收DMA中断优先级
    HAL_NVIC_EnableIRQ(DMA2_Stream3_IRQn);
    HAL_NVIC_SetPriority(DMA2_Stream6_IRQn, 5, 0);  //发送DMA中断优先级
    HAL_NVIC_EnableIRQ(DMA2_Stream6_IRQn);
#endif
}

//得到卡信息
//cardinfo:卡信息存储区
//返回值:错误状态
u8 SD_GetCardInfo(HAL_SD_CardInfoTypeDef *cardinfo)
{
    u8 sta;
    sta=HAL_SD_GetCardInfo(&SDCARD_Handler,cardinfo);
    return sta;
}

//判断SD卡是否可以传输(读写)数据
//返回值:SD_TRANSFER_OK 传输完成，可以继续下一次传输
//		 SD_TRANSFER_BUSY SD卡正忙，不可以进行下一次传输
u8 SD_GetCardState(void)
{
  return((HAL_SD_GetCardState(&SDCARD_Handler)==HAL_SD_CARD_TRANSFER )?SD_TRANSFER_OK:SD_TRANSFER_BUSY);
}

static u8 SD_WaitReady(uint32_t timeout_ms)
{
	uint32_t start_tick=HAL_GetTick();

	while(SD_GetCardState()!=SD_TRANSFER_OK)
	{
		if((HAL_GetTick()-start_tick)>=timeout_ms)
		{
			return (u8)HAL_TIMEOUT;
		}
		if(xTaskGetSchedulerState()==taskSCHEDULER_RUNNING)
			vTaskDelay(1U);
		else
			HAL_Delay(1U);
	}

	return (u8)HAL_OK;
}

 #if (SD_DMA_MODE==1)        //DMA模式

/* Wait for HAL's DMA/SDIO completion handlers, not just the card's CMD13
 * state: the card may be ready before DMA has written the last FIFO words. */
static u8 SD_WaitDMA(void)
{
    u32 start_tick=HAL_GetTick();
    while(HAL_SD_GetState(&SDCARD_Handler)!=HAL_SD_STATE_READY)
    {
        if((HAL_GetTick()-start_tick)>=SD_DATA_TIMEOUT_MS)
        {
            SDCARD_Handler.ErrorCode|=HAL_SD_ERROR_TIMEOUT;
            return (u8)HAL_TIMEOUT;
        }
        if(xTaskGetSchedulerState()==taskSCHEDULER_RUNNING)
            vTaskDelay(1U);
    }
    return (SDCARD_Handler.ErrorCode==HAL_SD_ERROR_NONE)?HAL_OK:HAL_ERROR;
}

/* Stop the card and DMA before diskio retries. A command-start failure can
 * leave DMA running even though HAL has already cleared hsd->Context. */
static void SD_RecoverDMA(void)
{
    u32 error=SDCARD_Handler.ErrorCode;
    u32 words=32U;
    SDCARD_Handler.Instance->MASK=0U;
    SDCARD_Handler.Instance->DCTRL=0U;
    if(HAL_DMA_GetState(&SDRxDMAHandler)==HAL_DMA_STATE_BUSY)
        (void)HAL_DMA_Abort(&SDRxDMAHandler);
    if(HAL_DMA_GetState(&SDTxDMAHandler)==HAL_DMA_STATE_BUSY)
        (void)HAL_DMA_Abort(&SDTxDMAHandler);
    (void)HAL_SD_Abort(&SDCARD_Handler);
    while(words-- && __HAL_SD_GET_FLAG(&SDCARD_Handler,SDIO_FLAG_RXDAVL))
        (void)SDIO_ReadFIFO(SDCARD_Handler.Instance);
    __HAL_SD_CLEAR_FLAG(&SDCARD_Handler,SDIO_STATIC_FLAGS);
    HAL_NVIC_ClearPendingIRQ(SDIO_IRQn);
    HAL_NVIC_ClearPendingIRQ(DMA2_Stream3_IRQn);
    HAL_NVIC_ClearPendingIRQ(DMA2_Stream6_IRQn);
    (void)SD_WaitReady(SD_READY_TIMEOUT_MS);
    SDCARD_Handler.ErrorCode=error;
}

static u8 SD_TransferDMA(u8 *buf,u32 sector,u32 cnt,u8 writing)
{
    u8 sta;
    u32 stage=writing?3U:1U;
    if(writing)
        sta=HAL_SD_WriteBlocks_DMA(&SDCARD_Handler,buf,sector,cnt);
    else
        sta=HAL_SD_ReadBlocks_DMA(&SDCARD_Handler,buf,sector,cnt);
    if(sta==HAL_OK)sta=SD_WaitDMA();
    if(sta==HAL_OK)
    {
        stage++;
        sta=SD_WaitReady(SD_READY_TIMEOUT_MS);
    }
    if(sta!=HAL_OK)
    {
        SD_RecordIOError(stage,sta,sector,cnt);
        SD_RecoverDMA();
    }
    return sta;
}

/* DMA cannot access CCM RAM. FatFs buffers may also be unaligned or on an
 * external task stack. Use the aligned internal 512-byte buffer in that case.
 * Limit direct requests to 128 sectors, within DMA's 16-bit word counter. */
static u8 SD_DiskDMA(u8 *buf,u32 sector,u32 cnt,u8 writing)
{
    u32 address=(u32)buf;
    u32 blocks;
    u8 sta;
    u8 direct;
    if(buf==NULL || cnt==0U || sector>=SDCardInfo.LogBlockNbr ||
       cnt>SDCardInfo.LogBlockNbr-sector)
        return (u8)HAL_ERROR;
    direct=((address&3U)==0U && address>=0x20000000U &&
            address<0x20020000U && cnt<=(0x20020000U-address)/512U);
    while(cnt!=0U)
    {
        blocks=direct?((cnt>128U)?128U:cnt):1U;
        if(!direct && writing)memcpy(SDIO_DATA_BUFFER,buf,512U);
        sta=SD_TransferDMA(direct?buf:SDIO_DATA_BUFFER,sector,blocks,writing);
        if(sta!=HAL_OK)return sta;
        if(!direct && !writing)memcpy(buf,SDIO_DATA_BUFFER,512U);
        sector+=blocks;
        cnt-=blocks;
        buf+=blocks*512U;
    }
    return (u8)HAL_OK;
}

//通过DMA读取SD卡一个扇区
//buf:读数据缓存区
//sector:扇区地址
//blocksize:扇区大小(一般都是512字节)
//cnt:扇区个数	
//返回值:错误状态;0,正常;其他,错误代码;
u8 SD_ReadBlocks_DMA(uint32_t *buf,uint64_t sector,uint32_t blocksize,uint32_t cnt)
{
    if(blocksize!=512U || sector>0xFFFFFFFFULL)return (u8)HAL_ERROR;
    return SD_DiskDMA((u8*)buf,(u32)sector,cnt,0U);
}

//写SD卡
//buf:写数据缓存区
//sector:扇区地址
//blocksize:扇区大小(一般都是512字节)
//cnt:扇区个数	
//返回值:错误状态;0,正常;其他,错误代码;	
u8 SD_WriteBlocks_DMA(uint32_t *buf,uint64_t sector,uint32_t blocksize,uint32_t cnt)
{
    if(blocksize!=512U || sector>0xFFFFFFFFULL)return (u8)HAL_ERROR;
    return SD_DiskDMA((u8*)buf,(u32)sector,cnt,1U);
}

//读SD卡
//buf:读数据缓存区
//sector:扇区地址
//cnt:扇区个数	
//返回值:错误状态;0,正常;其他,错误代码;
u8 SD_ReadDisk(u8* buf,u32 sector,u32 cnt)
{
	return SD_ReadBlocks_DMA((uint32_t*)buf,(uint64_t)sector,512,cnt);
}  

//写SD卡
//buf:写数据缓存区
//sector:扇区地址
//cnt:扇区个数	
//返回值:错误状态;0,正常;其他,错误代码;	
u8 SD_WriteDisk(u8 *buf,u32 sector,u32 cnt)
{
	return SD_WriteBlocks_DMA((uint32_t*)buf,(uint64_t)sector,512,cnt);//多个sector的写操作
} 

//SDIO中断服务函数
void SDIO_IRQHandler(void)
{
    HAL_SD_IRQHandler(&SDCARD_Handler);
}

//DMA2数据流6中断服务函数
void DMA2_Stream6_IRQHandler(void)
{
    HAL_DMA_IRQHandler(SDCARD_Handler.hdmatx);
}

//DMA2数据流3中断服务函数
void DMA2_Stream3_IRQHandler(void)
{
    HAL_DMA_IRQHandler(SDCARD_Handler.hdmarx);
}
#else                                   //轮训模式
 
//读SD卡
//buf:读数据缓存区
//sector:扇区地址
//cnt:扇区个数	
//返回值:错误状态;0,正常;其他,错误代码;
u8 SD_ReadDisk(u8* buf,u32 sector,u32 cnt)
{
	u8 sta;

	sta=HAL_SD_ReadBlocks(&SDCARD_Handler,(uint8_t*)buf,(uint64_t)sector,cnt,SD_DATA_TIMEOUT_MS);//多个sector的读操作
	if(sta!=HAL_OK)
	{
		SD_RecordIOError(1U,sta,sector,cnt);
		return sta;
	}

	sta=SD_WaitReady(SD_READY_TIMEOUT_MS);
	if(sta!=HAL_OK)SD_RecordIOError(2U,sta,sector,cnt);
	return sta;
}  


//写SD卡
//buf:写数据缓存区
//sector:扇区地址
//cnt:扇区个数	
//返回值:错误状态;0,正常;其他,错误代码;	
u8 SD_WriteDisk(u8 *buf,u32 sector,u32 cnt)
{
	u8 sta;

	sta=HAL_SD_WriteBlocks(&SDCARD_Handler,(uint8_t*)buf,(uint64_t)sector,cnt,SD_DATA_TIMEOUT_MS);//多个sector的写操作
	if(sta!=HAL_OK)
	{
		SD_RecordIOError(3U,sta,sector,cnt);
		return sta;
	}

	sta=SD_WaitReady(SD_READY_TIMEOUT_MS);
	if(sta!=HAL_OK)SD_RecordIOError(4U,sta,sector,cnt);
	return sta;
}
#endif
