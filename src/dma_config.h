#pragma once

#include "pico/stdlib.h"
#include "ff.h" // For FIL

// typedef void (*sd_dma_callback_t)(void); // No longer needed

void init_sd_dma(void);
// bool start_sd_dma_read(FIL *file, uint8_t *buffer, size_t length, sd_dma_callback_t callback); // No longer needed 