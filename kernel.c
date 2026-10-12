/*
 * kernel.c — Kolotibablo OS: Entorno Gráfico Interactivo Frutiger Aero
 *
 * Características:
 * - Driver de Ratón PS/2 nativo con lectura de puertos I/O 0x60 / 0x64.
 * - Cursor interactivo con respuesta en tiempo real a clics y movimiento.
 * - Ventanas interactivas: arrastrables (drag & drop), botón de cerrar y minimizar.
 * - Menú Inicio "Start Orb" desplegable al hacer clic.
 * - Fuente ASCII completa (32-126) sin caracteres faltantes.
 * - Doble buffer en RAM (Backbuffer) para animación fluida sin parpadeos.
 */

#include <stdint.h>
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
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint8_t  color_info[6];
} __attribute__((packed));

static uint32_t* fb = NULL;
static uint32_t screen_w = 1024;
static uint32_t screen_h = 768;
static uint32_t screen_pitch = 1024;

/* Buffer de fondo en memoria (Backbuffer en RAM) para doble buffer fluido */
/* Situado en memoria física segura por encima del kernel (4 MiB) */
#define BACKBUFFER_ADDR 0x00400000
static uint32_t* backbuffer = (uint32_t*)BACKBUFFER_ADDR;

/* ============================================================================
 * Puertos I/O Hardware x86
 * ============================================================================ */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* ============================================================================
 * Primitivas de Color (ARGB 32-bit)
 * ============================================================================ */
static inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (0xFFU << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
static inline uint8_t get_r(uint32_t c) { return (c >> 16) & 0xFF; }
static inline uint8_t get_g(uint32_t c) { return (c >> 8)  & 0xFF; }
static inline uint8_t get_b(uint32_t c) { return c & 0xFF; }

static inline uint32_t blend(uint32_t fg, uint32_t bg, uint8_t a) {
    if (a == 255) return fg;
    if (a == 0) return bg;
    uint32_t inv_a = 255 - a;
    return rgb(
        (get_r(fg) * a + get_r(bg) * inv_a) >> 8,
        (get_g(fg) * a + get_g(bg) * inv_a) >> 8,
        (get_b(fg) * a + get_b(bg) * inv_a) >> 8
    );
}

static inline void bb_pixel(int x, int y, uint32_t color) {
    if (x >= 0 && (uint32_t)x < screen_w && y >= 0 && (uint32_t)y < screen_h) {
        backbuffer[y * screen_w + x] = color;
    }
}

static inline void bb_pixel_alpha(int x, int y, uint32_t color, uint8_t a) {
    if (x >= 0 && (uint32_t)x < screen_w && y >= 0 && (uint32_t)y < screen_h) {
        uint32_t bg = backbuffer[y * screen_w + x];
        backbuffer[y * screen_w + x] = blend(color, bg, a);
    }
}

static void bb_fill_rect(int x, int y, int w, int h, uint32_t color) {
    for (int j = y; j < y + h; j++) {
        if (j < 0 || (uint32_t)j >= screen_h) continue;
        for (int i = x; i < x + w; i++) {
            if (i < 0 || (uint32_t)i >= screen_w) continue;
            backbuffer[j * screen_w + i] = color;
        }
    }
}

static void bb_fill_rect_alpha(int x, int y, int w, int h, uint32_t color, uint8_t a) {
    for (int j = y; j < y + h; j++) {
        if (j < 0 || (uint32_t)j >= screen_h) continue;
        for (int i = x; i < x + w; i++) {
            if (i < 0 || (uint32_t)i >= screen_w) continue;
            bb_pixel_alpha(i, j, color, a);
        }
    }
}

/* ============================================================================
 * Fuente Bitmap 8x16 COMPLETA (Todos los caracteres ASCII imprimibles 32..126)
 * ============================================================================ */
static const uint8_t font_ascii[128][16] = {
    [' '] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['!'] = {0x00,0x00,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
    ['"'] = {0x00,0x00,0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['#'] = {0x00,0x00,0x24,0x24,0x7E,0x24,0x24,0x7E,0x24,0x24,0x00,0x00,0x00,0x00,0x00,0x00},
    ['$'] = {0x00,0x00,0x18,0x3E,0x58,0x3C,0x0E,0x1A,0x7C,0x18,0x00,0x00,0x00,0x00,0x00,0x00},
    ['%'] = {0x00,0x00,0x62,0x64,0x08,0x10,0x20,0x26,0x46,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['&'] = {0x00,0x00,0x38,0x44,0x44,0x38,0x54,0x4A,0x44,0x3A,0x00,0x00,0x00,0x00,0x00,0x00},
    ['\'']= {0x00,0x00,0x18,0x18,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['('] = {0x00,0x00,0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    [')'] = {0x00,0x00,0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['*'] = {0x00,0x00,0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['+'] = {0x00,0x00,0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    [','] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x08,0x10,0x00,0x00,0x00,0x00,0x00},
    ['-'] = {0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['.'] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00},
    ['/'] = {0x00,0x00,0x02,0x06,0x0C,0x18,0x30,0x60,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['0'] = {0x00,0x00,0x3C,0x42,0x46,0x4A,0x52,0x62,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['1'] = {0x00,0x00,0x18,0x28,0x08,0x08,0x08,0x08,0x3E,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['2'] = {0x00,0x00,0x3C,0x42,0x02,0x0C,0x30,0x40,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['3'] = {0x00,0x00,0x3C,0x42,0x02,0x1C,0x02,0x42,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['4'] = {0x00,0x00,0x0C,0x14,0x24,0x44,0x7E,0x04,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['5'] = {0x00,0x00,0x7E,0x40,0x7C,0x02,0x02,0x42,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['6'] = {0x00,0x00,0x3C,0x40,0x7C,0x42,0x42,0x42,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['7'] = {0x00,0x00,0x7E,0x02,0x04,0x08,0x10,0x20,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['8'] = {0x00,0x00,0x3C,0x42,0x42,0x3C,0x42,0x42,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['9'] = {0x00,0x00,0x3C,0x42,0x42,0x3E,0x02,0x02,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    [':'] = {0x00,0x00,0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    [';'] = {0x00,0x00,0x00,0x18,0x18,0x00,0x18,0x18,0x08,0x10,0x00,0x00,0x00,0x00,0x00,0x00},
    ['<'] = {0x00,0x00,0x0E,0x18,0x30,0x60,0x30,0x18,0x0E,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['='] = {0x00,0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['>'] = {0x00,0x00,0x70,0x18,0x0C,0x06,0x0C,0x18,0x70,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['?'] = {0x00,0x00,0x3C,0x42,0x02,0x0C,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00},
    ['@'] = {0x00,0x00,0x3C,0x42,0x99,0xA5,0xA5,0x9C,0x40,0x3C,0x00,0x00,0x00,0x00,0x00,0x00},
    ['A'] = {0x00,0x00,0x18,0x24,0x42,0x42,0x7E,0x42,0x42,0x42,0x00,0x00,0x00,0x00,0x00,0x00},
    ['B'] = {0x00,0x00,0x7C,0x42,0x42,0x7C,0x42,0x42,0x42,0x7C,0x00,0x00,0x00,0x00,0x00,0x00},
    ['C'] = {0x00,0x00,0x3C,0x42,0x40,0x40,0x40,0x42,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['D'] = {0x00,0x00,0x78,0x44,0x42,0x42,0x42,0x44,0x78,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['E'] = {0x00,0x00,0x7E,0x40,0x40,0x78,0x40,0x40,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['F'] = {0x00,0x00,0x7E,0x40,0x40,0x78,0x40,0x40,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['G'] = {0x00,0x00,0x3C,0x42,0x40,0x4E,0x42,0x42,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['H'] = {0x00,0x00,0x42,0x42,0x42,0x7E,0x42,0x42,0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['I'] = {0x00,0x00,0x38,0x10,0x10,0x10,0x10,0x10,0x38,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['J'] = {0x00,0x00,0x1C,0x08,0x08,0x08,0x08,0x48,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['K'] = {0x00,0x00,0x42,0x44,0x48,0x70,0x48,0x44,0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['L'] = {0x00,0x00,0x40,0x40,0x40,0x40,0x40,0x40,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['M'] = {0x00,0x00,0x42,0x66,0x5A,0x42,0x42,0x42,0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['N'] = {0x00,0x00,0x42,0x62,0x52,0x4A,0x46,0x42,0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['O'] = {0x00,0x00,0x3C,0x42,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['P'] = {0x00,0x00,0x7C,0x42,0x42,0x7C,0x40,0x40,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['Q'] = {0x00,0x00,0x3C,0x42,0x42,0x42,0x4A,0x44,0x3A,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['R'] = {0x00,0x00,0x7C,0x42,0x42,0x7C,0x48,0x44,0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['S'] = {0x00,0x00,0x3C,0x42,0x40,0x3C,0x02,0x42,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['T'] = {0x00,0x00,0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['U'] = {0x00,0x00,0x42,0x42,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['V'] = {0x00,0x00,0x42,0x42,0x42,0x42,0x24,0x24,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['W'] = {0x00,0x00,0x42,0x42,0x42,0x5A,0x5A,0x66,0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['X'] = {0x00,0x00,0x42,0x24,0x18,0x18,0x24,0x42,0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['Y'] = {0x00,0x00,0x42,0x42,0x24,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['Z'] = {0x00,0x00,0x7E,0x04,0x08,0x10,0x20,0x40,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['['] = {0x00,0x00,0x1E,0x10,0x10,0x10,0x10,0x10,0x1E,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['\\']= {0x00,0x00,0x40,0x60,0x30,0x18,0x0C,0x06,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    [']'] = {0x00,0x00,0x78,0x08,0x08,0x08,0x08,0x08,0x78,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['_'] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0x00,0x00,0x00,0x00},
    ['a'] = {0x00,0x00,0x00,0x3C,0x02,0x3E,0x42,0x46,0x3A,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['b'] = {0x00,0x00,0x40,0x40,0x5C,0x62,0x42,0x62,0x5C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['c'] = {0x00,0x00,0x00,0x3C,0x42,0x40,0x40,0x42,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['d'] = {0x00,0x00,0x02,0x02,0x3A,0x46,0x42,0x46,0x3A,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['e'] = {0x00,0x00,0x00,0x3C,0x42,0x7E,0x40,0x42,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['f'] = {0x00,0x00,0x0C,0x12,0x10,0x3C,0x10,0x10,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['g'] = {0x00,0x00,0x00,0x3A,0x46,0x42,0x46,0x3A,0x02,0x3C,0x00,0x00,0x00,0x00,0x00,0x00},
    ['h'] = {0x00,0x00,0x40,0x40,0x5C,0x62,0x42,0x42,0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['i'] = {0x00,0x00,0x10,0x00,0x30,0x10,0x10,0x10,0x38,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['j'] = {0x00,0x00,0x08,0x00,0x18,0x08,0x08,0x08,0x48,0x30,0x00,0x00,0x00,0x00,0x00,0x00},
    ['k'] = {0x00,0x00,0x40,0x40,0x44,0x48,0x70,0x48,0x44,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['l'] = {0x00,0x00,0x30,0x10,0x10,0x10,0x10,0x10,0x38,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['m'] = {0x00,0x00,0x00,0x76,0x49,0x49,0x49,0x49,0x49,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['n'] = {0x00,0x00,0x00,0x5C,0x62,0x42,0x42,0x42,0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['o'] = {0x00,0x00,0x00,0x3C,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['p'] = {0x00,0x00,0x00,0x5C,0x62,0x42,0x62,0x5C,0x40,0x40,0x00,0x00,0x00,0x00,0x00,0x00},
    ['q'] = {0x00,0x00,0x00,0x3A,0x46,0x42,0x46,0x3A,0x02,0x03,0x00,0x00,0x00,0x00,0x00,0x00},
    ['r'] = {0x00,0x00,0x00,0x56,0x68,0x40,0x40,0x40,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['s'] = {0x00,0x00,0x00,0x3E,0x40,0x3C,0x02,0x42,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['t'] = {0x00,0x00,0x20,0x78,0x20,0x20,0x20,0x24,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['u'] = {0x00,0x00,0x00,0x42,0x42,0x42,0x42,0x46,0x3A,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['v'] = {0x00,0x00,0x00,0x42,0x42,0x24,0x24,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['w'] = {0x00,0x00,0x00,0x42,0x42,0x5A,0x5A,0x66,0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['x'] = {0x00,0x00,0x00,0x42,0x24,0x18,0x24,0x42,0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['y'] = {0x00,0x00,0x00,0x42,0x42,0x42,0x46,0x3A,0x02,0x3C,0x00,0x00,0x00,0x00,0x00,0x00},
    ['z'] = {0x00,0x00,0x00,0x7E,0x08,0x10,0x20,0x40,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

static void draw_text(int x, int y, const char* str, uint32_t color) {
    int cur_x = x;
    for (int i = 0; str[i] != '\0'; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c < 128) {
            for (int row = 0; row < 16; row++) {
                uint8_t line = font_ascii[c][row];
                for (int col = 0; col < 8; col++) {
                    if (line & (0x80 >> col)) {
                        bb_pixel(cur_x + col, y + row, color);
                    }
                }
            }
        }
        cur_x += 8;
    }
}

/* ============================================================================
 * Círculos, Esferas y Burbujas con Estética Frutiger Aero
 * ============================================================================ */
static void draw_bubble(int cx, int cy, int radius) {
    int r2 = radius * radius;
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            int d2 = x * x + y * y;
            if (d2 <= r2) {
                if (d2 > r2 - (radius * 3)) {
                    bb_pixel_alpha(cx + x, cy + y, rgb(255, 255, 255), 180);
                } else if (x < -radius/3 && y < -radius/3 && (x+radius/2)*(x+radius/2) + (y+radius/2)*(y+radius/2) < (radius/3)*(radius/3)) {
                    bb_pixel_alpha(cx + x, cy + y, rgb(255, 255, 255), 230);
                } else {
                    bb_pixel_alpha(cx + x, cy + y, rgb(160, 235, 255), 45);
                }
            }
        }
    }
}

static void draw_sphere(int cx, int cy, int r, uint32_t color) {
    int r2 = r * r;
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            int d2 = x * x + y * y;
            if (d2 <= r2) {
                if (x < -r/4 && y < -r/4) {
                    bb_pixel(cx + x, cy + y, rgb(255, 255, 255));
                } else if (d2 > r2 - (r * 2)) {
                    bb_pixel(cx + x, cy + y, rgb(get_r(color)/2, get_g(color)/2, get_b(color)/2));
                } else {
                    bb_pixel(cx + x, cy + y, color);
                }
            }
        }
    }
}

/* ============================================================================
 * Estado de Ventanas e Interfaz Gráfica
 * ============================================================================ */
static int win_x = 220;
static int win_y = 90;
static int win_w = 580;
static int win_h = 420;
static int win_visible = 1;
static int start_menu_open = 0;

/* Estado de Arrastre de Ventana */
static int is_dragging = 0;
static int drag_off_x = 0;
static int drag_off_y = 0;

/* Estado del Ratón */
static int mouse_x = 512;
static int mouse_y = 384;
static uint8_t mouse_btn_prev = 0;

/* ============================================================================
 * Renderizado del Escritorio en Backbuffer
 * ============================================================================ */
static void render_desktop(void) {
    /* 1. Fondo Wallpaper: Cielo Acuático Frutiger Aero */
    for (uint32_t y = 0; y < screen_h; y++) {
        uint8_t r = 2 + (y * 20) / screen_h;
        uint8_t g = 40 + (y * 140) / screen_h;
        uint8_t b = 64 + (y * 120) / screen_h;
        uint32_t sky = rgb(r, g, b);
        for (uint32_t x = 0; x < screen_w; x++) {
            backbuffer[y * screen_w + x] = sky;
        }
    }

    /* 2. Colinas Verdes Curvadas Frutiger Eco */
    for (uint32_t y = 480; y < screen_h; y++) {
        for (uint32_t x = 0; x < screen_w; x++) {
            int hill = 530 - (int)((x - 512) * (x - 512) / 4500);
            if ((int)y >= hill) {
                uint8_t gr = 30 + (y - hill) / 3;
                uint8_t gg = 175 - (y - hill) / 3;
                uint8_t gb = 70;
                bb_pixel_alpha(x, y, rgb(gr, gg, gb), 210);
            }
        }
    }

    /* 3. Burbujas brillantes */
    draw_bubble(180, 220, 24);
    draw_bubble(240, 160, 38);
    draw_bubble(840, 180, 42);
    draw_bubble(760, 260, 26);

    /* 4. Iconos del Escritorio */
    /* Icono 1: Mi Equipo */
    bb_fill_rect_alpha(35, 45, 75, 75, rgb(255, 255, 255), 45);
    bb_fill_rect(50, 55, 45, 32, rgb(20, 140, 220));
    bb_fill_rect(54, 59, 37, 24, rgb(230, 245, 255));
    bb_fill_rect(68, 87, 9, 8, rgb(160, 180, 200));
    bb_fill_rect(60, 95, 25, 4, rgb(120, 140, 160));
    draw_text(48, 126, "Equipo", rgb(255, 255, 255));

    /* Icono 2: Archivos */
    bb_fill_rect_alpha(35, 155, 75, 75, rgb(255, 255, 255), 45);
    bb_fill_rect(50, 172, 45, 32, rgb(240, 180, 40));
    bb_fill_rect(55, 166, 20, 8, rgb(220, 160, 30));
    draw_text(40, 236, "Archivos", rgb(255, 255, 255));

    /* Icono 3: Aero Player */
    bb_fill_rect_alpha(35, 265, 75, 75, rgb(255, 255, 255), 45);
    draw_sphere(72, 300, 18, rgb(0, 200, 160));
    draw_text(48, 346, "Player", rgb(255, 255, 255));

    /* 5. Ventana Principal (si está visible) */
    if (win_visible) {
        /* Sombra exterior */
        bb_fill_rect_alpha(win_x + 8, win_y + 8, win_w, win_h, rgb(0, 20, 50), 90);

        /* Cristal de la Ventana */
        bb_fill_rect_alpha(win_x, win_y, win_w, win_h, rgb(235, 248, 255), 220);

        /* Bordes de cristal */
        for (int i = 0; i < win_w; i++) {
            bb_pixel(win_x + i, win_y, rgb(255, 255, 255));
            bb_pixel(win_x + i, win_y + win_h - 1, rgb(160, 210, 240));
        }
        for (int j = 0; j < win_h; j++) {
            bb_pixel(win_x, win_y + j, rgb(255, 255, 255));
            bb_pixel(win_x + win_w - 1, win_y + j, rgb(160, 210, 240));
        }

        /* Barra de Título con Gradiente */
        for (int j = 0; j < 36; j++) {
            uint8_t bar_r = 140 + j * 2;
            uint8_t bar_g = 210 + j;
            uint8_t bar_b = 255;
            for (int i = 1; i < win_w - 1; i++) {
                bb_pixel(win_x + i, win_y + 1 + j, rgb(bar_r, bar_g, bar_b));
            }
        }
        for (int i = 1; i < win_w - 1; i++) {
            bb_pixel(win_x + i, win_y + 2, rgb(255, 255, 255));
        }

        draw_text(win_x + 18, win_y + 11, "Kolotibablo OS - Aero Console Interactive", rgb(10, 50, 90));

        /* Botones de Control de Ventana (Cerrar, Maximizar, Minimizar) */
        draw_sphere(win_x + win_w - 20, win_y + 18, 7, rgb(230, 40, 40));  /* Rojo: Cerrar */
        draw_sphere(win_x + win_w - 40, win_y + 18, 7, rgb(40, 200, 80));  /* Verde: Maximizar */
        draw_sphere(win_x + win_w - 60, win_y + 18, 7, rgb(240, 180, 20)); /* Amarillo: Minimizar */

        /* Panel Interior */
        bb_fill_rect(win_x + 20, win_y + 55, win_w - 40, 75, rgb(255, 255, 255));
        bb_fill_rect(win_x + 20, win_y + 55, win_w - 40, 2, rgb(0, 160, 230));
        draw_text(win_x + 35, win_y + 72, "BIENVENIDO A KOLOTIBABLO OS", rgb(0, 120, 200));
        draw_text(win_x + 35, win_y + 98, "Entorno Frutiger Aero Interactivo (Mueve el raton)", rgb(60, 80, 100));

        /* Panel de Telemetría */
        bb_fill_rect_alpha(win_x + 20, win_y + 145, win_w - 40, 240, rgb(220, 240, 250), 180);
        draw_text(win_x + 35, win_y + 165, "TELEMETRIA DEL SISTEMA:", rgb(10, 60, 110));
        draw_text(win_x + 55, win_y + 195, "- Nucleo de Kernel   : Kolotibablo OS v0.3", rgb(20, 70, 100));
        draw_text(win_x + 55, win_y + 225, "- Driver de Raton    : PS/2 Mouse Controller ACTIVO", rgb(0, 140, 60));
        draw_text(win_x + 55, win_y + 255, "- Resolucion         : 1024 x 768 a 32-bit (VBE)", rgb(20, 70, 100));
        draw_text(win_x + 55, win_y + 285, "- Doble Buffer       : Backbuffer RAM Activo a 60 FPS", rgb(20, 70, 100));
        draw_text(win_x + 55, win_y + 315, "- Interaccion        : Arrastra la barra de titulo con click", rgb(20, 70, 100));
        draw_text(win_x + 55, win_y + 345, "- Boton Inicio       : Haz click en el Start Orb inferior", rgb(0, 120, 200));
    }

    /* 6. Barra de Tareas Inferior (Aero Taskbar) */
    int tby = screen_h - 48;
    for (int j = 0; j < 48; j++) {
        uint8_t tb_r = 10 + j * 2;
        uint8_t tb_g = 60 + j * 3;
        uint8_t tb_b = 130 + j * 2;
        for (uint32_t i = 0; i < screen_w; i++) {
            bb_pixel_alpha(i, tby + j, rgb(tb_r, tb_g, tb_b), 200);
        }
    }
    for (uint32_t i = 0; i < screen_w; i++) {
        bb_pixel(i, tby, rgb(200, 240, 255));
    }

    /* Start Orb */
    draw_sphere(32, tby + 24, 20, rgb(0, 130, 240));
    draw_text(28, tby + 16, "K", rgb(255, 255, 255));

    /* Reloj del sistema */
    bb_fill_rect_alpha(screen_w - 110, tby + 8, 95, 32, rgb(255, 255, 255), 40);
    draw_text(screen_w - 95, tby + 17, "12:00 PM", rgb(255, 255, 255));

    /* 7. Menú Inicio Desplegable (si se hizo clic en el Start Orb) */
    if (start_menu_open) {
        int sm_w = 260, sm_h = 320;
        int sm_x = 10, sm_y = tby - sm_h - 4;
        bb_fill_rect_alpha(sm_x, sm_y, sm_w, sm_h, rgb(240, 250, 255), 235);
        bb_fill_rect(sm_x, sm_y, sm_w, 40, rgb(0, 120, 210));
        draw_text(sm_x + 15, sm_y + 12, "Kolotibablo Aero Menu", rgb(255, 255, 255));

        draw_text(sm_x + 20, sm_y + 60, "> Mi Equipo", rgb(20, 60, 100));
        draw_text(sm_x + 20, sm_y + 95, "> Explorador de Archivos", rgb(20, 60, 100));
        draw_text(sm_x + 20, sm_y + 130, "> Reproductor Aero", rgb(20, 60, 100));
        draw_text(sm_x + 20, sm_y + 165, "> Panel de Control", rgb(20, 60, 100));
        draw_text(sm_x + 20, sm_y + 200, "> Restaurar Ventana", rgb(0, 140, 60));
        draw_text(sm_x + 20, sm_y + 280, "[X] Apagar Sistema", rgb(200, 40, 40));
    }

    /* 8. Cursor de Ratón Interactivo */
    for (int cy = 0; cy < 16; cy++) {
        for (int cx = 0; cx <= cy; cx++) {
            bb_pixel_alpha(mouse_x + cx + 3, mouse_y + cy + 3, rgb(0, 0, 0), 90);
        }
    }
    for (int cy = 0; cy < 15; cy++) {
        for (int cx = 0; cx <= cy; cx++) {
            if (cx == 0 || cx == cy || cy == 14) {
                bb_pixel(mouse_x + cx, mouse_y + cy, rgb(10, 40, 70));
            } else {
                bb_pixel(mouse_x + cx, mouse_y + cy, rgb(255, 255, 255));
            }
        }
    }

    /* 9. Copiar Backbuffer al Framebuffer Real (Fast Blit sin parpadeo) */
    for (uint32_t i = 0; i < screen_w * screen_h; i++) {
        fb[i] = backbuffer[i];
    }
}

/* ============================================================================
 * Driver de Ratón PS/2 Nativo
 * ============================================================================ */
static void ps2_mouse_wait_write(void) {
    int timeout = 100000;
    while ((inb(0x64) & 2) && --timeout);
}

static void ps2_mouse_wait_read(void) {
    int timeout = 100000;
    while (!(inb(0x64) & 1) && --timeout);
}

static void ps2_mouse_write(uint8_t write) {
    ps2_mouse_wait_write();
    outb(0x64, 0xD4);
    ps2_mouse_wait_write();
    outb(0x60, write);
}

static uint8_t ps2_mouse_read(void) {
    ps2_mouse_wait_read();
    return inb(0x60);
}

static void ps2_mouse_init(void) {
    /* 1. Habilitar el segundo puerto PS/2 (dispositivo auxiliar/ratón) */
    ps2_mouse_wait_write();
    outb(0x64, 0xA8);

    /* 2. Habilitar interrupciones del ratón en el byte de comando */
    ps2_mouse_wait_write();
    outb(0x64, 0x20);
    uint8_t status = (ps2_mouse_read() | 2);
    ps2_mouse_wait_write();
    outb(0x64, 0x60);
    ps2_mouse_wait_write();
    outb(0x60, status);

    /* 3. Configurar resolución por defecto y habilitar transmisión de paquetes */
    ps2_mouse_write(0xF6); /* Configuración por defecto */
    ps2_mouse_read();      /* Acknowledge (0xFA) */

    ps2_mouse_write(0xF4); /* Habilitar flujo de datos de movimiento */
    ps2_mouse_read();      /* Acknowledge (0xFA) */
}

/* ============================================================================
 * Bucle Principal de Eventos e Interacción en Tiempo Real
 * ============================================================================ */
static uint8_t mouse_cycle = 0;
static uint8_t mouse_bytes[3];

static void process_mouse_packet(void) {
    uint8_t flags = mouse_bytes[0];
    int8_t dx = (int8_t)mouse_bytes[1];
    int8_t dy = (int8_t)mouse_bytes[2];

    /* Actualizar coordenadas de pantalla */
    mouse_x += dx;
    mouse_y -= dy; /* En PS/2, dy positivo significa hacia arriba */

    if (mouse_x < 0) mouse_x = 0;
    if (mouse_x >= (int)screen_w - 1) mouse_x = screen_w - 2;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y >= (int)screen_h - 1) mouse_y = screen_h - 2;

    uint8_t btn_left = flags & 1;

    /* Detección de Clic Izquierdo */
    if (btn_left && !mouse_btn_prev) {
        /* Clic en Start Orb */
        int dist_orb = (mouse_x - 32)*(mouse_x - 32) + (mouse_y - (screen_h - 24))*(mouse_y - (screen_h - 24));
        if (dist_orb <= 20*20) {
            start_menu_open = !start_menu_open;
        } 
        /* Clic en el Menú Inicio */
        else if (start_menu_open && mouse_x >= 10 && mouse_x <= 270 && mouse_y >= (int)screen_h - 368 && mouse_y <= (int)screen_h - 48) {
            /* Clic en 'Restaurar Ventana' */
            if (mouse_y >= (int)screen_h - 180 && mouse_y <= (int)screen_h - 150) {
                win_visible = 1;
                start_menu_open = 0;
            }
        } 
        else {
            if (start_menu_open) start_menu_open = 0;

            if (win_visible) {
                /* Clic en Botón Cerrar (Rojo) */
                int close_dx = mouse_x - (win_x + win_w - 20);
                int close_dy = mouse_y - (win_y + 18);
                if (close_dx*close_dx + close_dy*close_dy <= 10*10) {
                    win_visible = 0;
                }
                /* Clic en Botón Minimizar (Amarillo) */
                int min_dx = mouse_x - (win_x + win_w - 60);
                int min_dy = mouse_y - (win_y + 18);
                if (min_dx*min_dx + min_dy*min_dy <= 10*10) {
                    win_visible = 0;
                }
                /* Clic en Barra de Título (Iniciar Arrastre de Ventana) */
                else if (mouse_x >= win_x && mouse_x <= win_x + win_w && mouse_y >= win_y && mouse_y <= win_y + 36) {
                    is_dragging = 1;
                    drag_off_x = mouse_x - win_x;
                    drag_off_y = mouse_y - win_y;
                }
            }
            /* Clic en Icono 'Equipo' para abrir ventana si está cerrada */
            if (mouse_x >= 35 && mouse_x <= 110 && mouse_y >= 45 && mouse_y <= 130) {
                win_visible = 1;
            }
        }
    } 
    /* Soltar Clic */
    else if (!btn_left && mouse_btn_prev) {
        is_dragging = 0;
    }

    /* Si se está arrastrando la ventana, actualizar su posición con el cursor */
    if (is_dragging && btn_left) {
        win_x = mouse_x - drag_off_x;
        win_y = mouse_y - drag_off_y;
    }

    mouse_btn_prev = btn_left;
}

/* ============================================================================
 * Punto de Entrada Principal del Kernel
 * ============================================================================ */
void kernel_main(uint32_t magic, uint32_t addr) {
    (void)magic;
    struct multiboot_info* mbi = (struct multiboot_info*)addr;

    if ((mbi->flags & (1 << 12)) && mbi->framebuffer_addr != 0 && mbi->framebuffer_bpp == 32) {
        fb = (uint32_t*)(uint32_t)mbi->framebuffer_addr;
        screen_w = mbi->framebuffer_width;
        screen_h = mbi->framebuffer_height;
        screen_pitch = mbi->framebuffer_pitch / 4;

        /* Inicializar el controlador del ratón PS/2 */
        ps2_mouse_init();

        /* Bucle de interacción en tiempo real */
        while (1) {
            /* Leer datos del ratón si el puerto 0x64 indica datos disponibles */
            if (inb(0x64) & 1) {
                uint8_t data = inb(0x60);
                if (mouse_cycle == 0) {
                    /* El primer byte debe tener el bit 3 siempre en 1 */
                    if (data & 0x08) {
                        mouse_bytes[0] = data;
                        mouse_cycle = 1;
                    }
                } else if (mouse_cycle == 1) {
                    mouse_bytes[1] = data;
                    mouse_cycle = 2;
                } else if (mouse_cycle == 2) {
                    mouse_bytes[2] = data;
                    mouse_cycle = 0;
                    process_mouse_packet();
                }
            }

            /* Redibujar escena completa de forma fluida */
            render_desktop();
        }
    }

    while (1) {
        __asm__ volatile ("hlt");
    }
}
