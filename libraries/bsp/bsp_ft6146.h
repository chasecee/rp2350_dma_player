#ifndef __BSP_FT6146_H__
#define __BSP_FT6146_H__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"


#define BSP_FT6146_RST_PIN 17
#define BSP_FT6146_INT_PIN 16

#define FT6146_LCD_TOUCH_MAX_POINTS (2)

#define FT6146_DEVICE_ADDR 0x38
#define FT6146_ID 0x03

typedef enum
{
    FT6146_EREG_MODE_SW = 0X00,
    FT6146_REG_STATUS = 0x02,
    FT6146_REG_P1_XH,
    FT6146_REG_P1_XL,
    FT6146_REG_P1_YH,
    FT6146_REG_P1_YL,
    FT6146_REG_P1_WEIGHT,
    FT6146_REG_P1_MISC,
    FT6146_REG_P2_XH,
    FT6146_REG_P2_XL,
    FT6146_REG_P2_YH,
    FT6146_REG_P2_YL,
    FT6146_REG_P2_WEIGHT,
    FT6146_REG_P2_MISC,
    FT6146_REG_ID = 0xA0,
    FT6146_ID_G_CTRL = 0x86,
    FT6146_PERIODACTIVE = 0x88,
    FT6146_PERIODMONITOR,
    FT6146_ID_G_SPEC_GESTURE_ENABLE = 0xD0
} ft6146_reg_t;

typedef struct
{
    uint16_t rotation;
    uint16_t width;
    uint16_t height;

} bsp_ft6146_info_t;

typedef struct
{
    uint8_t points; // Number of touch points
    // bool read_data_done;
    struct
    {
        uint16_t x;        /*!< X coordinate */
        uint16_t y;        /*!< Y coordinate */
        // uint16_t pressure; /*!< pressure */
    } coords[FT6146_LCD_TOUCH_MAX_POINTS];
}bsp_ft6146_data_t;

void bsp_ft6146_init(bsp_ft6146_info_t *ft6146_info);
void bsp_ft6146_set_rotation(uint16_t rotation);
bool bsp_ft6146_get_touch_data(bsp_ft6146_data_t *ft6146_data);
void bsp_ft6146_read(void);

#endif