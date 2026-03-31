#include "common.h"

/* Hardware text mode color constants. */
enum vga_color {
	VGA_COLOR_BLACK = 0,
	VGA_COLOR_BLUE = 1,
	VGA_COLOR_GREEN = 2,
	VGA_COLOR_CYAN = 3,
	VGA_COLOR_RED = 4,
	VGA_COLOR_MAGENTA = 5,
	VGA_COLOR_BROWN = 6,
	VGA_COLOR_LIGHT_GREY = 7,
	VGA_COLOR_DARK_GREY = 8,
	VGA_COLOR_LIGHT_BLUE = 9,
	VGA_COLOR_LIGHT_GREEN = 10,
	VGA_COLOR_LIGHT_CYAN = 11,
	VGA_COLOR_LIGHT_RED = 12,
	VGA_COLOR_LIGHT_MAGENTA = 13,
	VGA_COLOR_LIGHT_BROWN = 14,
	VGA_COLOR_WHITE = 15,
};

static inline uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg) {
	return fg | bg << 4;
}

static inline uint16_t vga_entry(unsigned char uc, uint8_t color) {
	return (uint16_t) uc | (uint16_t) color << 8;
}

size_t strlen(const char* str) {
	size_t len = 0;
	while (str[len])
		len++;
	return len;
}

static bool streq(const char* a, const char* b) {
    if (!a || !b) return false;
    size_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return false;
        i++;
    }
    return a[i] == b[i];
}

static void u64_to_dec(uint64_t v, char* out, int out_cap)
{
    if (!out || out_cap <= 0) return;
    if (out_cap == 1) { out[0] = '\0'; return; }
    if (v == 0) { out[0] = '0'; out[1] = '\0'; return; }
    char tmp[32];
    int tl = 0;
    while (v > 0 && tl < (int)sizeof(tmp)) {
        tmp[tl++] = (char)('0' + (uint32_t)(v % 10ULL));
        v /= 10ULL;
    }
    int p = 0;
    for (int i = tl - 1; i >= 0 && p < out_cap - 1; i--) out[p++] = tmp[i];
    out[p] = '\0';
}

static uint64_t u64_div_u32(uint64_t n, uint32_t d, uint32_t* rem_out)
{
    // Long division (shift/subtract). Avoids libgcc __udivdi3.
    if (d == 0) { if (rem_out) *rem_out = 0; return 0; }
    uint64_t q = 0;
    uint64_t r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1ULL);
        if (r >= (uint64_t)d) {
            r -= (uint64_t)d;
            q |= (1ULL << i);
        }
    }
    if (rem_out) *rem_out = (uint32_t)r;
    return q;
}

static void cpu_get_brand(char out[49])
{
    uint32_t max_ext = 0;
    __asm__ volatile("cpuid" : "=a"(max_ext) : "a"(0x80000000) : "ebx", "ecx", "edx");
    for (int i = 0; i < 48; i++) out[i] = '\0';
    out[48] = '\0';
    if (max_ext < 0x80000004) return;

    uint32_t a, b, c, d;
    int p = 0;
    for (uint32_t leaf = 0x80000002; leaf <= 0x80000004; leaf++) {
        __asm__ volatile("cpuid"
                         : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                         : "a"(leaf));
        uint32_t regs[4] = {a, b, c, d};
        for (int r = 0; r < 4; r++) {
            uint32_t v = regs[r];
            out[p++] = (char)(v & 0xFF);
            out[p++] = (char)((v >> 8) & 0xFF);
            out[p++] = (char)((v >> 16) & 0xFF);
            out[p++] = (char)((v >> 24) & 0xFF);
        }
    }
    out[48] = '\0';
    int start = 0;
    while (out[start] == ' ') start++;
    if (start > 0) {
        int i = 0;
        while (out[start] && i < 48) out[i++] = out[start++];
        out[i] = '\0';
    }
}

static uint32_t desktop_bg_color = 0x00336699;
static uint8_t desktop_bg_mode = 0; // 0 solid, 1 checker, 2 stripes

void fill_screen(struct multiboot_info* mbi, uint32_t color);

static void draw_background(struct multiboot_info* mbi)
{
    if (desktop_bg_mode == 0) {
        fill_screen(mbi, desktop_bg_color);
        return;
    }
    if (desktop_bg_mode == 1) {
        uint32_t c1 = desktop_bg_color;
        uint32_t c2 = (desktop_bg_color ^ 0x00111111);
        for (uint32_t y = 0; y < mbi->framebuffer_height; y += 32) {
            for (uint32_t x = 0; x < mbi->framebuffer_width; x += 32) {
                uint32_t sel = (((x >> 5) ^ (y >> 5)) & 1) ? c1 : c2;
                draw_rect(mbi, x, y, 32, 32, sel);
            }
        }
        return;
    }
    uint32_t base = desktop_bg_color;
    for (uint32_t x = 0; x < mbi->framebuffer_width; x += 16) {
        uint32_t v = (x & 32) ? (base ^ 0x00080808) : base;
        draw_rect(mbi, x, 0, 16, mbi->framebuffer_height, v);
    }
}

static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;

size_t terminal_row;
size_t terminal_column;
uint8_t terminal_color;
uint16_t* terminal_buffer;

void terminal_initialize(void) {
	terminal_row = 0;
	terminal_column = 0;
	terminal_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
	terminal_buffer = (uint16_t*) 0xB8000;
	for (size_t y = 0; y < VGA_HEIGHT; y++) {
		for (size_t x = 0; x < VGA_WIDTH; x++) {
			const size_t index = y * VGA_WIDTH + x;
			terminal_buffer[index] = vga_entry(' ', terminal_color);
		}
	}
}

void terminal_setcolor(uint8_t color) {
	terminal_color = color;
}

void terminal_putentryat(char c, uint8_t color, size_t x, size_t y) {
	const size_t index = y * VGA_WIDTH + x;
	terminal_buffer[index] = vga_entry(c, color);
}

