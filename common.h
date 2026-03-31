#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint32_t framebuffer_addr_low;
    uint32_t framebuffer_addr_high;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
} __attribute__((packed));

// Kernel functions
void terminal_initialize(void);
void terminal_putchar(char c);
void terminal_writestring(const char* data);
void draw_pixel(struct multiboot_info* mbi, uint32_t x, uint32_t y, uint32_t color);
void draw_rect(struct multiboot_info* mbi, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void draw_char(struct multiboot_info* mbi, int x, int y, int char_idx, uint32_t color, int scale);
void draw_string(struct multiboot_info* mbi, int x, int y, const char* s, uint32_t color, int scale);
void swap_buffers(struct multiboot_info* mbi);
void wait_vblank(void);
void* get_mbi_ptr();

// PCI functions
uint32_t pci_config_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);

// Input functions
void ps2_install();
void keyboard_handler();
void mouse_handler();
int32_t get_mouse_x();
int32_t get_mouse_y();
uint32_t get_mouse_interrupt_count();
uint8_t get_mouse_byte(int i);
uint8_t get_ps2_status();
uint8_t get_mouse_buttons();
const char* get_kbd_buffer();

// USB functions
void usb_init();

// ATA (Storage) - minimal identify for capacity
struct ata_identify_info {
    bool present;
    uint64_t total_sectors;
    char model[41];
};
bool ata_identify_primary_master(struct ata_identify_info* out);

// GDT/IDT functions
void gdt_install();
void idt_install();

#endif
