#include "common.h"

static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void io_wait(void)
{
    // Classic I/O delay.
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0), "Nd"((uint16_t)0x80));
}

static void ata_read_words(uint16_t io_base, uint16_t* out256)
{
    for (int i = 0; i < 256; i++) {
        out256[i] = inw(io_base + 0);
    }
}

static void ata_model_from_words(const uint16_t* id, char out41[41])
{
    // Words 27..46: 40 ASCII chars, byte-swapped within each word.
    int o = 0;
    for (int w = 27; w <= 46; w++) {
        uint16_t v = id[w];
        out41[o++] = (char)((v >> 8) & 0xFF);
        out41[o++] = (char)(v & 0xFF);
    }
    out41[40] = '\0';

    // Trim trailing spaces.
    for (int i = 39; i >= 0; i--) {
        if (out41[i] == ' ' || out41[i] == '\0') out41[i] = '\0';
        else break;
    }
}

bool ata_identify_primary_master(struct ata_identify_info* out)
{
    if (!out) return false;
    out->present = false;
    out->total_sectors = 0;
    out->model[0] = '\0';

    const uint16_t io = 0x1F0;
    const uint16_t ctrl = 0x3F6;

    // Select drive: master on primary bus.
    outb(io + 6, 0xA0);
    io_wait();

    // If no device, status will be 0xFF on many systems.
    uint8_t st = inb(io + 7);
    if (st == 0xFF) return false;

    // Zero out regs.
    outb(io + 2, 0);
    outb(io + 3, 0);
    outb(io + 4, 0);
    outb(io + 5, 0);

    // IDENTIFY.
    outb(io + 7, 0xEC);
    io_wait();

    // Poll until DRQ or ERR; with timeout to avoid hang.
    for (uint32_t t = 0; t < 1000000; t++) {
        st = inb(io + 7);
        if (st & 0x01) return false;      // ERR
        if (st & 0x08) break;             // DRQ
    }
    if (!(st & 0x08)) return false;

    uint16_t id[256];
    ata_read_words(io, id);

    // Soft reset/disable IRQs (optional hygiene).
    outb(ctrl, 0x02);

    ata_model_from_words(id, out->model);

    // Prefer LBA48 if supported (word 83 bit 10).
    bool lba48 = (id[83] & (1u << 10)) != 0;
    uint64_t sectors = 0;
    if (lba48) {
        sectors =
            ((uint64_t)id[103] << 48) |
            ((uint64_t)id[102] << 32) |
            ((uint64_t)id[101] << 16) |
            ((uint64_t)id[100] << 0);
    } else {
        sectors = ((uint64_t)id[61] << 16) | (uint64_t)id[60];
    }

    if (sectors == 0) return false;

    out->present = true;
    out->total_sectors = sectors;
    return true;
}