void terminal_putchar(char c) {
	terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
	if (++terminal_column == VGA_WIDTH) {
		terminal_column = 0;
		if (++terminal_row == VGA_HEIGHT)
			terminal_row = 0;
	}
}

void terminal_write(const char* data, size_t size) {
	for (size_t i = 0; i < size; i++)
		terminal_putchar(data[i]);
}

void terminal_writestring(const char* data) {
	terminal_write(data, strlen(data));
}

extern void gdt_install();
extern void idt_install();
extern uint32_t kernel_end;

static uint32_t* back_buffer = (uint32_t*)0x2000000; // Alocăm buffer-ul la 32MB (după kernel)

void swap_buffers(struct multiboot_info* mbi) {
    uint32_t* fb = (uint32_t*)(uint32_t)mbi->framebuffer_addr_low;
    uint32_t size = mbi->framebuffer_width * mbi->framebuffer_height;
    for (uint32_t i = 0; i < size; i++) {
        fb[i] = back_buffer[i];
    }
}

void draw_pixel(struct multiboot_info* mbi, uint32_t x, uint32_t y, uint32_t color) {
    if (x >= mbi->framebuffer_width || y >= mbi->framebuffer_height) return;
    back_buffer[y * mbi->framebuffer_width + x] = color;
}

void fill_screen(struct multiboot_info* mbi, uint32_t color) {
    uint32_t size = mbi->framebuffer_width * mbi->framebuffer_height;
    for (uint32_t i = 0; i < size; i++) {
        back_buffer[i] = color;
    }
}

void draw_rect(struct multiboot_info* mbi, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t i = 0; i < h; i++) {
        for (uint32_t j = 0; j < w; j++) {
            draw_pixel(mbi, x + j, y + i, color);
        }
    }
}

static struct multiboot_info* global_mbi = NULL;

void* get_mbi_ptr() {
    return (void*)global_mbi;
}

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ( "outl %0, %1" : : "a"(val), "Nd"(port) );
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ( "inl %1, %0" : "=a"(ret) : "Nd"(port) );
    return ret;
}

void pci_config_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) |
              (func << 8) | (offset & 0xfc) | ((uint32_t)0x80000000));
    outl(0xCF8, address);
    outl(0xCFC, val);
}

uint32_t pci_config_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address;
    uint32_t lbus  = (uint32_t)bus;
    uint32_t lslot = (uint32_t)slot;
    uint32_t lfunc = (uint32_t)func;
    uint32_t tmp = 0;

    address = (uint32_t)((lbus << 16) | (lslot << 11) |
              (lfunc << 8) | (offset & 0xfc) | ((uint32_t)0x80000000));

    outl(0xCF8, address);
    tmp = (uint32_t)(inl(0xCFC));
    return tmp;
}

// Full ASCII 8x8 font.
#include "font8x8_basic.h"

void draw_string(struct multiboot_info* mbi, int x, int y, const char* s, uint32_t color, int scale) {
    for (int i = 0; s[i] != '\0'; i++) {
        uint8_t c = (uint8_t)s[i];
        if (c < 128) draw_char(mbi, x + i * 8 * scale, y, c, color, scale);
    }
}

void draw_char(struct multiboot_info* mbi, int x, int y, int char_idx, uint32_t color, int scale) {
    if (char_idx < 0 || char_idx >= 128) return;
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            if (font_8x8[char_idx][i] & (1 << (7 - j))) {
                draw_rect(mbi, x + j * scale, y + i * scale, scale, scale, color);
            }
        }
    }
}

struct window {
    int x, y, w, h;
    bool active;
    bool minimized;
    bool maximized;
    int old_x, old_y, old_w, old_h;
    const char* title;
};

static struct window windows[8] = {
    {100, 100, 300, 200, false, false, false, 0, 0, 0, 0, "Notepad"},
    {150, 150, 300, 200, false, false, false, 0, 0, 0, 0, "Calc"},
    {200, 200, 300, 200, false, false, false, 0, 0, 0, 0, "Settings"},
    {250, 100, 420, 300, false, false, false, 0, 0, 0, 0, "Device Manager"},
    {120, 120, 420, 320, false, false, false, 0, 0, 0, 0, "Breakout"},
    {180, 140, 420, 320, false, false, false, 0, 0, 0, 0, "Storage"},
    {220, 120, 460, 340, false, false, false, 0, 0, 0, 0, "Task Manager"},
    {260, 150, 460, 340, false, false, false, 0, 0, 0, 0, "Personalize"}
};

