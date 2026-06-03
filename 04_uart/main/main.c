#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
// #include "rgb_led.h"
#include "uart.h"


void app_main(void)
{
    // rgb_led_init();
    // rgb_led_effect_start();
    uint8_t len = 0;
    uint16_t times = 0;
    unsigned char data[RX_BUF_SIZE];

    led_init();
    usart_init(115200);
    while (1) {
       uart_get_buffered_data_len(USART_UX, (size_t*) &len);
       if (len > 0)
       {
            memset(data, 0, RX_BUF_SIZE);
            printf("\n您发送的消息是：\n");
            uart_read_bytes(USART_UX, data, len, 100);
            uart_write_bytes(USART_UX, (const char*)data, strlen((const char*)data));

            
       }
       else
       {
            times++;
            if (times % 5000 == 0)
            {
                printf("\n ESP32s3串口实验\n");
                
            }
            if (times % 200 == 0)
            {
                printf("\n请输入数据，以回车键结束:\n");
            }
            if (times % 20 == 0)
            {
                LED_TOGGLE();

            }
            vTaskDelay(10);
       }
    }
}
