#include <stdint.h>

/* VGA attribute controller / input status: port 0x3DA (read).
 * Bit 3 set during vertical retrace. Works with many VGA/VBE linear setups (e.g. QEMU);
 * on some bare-metal configs this may be a no-op for tearing — still cheap to try. */

static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

void wait_vblank(void)
{
    while (inb(0x3DA) & 0x08) {
    }
    while (!(inb(0x3DA) & 0x08)) {
    }
}