void draw_window(struct multiboot_info* mbi, struct window* win, uint32_t timer_ticks) {
    if (!win->active || win->minimized) return;
    
    // Window Shadow/Border
    draw_rect(mbi, win->x, win->y, win->w, win->h, 0x00888888);
    draw_rect(mbi, win->x + 1, win->y + 1, win->w - 2, win->h - 2, 0x00CCCCCC);
    
    // Title Bar
    draw_rect(mbi, win->x, win->y, win->w, 22, 0x000000AA);
    draw_string(mbi, win->x + 5, win->y + 5, win->title, 0x00FFFFFF, 1);
    
    // Control Buttons
    draw_rect(mbi, win->x + win->w - 20, win->y + 2, 18, 18, 0x00AA0000);
    draw_string(mbi, win->x + win->w - 14, win->y + 5, "X", 0x00FFFFFF, 1);
    
    draw_rect(mbi, win->x + win->w - 40, win->y + 2, 18, 18, 0x00555555);
    draw_rect(mbi, win->x + win->w - 35, win->y + 7, 8, 8, 0x00FFFFFF);
    draw_rect(mbi, win->x + win->w - 34, win->y + 8, 6, 6, 0x00555555);

    draw_rect(mbi, win->x + win->w - 60, win->y + 2, 18, 18, 0x00555555);
    draw_rect(mbi, win->x + win->w - 55, win->y + 14, 8, 2, 0x00FFFFFF);

    // Content Area
    draw_rect(mbi, win->x + 2, win->y + 22, win->w - 4, win->h - 24, 0x00FFFFFF);

    // App Content
    if (streq(win->title, "Notepad")) {
        extern const char* get_kbd_buffer();
        const char* text = get_kbd_buffer();
        
        // Suport pentru rânduri noi (Enter)
        int text_x = win->x + 10;
        int text_y = win->y + 30;
        int line_start = 0;
        int i = 0;
        
        while (text[i] != '\0') {
            if (text[i] == '\n') {
                // Desenăm linia curentă până la \n
                char temp[100];
                int len = i - line_start;
                if (len > 99) len = 99;
                for(int j=0; j<len; j++) temp[j] = text[line_start+j];
                temp[len] = '\0';
                
                draw_string(mbi, text_x, text_y, temp, 0x00000000, 1);
                text_y += 15; // Trecem pe rândul următor
                line_start = i + 1;
            }
            i++;
        }
        
        // Desenăm ultima linie (cea fără \n la final)
        draw_string(mbi, text_x, text_y, &text[line_start], 0x00000000, 1);
        
        // Blinking Cursor | la finalul ultimei linii
        if ((timer_ticks / 20) % 2 == 0) {
            int last_line_len = i - line_start;
            draw_string(mbi, text_x + last_line_len * 8, text_y, "|", 0x00000000, 1);
        }
    } else if (streq(win->title, "Calc")) {
        draw_string(mbi, win->x + 10, win->y + 30, "7 8 9 /", 0x00000000, 1);
        draw_string(mbi, win->x + 10, win->y + 50, "4 5 6 *", 0x00000000, 1);
        draw_string(mbi, win->x + 10, win->y + 70, "1 2 3 -", 0x00000000, 1);
        draw_string(mbi, win->x + 10, win->y + 90, "0 . = +", 0x00000000, 1);
    } else if (streq(win->title, "Settings")) {
        draw_string(mbi, win->x + 10, win->y + 30, "Volume: [||||||  ]", 0x00000000, 1);
        draw_string(mbi, win->x + 10, win->y + 50, "Bright: [||||||||]", 0x00000000, 1);
    } else if (streq(win->title, "Device Manager")) {
        draw_string(mbi, win->x + 10, win->y + 30, "Scanning PCI Bus...", 0x00000000, 1);
        
        int text_y = 50;
        int devices_found = 0;
        
        // Scanăm un subset mic al magistralei PCI (Bus 0, Slot 0-31) pentru a nu bloca kernelul
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t vendor_device = pci_config_read(0, slot, 0, 0);
            if (vendor_device != 0xFFFFFFFF) {
                uint16_t vendor_id = vendor_device & 0xFFFF;
                uint16_t device_id = (vendor_device >> 16) & 0xFFFF;
                
                char dev_str[50];
                int idx = 0;
                
                // Conversie simplă Hex -> String manuală
                const char hex_chars[] = "0123456789ABCDEF";
                
                // Construim stringul: "Bus 0 Slot X: "
                dev_str[idx++] = 'S'; dev_str[idx++] = 'l'; dev_str[idx++] = 'o'; dev_str[idx++] = 't'; dev_str[idx++] = ' ';
                dev_str[idx++] = hex_chars[(slot >> 4) & 0xF];
                dev_str[idx++] = hex_chars[slot & 0xF];
                dev_str[idx++] = ':'; dev_str[idx++] = ' ';
                
                // Nume Hardware Cunoscut
                bool is_known = false;
                
                if (vendor_id == 0x8086) { // Intel
                    if (device_id == 0x0116 || device_id == 0x0126) {
                        const char* name = "Intel HD Graphics 3000";
                        for(int k=0; name[k]; k++) dev_str[idx++] = name[k];
                        is_known = true;
                        
                        // Citim BAR0 (Base Address Register 0) la offset-ul 0x10
                        uint32_t bar0 = pci_config_read(0, slot, 0, 0x10);
                        dev_str[idx++] = ' '; dev_str[idx++] = '[';
                        dev_str[idx++] = hex_chars[(bar0 >> 28) & 0xF];
                        dev_str[idx++] = hex_chars[(bar0 >> 24) & 0xF];
                        dev_str[idx++] = hex_chars[(bar0 >> 20) & 0xF];
                        dev_str[idx++] = hex_chars[(bar0 >> 16) & 0xF];
                        dev_str[idx++] = '0'; dev_str[idx++] = '0'; dev_str[idx++] = '0'; dev_str[idx++] = '0'; // Lower 16 bits are usually 0 for memory
                        dev_str[idx++] = ']';
                        
                    } else if (device_id == 0x0104 || device_id == 0x0100) {
                        const char* name = "Intel Sandy Bridge Host Bridge";
                        for(int k=0; name[k]; k++) dev_str[idx++] = name[k];
                        is_known = true;
                    } else if (device_id == 0x1C2D || device_id == 0x1C26) {
                        const char* name = "Intel EHCI USB Controller";
                        for(int k=0; name[k]; k++) dev_str[idx++] = name[k];
                        is_known = true;
                    }
                }
                
                if (!is_known) {
                    // Vendor ID
                    dev_str[idx++] = hex_chars[(vendor_id >> 12) & 0xF];
                    dev_str[idx++] = hex_chars[(vendor_id >> 8) & 0xF];
                    dev_str[idx++] = hex_chars[(vendor_id >> 4) & 0xF];
                    dev_str[idx++] = hex_chars[vendor_id & 0xF];
                    dev_str[idx++] = ':';
                    
                    // Device ID
                    dev_str[idx++] = hex_chars[(device_id >> 12) & 0xF];
                    dev_str[idx++] = hex_chars[(device_id >> 8) & 0xF];
                    dev_str[idx++] = hex_chars[(device_id >> 4) & 0xF];
                    dev_str[idx++] = hex_chars[device_id & 0xF];
                }
                
                dev_str[idx] = '\0';
                
                draw_string(mbi, win->x + 10, win->y + text_y, dev_str, 0x000000AA, 1);
                text_y += 15;
                devices_found++;
                
                if (text_y > win->h - 20) break; // Nu desenăm în afara ferestrei
            }
        }
        
        if (devices_found == 0) {
            draw_string(mbi, win->x + 10, win->y + text_y, "No PCI devices found.", 0x00AA0000, 1);
        }
    } else if (streq(win->title, "Breakout")) {
        // Simple Breakout: mouse controls paddle in window.
        static bool inited = false;
        static int paddle_x = 0;
        static int ball_x = 0, ball_y = 0;
        static int ball_vx = 2, ball_vy = 2;
        static uint8_t bricks[6 * 10];
        static int score = 0;

        int play_x = win->x + 6;
        int play_y = win->y + 28;
        int play_w = win->w - 12;
        int play_h = win->h - 36;

        if (!inited) {
            inited = true;
            paddle_x = play_w / 2;
            ball_x = play_w / 2;
            ball_y = play_h / 2;
            ball_vx = 2; ball_vy = 2;
            score = 0;
            for (int i = 0; i < 60; i++) bricks[i] = 1;
        }

        // Input: follow mouse X if cursor is inside content area.
        int32_t mx = get_mouse_x();
        int32_t my = get_mouse_y();
        if (mx >= play_x && mx < play_x + play_w && my >= play_y && my < play_y + play_h) {
            paddle_x = (int)(mx - play_x);
        }

        // Update at a tame rate.
        if ((timer_ticks % 2) == 0) {
            ball_x += ball_vx;
            ball_y += ball_vy;

            if (ball_x < 2) { ball_x = 2; ball_vx = -ball_vx; }
            if (ball_x > play_w - 6) { ball_x = play_w - 6; ball_vx = -ball_vx; }
            if (ball_y < 2) { ball_y = 2; ball_vy = -ball_vy; }

            // Paddle collision.
            int paddle_w = 60;
            int paddle_y = play_h - 18;
            int px0 = paddle_x - paddle_w / 2;
            if (px0 < 2) px0 = 2;
            if (px0 > play_w - paddle_w - 2) px0 = play_w - paddle_w - 2;
            int px1 = px0 + paddle_w;
            if (ball_y >= paddle_y - 6 && ball_y <= paddle_y && ball_x >= px0 - 2 && ball_x <= px1 + 2) {
                ball_vy = -2;
                int hit = ball_x - (px0 + paddle_w / 2);
                if (hit < -15) ball_vx = -3;
                else if (hit > 15) ball_vx = 3;
                else ball_vx = (ball_vx < 0) ? -2 : 2;
            }

            // Brick collision.
            int cols = 10, rows = 6;
            int brick_w = play_w / cols;
            int brick_h = 14;
            int bx = ball_x;
            int by = ball_y;
            if (by < rows * brick_h) {
                int c = bx / brick_w;
                int r = by / brick_h;
                if (c >= 0 && c < cols && r >= 0 && r < rows) {
                    int idx = r * cols + c;
                    if (bricks[idx]) {
                        bricks[idx] = 0;
                        score++;
                        ball_vy = -ball_vy;
                    }
                }
            }

            // Lose condition: reset ball.
            if (ball_y > play_h - 4) {
                ball_x = play_w / 2;
                ball_y = play_h / 2;
                ball_vx = 2;
                ball_vy = 2;
            }
        }

        // Draw playfield.
        draw_rect(mbi, play_x, play_y, play_w, play_h, 0x00FFFFFF);
        draw_rect(mbi, play_x, play_y, play_w, 2, 0x00333333);
        draw_rect(mbi, play_x, play_y, 2, play_h, 0x00333333);
        draw_rect(mbi, play_x + play_w - 2, play_y, 2, play_h, 0x00333333);

        // Bricks.
        int cols = 10, rows = 6;
        int brick_w = play_w / cols;
        int brick_h = 14;
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                if (!bricks[r * cols + c]) continue;
                uint32_t col = 0x00AA0000;
                if (r == 1) col = 0x00AA5500;
                else if (r == 2) col = 0x00AAAA00;
                else if (r == 3) col = 0x0000AA00;
                else if (r == 4) col = 0x0000AAAA;
                else if (r == 5) col = 0x000000AA;
                int x0 = play_x + c * brick_w + 1;
                int y0 = play_y + r * brick_h + 1;
                draw_rect(mbi, x0, y0, brick_w - 2, brick_h - 2, col);
            }
        }

        // Paddle + ball.
        int paddle_w = 60;
        int paddle_y = play_y + play_h - 18;
        int px0 = paddle_x - paddle_w / 2;
        if (px0 < 2) px0 = 2;
        if (px0 > play_w - paddle_w - 2) px0 = play_w - paddle_w - 2;
        draw_rect(mbi, play_x + px0, paddle_y, paddle_w, 8, 0x002E7D32);
        draw_rect(mbi, play_x + ball_x, play_y + ball_y, 6, 6, 0x00000000);

        // Score.
        char s[32];
        int idx = 0;
        const char* prefix = "Score ";
        for (int i = 0; prefix[i]; i++) s[idx++] = prefix[i];
        int v = score;
        if (v == 0) s[idx++] = '0';
        else {
            char tmp[10];
            int tl = 0;
            while (v > 0 && tl < 10) { tmp[tl++] = (char)('0' + (v % 10)); v /= 10; }
            for (int i = tl - 1; i >= 0; i--) s[idx++] = tmp[i];
        }
        s[idx] = '\0';
        draw_string(mbi, win->x + 10, win->y + 30, s, 0x00000000, 1);

    } else if (streq(win->title, "Storage")) {
        // Minimal "explorer": show detected disk size + model.
        static bool did = false;
        static struct ata_identify_info info;
        if (!did) {
            did = true;
            ata_identify_primary_master(&info);
        }

        draw_string(mbi, win->x + 10, win->y + 30, "Disk (ATA Primary Master):", 0x00000000, 1);
        if (!info.present) {
            draw_string(mbi, win->x + 10, win->y + 50, "Not detected (or not ATA).", 0x00AA0000, 1);
            draw_string(mbi, win->x + 10, win->y + 70, "Tip: try QEMU with an IDE disk.", 0x00000000, 1);
        } else {
            draw_string(mbi, win->x + 10, win->y + 50, info.model[0] ? info.model : "Unknown model", 0x00000000, 1);

            char line[64];
            int p = 0;
            const char* a = "Total ";
            for (int i = 0; a[i]; i++) line[p++] = a[i];
            char sec_s[32];
            u64_to_dec(info.total_sectors, sec_s, (int)sizeof(sec_s));
            for (int i = 0; sec_s[i] && p < (int)sizeof(line) - 1; i++) line[p++] = sec_s[i];
            const char* b = " sectors";
            for (int i = 0; b[i] && p < (int)sizeof(line) - 1; i++) line[p++] = b[i];
            line[p] = '\0';
            draw_string(mbi, win->x + 10, win->y + 70, line, 0x00000000, 1);

            uint64_t mib = u64_div_u32(info.total_sectors, 2048u, NULL);
            uint64_t gib = u64_div_u32(mib, 1024u, NULL);
            char mib_s[32], gib_s[32];
            u64_to_dec(mib, mib_s, (int)sizeof(mib_s));
            u64_to_dec(gib, gib_s, (int)sizeof(gib_s));
            char cap[80];
            int c = 0;
            const char* c0 = "Approx ";
            for (int i = 0; c0[i] && c < (int)sizeof(cap) - 1; i++) cap[c++] = c0[i];
            for (int i = 0; gib_s[i] && c < (int)sizeof(cap) - 1; i++) cap[c++] = gib_s[i];
            const char* c1 = " GiB (";
            for (int i = 0; c1[i] && c < (int)sizeof(cap) - 1; i++) cap[c++] = c1[i];
            for (int i = 0; mib_s[i] && c < (int)sizeof(cap) - 1; i++) cap[c++] = mib_s[i];
            const char* c2 = " MiB)";
            for (int i = 0; c2[i] && c < (int)sizeof(cap) - 1; i++) cap[c++] = c2[i];
            cap[c] = '\0';
            draw_string(mbi, win->x + 10, win->y + 95, cap, 0x00000000, 1);

            draw_string(mbi, win->x + 10, win->y + 120, "Free space needs a filesystem driver.", 0x00000000, 1);
        }
    } else if (streq(win->title, "Task Manager")) {
        draw_string(mbi, win->x + 10, win->y + 30, "Performance (approx):", 0x00000000, 1);

        uint32_t total_kb = (uint32_t)(mbi->mem_lower + mbi->mem_upper);
        uint32_t total_mb = total_kb / 1024u;

        uint32_t kernel_used_kb = 0;
        uint32_t ke = (uint32_t)&kernel_end;
        if (ke > 0x100000u) kernel_used_kb = (ke - 0x100000u) / 1024u;
        uint32_t backbuf_kb = (mbi->framebuffer_width * mbi->framebuffer_height * 4u) / 1024u;
        uint32_t used_kb = kernel_used_kb + backbuf_kb;
        uint32_t used_mb = used_kb / 1024u;

        char ram[80];
        int rp = 0;
        const char* ra = "RAM ";
        for (int i = 0; ra[i]; i++) ram[rp++] = ra[i];
        uint32_t v = used_mb;
        if (v == 0) ram[rp++] = '0';
        else {
            char t[16]; int tl = 0;
            while (v > 0 && tl < 16) { t[tl++] = (char)('0' + (v % 10u)); v /= 10u; }
            for (int i = tl - 1; i >= 0; i--) ram[rp++] = t[i];
        }
        const char* rb = " MB / ";
        for (int i = 0; rb[i]; i++) ram[rp++] = rb[i];
        v = total_mb;
        if (v == 0) ram[rp++] = '0';
        else {
            char t[16]; int tl = 0;
            while (v > 0 && tl < 16) { t[tl++] = (char)('0' + (v % 10u)); v /= 10u; }
            for (int i = tl - 1; i >= 0; i--) ram[rp++] = t[i];
        }
        const char* rc = " MB";
        for (int i = 0; rc[i]; i++) ram[rp++] = rc[i];
        ram[rp] = '\0';
        draw_string(mbi, win->x + 10, win->y + 50, ram, 0x00000000, 1);

        char cpu[49];
        cpu_get_brand(cpu);
        draw_string(mbi, win->x + 10, win->y + 70, "CPU:", 0x00000000, 1);
        draw_string(mbi, win->x + 60, win->y + 70, cpu[0] ? cpu : "Unknown", 0x00000000, 1);

        static bool did2 = false;
        static struct ata_identify_info info2;
        if (!did2) { did2 = true; ata_identify_primary_master(&info2); }
        draw_string(mbi, win->x + 10, win->y + 95, "Disk:", 0x00000000, 1);
        draw_string(mbi, win->x + 60, win->y + 95, info2.present ? (info2.model[0] ? info2.model : "Detected") : "Not detected", 0x00000000, 1);

        draw_string(mbi, win->x + 10, win->y + 120, "Tasks (windows):", 0x00000000, 1);
        int y = win->y + 140;
        for (int i = 0; i < 8; i++) {
            if (!windows[i].active) continue;
            if (y > win->y + win->h - 20) break;
            draw_string(mbi, win->x + 10, y, windows[i].title, 0x000000AA, 1);
            draw_string(mbi, win->x + 240, y, windows[i].minimized ? "[min]" : "[on]", 0x00000000, 1);
            y += 14;
        }
    } else if (streq(win->title, "Personalize")) {
        draw_string(mbi, win->x + 10, win->y + 30, "Desktop background:", 0x00000000, 1);
        draw_string(mbi, win->x + 10, win->y + 50, "Click a color or pattern.", 0x00000000, 1);

        uint32_t colors[] = {
            0x00336699, 0x001d6f42, 0x006b1f7a, 0x007a1f1f,
            0x00333333, 0x00666666, 0x00a0c0ff, 0x00000000
        };
        int sx = win->x + 10;
        int sy = win->y + 75;
        for (int i = 0; i < 8; i++) {
            draw_rect(mbi, sx + i * 26, sy, 22, 22, colors[i]);
            if (colors[i] == desktop_bg_color) draw_rect(mbi, sx + i * 26, sy + 22, 22, 2, 0x002E7D32);
        }

        draw_string(mbi, win->x + 10, win->y + 110, "Pattern:", 0x00000000, 1);
        draw_rect(mbi, win->x + 90, win->y + 106, 60, 18, (desktop_bg_mode == 0) ? 0x002E7D32 : 0x00888888);
        draw_string(mbi, win->x + 98, win->y + 110, "Solid", 0x00FFFFFF, 1);
        draw_rect(mbi, win->x + 160, win->y + 106, 70, 18, (desktop_bg_mode == 1) ? 0x002E7D32 : 0x00888888);
        draw_string(mbi, win->x + 168, win->y + 110, "Checker", 0x00FFFFFF, 1);
        draw_rect(mbi, win->x + 240, win->y + 106, 70, 18, (desktop_bg_mode == 2) ? 0x002E7D32 : 0x00888888);
        draw_string(mbi, win->x + 248, win->y + 110, "Stripes", 0x00FFFFFF, 1);
        draw_string(mbi, win->x + 10, win->y + 140, "Tip: right-click desktop -> Personalize.", 0x00000000, 1);
        draw_rect(mbi, win->x + 10, win->y + 165, 120, 60, desktop_bg_color);
    }
}

