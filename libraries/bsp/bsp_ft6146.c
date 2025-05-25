#include "bsp_ft6146.h"
#include "bsp_i2c.h"


bsp_ft6146_info_t *g_ft6146_info;
bsp_ft6146_data_t g_ft6146_data;
bool g_ft6146_irq_flag = false;

void bsp_ft6146_reg_read_byte(uint8_t reg_addr, uint8_t *data, size_t len)
{
    bsp_i2c_read_reg8(FT6146_DEVICE_ADDR, reg_addr, data, len);
}

void bsp_ft6146_reg_write_byte(uint8_t reg_addr, uint8_t *data, size_t len)
{
    bsp_i2c_write_reg8(FT6146_DEVICE_ADDR, reg_addr, data, len);
}

void bsp_ft6146_reset(void)
{
    gpio_put(BSP_FT6146_RST_PIN, 1);
    sleep_ms(20);
    gpio_put(BSP_FT6146_RST_PIN, 0);
    sleep_ms(20);
    gpio_put(BSP_FT6146_RST_PIN, 1);
    sleep_ms(20);
}

void bsp_ft6146_read(void)
{
    uint8_t buffer[5];
    if (g_ft6146_irq_flag == false)
    {
        return;
    }
    g_ft6146_irq_flag = false;

    bsp_ft6146_reg_read_byte(FT6146_REG_STATUS, buffer, 11);
    if (buffer[0] > 0 && buffer[0] < 3)
    {
        g_ft6146_data.points = buffer[0];
        for (int i = 0; i < g_ft6146_data.points; i++)
        {
            g_ft6146_data.coords[i].x = (uint16_t)((buffer[1 + 6 * i] & 0x0f) << 8);
            g_ft6146_data.coords[i].x |= buffer[2 + 6 * i];

            g_ft6146_data.coords[i].y = (uint16_t)((buffer[3 + 6 * i] & 0x0f) << 8);
            g_ft6146_data.coords[i].y |= buffer[4 + 6 * i];
        }
    }
    else
        g_ft6146_data.points = 0;
}

bool bsp_ft6146_get_touch_data(bsp_ft6146_data_t *ft6146_data)
{
    memcpy(ft6146_data, &g_ft6146_data, sizeof(bsp_ft6146_data_t));
    ft6146_data->points = g_ft6146_data.points;
    g_ft6146_data.points = 0;

    switch (g_ft6146_info->rotation)
    {
    case 1:
        for (int i = 0; i < ft6146_data->points; i++)
        {
            ft6146_data->coords[i].x = g_ft6146_data.coords[i].y;
            ft6146_data->coords[i].y = g_ft6146_info->height - 1 - g_ft6146_data.coords[i].x;
        }
        break;
    case 2:
        for (int i = 0; i < ft6146_data->points; i++)
        {
            ft6146_data->coords[i].x = g_ft6146_info->width - 1 - g_ft6146_data.coords[i].x;
            ft6146_data->coords[i].y = g_ft6146_info->height - 1 - g_ft6146_data.coords[i].y;
        }
        break;

    case 3:
        for (int i = 0; i < ft6146_data->points; i++)
        {
            ft6146_data->coords[i].x = g_ft6146_info->width - g_ft6146_data.coords[i].y;
            ft6146_data->coords[i].y = g_ft6146_data.coords[i].x;
        }
        break;
    default:
        break;
    }

    // printf("x:%d, y:%d rotation:%d\r\n", ft6146_data->coords[0].x, ft6146_data->coords[0].y, g_ft6146_info->rotation);
    // printf("g_ft6146_info ->width:%d ->height:%d  rotation:%d \r\n", g_ft6146_info->width, g_ft6146_info->height, g_ft6146_info->rotation);
    if (ft6146_data->points == 0)
        return false;

    return true;
}

void bsp_ft6146_set_rotation(uint16_t rotation)
{
    uint16_t swap;
    g_ft6146_info->rotation = rotation;
    if (rotation == 1 || rotation == 3)
    {
        if (g_ft6146_info->width < g_ft6146_info->height)
        {
            swap = g_ft6146_info->width;
            g_ft6146_info->width = g_ft6146_info->height;
            g_ft6146_info->height = swap;
        }
    }
    else
    {
        if (g_ft6146_info->width > g_ft6146_info->height)
        {
            swap = g_ft6146_info->width;
            g_ft6146_info->width = g_ft6146_info->height;
            g_ft6146_info->height = swap;
        }
    }
}

static void gpio_irq_callbac(uint gpio, uint32_t event_mask)
{
    if (event_mask == GPIO_IRQ_EDGE_FALL)
    {
        g_ft6146_irq_flag = true;
        // printf("gpio_irq_callbac\r\n");
    }
}

void bsp_ft6146_init(bsp_ft6146_info_t *ft6146_info)
{
    uint8_t id = 0;
    gpio_init(BSP_FT6146_RST_PIN);
    gpio_set_dir(BSP_FT6146_RST_PIN, GPIO_OUT);

    gpio_init(BSP_FT6146_INT_PIN);
    gpio_set_dir(BSP_FT6146_INT_PIN, GPIO_IN);
    gpio_pull_up(BSP_FT6146_INT_PIN);

    bsp_ft6146_reset();
    g_ft6146_info = ft6146_info;
    // do
    // {
        sleep_ms(100);
        bsp_ft6146_reg_read_byte(FT6146_REG_ID, &id, 1);
        printf("id: 0x%02x\r\n", id);
    // } while (id != FT6146_ID);
    bsp_ft6146_reg_write_byte(FT6146_EREG_MODE_SW, (uint8_t[]){0x00}, 1);
    bsp_ft6146_reg_write_byte(FT6146_PERIODACTIVE, (uint8_t[]){0x14}, 1);
    bsp_ft6146_reg_write_byte(FT6146_PERIODMONITOR, (uint8_t[]){0x14}, 1);
    gpio_set_irq_enabled_with_callback(BSP_FT6146_INT_PIN, GPIO_IRQ_EDGE_FALL, true, gpio_irq_callbac);
}
