#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "key.h"


void app_main(void)
{
    led_init();
    key_init();

    uint8_t key;

    while (1)
     {
        key = key_scan(0);
        switch (key)
        {
            case BOOT_PRESS:
            {
                LED_TOGGLE();
                break;
            }
            default:
                break;
        }
        vTaskDelay(10);
    }
}
