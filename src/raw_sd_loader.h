#ifndef RAW_SD_LOADER_H
#define RAW_SD_LOADER_H

#include <stdint.h>
#include <stdbool.h>

// Frame dimensions (must match main.c)
#ifndef FRAME_WIDTH
#define FRAME_WIDTH 233
#endif
#ifndef FRAME_HEIGHT
#define FRAME_HEIGHT 233
#endif

// External buffer storage (defined in raw_sd_loader.c)
extern uint16_t frame_buffers[2][FRAME_HEIGHT * FRAME_WIDTH];
extern volatile bool buffer_ready[2];
extern volatile int target_frame_for_buffer[2];

// Raw SD loader interface
void raw_sd_loader_init(int total_frames);
void raw_sd_loader_process(void);
void raw_sd_loader_mark_buffer_consumed(int buffer_idx, int next_target_frame);
int raw_sd_loader_get_target_frame_for_buffer(int buffer_idx);

#endif // RAW_SD_LOADER_H