static bool start_menu_open = false;

void draw_start_menu(struct multiboot_info* mbi) {
    if (!start_menu_open) return;
    int menu_h = 210;
    int menu_w = 120;
    int start_y = mbi->framebuffer_height - 40 - menu_h;
    
    draw_rect(mbi, 0, start_y, menu_w, menu_h, 0x00222222);
    draw_rect(mbi, 2, start_y + 2, menu_w - 4, menu_h - 4, 0x00CCCCCC);
    
    draw_string(mbi, 10, start_y + 10, "Programs", 0x00000000, 1);
    draw_rect(mbi, 5, start_y + 25, menu_w - 10, 1, 0x00888888);
    
    draw_string(mbi, 10, start_y + 35, "> Notepad", 0x00000000, 1);
    draw_string(mbi, 10, start_y + 55, "> Calculator", 0x00000000, 1);
    draw_string(mbi, 10, start_y + 75, "> Settings", 0x00000000, 1);
    draw_string(mbi, 10, start_y + 95, "> Devices", 0x00000000, 1);
    draw_string(mbi, 10, start_y + 115, "> Breakout", 0x00000000, 1);
    draw_string(mbi, 10, start_y + 135, "> Storage", 0x00000000, 1);
    
    draw_rect(mbi, 5, start_y + 170, menu_w - 10, 1, 0x00888888);
    draw_string(mbi, 10, start_y + 180, "Shut Down", 0x00AA0000, 1);
}

