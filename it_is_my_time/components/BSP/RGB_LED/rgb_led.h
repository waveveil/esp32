#ifndef __RGB_LED_H
#define __RGB_LED_H

#include "driver/gpio.h"

#define RGB_LED_GPIO  GPIO_NUM_38

void rgb_led_init(void);
void rgb_led_effect_start(void);

#endif
