#pragma once

#include "driver/gpio.h"

// ── I²C shared bus (Touch GT1151 + Audio ES8311/ES7210 + IO expander TCA9554)
// v1.5 mainboard: IO47/IO48 with 1.8V→3.3V level shifters
#define BSP_I2C_PORT            I2C_NUM_0
#define BSP_I2C_SCL             GPIO_NUM_48
#define BSP_I2C_SDA             GPIO_NUM_47
#define BSP_I2C_FREQ_HZ         400000

// I²C device addresses
#define BSP_ES8311_ADDR         0x18
#define BSP_ES7210_ADDR         0x41
#define BSP_GT1151_ADDR         0x14  // alt: 0x5D
#define BSP_TCA9554_ADDR        0x20

// TCA9554 pin assignments
#define BSP_PA_CTRL_EXP_PIN     0    // IO expander pin 0 → NS4150 PA enable

// ── I²S (ES8311 speaker out + ES7210 mic in, shared bus)
#define BSP_I2S_NUM             0   // i2s_port_t I2S_NUM_0
#define BSP_I2S_MCLK            GPIO_NUM_5
#define BSP_I2S_BCLK            GPIO_NUM_16
#define BSP_I2S_WS              GPIO_NUM_7
#define BSP_I2S_DOUT            GPIO_NUM_6   // ESP32 → ES8311 (speaker)
#define BSP_I2S_DSIN            GPIO_NUM_15  // ES7210 → ESP32 (mic)

// ── RGB LCD — SUB3 subboard: 4.3" 800×480 ST7262E43
#define BSP_LCD_H_RES           800
#define BSP_LCD_V_RES           480
#define BSP_LCD_PCLK_HZ         (16 * 1000 * 1000)

#define BSP_LCD_VSYNC           GPIO_NUM_3
#define BSP_LCD_HSYNC           GPIO_NUM_46
#define BSP_LCD_DE              GPIO_NUM_17
#define BSP_LCD_PCLK            GPIO_NUM_9

// 16-bit RGB data bus (v1.5 mainboard: DATA6=IO8, DATA7=IO18, not 47/48)
#define BSP_LCD_DATA0           GPIO_NUM_10
#define BSP_LCD_DATA1           GPIO_NUM_11
#define BSP_LCD_DATA2           GPIO_NUM_12
#define BSP_LCD_DATA3           GPIO_NUM_13
#define BSP_LCD_DATA4           GPIO_NUM_14
#define BSP_LCD_DATA5           GPIO_NUM_21
#define BSP_LCD_DATA6           GPIO_NUM_8
#define BSP_LCD_DATA7           GPIO_NUM_18
#define BSP_LCD_DATA8           GPIO_NUM_45
#define BSP_LCD_DATA9           GPIO_NUM_38
#define BSP_LCD_DATA10          GPIO_NUM_39
#define BSP_LCD_DATA11          GPIO_NUM_40
#define BSP_LCD_DATA12          GPIO_NUM_41
#define BSP_LCD_DATA13          GPIO_NUM_42
#define BSP_LCD_DATA14          GPIO_NUM_2
#define BSP_LCD_DATA15          GPIO_NUM_1

// ── Misc
#define BSP_BTN_BOOT            GPIO_NUM_0
#define BSP_LED_IO              GPIO_NUM_4
