#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

// Display modes
typedef enum {
    DISPLAY_MODE_NATIVE,  // 233x233 centered on 466x466
    DISPLAY_MODE_SCALED   // 233x233 scaled to 466x466
} display_mode_t;

// Display configuration
typedef struct {
    display_mode_t mode;
    int physical_width;   // 466
    int physical_height;  // 466
    int frame_width;      // 233
    int frame_height;     // 233
    volatile bool *dma_complete_flag;
} display_config_t;

// Initialize display manager with the specified mode
bool display_manager_init(const display_config_t *config);

// Display a frame using the configured mode
void display_manager_show_frame(const uint16_t *frame_buffer);

// Check if display is ready for next frame
bool display_manager_is_ready(void);

// Get current display mode
display_mode_t display_manager_get_mode(void);

#endif // DISPLAY_MANAGER_H 