void draw_linda_logo(struct multiboot_info* mbi, int x, int y, uint32_t color, int scale) {
    int offset = 10 * scale;
    draw_char(mbi, x + 0 * offset, y, 'L', color, scale);
    draw_char(mbi, x + 1 * offset, y, 'i', color, scale);
    draw_char(mbi, x + 2 * offset, y, 'n', color, scale);
    draw_char(mbi, x + 3 * offset, y, 'd', color, scale);
    draw_char(mbi, x + 4 * offset, y, 'a', color, scale);
    draw_char(mbi, x + 5 * offset, y, 'O', color, scale);
    draw_char(mbi, x + 6 * offset, y, 'S', color, scale);
}

void draw_notepad_icon(struct multiboot_info* mbi, int x, int y) {
    draw_rect(mbi, x+10, y+5, 44, 54, 0x00FFFFFF); // Page
    draw_rect(mbi, x+15, y+15, 34, 2, 0x00888888); // Lines
    draw_rect(mbi, x+15, y+25, 34, 2, 0x00888888);
    draw_rect(mbi, x+15, y+35, 34, 2, 0x00888888);
}

void draw_calc_icon(struct multiboot_info* mbi, int x, int y) {
    draw_rect(mbi, x+10, y+5, 44, 54, 0x00333333); // Body
    draw_rect(mbi, x+15, y+10, 34, 15, 0x00AAFFAA); // Screen
    draw_rect(mbi, x+15, y+30, 8, 8, 0x00888888); // Buttons
    draw_rect(mbi, x+28, y+30, 8, 8, 0x00888888);
    draw_rect(mbi, x+41, y+30, 8, 8, 0x00888888);
}

