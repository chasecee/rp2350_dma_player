#include "display_manager.h"
#include "display_native.h"
#include "display_scaled.h"
#include "debug.h"

// Internal state
static display_config_t config;
static bool initialized = false;

bool display_manager_init(const display_config_t *cfg)
{
    if (!cfg || !cfg->dma_complete_flag)
    {
        return false;
    }

    config = *cfg;

    DBG_PRINTF("DISPLAY_MANAGER: Initializing %s mode\n",
               (config.mode == DISPLAY_MODE_NATIVE) ? "NATIVE" : "SCALED");

    bool success = false;

    if (config.mode == DISPLAY_MODE_NATIVE)
    {
        display_native_config_t native_cfg = {
            .physical_width = config.physical_width,
            .physical_height = config.physical_height,
            .frame_width = config.frame_width,
            .frame_height = config.frame_height,
            .dma_complete_flag = config.dma_complete_flag};
        success = display_native_init(&native_cfg);
    }
    else
    {
        display_scaled_config_t scaled_cfg = {
            .physical_width = config.physical_width,
            .physical_height = config.physical_height,
            .frame_width = config.frame_width,
            .frame_height = config.frame_height,
            .dma_complete_flag = config.dma_complete_flag};
        success = display_scaled_init(&scaled_cfg);

        // DISABLED: No longer using cooperative multitasking during display
        // This was causing choppy playback by interrupting frame display
        /*
        if (success)
        {
            display_scaled_yield_to_loader();
        }
        */
    }

    if (success)
    {
        initialized = true;
        DBG_PRINTF("DISPLAY_MANAGER: %s mode initialized successfully\n",
                   (config.mode == DISPLAY_MODE_NATIVE) ? "NATIVE" : "SCALED");
    }
    else
    {
        DBG_PRINTF("DISPLAY_MANAGER: Failed to initialize %s mode\n",
                   (config.mode == DISPLAY_MODE_NATIVE) ? "NATIVE" : "SCALED");
    }

    return success;
}

void display_manager_show_frame(const uint16_t *frame_buffer)
{
    if (!initialized || !frame_buffer)
    {
        return;
    }

    if (config.mode == DISPLAY_MODE_NATIVE)
    {
        display_native_show_frame(frame_buffer);
    }
    else
    {
        display_scaled_show_frame(frame_buffer);
    }
}

bool display_manager_is_ready(void)
{
    if (!initialized)
    {
        return false;
    }

    if (config.mode == DISPLAY_MODE_NATIVE)
    {
        return display_native_is_ready();
    }
    else
    {
        return display_scaled_is_ready();
    }
}

display_mode_t display_manager_get_mode(void)
{
    return config.mode;
}