/* Host stand-in. vTaskDelay does not wait: display_flash_count() would
 * otherwise cost the suite six seconds, and what is checked is what lands in
 * the framebuffer, not how long it stayed. It does tell hostgfx it was
 * called, and for how long, which is how preview.c captures an animation:
 * every delay is a frame, and its length is how long that frame is up. */
#ifndef LNURLVAULT_HOSTGFX_TASK_H
#define LNURLVAULT_HOSTGFX_TASK_H

void hostgfx_delay(unsigned int ticks);

static inline void vTaskDelay(unsigned int ticks) {
    hostgfx_delay(ticks);
}

#endif