void draw_settings_icon(struct multiboot_info* mbi, int x, int y) {
    draw_rect(mbi, x+25, y+5, 14, 54, 0x00777777); // Gear cross
    draw_rect(mbi, x+5, y+25, 54, 14, 0x00777777);
    draw_rect(mbi, x+20, y+20, 24, 24, 0x00555555); // Center
}

static void draw_breakout_icon(struct multiboot_info* mbi, int x, int y) {
    draw_rect(mbi, x+8, y+10, 48, 10, 0x00AA0000);
    draw_rect(mbi, x+8, y+24, 48, 10, 0x0000AA00);
    draw_rect(mbi, x+8, y+38, 48, 10, 0x000000AA);
    draw_rect(mbi, x+18, y+52, 28, 6, 0x002E7D32);
    draw_rect(mbi, x+30, y+46, 6, 6, 0x00000000);
}

static void draw_storage_icon(struct multiboot_info* mbi, int x, int y) {
    draw_rect(mbi, x+10, y+10, 44, 30, 0x00555555);
    draw_rect(mbi, x+10, y+40, 44, 18, 0x00777777);
    draw_rect(mbi, x+14, y+44, 8, 8, 0x00AAFFAA);
    draw_rect(mbi, x+26, y+44, 8, 8, 0x00FFD700);
    draw_rect(mbi, x+38, y+44, 8, 8, 0x00FFAAAA);
}

