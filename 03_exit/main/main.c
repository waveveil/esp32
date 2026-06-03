#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "exit.h"


void app_main(void)
{
    led_init();
    exit_init();

    uint8_t key;

    while (1)
     {
        vTaskDelay(10);
      
    }
}
