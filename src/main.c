#include "sys.h"
#include "mpu.h"
#include "usart.h"
#include "lcd.h"
#include "wm8978.h"
#include "sai.h"
#include "arm_math.h"
#include "morse.h"

#define NPT 1024 // 16 64 256 1024 4096
#define SAI_RX_DMA_BUF_SIZE (4 * NPT)

// 好像内存必须在d1区域里才能连上dma
u16 saiplaybuf[2] __attribute__((section(".memsec_d1"))) = {0X0000, 0X0000};
u8 sairecbuf1[SAI_RX_DMA_BUF_SIZE] __attribute__((section(".memsec_d1")));
u8 sairecbuf2[SAI_RX_DMA_BUF_SIZE] __attribute__((section(".memsec_d1")));

arm_rfft_fast_instance_f32 S;
float32_t sInput[NPT];
float32_t sOutput[NPT];
float32_t fftOutput[NPT];

morse_t morse;

void rec_sai_dma_rx_callback(void);

int main()
{
    Stm32_Clock_Init(160, 5, 2, 4);
    SCB->CPACR |= 0b1111 << 20; // enable fpu
    delay_init(400);
    uart_init(100, 115200);
    MPU_Memory_Protection();
    lcd_init();

    RCC->AHB4ENR |= 1 << 1;
    GPIO_Set(GPIOB, PIN0 | PIN1, GPIO_MODE_OUT, GPIO_OTYPE_PP, GPIO_SPEED_MID, GPIO_PUPD_PU);
    GPIOB->BSRR |= 0b11;

    // 初始化声卡
    WM8978_Init();
    WM8978_HPvol_Set(40, 40);
    WM8978_SPKvol_Set(50);

    WM8978_ADDA_Cfg(0, 1);     //开启ADC
    WM8978_Input_Cfg(1, 1, 0); //开启输入通道(MIC&LINE IN)
    WM8978_Output_Cfg(0, 1);   //开启BYPASS输出
    WM8978_MIC_Gain(46);       //MIC增益设置
    WM8978_SPKvol_Set(0);      //关闭喇叭.
    WM8978_I2S_Cfg(2, 0);      //飞利浦标准,16位数据长度

    // 初始化fft参数
    arm_rfft_fast_init_f32(&S, NPT);

    // 初始化定时器  t3计发声时间 t4计间隔时间
    RCC->APB1LENR |= RCC_APB1LENR_TIM3EN;
    while (!(RCC->APB1LENR & RCC_APB1LENR_TIM3EN))
    {
    }
    TIM3->CR1 = TIM_CR1_CKD_1;
    TIM3->PSC = 5000 - 1;
    TIM3->SMCR = 0;
    TIM3->ARR = 0xFFFF;
    TIM3->DIER = 0;

    RCC->APB1LENR |= RCC_APB1LENR_TIM4EN;
    while (!(RCC->APB1LENR & RCC_APB1LENR_TIM4EN))
    {
    }
    TIM4->CR1 = TIM_CR1_CKD_1;
    TIM4->PSC = 5000 - 1;
    TIM4->SMCR = 0;
    TIM4->ARR = 6200; // 字符超时
    TIM4->DIER = TIM_DIER_UIE;
    MY_NVIC_Init(0, 1, TIM4_IRQn, 2);

    morse_rst(&morse);

    // 开始录音
    SAIA_Init(0, 1, 4);                                                   //SAI1 Block A,主发送,16位数据
    SAIB_Init(3, 1, 4);                                                   //SAI1 Block B从模式接收,16位
    SAIA_SampleRate_Set(44100);                                           //设置采样率
    SAIA_TX_DMA_Init((u8 *)&saiplaybuf[0], (u8 *)&saiplaybuf[1], 1, 1);   //配置TX DMA,16位
    DMA2_Stream3->CR &= ~(1 << 4);                                        //关闭传输完成中断(这里不用中断送数据)
    SAIB_RX_DMA_Init(sairecbuf1, sairecbuf2, SAI_RX_DMA_BUF_SIZE / 2, 1); //配置RX DMA
    sai_rx_callback = rec_sai_dma_rx_callback;                            //初始化回调函数指sai_rx_callback
    SAI_Play_Start();                                                     //开始SAI数据发送(主机)
    SAI_Rec_Start();                                                      //开始SAI数据接收(从机)

    while (1)
    {
    }
}

void pcm_decode(u16 *buf)
{
    u16 i;
    for (i = 0; i < NPT; i++)
    {
        sInput[i] = *(buf + (i * 2));
    }
    arm_rfft_fast_f32(&S, sInput, sOutput, 0);
    arm_cmplx_mag_f32(sOutput, fftOutput, NPT);
    if (fftOutput[47] > 8000000) // 比较1978~2064Hz频率的声音音量
    {
        GPIOB->BSRR |= 3 << 16;
        if (!(TIM3->CR1 & TIM_CR1_CEN))
        {
            // 开始响了 开始计时间 并停止计停止时间
            TIM4->CR1 &= ~TIM_CR1_CEN;
            // printf("4 %d\r\n", TIM4->CNT);
            TIM3->CNT = 0;
            TIM3->CR1 |= TIM_CR1_CEN;
        }
    }
    else
    {
        GPIOB->BSRR |= 3 << 0;
        if (TIM3->CR1 & TIM_CR1_CEN)
        {
            // 不响了 停止计时间 并开始计停止时间 然后计算内容
            TIM3->CR1 &= ~TIM_CR1_CEN;
            TIM4->CNT = 0;
            TIM4->CR1 |= TIM_CR1_CEN;
            // printf("3 %d\r\n", TIM3->CNT);
            if (TIM3->CNT > 4000) { // 一个tick大约是2000 正常情况下 短1t 长3t
                morse_dam(&morse);
            }
            else
            {
                morse_dit(&morse);
            }
        }
    }
}

void rec_sai_dma_rx_callback(void)
{
    u16 *p;
    // 如果此位是1 正在传输buf2 buf1传输完成
    if (DMA2_Stream5->CR & DMA_SxCR_CT)
    {
        sairecbuf_curvalid = 1;
        p = (u16 *)sairecbuf1;
    }
    else
    {
        sairecbuf_curvalid = 2;
        p = (u16 *)sairecbuf2;
    }
    // 解码
    pcm_decode(p);
}

void TIM4_IRQHandler()
{
    if (TIM4->SR & TIM_SR_UIF)
    {
        // 一个字符发送完了 超时时间到 处理并复位
        TIM4->CR1 &= ~TIM_CR1_CEN;
        TIM4->SR &= ~TIM_SR_UIF;

        USART1->TDR = morse_get(&morse);
        morse_rst(&morse);
    }
}

void HardFault_Handler()
{
    while (1)
    {
    }
}