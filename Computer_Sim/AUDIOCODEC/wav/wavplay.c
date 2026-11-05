#include "wavplay.h" 
#include "audioplay.h"
#include "usart.h" 
#include "delay.h" 
#include "malloc.h"
#include "ff.h"
#include "i2s.h"
#include "wm8978.h"
#include "key.h"
#include "led.h"
#include <string.h>
//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//ALIENTEK STM32F407开发板
//WAV 解码代码	   
//正点原子@ALIENTEK
//技术论坛:www.openedv.com
//创建日期:2014/6/29
//版本：V1.0
//版权所有，盗版必究。
//Copyright(C) 广州市星翼电子科技有限公司 2014-2024
//All rights reserved				
//********************************************************************************
//V1.0 说明
//1,支持16位/24位WAV文件播放
//2,最高可以支持到192K/24bit的WAV格式. 
////////////////////////////////////////////////////////////////////////////////// 	
 
__wavctrl wavctrl;		//WAV控制结构体
vu8 wavtransferend=0;	//i2s传输完成标志
vu32 wav_dma_completions=0;
WavReadInfo wav_read_info;
vu8 wavwitchbuf=0;		//i2sbufx指示标志
 
//WAV解析初始化
//fname:文件路径+文件名
//wavx:wav 信息存放结构体指针
//返回值:0,成功;1,打开文件失败;2,非WAV文件;3,DATA区域未找到.
u8 wav_decode_init(u8* fname,__wavctrl* wavx)
{
    FIL *ftemp = NULL;
    u8 *buf = NULL;
    UINT br = 0;
    FRESULT res = FR_NOT_ENOUGH_CORE;
    u32 offset, limit;
    u8 file_open = 0;
    ChunkRIFF *riff;
    ChunkFMT *fmt;
    ChunkDATA *data;

    memset(wavx, 0, sizeof(*wavx));
    ftemp = (FIL *)mymalloc(SRAMIN, sizeof(FIL));
    buf = (u8 *)mymalloc(SRAMIN, 512);
    if(ftemp == NULL || buf == NULL) goto cleanup;
    res = f_open(ftemp, (const TCHAR *)fname, FA_READ);
    if(res != FR_OK) goto cleanup;
    file_open = 1;
    res = f_read(ftemp, buf, 512, &br);
    if(res != FR_OK) goto cleanup;
    res = FR_INVALID_OBJECT;
    if(br < 36U || f_size(ftemp) > 0xFFFFFFFFUL) goto cleanup;

    /* Keep the example's packed WAV header structures and 512-byte header
     * read. Bound every optional LIST/fact chunk before dereferencing it. */
    riff = (ChunkRIFF *)buf;
    fmt = (ChunkFMT *)(buf + 12);
    if(riff->ChunkID != 0x46464952UL || riff->Format != 0x45564157UL ||
       riff->ChunkSize < 4U || riff->ChunkSize > (u32)f_size(ftemp) - 8U ||
       fmt->ChunkID != 0x20746D66UL || fmt->ChunkSize < 16U ||
       fmt->ChunkSize > br - 20U) goto cleanup;
    if(fmt->AudioFormat != 1U ||
       (fmt->NumOfChannels != 1U && fmt->NumOfChannels != 2U) ||
       (fmt->BitsPerSample != 16U && fmt->BitsPerSample != 24U) ||
       fmt->SampleRate == 0U || fmt->SampleRate > 192000U ||
       fmt->BlockAlign != fmt->NumOfChannels * (fmt->BitsPerSample / 8U) ||
       fmt->ByteRate != fmt->SampleRate * fmt->BlockAlign) goto cleanup;
    limit = riff->ChunkSize + 8U;
    offset = 20U + fmt->ChunkSize + (fmt->ChunkSize & 1U);
    for(;;) {
        if(offset > br || br - offset < sizeof(ChunkDATA) ||
           offset > limit || limit - offset < sizeof(ChunkDATA)) goto cleanup;
        data = (ChunkDATA *)(buf + offset);
        offset += sizeof(ChunkDATA);
        if(data->ChunkSize > limit - offset) goto cleanup;
        if(data->ChunkID == 0x61746164UL) break;
        if((data->ChunkSize & 1U) > limit - offset - data->ChunkSize) goto cleanup;
        offset += data->ChunkSize + (data->ChunkSize & 1U);
    }
    if(data->ChunkSize == 0U || data->ChunkSize % fmt->BlockAlign != 0U) goto cleanup;
    wavx->audioformat = fmt->AudioFormat;
    wavx->nchannels = fmt->NumOfChannels;
    wavx->samplerate = fmt->SampleRate;
    wavx->bitrate = fmt->ByteRate * 8U;
    wavx->blockalign = fmt->BlockAlign;
    wavx->bps = fmt->BitsPerSample;
    wavx->datasize = data->ChunkSize;
    wavx->datastart = offset;
    wavx->totsec = data->ChunkSize / fmt->ByteRate;
    res = FR_OK;
cleanup:
    if(file_open) (void)f_close(ftemp);
    myfree(SRAMIN, ftemp);
    myfree(SRAMIN, buf);
    return (u8)res;
}
//填充buf
//buf:数据区
//size:填充数据量
//bits:位数(16/24)
//返回值:读到的数据个数
u32 wav_buffill(u8 *buf,u16 size,u8 bits)
{
    UINT bread = 0;
    u32 readlen, remaining, position, produced = 0, i, copies;
    FRESULT res;

    memset(&wav_read_info, 0, sizeof(wav_read_info));
    if(buf == NULL || audiodev.file == NULL || audiodev.tbuf == NULL ||
       (bits != 16U && bits != 24U)) {
        wav_read_info.result = FR_INVALID_OBJECT;
        return 0;
    }
    position = (u32)f_tell(audiodev.file);
    if(position < wavctrl.datastart || position - wavctrl.datastart > wavctrl.datasize) {
        wav_read_info.result = FR_INVALID_OBJECT;
        return 0;
    }
    remaining = wavctrl.datasize - (position - wavctrl.datastart);
    if(bits == 16U) readlen = wavctrl.nchannels == 1U ? size / 2U : size;
    else readlen = wavctrl.nchannels == 1U ? (size / 8U) * 3U : (size / 4U) * 3U;
    if(readlen > remaining) readlen = remaining;
    wav_read_info.requested = readlen;

    /* The example reads stereo 16-bit PCM straight into the DMA buffer. */
    if(bits == 16U && wavctrl.nchannels == 2U) {
        res = f_read(audiodev.file, buf, readlen, &bread);
        produced = bread;
    }
    else {
        res = f_read(audiodev.file, audiodev.tbuf, readlen, &bread);
        copies = wavctrl.nchannels == 1U ? 2U : 1U;
        if(res == FR_OK) {
            for(i = 0; i + bits / 8U <= bread; i += bits / 8U) {
                u32 channel;
                for(channel = 0; channel < copies; channel++) {
                    if(bits == 16U) {
                        buf[produced++] = audiodev.tbuf[i];
                        buf[produced++] = audiodev.tbuf[i + 1U];
                    }
                    else {
                        buf[produced++] = audiodev.tbuf[i + 1U];
                        buf[produced++] = audiodev.tbuf[i + 2U];
                        buf[produced++] = 0;
                        buf[produced++] = audiodev.tbuf[i];
                    }
                }
            }
        }
    }
    wav_read_info.result = (u32)res;
    wav_read_info.actual = bread;
    if(produced < size) memset(buf + produced, 0, size - produced);
    return res == FR_OK ? produced : 0U;
}
//WAV播放时,I2S DMA传输回调函数
void wav_i2s_dma_tx_callback(void) 
{   
	u16 i;
	if(DMA1_Stream4->CR&(1<<19))
	{
		wavwitchbuf=0;
		if((audiodev.status&0X01)==0)
		{
			for(i=0;i<WAV_I2S_TX_DMA_BUFSIZE;i++)//暂停
			{
				audiodev.i2sbuf1[i]=0;//填充0
			}
		}
	}else 
	{
		wavwitchbuf=1;
		if((audiodev.status&0X01)==0)
		{
			for(i=0;i<WAV_I2S_TX_DMA_BUFSIZE;i++)//暂停
			{
				audiodev.i2sbuf2[i]=0;//填充0
			}
		}
	}
	wav_dma_completions++;
	wavtransferend=1;
} 
//得到当前播放时间
//fx:文件指针
//wavx:wav播放控制器
void wav_get_curtime(FIL*fx,__wavctrl *wavx)
{
	long long fpos;  	
 	wavx->totsec=wavx->datasize/(wavx->bitrate/8);	//歌曲总长度(单位:秒) 
	fpos=fx->fptr-wavx->datastart; 					//得到当前文件播放到的地方 
	wavx->cursec=fpos*wavx->totsec/wavx->datasize;	//当前播放到第多少秒了?	
}
//播放某个WAV文件
//fname:wav文件路径.
//返回值:
//KEY0_PRES:下一曲
//KEY1_PRES:上一曲
//其他:错误
u8 wav_play_song(u8* fname)
{
	u8 key;
	u8 t=0; 
	u8 res;  
	u32 fillnum; 
	audiodev.file=(FIL*)mymalloc(SRAMIN,sizeof(FIL));
	audiodev.i2sbuf1=mymalloc(SRAMIN,WAV_I2S_TX_DMA_BUFSIZE);
	audiodev.i2sbuf2=mymalloc(SRAMIN,WAV_I2S_TX_DMA_BUFSIZE);
	audiodev.tbuf=mymalloc(SRAMIN,WAV_I2S_TX_DMA_BUFSIZE);
	if(audiodev.file&&audiodev.i2sbuf1&&audiodev.i2sbuf2&&audiodev.tbuf)
	{ 
		res=wav_decode_init(fname,&wavctrl);//得到文件的信息
		if(res==0)//解析文件成功
		{
			if(wavctrl.bps==16)
			{
				WM8978_I2S_Cfg(2,0);	//飞利浦标准,16位数据长度
				I2S2_Init(I2S_STANDARD_PHILIPS,I2S_MODE_MASTER_TX,I2S_CPOL_LOW,I2S_DATAFORMAT_16B_EXTENDED);	//飞利浦标准,主机发送,时钟低电平有效,16位扩展帧长度
			}else if(wavctrl.bps==24)
			{
				WM8978_I2S_Cfg(2,2);	//飞利浦标准,24位数据长度
				I2S2_Init(I2S_STANDARD_PHILIPS,I2S_MODE_MASTER_TX,I2S_CPOL_LOW,I2S_DATAFORMAT_24B);	//飞利浦标准,主机发送,时钟低电平有效,24位长度
			}
			I2S2_SampleRate_Set(wavctrl.samplerate);//设置采样率
			I2S2_TX_DMA_Init(audiodev.i2sbuf1,audiodev.i2sbuf2,WAV_I2S_TX_DMA_BUFSIZE/2); //配置TX DMA
			i2s_tx_callback=wav_i2s_dma_tx_callback;			//回调函数指wav_i2s_dma_callback
			audio_stop();
			res=f_open(audiodev.file,(TCHAR*)fname,FA_READ);	//打开文件
			if(res==0)
			{
				f_lseek(audiodev.file, wavctrl.datastart);		//跳过文件头
				fillnum=wav_buffill(audiodev.i2sbuf1,WAV_I2S_TX_DMA_BUFSIZE,wavctrl.bps);
				fillnum=wav_buffill(audiodev.i2sbuf2,WAV_I2S_TX_DMA_BUFSIZE,wavctrl.bps);
				audio_start();  
				while(res==0)
				{ 
					while(wavtransferend==0);//等待wav传输完成; 
					wavtransferend=0;
					if(fillnum!=WAV_I2S_TX_DMA_BUFSIZE)//播放结束?
					{
						res=KEY0_PRES;
						break;
					} 
 					if(wavwitchbuf)fillnum=wav_buffill(audiodev.i2sbuf2,WAV_I2S_TX_DMA_BUFSIZE,wavctrl.bps);//填充buf2
					else fillnum=wav_buffill(audiodev.i2sbuf1,WAV_I2S_TX_DMA_BUFSIZE,wavctrl.bps);//填充buf1
					while(1)
					{
						key=KEY_Scan(0); 
						if(key==WKUP_PRES)//暂停
						{
							if(audiodev.status&0X01)audiodev.status&=~(1<<0);
							else audiodev.status|=0X01;  
						}
						if(key==KEY2_PRES||key==KEY0_PRES)//下一曲/上一曲
						{
							res=key;
							break; 
						}
						wav_get_curtime(audiodev.file,&wavctrl);//得到总时间和当前播放的时间 
						audio_msg_show(wavctrl.totsec,wavctrl.cursec,wavctrl.bitrate);
						t++;
						if(t==20)
						{
							t=0;
 							LED0=!LED0;
						}
						if((audiodev.status&0X01)==0)delay_ms(10);
						else break;
					}
				}
				audio_stop(); 
			}else res=0XFF; 
		}else res=0XFF;
	}else res=0XFF; 
	myfree(SRAMIN,audiodev.tbuf);	//释放内存
	myfree(SRAMIN,audiodev.i2sbuf1);//释放内存
	myfree(SRAMIN,audiodev.i2sbuf2);//释放内存 
	myfree(SRAMIN,audiodev.file);	//释放内存 
	return res;
} 
	









