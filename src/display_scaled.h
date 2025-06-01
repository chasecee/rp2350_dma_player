#ifndef DISPLAY_SCALED_H
#define DISPLAY_SCALED_H

#include <stdint.h>
#include <stdbool.h>

// Scaled display interface - handles 233x233 -> 466x466 scaling
typedef struct {
    int physical_width;   // Target display size (466)
    int physical_height;  // Target display size (466)
    int frame_width;      // Source frame size (233)
    int frame_height;     // Source frame size (233)
    volatile bool *dma_complete_flag;
} display_scaled_config_t;

// Initialize scaled display mode
bool display_scaled_init(const display_scaled_config_t *config);

// Display a frame from the given buffer with 2x2 scaling
void display_scaled_show_frame(const uint16_t *frame_buffer);

// Check if display is ready for next frame
bool display_scaled_is_ready(void);

// Allow SD loader to run between display batches (for cooperative multitasking)
void display_scaled_yield_to_loader(void);

#endif // DISPLAY_SCALED_H 