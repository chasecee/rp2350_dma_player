#ifndef DISPLAY_MODULE_H
#define DISPLAY_MODULE_H

#include "pico/stdlib.h"
#include "../../libraries/bsp/bsp_co5300.h"          // For bsp_co5300_flush
#include "bsp_dma_channel_irq.h" // Explicitly include for channel_irq_callback_t
#include "sd_loader.h"           // For FRAME_WIDTH, FRAME_HEIGHT, and frame_buffers access

// Using existing definitions from main.c, ensure these are consistent
// If they change in main.c, they need to change here too, or be centralized.
#define DISPLAY_WIDTH 466
#define DISPLAY_HEIGHT 466
// FRAME_WIDTH and FRAME_HEIGHT are now sourced from sd_loader.h

extern volatile bool g_dma_transfer_complete;

void display_module_init(channel_irq_callback_t dma_callback);
void display_module_render_frame(const uint16_t *frame_buffer_data);
void display_module_dma_done_callback(void); // So main can pass it to bsp_co5300_init

#endif // DISPLAY_MODULE_H