static bool context_menu_open = false;
static int context_menu_x = 0, context_menu_y = 0;

void draw_context_menu(struct multiboot_info* mbi) {
    if (!context_menu_open) return;
    draw_rect(mbi, context_menu_x, context_menu_y, 150, 100, 0x00222222);
    draw_rect(mbi, context_menu_x + 2, context_menu_y + 2, 146, 96, 0x00CCCCCC);
    draw_string(mbi, context_menu_x + 10, context_menu_y + 10, "Personalize", 0x00000000, 1);
    draw_string(mbi, context_menu_x + 10, context_menu_y + 35, "Task Manager", 0x00000000, 1);
    draw_string(mbi, context_menu_x + 10, context_menu_y + 60, "Refresh", 0x00000000, 1);
    draw_string(mbi, context_menu_x + 10, context_menu_y + 80, "Exit", 0x00AA0000, 1);
}

void draw_loading_circle(struct multiboot_info* mbi, int x, int y, int frame) {
    int points = 8;
    for (int i = 0; i < points; i++) {
        int angle_idx = (i + frame) % points;
        uint32_t color = 0x00333333; // Default gray
        if (angle_idx == 0) color = 0x00FFFFFF;
        else if (angle_idx == 1) color = 0x00CCCCCC;
        else if (angle_idx == 2) color = 0x00888888;
        
        // Simulare cerc fără sin/cos
        int ox = 0, oy = 0;
        if (i == 0) { ox = 20; oy = 0; }
        else if (i == 1) { ox = 14; oy = 14; }
        else if (i == 2) { ox = 0; oy = 20; }
        else if (i == 3) { ox = -14; oy = 14; }
        else if (i == 4) { ox = -20; oy = 0; }
        else if (i == 5) { ox = -14; oy = -14; }
        else if (i == 6) { ox = 0; oy = -20; }
        else if (i == 7) { ox = 14; oy = -14; }
        
        draw_rect(mbi, x + ox, y + oy, 6, 6, color);
    }
}

