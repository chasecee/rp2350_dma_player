#include "hardware/dma.h"
// #include "hardware/irq.h" // No longer directly used for IRQ setup
#include "pico/stdlib.h"
// #include "ff.h"         // No longer needed here
#include "dma_config.h"          // Include the header for DMA configuration
#include <stdio.h>               // For debug prints
#include "bsp_dma_channel_irq.h" // May still be used by other modules

// DMA channel placeholder - init_sd_dma might be called by other things,
// but it's not strictly for SD card file reading anymore.
static int general_purpose_dma_channel;

// Structure to hold DMA transfer information - NO LONGER USED FOR SD CARD
// typedef struct
// {
//     uint8_t *buffer;
//     size_t length;
//     sd_dma_callback_t callback;
//     bool transfer_complete;
// } sd_dma_transfer_t;

// Global variable to hold the current transfer - NO LONGER USED FOR SD CARD
// static sd_dma_transfer_t current_transfer;

// static volatile uint8_t mock_dma_destination_byte; // No longer needed

// Function to start a DMA transfer for SD card reads - REMOVED
// bool start_sd_dma_read(FIL *file, uint8_t *buffer, size_t length, sd_dma_callback_t callback)
// {
//    ...
// }

// void sd_dma_complete_callback(void) // REMOVED
// {
//    ...
// }

void init_sd_dma(void) // Now a more general DMA init, if needed elsewhere.
{
    printf("Initializing a general purpose DMA channel (if unused by others)...\n");
    general_purpose_dma_channel = dma_claim_unused_channel(true);
    // dma_channel_config c = dma_channel_get_default_config(general_purpose_dma_channel);
    // channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    // dma_channel_configure(
    //     general_purpose_dma_channel,
    //     &c,
    //     NULL,
    //     NULL,
    //     0,
    //     false
    // );

    // The bsp_dma_channel_irq_add is no longer relevant for SD card reading here,
    // as f_read() is blocking and we won't use a mock DMA for its completion.
    // If other modules need shared IRQ1 handling for this general_purpose_dma_channel,
    // they would call bsp_dma_channel_irq_add themselves.
    // bsp_dma_channel_irq_add(1, general_purpose_dma_channel, some_other_callback_if_needed);

    printf("General purpose DMA channel %d claimed. Configure and use as needed by other modules.\n", general_purpose_dma_channel);
    // IMPORTANT: The SD card library (no-OS-FatFS-SD-SDIO-SPI-RPi-Pico)
    // internally handles its own DMA channel claiming and configuration for SPI transfers.
    // This init_sd_dma function is now mostly vestigial unless some *other* part of your
    // application wants to pre-claim a DMA channel through it.
}