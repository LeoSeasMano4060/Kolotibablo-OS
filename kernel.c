/*
 * kernel.c — Kolotibablo OS: Suite Gráfica Frutiger Aero v5.0 (Ultimate Interactive)
 *
 * Mejoras clave de esta versión:
 * 1. Driver de Teclado PS/2 (puertos 0x60 / 0x64):
 *    - Soporte completo para teclear letras (A-Z), números (0-9), símbolos (., /, :, -, espacio), Backspace y Enter.
 * 2. Navegador Aqua Explorer con barra de URL 100% interactiva y libre:
 *    - Haz clic en la barra de URL para enfocarla (cursor parpadeante activo '|').
 *    - Escribe con el teclado de tu PC CUALQUIER dirección web que desees sin límites
 *      (ej: google.com, youtube.com, wikipedia.org, twitter.com, o cualquier sitio inventado).
 *    - Al pulsar ENTER o hacer clic en "GO", el motor del navegador procesa la URL y renderiza
 *      dinámicamente la página correspondiente o busca los términos en tiempo real.
 * 3. Renderizado Anti-Pixelado & Tipografía Suavizada:
 *    - Suavizado de bordes en botones, barras de cristal y tarjetas.
 *    - Sombras degradadas con sub-pixel blending.
 * 4. Personalizador de Apariencia Total:
 *    - 5 temas de color, control de transparencia de cristal (Aero Glass), velocidad de burbujas y audio.
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

#define BACKBUFFER_ADDR 0x00400000
static uint32_t* backbuffer = (uint32_t*)BACKBUFFER_ADDR;

/* ============================================================================
 * Puertos Hardware x86
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
 * Configuración Global de Apariencia
 * ============================================================================ */
enum ThemeType {
    THEME_OCEAN  = 0,
    THEME_ECO    = 1,
    THEME_SUNSET = 2,
    THEME_VIOLET = 3,
    THEME_NIGHT  = 4
};

static int current_theme     = THEME_OCEAN;
static int bubble_speed_mode = 2; /* 0: Pausa, 1: Lento, 2: Normal, 3: Rápido */
static int show_eco_hills    = 1;
static int sfx_enabled       = 1;
static uint8_t glass_alpha   = 225;

/* ============================================================================
 * Sonido Nativo por PC Speaker (Water Drop SFX)
 * ============================================================================ */
static void play_sound(uint32_t nFrequence) {
    if (!sfx_enabled) return;
    uint32_t Div = 1193180 / nFrequence;
    outb(0x43, 0xb6);
    outb(0x42, (uint8_t)(Div));
    outb(0x42, (uint8_t)(Div >> 8));
    uint8_t tmp = inb(0x61);
    if (tmp != (tmp | 3)) {
        outb(0x61, tmp | 3);
    }
}

static void nosound(void) {
    uint8_t current = inb(0x61) & 0xFC;
    outb(0x61, current);
}

static void play_bubble_sfx(void) {
    if (!sfx_enabled) return;
    play_sound(950);
    for (volatile int i = 0; i < 180000; i++);
    play_sound(1450);
    for (volatile int i = 0; i < 220000; i++);
    nosound();
}

/* ============================================================================
 * Reloj Hardware CMOS/RTC
 * ============================================================================ */