void kernel_main(uint32_t magic, struct multiboot_info* mbi) {
    global_mbi = mbi;
	gdt_install();
    idt_install();
    ps2_install();
    usb_init();
    
    __asm__ volatile("sti");
    
    uint32_t last_click_time = 0;
    uint8_t last_buttons = 0;
    uint32_t timer_ticks = 0;
    int dragging_win = -1;
    int drag_off_x = 0, drag_off_y = 0;

    if (magic == 0x2BADB002 && (mbi->flags & (1 << 12))) {
        // Boot Animation - LindaOS Logo & Windows 10 style Circle
        for (int alpha = 0; alpha <= 255; alpha += 5) {
            fill_screen(mbi, 0x00000000);
            uint32_t gray = (alpha << 16) | (alpha << 8) | alpha;
            draw_linda_logo(mbi, (mbi->framebuffer_width / 2) - 140, (mbi->framebuffer_height / 2) - 100, gray, 4);
            
            // Loading Circle
            draw_loading_circle(mbi, mbi->framebuffer_width / 2, mbi->framebuffer_height / 2 + 50, alpha / 10);
            
            wait_vblank();
            swap_buffers(mbi);
            for(volatile int i = 0; i < 2000000; i++);
        }

        while(1) {
            timer_ticks++;
            extern void mouse_handler();
            mouse_handler();

            draw_background(mbi);
            
            // Taskbar
            draw_rect(mbi, 0, mbi->framebuffer_height - 40, mbi->framebuffer_width, 40, 0x001A1A1A);
            draw_rect(mbi, 5, mbi->framebuffer_height - 35, 80, 30, 0x002E7D32);
            draw_string(mbi, 15, mbi->framebuffer_height - 28, "Start", 0x00FFFFFF, 1);
            
            extern const char* get_kbd_buffer();
            draw_string(mbi, 100, mbi->framebuffer_height - 28, get_kbd_buffer(), 0x00FFFFFF, 1);
            
            // App Icons
            int icon_x[] = {50, 150, 250, 350, 450, 550};
            const char* icon_names[] = {"Notepad", "Calculator", "Settings", "Devices", "Breakout", "Storage"};

            for(int i = 0; i < 6; i++) {
                if (i == 0) draw_notepad_icon(mbi, icon_x[i], 50);
                else if (i == 1) draw_calc_icon(mbi, icon_x[i], 50);
                else if (i == 2) draw_settings_icon(mbi, icon_x[i], 50);
                else if (i == 3) { // Device Manager Icon (Chip/CPU style)
                    draw_rect(mbi, icon_x[i]+15, 55, 34, 44, 0x00222222);
                    draw_rect(mbi, icon_x[i]+10, 65, 5, 5, 0x00FFD700);
                    draw_rect(mbi, icon_x[i]+10, 75, 5, 5, 0x00FFD700);
                    draw_rect(mbi, icon_x[i]+10, 85, 5, 5, 0x00FFD700);
                    draw_rect(mbi, icon_x[i]+49, 65, 5, 5, 0x00FFD700);
                    draw_rect(mbi, icon_x[i]+49, 75, 5, 5, 0x00FFD700);
                    draw_rect(mbi, icon_x[i]+49, 85, 5, 5, 0x00FFD700);
                } else if (i == 4) {
                    draw_breakout_icon(mbi, icon_x[i], 50);
                } else if (i == 5) {
                    draw_storage_icon(mbi, icon_x[i], 50);
                }
                
                draw_string(mbi, icon_x[i], 120, icon_names[i], 0x00FFFFFF, 1);
            }

            // Mouse Interaction
            int32_t mx = get_mouse_x();
            int32_t my = get_mouse_y();
            uint8_t buttons = get_mouse_buttons();

            // Window Dragging
            if (buttons & 1) {
                if (dragging_win != -1) {
                    windows[dragging_win].x = mx - drag_off_x;
                    windows[dragging_win].y = my - drag_off_y;
                }
            } else {
                dragging_win = -1;
            }

            // Detect Click and Double Click
            if ((buttons & 1) && !(last_buttons & 1)) {
                // If context menu is open, handle it first.
                if (context_menu_open) {
                    int cmx = context_menu_x;
                    int cmy = context_menu_y;
                    if (mx >= cmx && mx <= cmx + 150 && my >= cmy && my <= cmy + 100) {
                        // Items: Personalize(10), Task Manager(35), Refresh(60), Exit(80)
                        if (my >= cmy + 5 && my <= cmy + 25) { windows[7].active = true; windows[7].minimized = false; }
                        else if (my >= cmy + 30 && my <= cmy + 50) { windows[6].active = true; windows[6].minimized = false; }
                        // Refresh: no-op (redraw happens every frame)
                        context_menu_open = false;
                    } else {
                        context_menu_open = false;
                    }
                } else {
                // Check Start Button
                if (mx >= 5 && mx <= 85 && my >= mbi->framebuffer_height - 35 && my <= mbi->framebuffer_height - 5) {
                    start_menu_open = !start_menu_open;
                } else if (start_menu_open) {
                    // Check Start Menu Items
                    int menu_h = 210;
                    int start_y = mbi->framebuffer_height - 40 - menu_h;
                    if (mx >= 0 && mx <= 120 && my >= start_y && my <= mbi->framebuffer_height - 40) {
                        if (my >= start_y + 30 && my <= start_y + 50) { windows[0].active = true; windows[0].minimized = false; }
                        if (my >= start_y + 50 && my <= start_y + 70) { windows[1].active = true; windows[1].minimized = false; }
                        if (my >= start_y + 70 && my <= start_y + 90) { windows[2].active = true; windows[2].minimized = false; }
                        if (my >= start_y + 90 && my <= start_y + 110) { windows[3].active = true; windows[3].minimized = false; }
                        if (my >= start_y + 110 && my <= start_y + 130) { windows[4].active = true; windows[4].minimized = false; }
                        if (my >= start_y + 130 && my <= start_y + 150) { windows[5].active = true; windows[5].minimized = false; }
                        start_menu_open = false;
                    } else {
                        start_menu_open = false;
                    }
                } else {
                    // Check Window Controls and Icons
                    bool handled = false;
                    for(int i = 7; i >= 0; i--) { // Top to bottom (now 8 windows)
                        if (windows[i].active && !windows[i].minimized) {
                            // Close Button
                            if (mx >= windows[i].x + windows[i].w - 20 && mx <= windows[i].x + windows[i].w - 2 &&
                                my >= windows[i].y + 2 && my <= windows[i].y + 20) {
                                windows[i].active = false;
                                handled = true;
                                break;
                            }
                            // Maximize Button
                            if (mx >= windows[i].x + windows[i].w - 40 && mx <= windows[i].x + windows[i].w - 22 &&
                                my >= windows[i].y + 2 && my <= windows[i].y + 20) {
                                if (windows[i].maximized) {
                                    windows[i].x = windows[i].old_x;
                                    windows[i].y = windows[i].old_y;
                                    windows[i].w = windows[i].old_w;
                                    windows[i].h = windows[i].old_h;
                                    windows[i].maximized = false;
                                } else {
                                    windows[i].old_x = windows[i].x;
                                    windows[i].old_y = windows[i].y;
                                    windows[i].old_w = windows[i].w;
                                    windows[i].old_h = windows[i].h;
                                    windows[i].x = 0;
                                    windows[i].y = 0;
                                    windows[i].w = mbi->framebuffer_width;
                                    windows[i].h = mbi->framebuffer_height - 40;
                                    windows[i].maximized = true;
                                }
                                handled = true;
                                break;
                            }
                            // Minimize Button
                            if (mx >= windows[i].x + windows[i].w - 60 && mx <= windows[i].x + windows[i].w - 42 &&
                                my >= windows[i].y + 2 && my <= windows[i].y + 20) {
                                windows[i].minimized = true;
                                handled = true;
                                break;
                            }

                            // Title Bar Dragging
                            if (my >= windows[i].y && my <= windows[i].y + 22) {
                                dragging_win = i;
                                drag_off_x = mx - windows[i].x;
                                drag_off_y = my - windows[i].y;
                                handled = true;
                                break;
                            }
                        }
                    }

                    if (!handled) {
                        for(int i = 0; i < 6; i++) {
                            if (mx >= icon_x[i] && mx <= icon_x[i] + 64 && my >= 50 && my <= 114) {
                                if (timer_ticks - last_click_time < 50) { 
                                    windows[i].active = true;
                                    windows[i].minimized = false;
                                }
                                last_click_time = timer_ticks;
                            }
                        }
                    }
                }
                }
            }

            // Right Click Detection
            if ((buttons & 2) && !(last_buttons & 2)) {
                context_menu_open = true;
                context_menu_x = mx;
                context_menu_y = my;
                start_menu_open = false;
            }
            last_buttons = buttons;

            // Draw Context Menu
            draw_context_menu(mbi);

            // Draw Start Menu
            draw_start_menu(mbi);

            // Draw Windows
            for(int i = 0; i < 8; i++) {
                draw_window(mbi, &windows[i], timer_ticks);
            }

            // Mouse Cursor
            draw_rect(mbi, mx, my, 8, 8, (buttons & 1) ? 0x00FF0000 : 0x00FFFFFF);
            draw_rect(mbi, mx+1, my+1, 6, 6, 0x00000000);

            wait_vblank();
            swap_buffers(mbi);
            for(volatile int i = 0; i < 5000; i++);
        }
    }
    
    terminal_initialize();
    terminal_writestring("LindaOS: VESA Error.\n");
    while(1) { __asm__("hlt"); }
}
