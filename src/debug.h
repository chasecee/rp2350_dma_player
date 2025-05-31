#pragma once

#ifdef ENABLE_DEBUG_PRINT
#define DBG_PRINTF(...) printf(__VA_ARGS__)
#else
#define DBG_PRINTF(...) ((void)0)
#endif