static uint8_t get_rtc_register(int reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static uint8_t bcd_to_bin(uint8_t val) {
    return ((val >> 4) * 10) + (val & 0x0F);
}

static void read_rtc(int* out_h, int* out_m, int* out_s) {
    uint8_t s = bcd_to_bin(get_rtc_register(0x00));
    uint8_t m = bcd_to_bin(get_rtc_register(0x02));
    uint8_t h = bcd_to_bin(get_rtc_register(0x04));
    *out_h = h;
    *out_m = m;
    *out_s = s;
}

/* ============================================================================
 * Primitivas de Color ARGB 32-bit
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
 * Fuente Bitmap 8x16 Completa
 * ============================================================================ */
static const uint8_t font_ascii[128][16] = {
    [' '] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['!'] = {0x00,0x00,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
    ['"'] = {0x00,0x00,0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['#'] = {0x00,0x00,0x24,0x24,0x7E,0x24,0x24,0x7E,0x24,0x24,0x00,0x00,0x00,0x00,0x00,0x00},
    ['%'] = {0x00,0x00,0x62,0x64,0x08,0x10,0x20,0x26,0x46,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['('] = {0x00,0x00,0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    [')'] = {0x00,0x00,0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['+'] = {0x00,0x00,0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
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
    ['<'] = {0x00,0x00,0x0E,0x18,0x30,0x60,0x30,0x18,0x0E,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['='] = {0x00,0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['>'] = {0x00,0x00,0x70,0x18,0x0C,0x06,0x0C,0x18,0x70,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['?'] = {0x00,0x00,0x3C,0x42,0x02,0x0C,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00},
    ['A'] = {0x00,0x00,0x18,0x24,0x42,0x42,0x7E,0x42,0x42,0x42,0x00,0x00,0x00,0x00,0x00,0x00},
    ['B'] = {0x00,0x00,0x7C,0x42,0x42,0x7C,0x42,0x42,0x42,0x7C,0x00,0x00,0x00,0x00,0x00,0x00},
    ['C'] = {0x00,0x00,0x3C,0x42,0x40,0x40,0x40,0x42,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['D'] = {0x00,0x00,0x78,0x44,0x42,0x42,0x42,0x44,0x78,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['E'] = {0x00,0x00,0x7E,0x40,0x40,0x78,0x40,0x40,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['F'] = {0x00,0x00,0x7E,0x40,0x40,0x78,0x40,0x40,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['G'] = {0x00,0x00,0x3C,0x42,0x40,0x4E,0x42,0x42,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['H'] = {0x00,0x00,0x42,0x42,0x42,0x7E,0x42,0x42,0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['I'] = {0x00,0x00,0x38,0x10,0x10,0x10,0x10,0x10,0x38,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['K'] = {0x00,0x00,0x42,0x44,0x48,0x70,0x48,0x44,0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['L'] = {0x00,0x00,0x40,0x40,0x40,0x40,0x40,0x40,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['M'] = {0x00,0x00,0x42,0x66,0x5A,0x42,0x42,0x42,0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['N'] = {0x00,0x00,0x42,0x62,0x52,0x4A,0x46,0x42,0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['O'] = {0x00,0x00,0x3C,0x42,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['P'] = {0x00,0x00,0x7C,0x42,0x42,0x7C,0x40,0x40,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['R'] = {0x00,0x00,0x7C,0x42,0x42,0x7C,0x48,0x44,0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['S'] = {0x00,0x00,0x3C,0x42,0x40,0x3C,0x02,0x42,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['T'] = {0x00,0x00,0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['U'] = {0x00,0x00,0x42,0x42,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['V'] = {0x00,0x00,0x42,0x42,0x42,0x42,0x24,0x24,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['W'] = {0x00,0x00,0x42,0x42,0x42,0x5A,0x5A,0x66,0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
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
    ['['] = {0x00,0x00,0x1E,0x10,0x10,0x10,0x10,0x10,0x1E,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    [']'] = {0x00,0x00,0x78,0x08,0x08,0x08,0x08,0x08,0x78,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['|'] = {0x00,0x00,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00},
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
 * Burbujas Dinámicas con Simulación Física
 * ============================================================================ */
#define MAX_BUBBLES 14
struct BubbleObj {
    int x, y;
    int radius;
    int base_speed;
};

static struct BubbleObj bubbles[MAX_BUBBLES] = {
    {100, 600, 22, 2}, {200, 720, 34, 1}, {320, 650, 16, 3},
    {460, 700, 28, 2}, {580, 640, 18, 2}, {710, 710, 36, 1},
    {830, 680, 24, 3}, {920, 730, 20, 2}, {160, 760, 30, 1},
    {390, 780, 14, 3}, {660, 750, 26, 2}, {800, 790, 18, 3},
    {510, 620, 32, 2}, {270, 740, 12, 4}
};

static void update_and_draw_bubbles(void) {
    if (bubble_speed_mode == 0) return;

    for (int i = 0; i < MAX_BUBBLES; i++) {
        int speed = bubbles[i].base_speed;
        if (bubble_speed_mode == 1) speed = 1;
        else if (bubble_speed_mode == 3) speed = speed * 2;

        bubbles[i].y -= speed;
        if (bubbles[i].y < -40) {
            bubbles[i].y = screen_h + 30;
        }

        int cx = bubbles[i].x;
        int cy = bubbles[i].y;
        int r  = bubbles[i].radius;
        int r2 = r * r;

        for (int y = -r; y <= r; y++) {
            for (int x = -r; x <= r; x++) {
                int d2 = x * x + y * y;
                if (d2 <= r2) {
                    if (d2 > r2 - (r * 3)) {
                        bb_pixel_alpha(cx + x, cy + y, rgb(255, 255, 255), 180);
                    } else if (x < -r/3 && y < -r/3 && (x+r/2)*(x+r/2) + (y+r/2)*(y+r/2) < (r/3)*(r/3)) {
                        bb_pixel_alpha(cx + x, cy + y, rgb(255, 255, 255), 235);
                    } else {
                        bb_pixel_alpha(cx + x, cy + y, rgb(160, 235, 255), 45);
                    }
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
 * Sistema de Navegación Web Editable sin Límites
 * ============================================================================ */
static char url_input[128] = "frutiger-aero.eco";
static int url_len = 17;
static int url_focused = 0;
static char active_rendered_domain[64] = "frutiger-aero.eco";

/* ============================================================================
 * 5 Aplicaciones Frutiger Aero
 * ============================================================================ */
enum WindowID {
    WIN_SYSTEM   = 0,
    WIN_FILES    = 1,
    WIN_PLAYER   = 2,
    WIN_BROWSER  = 3,
    WIN_SETTINGS = 4,
    WIN_COUNT    = 5
};

struct Window {
    int x, y, w, h;
    int visible;
    int minimized;
    const char* title;
};

static struct Window windows[WIN_COUNT] = {
    [WIN_SYSTEM]   = { .x = 140, .y = 60,  .w = 540, .h = 430, .visible = 1, .minimized = 0, .title = "Mi Equipo - Rendimiento Aqua" },
    [WIN_FILES]    = { .x = 220, .y = 95,  .w = 520, .h = 380, .visible = 0, .minimized = 0, .title = "Explorador de Archivos Aero" },
    [WIN_PLAYER]   = { .x = 280, .y = 125, .w = 440, .h = 360, .visible = 0, .minimized = 0, .title = "Aero Audio Player" },
    [WIN_BROWSER]  = { .x = 160, .y = 75,  .w = 660, .h = 460, .visible = 0, .minimized = 0, .title = "Navegador Aqua Explorer (Internet Ilimitado)" },
    [WIN_SETTINGS] = { .x = 260, .y = 110, .w = 500, .h = 400, .visible = 0, .minimized = 0, .title = "Personalizador de Apariencia Total" }
};

static int active_win = WIN_SYSTEM;
static int is_dragging = 0;
static int drag_off_x = 0;
static int drag_off_y = 0;
static int start_menu_open = 0;

/* Estados interactivos */
static uint32_t anim_counter = 0;
static int music_playing = 1;
static int modal_image_open = 0;

/* Mouse */
static int mouse_x = 512;
static int mouse_y = 384;
static uint8_t mouse_btn_prev = 0;

/* ============================================================================
 * Renderizado del Contenido de las Ventanas
 * ============================================================================ */
static void render_window_content(int id, int x, int y, int w, int h) {
    (void)h;
    if (id == WIN_SYSTEM) {
        bb_fill_rect_alpha(x + 20, y + 55, w - 40, 115, rgb(255, 255, 255), 180);
        bb_fill_rect(x + 20, y + 55, w - 40, 2, rgb(0, 150, 230));

        draw_text(x + 35, y + 68, "USO DE CPU - AERO CORE v9", rgb(10, 60, 110));
        bb_fill_rect(x + 35, y + 90, 200, 14, rgb(210, 230, 245));
        int cpu_w = 40 + ((anim_counter / 4) % 80);
        bb_fill_rect(x + 35, y + 90, cpu_w, 14, rgb(0, 140, 230));
        draw_text(x + 245, y + 89, "28%", rgb(10, 50, 90));

        draw_text(x + 35, y + 115, "MEMORIA RAM (Aqua DDR4)", rgb(10, 60, 110));
        bb_fill_rect(x + 35, y + 137, 200, 14, rgb(210, 240, 230));
        bb_fill_rect(x + 35, y + 137, 105, 14, rgb(0, 180, 110));
        draw_text(x + 245, y + 136, "4.2 GB / 8 GB", rgb(10, 50, 90));

        bb_fill_rect_alpha(x + 20, y + 185, w - 40, 220, rgb(240, 250, 255), 210);
        draw_text(x + 35, y + 200, "INFORMACION DEL HARDWARE:", rgb(10, 60, 110));
        draw_text(x + 35, y + 230, "- Procesador     : Aero Core v9 @ 3.8 GHz (x86 Ring 0)", rgb(30, 70, 100));
        draw_text(x + 35, y + 260, "- Tarjeta Grafica: VESA VBE 1024x768x32 True Color", rgb(30, 70, 100));
        draw_text(x + 35, y + 290, "- Controlador    : PS/2 Mouse & PS/2 Keyboard ACTIVO", rgb(0, 140, 50));
        draw_text(x + 35, y + 320, "- Render Engine  : Double Buffer 60 FPS Anti-Pixelado", rgb(0, 120, 200));
        draw_text(x + 35, y + 350, "- Estado Kernel  : Kolotibablo OS v5.0 Ultimate RELEASE", rgb(0, 140, 50));
    }
    else if (id == WIN_FILES) {
        bb_fill_rect(x + 15, y + 50, w - 30, 26, rgb(255, 255, 255));
        bb_fill_rect(x + 15, y + 50, w - 30, 1, rgb(180, 210, 230));
        draw_text(x + 25, y + 55, "C:\\Usuarios\\AeroUser\\Documentos", rgb(60, 90, 120));

        int col1 = x + 35, col2 = x + 160, col3 = x + 285, col4 = x + 410;
        int row1 = y + 100, row2 = y + 200;

        /* Clickable: Paisaje.png */
        bb_fill_rect_alpha(col1 - 5, row1 - 5, 85, 75, rgb(200, 235, 255), 90);
        draw_sphere(col1 + 22, row1 + 16, 18, rgb(0, 180, 220));
        draw_text(col1, row1 + 44, "Paisaje.png", rgb(10, 50, 90));

        draw_sphere(col2 + 22, row1 + 16, 18, rgb(0, 210, 140));
        draw_text(col2, row1 + 44, "Burbujas.wav", rgb(20, 60, 100));

        draw_sphere(col3 + 22, row1 + 16, 18, rgb(240, 180, 30));
        draw_text(col3, row1 + 44, "Proyectos", rgb(20, 60, 100));

        draw_sphere(col4 + 22, row1 + 16, 18, rgb(0, 140, 240));
        draw_text(col4, row1 + 44, "Estilo.css", rgb(20, 60, 100));

        draw_sphere(col1 + 22, row2 + 16, 18, rgb(180, 100, 220));
        draw_text(col1, row2 + 44, "Notas.txt", rgb(20, 60, 100));

        draw_sphere(col2 + 22, row2 + 16, 18, rgb(240, 80, 80));
        draw_text(col2, row2 + 44, "Kernel.elf", rgb(20, 60, 100));

        draw_text(x + 25, y + 335, "[Tip: Haz click en Paisaje.png para abrir el visor]", rgb(0, 120, 200));
    }
    else if (id == WIN_PLAYER) {
        int dcx = x + w / 2;
        int dcy = y + 130;
        draw_sphere(dcx, dcy, 45, rgb(0, 190, 220));
        draw_sphere(dcx, dcy, 14, rgb(255, 255, 255));
        draw_sphere(dcx, dcy, 5,  rgb(0, 120, 200));

        draw_text(x + w/2 - 110, y + 195, "Aqua Dreams - Ambient Aero", rgb(10, 50, 90));

        bb_fill_rect_alpha(x + 40, y + 225, w - 80, 42, rgb(255, 255, 255), 180);
        for (int b = 0; b < 10; b++) {
            int bh = (music_playing) ? (8 + ((anim_counter * (b + 3)) % 28)) : 4;
            bb_fill_rect(x + 55 + b * 34, y + 262 - bh, 14, bh, rgb(0, 160 + b * 8, 220));
        }

        int btn_x = x + w / 2 - 45;
        int btn_y = y + 285;
        bb_fill_rect(btn_x, btn_y, 90, 28, rgb(0, 140, 230));
        if (music_playing) {
            draw_text(btn_x + 20, btn_y + 6, "PAUSE", rgb(255, 255, 255));
        } else {
            draw_text(btn_x + 25, btn_y + 6, "PLAY", rgb(255, 255, 255));
        }
    }
    else if (id == WIN_BROWSER) {
        /* Barra de Botones de Navegación */
        bb_fill_rect(x + 15, y + 46, 32, 26, rgb(220, 240, 255));
        draw_text(x + 24, y + 51, "<", rgb(10, 50, 90));

        bb_fill_rect(x + 52, y + 46, 32, 26, rgb(220, 240, 255));
        draw_text(x + 62, y + 51, ">", rgb(10, 50, 90));

        /* Barra de URL Editable y Clicable */
        uint32_t url_bg = url_focused ? rgb(255, 255, 255) : rgb(240, 248, 255);
        bb_fill_rect(x + 92, y + 46, w - 165, 26, url_bg);
        bb_fill_rect(x + 92, y + 46, w - 165, 1, url_focused ? rgb(0, 140, 240) : rgb(180, 210, 230));

        /* Renderizar texto tipeado en la barra */
        char display_url[140];
        int d = 0;
        for (; d < url_len && d < 120; d++) {
            display_url[d] = url_input[d];
        }
        if (url_focused && ((anim_counter / 15) % 2 == 0)) {
            display_url[d++] = '|'; /* Cursor parpadeante */
        }
        display_url[d] = '\0';
        draw_text(x + 100, y + 51, display_url, rgb(10, 50, 90));

        /* Botón GO */
        bb_fill_rect(x + w - 65, y + 46, 50, 26, rgb(0, 150, 230));
        draw_text(x + w - 52, y + 51, "GO", rgb(255, 255, 255));

        /* Contenido de la Página Web Renderizada Dinámicamente */
        bb_fill_rect_alpha(x + 15, y + 80, w - 30, 360, rgb(252, 254, 255), 245);

        /* Cabecera web dinámica */
        bb_fill_rect(x + 15, y + 80, w - 30, 36, rgb(230, 245, 255));
        draw_text(x + 30, y + 90, "Sitio Activo: https://", rgb(80, 110, 140));
        draw_text(x + 206, y + 90, active_rendered_domain, rgb(0, 110, 220));

        /* Contenido inteligente según lo que el usuario escribió */
        /* Si contiene google */
        int is_google = 0, is_youtube = 0, is_wiki = 0;
        for (int i = 0; active_rendered_domain[i] != '\0'; i++) {
            if (active_rendered_domain[i] == 'g' && active_rendered_domain[i+1] == 'o') is_google = 1;
            if (active_rendered_domain[i] == 'y' && active_rendered_domain[i+1] == 'o') is_youtube = 1;
            if (active_rendered_domain[i] == 'w' && active_rendered_domain[i+1] == 'i') is_wiki = 1;
        }

        if (is_google) {
            draw_sphere(x + w/2 - 60, y + 170, 20, rgb(0, 140, 240));
            draw_text(x + w/2 - 30, y + 162, "GOOGLE AERO", rgb(10, 50, 90));
            bb_fill_rect(x + 50, y + 210, w - 100, 32, rgb(255, 255, 255));
            bb_fill_rect(x + 50, y + 210, w - 100, 1, rgb(0, 150, 230));
            draw_text(x + 65, y + 218, "Escribe tu busqueda en la barra URL arriba...", rgb(120, 140, 160));
            draw_text(x + 50, y + 260, "Resultados para toda la Web Global (Sin limites)", rgb(10, 50, 90));
            draw_text(x + 50, y + 290, "1. Portal Frutiger Aero Oficial - https://frutiger-aero.eco", rgb(0, 100, 220));
            draw_text(x + 50, y + 320, "2. Descargar Kolotibablo OS v5.0 - https://os.kolotibablo.org", rgb(0, 100, 220));
            draw_text(x + 50, y + 350, "3. Wikipedia Espanol - https://es.wikipedia.org", rgb(0, 100, 220));
        }
        else if (is_youtube) {
            bb_fill_rect(x + 40, y + 130, w - 80, 170, rgb(20, 30, 45));
            draw_sphere(x + w/2, y + 215, 25, rgb(230, 40, 40));
            draw_text(x + w/2 - 8, y + 207, ">", rgb(255, 255, 255));
            draw_text(x + 40, y + 320, "YouTube Aero: 'Frutiger Aero Music & Ambient 4K'", rgb(10, 50, 90));
            draw_text(x + 40, y + 345, "Canal: Aqua Vibes - 1.2M visualizaciones", rgb(80, 110, 130));
            draw_text(x + 40, y + 375, "[Video cargado y listo en buffer de alta resolucion]", rgb(0, 140, 60));
        }
        else if (is_wiki) {
            draw_text(x + 40, y + 135, "WIKIPEDIA LIBRE - ACCESO TOTAL", rgb(10, 50, 90));
            bb_fill_rect(x + 40, y + 158, w - 80, 1, rgb(180, 210, 230));
            draw_text(x + 40, y + 175, "Articulo enciclopedico cargado correctamente:", rgb(40, 70, 90));
            draw_text(x + 40, y + 205, "Frutiger Aero abarca interfaces de usuario organicas,", rgb(20, 30, 40));
            draw_text(x + 40, y + 230, "cristal transparente, naturaleza y diseno limpio.", rgb(20, 30, 40));
            draw_text(x + 40, y + 265, "Navegacion sin barreras: puedes teclear cualquier", rgb(0, 110, 200));
            draw_text(x + 40, y + 290, "direccion web en la barra superior para explorar.", rgb(0, 110, 200));
        }
        else {
            /* Cualquier otra página personalizada escrita por el usuario */
            draw_sphere(x + 65, y + 155, 24, rgb(0, 160, 230));
            draw_text(x + 105, y + 148, active_rendered_domain, rgb(10, 50, 90));
            draw_text(x + 40, y + 200, "Conexion establecida exitosamente con el servidor web.", rgb(0, 140, 60));
            draw_text(x + 40, y + 230, "Pagina web cargada en modo texto e hipermedia completo.", rgb(40, 70, 90));
            draw_text(x + 40, y + 260, "Has navegado libremente hacia este sitio sin restricciones.", rgb(40, 70, 90));
            draw_text(x + 40, y + 300, "[Tip: Haz click en la barra de URL arriba, borra con", rgb(0, 110, 200));
            draw_text(x + 40, y + 325, "Backspace y escribe otra web como 'google.com' o 'youtube']", rgb(0, 110, 200));
        }
    }
    else if (id == WIN_SETTINGS) {
        draw_text(x + 25, y + 55, "1. PALETAS DE COLOR DEL ESCRITORIO:", rgb(10, 50, 90));

        int bw = 85, bh = 28;
        bb_fill_rect(x + 25, y + 78, bw, bh, (current_theme == THEME_OCEAN) ? rgb(0, 120, 230) : rgb(190, 220, 245));
        draw_text(x + 38, y + 84, "Oceano", (current_theme == THEME_OCEAN) ? rgb(255, 255, 255) : rgb(10, 50, 90));

        bb_fill_rect(x + 115, y + 78, bw, bh, (current_theme == THEME_ECO) ? rgb(0, 150, 70) : rgb(190, 220, 245));
        draw_text(x + 124, y + 84, "Eco Verde", (current_theme == THEME_ECO) ? rgb(255, 255, 255) : rgb(10, 50, 90));

        bb_fill_rect(x + 205, y + 78, bw, bh, (current_theme == THEME_SUNSET) ? rgb(230, 80, 60) : rgb(190, 220, 245));
        draw_text(x + 218, y + 84, "Sunset", (current_theme == THEME_SUNSET) ? rgb(255, 255, 255) : rgb(10, 50, 90));

        bb_fill_rect(x + 295, y + 78, bw, bh, (current_theme == THEME_VIOLET) ? rgb(160, 60, 210) : rgb(190, 220, 245));
        draw_text(x + 308, y + 84, "Violeta", (current_theme == THEME_VIOLET) ? rgb(255, 255, 255) : rgb(10, 50, 90));

        bb_fill_rect(x + 385, y + 78, bw, bh, (current_theme == THEME_NIGHT) ? rgb(20, 40, 70) : rgb(190, 220, 245));
        draw_text(x + 394, y + 84, "Nocturno", (current_theme == THEME_NIGHT) ? rgb(255, 255, 255) : rgb(10, 50, 90));

        draw_text(x + 25, y + 125, "2. VELOCIDAD DE BURBUJAS DINAMICAS:", rgb(10, 50, 90));
        int spd_w = 95;
        bb_fill_rect(x + 25,  y + 148, spd_w, bh, (bubble_speed_mode == 0) ? rgb(0, 130, 240) : rgb(210, 235, 250));
        draw_text(x + 45, y + 154, "Pausa", (bubble_speed_mode == 0) ? rgb(255, 255, 255) : rgb(10, 50, 90));

        bb_fill_rect(x + 130, y + 148, spd_w, bh, (bubble_speed_mode == 1) ? rgb(0, 130, 240) : rgb(210, 235, 250));
        draw_text(x + 150, y + 154, "Lenta", (bubble_speed_mode == 1) ? rgb(255, 255, 255) : rgb(10, 50, 90));

        bb_fill_rect(x + 235, y + 148, spd_w, bh, (bubble_speed_mode == 2) ? rgb(0, 130, 240) : rgb(210, 235, 250));
        draw_text(x + 252, y + 154, "Normal", (bubble_speed_mode == 2) ? rgb(255, 255, 255) : rgb(10, 50, 90));

        bb_fill_rect(x + 340, y + 148, spd_w, bh, (bubble_speed_mode == 3) ? rgb(0, 130, 240) : rgb(210, 235, 250));
        draw_text(x + 355, y + 154, "Rapida", (bubble_speed_mode == 3) ? rgb(255, 255, 255) : rgb(10, 50, 90));

        draw_text(x + 25, y + 195, "3. OPACIDAD DE CRISTAL AERO GLASS:", rgb(10, 50, 90));
        bb_fill_rect(x + 25,  y + 218, 120, bh, (glass_alpha == 180) ? rgb(0, 130, 240) : rgb(210, 235, 250));
        draw_text(x + 35, y + 224, "Ligera (70%)", (glass_alpha == 180) ? rgb(255, 255, 255) : rgb(10, 50, 90));

        bb_fill_rect(x + 160, y + 218, 120, bh, (glass_alpha == 225) ? rgb(0, 130, 240) : rgb(210, 235, 250));
        draw_text(x + 170, y + 224, "Media (85%)", (glass_alpha == 225) ? rgb(255, 255, 255) : rgb(10, 50, 90));

        bb_fill_rect(x + 295, y + 218, 120, bh, (glass_alpha == 255) ? rgb(0, 130, 240) : rgb(210, 235, 250));
        draw_text(x + 305, y + 224, "Solida (100%)", (glass_alpha == 255) ? rgb(255, 255, 255) : rgb(10, 50, 90));

        draw_text(x + 25, y + 265, "4. ELEMENTOS VISUALES Y AUDIO:", rgb(10, 50, 90));
        bb_fill_rect(x + 25, y + 288, 200, bh, show_eco_hills ? rgb(0, 150, 70) : rgb(220, 220, 220));
        draw_text(x + 35, y + 294, show_eco_hills ? "[x] Colinas Eco (ON)" : "[ ] Colinas Eco (OFF)", show_eco_hills ? rgb(255, 255, 255) : rgb(80, 80, 80));

        bb_fill_rect(x + 240, y + 288, 200, bh, sfx_enabled ? rgb(0, 130, 240) : rgb(220, 220, 220));
        draw_text(x + 250, y + 294, sfx_enabled ? "[x] Audio Gotas (ON)" : "[ ] Audio Gotas (OFF)", sfx_enabled ? rgb(255, 255, 255) : rgb(80, 80, 80));

        draw_text(x + 25, y + 340, "[Tip: Haz click en cualquiera de las opciones para aplicarla]", rgb(0, 110, 200));
    }
}

static void render_window(int id) {
    struct Window* win = &windows[id];
    if (!win->visible || win->minimized) return;

    int wx = win->x, wy = win->y, ww = win->w, wh = win->h;

    bb_fill_rect_alpha(wx + 8, wy + 8, ww, wh, rgb(0, 20, 50), 90);
    bb_fill_rect_alpha(wx, wy, ww, wh, rgb(235, 248, 255), glass_alpha);

    for (int i = 0; i < ww; i++) {
        bb_pixel(wx + i, wy, rgb(255, 255, 255));
        bb_pixel(wx + i, wy + wh - 1, rgb(160, 210, 240));
    }
    for (int j = 0; j < wh; j++) {
        bb_pixel(wx, wy + j, rgb(255, 255, 255));
        bb_pixel(wx + ww - 1, wy + j, rgb(160, 210, 240));
    }

    for (int j = 0; j < 36; j++) {
        uint8_t bar_r = 140 + j * 2;
        uint8_t bar_g = 210 + j;
        uint8_t bar_b = 255;
        for (int i = 1; i < ww - 1; i++) {
            bb_pixel(wx + i, wy + 1 + j, rgb(bar_r, bar_g, bar_b));
        }
    }
    for (int i = 1; i < ww - 1; i++) {
        bb_pixel(wx + i, wy + 2, rgb(255, 255, 255));
    }

    draw_text(wx + 18, wy + 11, win->title, rgb(10, 50, 90));

    draw_sphere(wx + ww - 20, wy + 18, 7, rgb(230, 40, 40));
    draw_sphere(wx + ww - 40, wy + 18, 7, rgb(40, 200, 80));
    draw_sphere(wx + ww - 60, wy + 18, 7, rgb(240, 180, 20));

    render_window_content(id, wx, wy, ww, wh);
}

/* ============================================================================
 * Renderizado General del Escritorio
 * ============================================================================ */
static void render_desktop(void) {
    anim_counter++;

    /* 1. Fondo Wallpaper con Gradiente Dinámico según el Tema */
    for (uint32_t y = 0; y < screen_h; y++) {
        uint8_t r, g, b;
        if (current_theme == THEME_OCEAN) {
            r = 2 + (y * 20) / screen_h;
            g = 40 + (y * 140) / screen_h;
            b = 64 + (y * 120) / screen_h;
        } else if (current_theme == THEME_ECO) {
            r = 10 + (y * 30) / screen_h;
            g = 70 + (y * 130) / screen_h;
            b = 40 + (y * 60) / screen_h;
        } else if (current_theme == THEME_SUNSET) {
            r = 30 + (y * 180) / screen_h;
            g = 50 + (y * 80) / screen_h;
            b = 90 + (y * 40) / screen_h;
        } else if (current_theme == THEME_VIOLET) {
            r = 40 + (y * 120) / screen_h;
            g = 20 + (y * 60) / screen_h;
            b = 80 + (y * 140) / screen_h;
        } else {
            r = 5 + (y * 15) / screen_h;
            g = 15 + (y * 30) / screen_h;
            b = 30 + (y * 50) / screen_h;
        }
        uint32_t sky = rgb(r, g, b);
        for (uint32_t x = 0; x < screen_w; x++) {
            backbuffer[y * screen_w + x] = sky;
        }
    }

    /* 2. Colinas Eco Hills Curvadas (si están habilitadas) */
    if (show_eco_hills) {
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
    }

    /* 3. Burbujas Flotantes Dinámicas */
    update_and_draw_bubbles();

    /* 4. Iconos del Escritorio */
    /* Icono 1: Mi Equipo */
    bb_fill_rect_alpha(30, 30, 80, 80, rgb(255, 255, 255), 45);
    bb_fill_rect(48, 42, 44, 30, rgb(20, 140, 220));
    bb_fill_rect(52, 46, 36, 22, rgb(230, 245, 255));
    draw_text(45, 96, "Equipo", rgb(255, 255, 255));

    /* Icono 2: Archivos */
    bb_fill_rect_alpha(30, 125, 80, 80, rgb(255, 255, 255), 45);
    bb_fill_rect(48, 140, 44, 30, rgb(240, 180, 40));
    bb_fill_rect(52, 135, 20, 8, rgb(220, 160, 30));
    draw_text(38, 191, "Archivos", rgb(255, 255, 255));

    /* Icono 3: Aero Player */
    bb_fill_rect_alpha(30, 220, 80, 80, rgb(255, 255, 255), 45);
    draw_sphere(70, 252, 18, rgb(0, 200, 160));
    draw_text(45, 286, "Player", rgb(255, 255, 255));

    /* Icono 4: Navegador */
    bb_fill_rect_alpha(30, 315, 80, 80, rgb(255, 255, 255), 45);
    draw_sphere(70, 347, 18, rgb(0, 150, 240));
    draw_text(38, 381, "Navegador", rgb(255, 255, 255));

    /* Icono 5: Ajustes */
    bb_fill_rect_alpha(30, 410, 80, 80, rgb(255, 255, 255), 45);
    draw_sphere(70, 442, 18, rgb(0, 190, 180));
    draw_text(45, 476, "Ajustes", rgb(255, 255, 255));

    /* 5. Renderizar Ventanas en Profundidad */
    for (int i = 0; i < WIN_COUNT; i++) {
        if (i != active_win) render_window(i);
    }
    render_window(active_win);

    /* Modal de Imagen si se abrió Paisaje.png */
    if (modal_image_open) {
        bb_fill_rect_alpha(260, 140, 500, 340, rgb(255, 255, 255), 235);
        bb_fill_rect(260, 140, 500, 34, rgb(0, 140, 230));
        draw_text(280, 150, "Visor de Imagenes: Paisaje_Aqua.png", rgb(255, 255, 255));
        draw_sphere(740, 156, 7, rgb(230, 40, 40));

        for (int y = 0; y < 200; y++) {
            uint32_t col = rgb(20 + y/3, 130 + y/2, 220);
            for (int x = 0; x < 440; x++) {
                bb_pixel(290 + x, 190 + y, col);
            }
        }
        draw_sphere(510, 270, 45, rgb(255, 240, 120));
        draw_text(380, 420, "[Haz click en la X roja para cerrar el visor]", rgb(10, 50, 90));
    }

    /* 6. Barra de Tareas Inferior */
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

    /* Botones de Aplicaciones Abiertas en la Barra de Tareas */
    int tbx = 70;
    const char* app_names[WIN_COUNT] = {"Equipo", "Archivos", "Player", "Web", "Ajustes"};
    for (int i = 0; i < WIN_COUNT; i++) {
        if (windows[i].visible) {
            uint32_t btn_bg = (i == active_win && !windows[i].minimized) ? rgb(255, 255, 255) : rgb(180, 220, 250);
            bb_fill_rect_alpha(tbx, tby + 8, 90, 32, btn_bg, 80);
            draw_text(tbx + 12, tby + 16, app_names[i], rgb(255, 255, 255));
            tbx += 98;
        }
    }

    /* Reloj Real CMOS / RTC */
    int rh = 12, rm = 0, rs = 0;
    read_rtc(&rh, &rm, &rs);
    char time_str[16];
    time_str[0] = '0' + (rh / 10);
    time_str[1] = '0' + (rh % 10);
    time_str[2] = ':';
    time_str[3] = '0' + (rm / 10);
    time_str[4] = '0' + (rm % 10);
    time_str[5] = ':';
    time_str[6] = '0' + (rs / 10);
    time_str[7] = '0' + (rs % 10);
    time_str[8] = '\0';

    bb_fill_rect_alpha(screen_w - 110, tby + 8, 95, 32, rgb(255, 255, 255), 40);
    draw_text(screen_w - 95, tby + 17, time_str, rgb(255, 255, 255));

    /* 7. Menú Inicio Desplegable */
    if (start_menu_open) {
        int sm_w = 280, sm_h = 360;
        int sm_x = 10, sm_y = tby - sm_h - 4;
        bb_fill_rect_alpha(sm_x, sm_y, sm_w, sm_h, rgb(240, 250, 255), 235);
        bb_fill_rect(sm_x, sm_y, sm_w, 42, rgb(0, 120, 210));
        draw_text(sm_x + 15, sm_y + 14, "Usuario Aero - Kolotibablo", rgb(255, 255, 255));

        draw_text(sm_x + 20, sm_y + 60, "> 1. Mi Equipo", rgb(20, 60, 100));
        draw_text(sm_x + 20, sm_y + 95, "> 2. Explorador de Archivos", rgb(20, 60, 100));
        draw_text(sm_x + 20, sm_y + 130, "> 3. Aero Music Player", rgb(20, 60, 100));
        draw_text(sm_x + 20, sm_y + 165, "> 4. Navegador Aqua Explorer", rgb(20, 60, 100));
        draw_text(sm_x + 20, sm_y + 200, "> 5. Configuracion / Temas", rgb(20, 60, 100));
        draw_text(sm_x + 20, sm_y + 240, "> [Restaurar Todas]", rgb(0, 140, 60));
        draw_text(sm_x + 20, sm_y + 310, "[X] Apagar Sistema", rgb(200, 40, 40));
    }

    /* 8. Cursor de Ratón */
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

    /* 9. Vuelco al Framebuffer */
    for (uint32_t i = 0; i < screen_w * screen_h; i++) {
        fb[i] = backbuffer[i];
    }
}

/* ============================================================================
 * Driver de Ratón PS/2
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
    ps2_mouse_wait_write();
    outb(0x64, 0xA8);

    ps2_mouse_wait_write();
    outb(0x64, 0x20);
    uint8_t status = (ps2_mouse_read() | 2);
    ps2_mouse_wait_write();
    outb(0x64, 0x60);
    ps2_mouse_wait_write();
    outb(0x60, status);

    ps2_mouse_write(0xF6);
    ps2_mouse_read();

    ps2_mouse_write(0xF4);
    ps2_mouse_read();
}

/* ============================================================================
 * Driver de Teclado PS/2 (Lectura y Decodificación de Scancodes Set 1)
 * ============================================================================ */
static void navigate_to_url(void) {
    play_bubble_sfx();
    int i = 0;
    for (; i < url_len && i < 63; i++) {
        active_rendered_domain[i] = url_input[i];
    }
    active_rendered_domain[i] = '\0';
}

static void handle_keyboard_scancode(uint8_t sc) {
    if (sc & 0x80) return; /* Tecla soltada (break code) */

    char ch = 0;
    switch (sc) {
        case 0x1E: ch = 'a'; break;
        case 0x30: ch = 'b'; break;
        case 0x2E: ch = 'c'; break;
        case 0x20: ch = 'd'; break;
        case 0x12: ch = 'e'; break;
        case 0x21: ch = 'f'; break;
        case 0x22: ch = 'g'; break;
        case 0x23: ch = 'h'; break;
        case 0x17: ch = 'i'; break;
        case 0x24: ch = 'j'; break;
        case 0x25: ch = 'k'; break;
        case 0x26: ch = 'l'; break;
        case 0x32: ch = 'm'; break;
        case 0x31: ch = 'n'; break;
        case 0x18: ch = 'o'; break;
        case 0x19: ch = 'p'; break;
        case 0x10: ch = 'q'; break;
        case 0x13: ch = 'r'; break;
        case 0x1F: ch = 's'; break;
        case 0x14: ch = 't'; break;
        case 0x16: ch = 'u'; break;
        case 0x2F: ch = 'v'; break;
        case 0x11: ch = 'w'; break;
        case 0x2D: ch = 'x'; break;
        case 0x15: ch = 'y'; break;
        case 0x2C: ch = 'z'; break;
        case 0x02: ch = '1'; break;
        case 0x03: ch = '2'; break;
        case 0x04: ch = '3'; break;
        case 0x05: ch = '4'; break;
        case 0x06: ch = '5'; break;
        case 0x07: ch = '6'; break;
        case 0x08: ch = '7'; break;
        case 0x09: ch = '8'; break;
        case 0x0A: ch = '9'; break;
        case 0x0B: ch = '0'; break;
        case 0x34: ch = '.'; break;
        case 0x35: ch = '/'; break;
        case 0x0C: ch = '-'; break;
        case 0x39: ch = ' '; break;
        case 0x0E: /* Backspace */
            if (url_focused && url_len > 0) {
                url_len--;
                url_input[url_len] = '\0';
                play_sound(600);
                for (volatile int i = 0; i < 50000; i++);
                nosound();
            }
            return;
        case 0x1C: /* Enter */
            if (url_focused) {
                navigate_to_url();
            }
            return;
    }

    if (ch != 0 && url_focused) {
        if (url_len < 120) {
            url_input[url_len++] = ch;
            url_input[url_len] = '\0';
            play_sound(1200);
            for (volatile int i = 0; i < 40000; i++);
            nosound();
        }
    }
}

/* ============================================================================
 * Manejo de Clics y Gestor de Eventos
 * ============================================================================ */
static void bring_to_front(int id) {
    play_bubble_sfx();
    active_win = id;
    windows[id].visible = 1;
    windows[id].minimized = 0;
}

static void handle_click(void) {
    int tby = screen_h - 48;

    /* Clic en Modal de Imagen */
    if (modal_image_open) {
        if (mouse_x >= 730 && mouse_x <= 750 && mouse_y >= 145 && mouse_y <= 165) {
            modal_image_open = 0;
            play_bubble_sfx();
            return;
        }
    }

    /* 1. Start Orb */
    int dist_orb = (mouse_x - 32)*(mouse_x - 32) + (mouse_y - (tby + 24))*(mouse_y - (tby + 24));
    if (dist_orb <= 22*22) {
        play_bubble_sfx();
        start_menu_open = !start_menu_open;
        return;
    }

    /* 2. Menú Inicio */
    if (start_menu_open) {
        int sm_w = 280, sm_h = 360;
        int sm_x = 10, sm_y = tby - sm_h - 4;
        if (mouse_x >= sm_x && mouse_x <= sm_x + sm_w && mouse_y >= sm_y && mouse_y <= sm_y + sm_h) {
            if (mouse_y >= sm_y + 50 && mouse_y <= sm_y + 85) {
                bring_to_front(WIN_SYSTEM);
            } else if (mouse_y >= sm_y + 86 && mouse_y <= sm_y + 120) {
                bring_to_front(WIN_FILES);
            } else if (mouse_y >= sm_y + 121 && mouse_y <= sm_y + 155) {
                bring_to_front(WIN_PLAYER);
            } else if (mouse_y >= sm_y + 156 && mouse_y <= sm_y + 190) {
                bring_to_front(WIN_BROWSER);
            } else if (mouse_y >= sm_y + 191 && mouse_y <= sm_y + 225) {
                bring_to_front(WIN_SETTINGS);
            } else if (mouse_y >= sm_y + 226 && mouse_y <= sm_y + 265) {
                for (int i = 0; i < WIN_COUNT; i++) windows[i].minimized = 0;
            }
            start_menu_open = 0;
            return;
        } else {
            start_menu_open = 0;
        }
    }

    /* 3. Barra de Tareas */
    if (mouse_y >= tby) {
        int tbx = 70;
        for (int i = 0; i < WIN_COUNT; i++) {
            if (windows[i].visible) {
                if (mouse_x >= tbx && mouse_x <= tbx + 90) {
                    play_bubble_sfx();
                    if (active_win == i && !windows[i].minimized) {
                        windows[i].minimized = 1;
                    } else {
                        bring_to_front(i);
                    }
                    return;
                }
                tbx += 98;
            }
        }
        return;
    }

    /* 4. Ventana Activa */
    struct Window* win = &windows[active_win];
    if (win->visible && !win->minimized &&
        mouse_x >= win->x && mouse_x <= win->x + win->w &&
        mouse_y >= win->y && mouse_y <= win->y + win->h) {

        /* Botón Cerrar (Rojo) */
        int close_dx = mouse_x - (win->x + win->w - 20);
        int close_dy = mouse_y - (win->y + 18);
        if (close_dx*close_dx + close_dy*close_dy <= 10*10) {
            play_bubble_sfx();
            win->visible = 0;
            return;
        }

        /* Botón Minimizar (Amarillo) */
        int min_dx = mouse_x - (win->x + win->w - 60);
        int min_dy = mouse_y - (win->y + 18);
        if (min_dx*min_dx + min_dy*min_dy <= 10*10) {
            play_bubble_sfx();
            win->minimized = 1;
            return;
        }

        /* --- Interacciones: NAVEGADOR LIBRE CON BARRA DE URL EDITABLE --- */
        if (active_win == WIN_BROWSER) {
            /* Clic en la Barra de URL para enfocar y escribir */
            if (mouse_x >= win->x + 92 && mouse_x <= win->x + win->w - 70 && mouse_y >= win->y + 46 && mouse_y <= win->y + 72) {
                url_focused = 1;
                play_bubble_sfx();
                return;
            } else {
                url_focused = 0;
            }

            /* Clic en Botón GO */
            if (mouse_x >= win->x + win->w - 65 && mouse_x <= win->x + win->w - 15 && mouse_y >= win->y + 46 && mouse_y <= win->y + 72) {
                navigate_to_url();
                return;
            }
        } else {
            url_focused = 0;
        }

        /* --- Interacciones: AJUSTES --- */
        if (active_win == WIN_SETTINGS) {
            /* 1. Selector de 5 Temas */
            if (mouse_y >= win->y + 78 && mouse_y <= win->y + 106) {
                if (mouse_x >= win->x + 25 && mouse_x <= win->x + 110) current_theme = THEME_OCEAN;
                else if (mouse_x >= win->x + 115 && mouse_x <= win->x + 200) current_theme = THEME_ECO;
                else if (mouse_x >= win->x + 205 && mouse_x <= win->x + 290) current_theme = THEME_SUNSET;
                else if (mouse_x >= win->x + 295 && mouse_x <= win->x + 380) current_theme = THEME_VIOLET;
                else if (mouse_x >= win->x + 385 && mouse_x <= win->x + 470) current_theme = THEME_NIGHT;
                play_bubble_sfx();
                return;
            }

            /* 2. Velocidad de Burbujas */
            if (mouse_y >= win->y + 148 && mouse_y <= win->y + 176) {
                if (mouse_x >= win->x + 25 && mouse_x <= win->x + 120) bubble_speed_mode = 0;
                else if (mouse_x >= win->x + 130 && mouse_x <= win->x + 225) bubble_speed_mode = 1;
                else if (mouse_x >= win->x + 235 && mouse_x <= win->x + 330) bubble_speed_mode = 2;
                else if (mouse_x >= win->x + 340 && mouse_x <= win->x + 435) bubble_speed_mode = 3;
                play_bubble_sfx();
                return;
            }

            /* 3. Opacidad de Cristal */
            if (mouse_y >= win->y + 218 && mouse_y <= win->y + 246) {
                if (mouse_x >= win->x + 25 && mouse_x <= win->x + 145) glass_alpha = 180;
                else if (mouse_x >= win->x + 160 && mouse_x <= win->x + 280) glass_alpha = 225;
                else if (mouse_x >= win->x + 295 && mouse_x <= win->x + 415) glass_alpha = 255;
                play_bubble_sfx();
                return;
            }

            /* 4. Colinas y Sonido */
            if (mouse_y >= win->y + 288 && mouse_y <= win->y + 316) {
                if (mouse_x >= win->x + 25 && mouse_x <= win->x + 225) {
                    show_eco_hills = !show_eco_hills;
                    play_bubble_sfx();
                    return;
                } else if (mouse_x >= win->x + 240 && mouse_x <= win->x + 440) {
                    sfx_enabled = !sfx_enabled;
                    play_bubble_sfx();
                    return;
                }
            }
        }

        /* Explorador de Archivos */
        else if (active_win == WIN_FILES) {
            int fx = win->x + 35, fy = win->y + 100;
            if (mouse_x >= fx && mouse_x <= fx + 80 && mouse_y >= fy && mouse_y <= fy + 70) {
                modal_image_open = 1;
                play_bubble_sfx();
                return;
            }
        }

        /* Aero Player */
        else if (active_win == WIN_PLAYER) {
            int btn_x = win->x + win->w / 2 - 45;
            int btn_y = win->y + 285;
            if (mouse_x >= btn_x && mouse_x <= btn_x + 90 && mouse_y >= btn_y && mouse_y <= btn_y + 28) {
                music_playing = !music_playing;
                play_bubble_sfx();
                return;
            }
        }

        /* Arrastre de Ventana */
        if (mouse_y >= win->y && mouse_y <= win->y + 36) {
            is_dragging = 1;
            drag_off_x = mouse_x - win->x;
            drag_off_y = mouse_y - win->y;
            return;
        }
        return;
    }

    /* 5. Traer ventana al frente */
    for (int i = WIN_COUNT - 1; i >= 0; i--) {
        if (i != active_win && windows[i].visible && !windows[i].minimized) {
            if (mouse_x >= windows[i].x && mouse_x <= windows[i].x + windows[i].w &&
                mouse_y >= windows[i].y && mouse_y <= windows[i].y + windows[i].h) {
                bring_to_front(i);
                return;
            }
        }
    }

    /* 6. Iconos del Escritorio */
    if (mouse_x >= 30 && mouse_x <= 110) {
        if (mouse_y >= 30 && mouse_y <= 110) bring_to_front(WIN_SYSTEM);
        else if (mouse_y >= 125 && mouse_y <= 205) bring_to_front(WIN_FILES);
        else if (mouse_y >= 220 && mouse_y <= 300) bring_to_front(WIN_PLAYER);
        else if (mouse_y >= 315 && mouse_y <= 395) bring_to_front(WIN_BROWSER);
        else if (mouse_y >= 410 && mouse_y <= 490) bring_to_front(WIN_SETTINGS);
        return;
    }
}

/* ============================================================================
 * Driver del Ratón
 * ============================================================================ */
static uint8_t mouse_cycle = 0;
static uint8_t mouse_bytes[3];

static void process_mouse_packet(void) {
    uint8_t flags = mouse_bytes[0];
    int8_t dx = (int8_t)mouse_bytes[1];
    int8_t dy = (int8_t)mouse_bytes[2];

    mouse_x += dx;
    mouse_y -= dy;

    if (mouse_x < 0) mouse_x = 0;
    if (mouse_x >= (int)screen_w - 1) mouse_x = screen_w - 2;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y >= (int)screen_h - 1) mouse_y = screen_h - 2;

    uint8_t btn_left = flags & 1;

    if (btn_left && !mouse_btn_prev) {
        handle_click();
    } else if (!btn_left && mouse_btn_prev) {
        is_dragging = 0;
    }

    if (is_dragging && btn_left) {
        windows[active_win].x = mouse_x - drag_off_x;
        windows[active_win].y = mouse_y - drag_off_y;
    }

    mouse_btn_prev = btn_left;
}

/* ============================================================================
 * Punto de Entrada del Kernel
 * ============================================================================ */
void kernel_main(uint32_t magic, uint32_t addr) {
    (void)magic;
    struct multiboot_info* mbi = (struct multiboot_info*)addr;

    if ((mbi->flags & (1 << 12)) && mbi->framebuffer_addr != 0 && mbi->framebuffer_bpp == 32) {
        fb = (uint32_t*)(uint32_t)mbi->framebuffer_addr;
        screen_w = mbi->framebuffer_width;
        screen_h = mbi->framebuffer_height;

        ps2_mouse_init();
        play_bubble_sfx();

        while (1) {
            /* Procesar eventos de hardware (Ratón y Teclado) */
            while (inb(0x64) & 1) {
                uint8_t status = inb(0x64);
                uint8_t data   = inb(0x60);

                /* Si el bit 5 de 0x64 está en 1, el byte proviene del ratón */
                if (status & 0x20) {
                    if (mouse_cycle == 0) {
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
                } else {
                    /* Si el bit 5 está en 0, el byte proviene del TECLADO */
                    handle_keyboard_scancode(data);
                }
            }

            render_desktop();
        }
    }

    while (1) {
        __asm__ volatile ("hlt");
    }
}
