#include "bsp_dma_channel_irq.h"
#include "hardware/irq.h"
#include "hardware/dma.h"

typedef struct
{
    channel_irq_callback_t callback[13];
    uint dma_channel[13];
    uint16_t len;
} bsp_channel_irq_info_t;

static bsp_channel_irq_info_t g_irq0_info = {.len = 0};
static bsp_channel_irq_info_t g_irq1_info = {.len = 0};

void bsp_dma_channel_irq_handler(void)
{
    for (int i = 0; i < g_irq0_info.len; i++)
    {
        if (dma_channel_get_irq0_status(g_irq0_info.dma_channel[i]))
        {
            dma_channel_acknowledge_irq0(g_irq0_info.dma_channel[i]);
            if (g_irq0_info.callback[i])
            {
                g_irq0_info.callback[i]();
            }
        }
    }

    for (int i = 0; i < g_irq1_info.len; i++)
    {
        if (dma_channel_get_irq1_status(g_irq1_info.dma_channel[i]))
        {
            dma_channel_acknowledge_irq1(g_irq1_info.dma_channel[i]);
            if (g_irq1_info.callback[i])
            {
                g_irq1_info.callback[i]();
            }
        }
    }
}

void bsp_dma_channel_irq_add(uint8_t irq_num, uint dma_channel, channel_irq_callback_t callback)
{
    printf("bsp_dma_channel_irq_add: irq_num=%d, dma_channel=%d\n", irq_num, dma_channel);
    if (NULL == callback)
    {
        printf("callback is NULL!\r\n");
        return;
    }

    if (0 == irq_num)
    {
        if (g_irq0_info.len < 12)
        {
            dma_channel_set_irq0_enabled(dma_channel, true);
            g_irq0_info.dma_channel[g_irq0_info.len] = dma_channel;
            g_irq0_info.callback[g_irq0_info.len] = callback;
            g_irq0_info.len++;
        }
        else
        {
            printf("Max callbacks for IRQ0 reached\r\n");
        }
    }
    else if (1 == irq_num)
    {
        if (g_irq1_info.len < 12)
        {
            dma_channel_set_irq1_enabled(dma_channel, true);
            g_irq1_info.dma_channel[g_irq1_info.len] = dma_channel;
            g_irq1_info.callback[g_irq1_info.len] = callback;
            g_irq1_info.len++;
        }
        else
        {
            printf("Max callbacks for IRQ1 reached\r\n");
        }
    }
    else
    {
        printf("Unsupported DMA IRQ number: %d\r\n", irq_num);
    }
}

void bsp_dma_channel_irq0_init(void)
{
}

void bsp_dma_channel_irq1_init(void)
{
}
