#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "rgb_led.h"


void app_main(void)
{
    led_init();
    rgb_led_init();
    rgb_led_effect_start();

    while (1) {
        LED_TOGGLE();
        vTaskDelay(100);
    }
}
