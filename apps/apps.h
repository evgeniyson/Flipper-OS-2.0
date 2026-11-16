#pragma once
#include "../app/options.h"
#include "../app/rx.h"
#include "../app/settings.h"
#include "../app/tx.h"
#include "../app/ui.h"
#include "../core/fmt.h"
#include "../core/util.h"
#include "../hal/hal.h"
#include "../hal/storage.h"
#include "../subghz/analyzer.h"
#include "../subghz/protocol.h"
#include "../subghz/region.h"
#include "../subghz/sub_file.h"
#include <string.h>

#define FOS_VERSION "0.1"
#define SUBGHZ_DIR  "/subghz"

extern const Screen menu_screen;
extern const Screen spectrum_screen;
extern const Screen finder_screen;
extern const Screen receiver_screen;
extern const Screen recorder_screen;
extern const Screen saved_screen;
extern const Screen settings_screen;
extern const Screen radio_config_screen; /* frequency / preset / hopping */

/* Open the signal analyser on a copy of this packet. */
void analyzer_open(const SgPacket* pkt);

/* Step a frequency: through the list of common frequencies, or by 10 kHz
 * when `fine`. Stays inside the tunable bands. */
void freq_step(uint32_t* hz, int dir, bool fine);
/* SubWriteFn adapter: ctx is a HalFile*. */
int app_file_write(void* file, const char* data, size_t len);
/* "433.92 AM650" for the current settings */
void radio_summary(char* buf, size_t size, uint32_t hz);
