#include "common.h"

static uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

unsigned char kbd_us[128] =
{
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8',	/* 9 */
  '9', '0', '-', '=', '\b',	/* Backspace */
  '\t',			/* Tab */
  'q', 'w', 'e', 'r',	/* 19 */
  't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',	/* Enter key */
    0,			/* 29   - Control */
  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',	/* 39 */
 '\'', '`',   0,		/* Left shift */
 '\\', 'z', 'x', 'c', 'v', 'b', 'n',			/* 49 */
  'm', ',', '.', '/',   0,				/* Right shift */
  '*',
    0,	/* Alt */
  ' ',	/* Space bar */
    0,	/* Caps lock */
    0,	/* 59 - F1 key ... > */
    0,   0,   0,   0,   0,   0,   0,   0,
    0,	/* < ... F10 */
    0,	/* 69 - Num lock*/
    0,	/* Scroll Lock */
    0,	/* Home key */
    0,	/* Up Arrow */
    0,	/* Page Up */
  '-',
    0,	/* Left Arrow */
    0,
    0,	/* Right Arrow */
  '+',
    0,	/* 79 - End key*/
    0,	/* Down Arrow */
    0,	/* Page Down */
    0,	/* Insert Key */
    0,	/* Delete Key */
    0,   0,   0,
    0,	/* F11 Key */
    0,	/* F12 Key */
    0,	/* All other keys are undefined */
};

static char kbd_buffer[1024];
static int kbd_ptr = 0;
static bool shift_pressed = false;

// Scan code to ASCII map (Set 1) - Shifted
static const char kbd_us_shift[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

void keyboard_handler_manual(uint8_t scancode) {
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = true;
        return;
    }
    if (scancode == 0xAA || scancode == 0xB6) {
        shift_pressed = false;
        return;
    }

    if (scancode & 0x80) {
        // Key released
    } else {
        // Key pressed
        char c = shift_pressed ? kbd_us_shift[scancode] : kbd_us[scancode];
        if (c > 0) {
            if (c == '\b') {
                if (kbd_ptr > 0) {
                    kbd_ptr--;
                    kbd_buffer[kbd_ptr] = '\0';
                }
            } else {
                if (kbd_ptr < 1023) {
                    kbd_buffer[kbd_ptr++] = c;
                    kbd_buffer[kbd_ptr] = '\0';
                }
            }
        }
    }
}

const char* get_kbd_buffer() {
    return kbd_buffer;
}

static uint8_t mouse_cycle = 0;
static uint8_t mouse_byte[4];
static int32_t mouse_x = 400, mouse_y = 300;
static uint8_t mouse_buttons = 0;
static uint32_t mouse_max_x = 799;
static uint32_t mouse_max_y = 599;

uint8_t get_mouse_buttons() { return mouse_buttons; }

void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if (type == 0) {
        while (timeout--) {
            if ((inb(0x64) & 1) == 1) return;
        }
    } else {
        while (timeout--) {
            if ((inb(0x64) & 2) == 0) return;
        }
    }
}

void mouse_write(uint8_t write) {
    mouse_wait(1);
    outb(0x64, 0xD4);
    mouse_wait(1);
    outb(0x60, write);
}

uint8_t mouse_read() {
    uint32_t timeout = 100000;
    while (timeout--) {
        if (inb(0x64) & 1) return inb(0x60);
    }
    return 0;
}

uint8_t mouse_read_ack() {
    uint32_t timeout = 100000;
    while (timeout--) {
        if (inb(0x64) & 1) {
            uint8_t res = inb(0x60);
            if (res == 0xFA) return 1;
        }
    }
    return 0;
}

void ps2_install() {
    uint8_t status;
    
    // 1. Flush
    while (inb(0x64) & 1) inb(0x60);

    // 2. Enable Mouse Port
    mouse_wait(1);
    outb(0x64, 0xA8);

    // 3. Enable Interrupts & Clocks (Enable IRQ1, IRQ12, clear Disable bits)
    mouse_wait(1);
    outb(0x64, 0x20);
    mouse_wait(0);
    status = (inb(0x60) | 0x03); // Enable IRQ1 and IRQ12
    status &= ~0x30;            // Enable both clocks
    
    mouse_wait(1);
    outb(0x64, 0x60);
    mouse_wait(1);
    outb(0x60, status);

    // 4. Reset & Enable Reporting
    mouse_write(0xFF);
    for(int i=0; i<3; i++) mouse_read();
    
    mouse_write(0xF4);
    mouse_read();

    // Set bounds based on current framebuffer (if available).
    // Kernel sets global MBI before calling ps2_install().
    struct multiboot_info* mbi = (struct multiboot_info*)get_mbi_ptr();
    if (mbi && (mbi->flags & (1 << 12)) && mbi->framebuffer_width && mbi->framebuffer_height) {
        mouse_max_x = mbi->framebuffer_width - 5;
        mouse_max_y = mbi->framebuffer_height - 5;
        if (mouse_x > (int32_t)mouse_max_x) mouse_x = (int32_t)mouse_max_x;
        if (mouse_y > (int32_t)mouse_max_y) mouse_y = (int32_t)mouse_max_y;
    }
}

void keyboard_handler() {
    uint8_t scancode = inb(0x60);
    keyboard_handler_manual(scancode);
}

void mouse_handler() {
    while (inb(0x64) & 1) {
        uint8_t status = inb(0x64);
        uint8_t data = inb(0x60);

        if (!(status & 0x20)) {
            // Trimitem datele către noul handler de tastatură
            keyboard_handler_manual(data);
            continue; 
        }

        if (mouse_cycle == 0 && !(data & 0x08)) continue; 

        mouse_byte[mouse_cycle++] = data;

        if (mouse_cycle == 3) {
            mouse_cycle = 0;
            
            // Verificăm butoanele
            mouse_buttons = mouse_byte[0] & 0x07;

            // Procesare coordonate cu sign extension corect
            int32_t rel_x = (int32_t)mouse_byte[1];
            int32_t rel_y = (int32_t)mouse_byte[2];

            if (mouse_byte[0] & 0x10) rel_x -= 256;
            if (mouse_byte[0] & 0x20) rel_y -= 256;

            // Aplicăm mișcarea
            mouse_x += rel_x;
            mouse_y -= rel_y;

            // Limitări ecran
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x > (int32_t)mouse_max_x) mouse_x = (int32_t)mouse_max_x;
            if (mouse_y > (int32_t)mouse_max_y) mouse_y = (int32_t)mouse_max_y;
        }
    }
}

int32_t get_mouse_x() { return mouse_x; }
int32_t get_mouse_y() { return mouse_y; }
