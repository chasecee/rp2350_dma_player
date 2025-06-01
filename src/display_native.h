#ifndef DISPLAY_NATIVE_H
#define DISPLAY_NATIVE_H

#include <stdint.h>
#include <stdbool.h>

// Native display interface - handles 233x233 centered on 466x466 display
typedef struct {
    int physical_width;
    int physical_height;
    int frame_width;
    int frame_height;
    volatile bool *dma_complete_flag;
} display_native_config_t;

// Initialize native display mode
bool display_native_init(const display_native_config_t *config);

// Display a frame from the given buffer
void display_native_show_frame(const uint16_t *frame_buffer);

// Check if display is ready for next frame
bool display_native_is_ready(void);

#endif // DISPLAY_NATIVE_H 