#include "common.h"

void draw_hex(struct multiboot_info* mbi, int x, int y, uint32_t value, uint32_t color, int scale) {
    for (int i = 0; i < 8; i++) {
        int digit = (value >> ((7 - i) * 4)) & 0x0F;
        // Desenăm o reprezentare simplă (pătrățele colorate sau litere dacă avem font)
        // Pentru simplitate, folosim draw_char dacă avem mapate toate cifrele, 
        // dar momentan desenăm doar mici indicatori.
        draw_rect(mbi, x + i * 10 * scale, y, 8 * scale, 8 * scale, (digit + 1) * 0x00111111 + color);
    }
}

void usb_init() {
    void* mbi = get_mbi_ptr();
    int y_offset = 100;
    
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                uint32_t vendor_device = pci_config_read(bus, slot, func, 0);
                if ((vendor_device & 0xFFFF) == 0xFFFF) continue;

                uint32_t class_rev = pci_config_read(bus, slot, func, 0x08);
                uint8_t class_code = (class_rev >> 24) & 0xFF;
                uint8_t sub_class = (class_rev >> 16) & 0xFF;
                uint8_t prog_if = (class_rev >> 8) & 0xFF;

                if (class_code == 0x0C && sub_class == 0x03) {
                    // Afișăm Vendor/Device ID pentru diagnostic
                    if (mbi) {
                        draw_hex(mbi, 10, y_offset, vendor_device, 0x00FFFFFF, 1);
                        y_offset += 15;
                    }

                    if (prog_if == 0x20) { // EHCI
                        // EHCI Handover - Dezactivat momentan pentru a nu pierde emularea BIOS
                        // Dell Vostro 3350 are nevoie de BIOS emulation dacă nu avem un driver USB complet.
                        /*
                        uint32_t eecp_reg = pci_config_read(bus, slot, func, 0x68);
                        uint8_t eecp = (eecp_reg & 0xFF);
                        if (eecp >= 0x40) {
                            uint32_t legsup = pci_config_read(bus, slot, func, eecp);
                            pci_config_write(bus, slot, func, eecp, legsup | (1 << 24));
                            if (mbi) draw_rect(mbi, 150, y_offset - 15, 10, 10, 0x0000FFFF);
                        }
                        */
                        if (mbi) draw_rect(mbi, 150, y_offset - 15, 10, 10, 0x00FFFF00); // GALBEN = EHCI găsit, dar nepreluat
                    }
                }
            }
        }
    }
}
