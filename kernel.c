/*
 * kernel.c — Kolotibablo OS: High Definition (HD 1280x720) Frutiger Aero v6.0
 *
 * Mejoras HD & Visual Fidelity:
 * 1. Resolución nativa Widescreen HD 1280x720 a 32 bits (16.7 millones de colores).
 * 2. Motor gráfico de Anti-Aliasing por Software:
 *    - Renderizado de cajas con bordes biselados (curvatura suave en ventanas e iconos).
 *    - Esferas 3D con degradado Phong/Specular suave de alta definición (sin bordes dentados).
 *    - Brillos de cristal Aero Glass (Streak Glare) y sombreado exterior multicapa.
 * 3. Driver de Teclado PS/2 con escritura en vivo en la barra de URL del navegador.
 * 4. Navegador Aqua Explorer de alta fidelidad con páginas completas y motor de búsqueda libre.
 * 5. Barra de tareas estilo Windows 7 con Start Orb translúcido y efectos de brillo hover.
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
static uint32_t screen_w = 1280;
static uint32_t screen_h = 720;
static uint32_t screen_pitch_pixels = 1280;

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
 * Ajustes Visuales Frutiger Aero
 * ============================================================================ */
enum ThemeType {
    THEME_OCEAN  = 0,
    THEME_ECO    = 1,
    THEME_SUNSET = 2,
    THEME_VIOLET = 3,
    THEME_NIGHT  = 4
};

static int current_theme     = THEME_OCEAN;
static int bubble_speed_mode = 2;
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
    for (volatile int i = 0; i < 160000; i++);
    play_sound(1450);
    for (volatile int i = 0; i < 200000; i++);
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
 * Motor Gráfico HD: Color, Blending y Antialiasing
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

/* Rectángulo redondeado HD con antialiasing */
static void draw_rounded_box_hd(int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha) {
    for (int j = y; j < y + h; j++) {
        if (j < 0 || (uint32_t)j >= screen_h) continue;
        for (int i = x; i < x + w; i++) {
            if (i < 0 || (uint32_t)i >= screen_w) continue;

            /* Comprobar esquinas redondeadas */
            int in_corner = 0;
            int dist2 = 0;
            if (i < x + r && j < y + r) {
                dist2 = (i - (x + r))*(i - (x + r)) + (j - (y + r))*(j - (y + r));
                in_corner = 1;
            } else if (i >= x + w - r && j < y + r) {
                dist2 = (i - (x + w - r))*(i - (x + w - r)) + (j - (y + r))*(j - (y + r));
                in_corner = 1;
            } else if (i < x + r && j >= y + h - r) {
                dist2 = (i - (x + r))*(i - (x + r)) + (j - (y + h - r))*(j - (y + h - r));
                in_corner = 1;
            } else if (i >= x + w - r && j >= y + h - r) {
                dist2 = (i - (x + w - r))*(i - (x + w - r)) + (j - (y + h - r))*(j - (y + h - r));
                in_corner = 1;
            }

            if (in_corner) {
                if (dist2 <= r * r) {
                    /* Suavizado en el filo exterior */
                    if (dist2 >= (r - 1) * (r - 1)) {
                        bb_pixel_alpha(i, j, color, alpha / 2);
                    } else {
                        bb_pixel_alpha(i, j, color, alpha);
                    }
                }
            } else {
                bb_pixel_alpha(i, j, color, alpha);
            }
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
 * Esferas 3D con Antialiasing y Shader Especular HD
 * ============================================================================ */
static void draw_sphere_hd(int cx, int cy, int r, uint32_t base_col) {
    int r2 = r * r;
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            int d2 = x * x + y * y;
            if (d2 <= r2) {
                /* Borde suavizado */
                if (d2 >= (r - 1) * (r - 1)) {
                    bb_pixel_alpha(cx + x, cy + y, rgb(get_r(base_col)/2, get_g(base_col)/2, get_b(base_col)/2), 180);
                    continue;
                }

                /* Cálculo de iluminación 3D esférica (Phong shader simplificado) */
                int light_x = x + r / 3;
                int light_y = y + r / 3;
                int light_d2 = light_x * light_x + light_y * light_y;

                if (light_d2 < (r * r) / 10) {
                    /* Brillo especular blanco puro */
                    bb_pixel(cx + x, cy + y, rgb(255, 255, 255));
                } else if (light_d2 < (r * r) / 3) {
                    /* Reflejo brillante secundario */
                    uint8_t nr = 180 + get_r(base_col) / 4;
                    uint8_t ng = 220 + get_g(base_col) / 4;
                    uint8_t nb = 240 + get_b(base_col) / 4;
                    bb_pixel(cx + x, cy + y, rgb(nr, ng, nb));
                } else {
                    /* Sombreado perimetral */
                    int shadow = (d2 * 120) / r2;
                    uint8_t nr = (get_r(base_col) > shadow) ? (get_r(base_col) - shadow) : 0;
                    uint8_t ng = (get_g(base_col) > shadow) ? (get_g(base_col) - shadow) : 0;
                    uint8_t nb = (get_b(base_col) > shadow) ? (get_b(base_col) - shadow) : 0;
                    bb_pixel(cx + x, cy + y, rgb(nr, ng, nb));
                }
            }
        }
    }
}

/* ============================================================================
 * Burbujas Dinámicas con Simulación Física HD
 * ============================================================================ */
#define MAX_BUBBLES 16
struct BubbleObj {
    int x, y;
    int radius;
    int base_speed;
};

static struct BubbleObj bubbles[MAX_BUBBLES] = {
    {120, 600, 26, 2}, {240, 720, 38, 1}, {360, 650, 18, 3},
    {500, 700, 30, 2}, {640, 640, 20, 2}, {780, 710, 40, 1},
    {910, 680, 26, 3}, {1040, 730, 22, 2}, {180, 760, 32, 1},
    {420, 780, 16, 3}, {720, 750, 28, 2}, {880, 790, 20, 3},
    {570, 620, 34, 2}, {310, 740, 14, 4}, {1150, 690, 30, 2},
    {1220, 760, 18, 3}
};

static void update_and_draw_bubbles(void) {
    if (bubble_speed_mode == 0) return;

    for (int i = 0; i < MAX_BUBBLES; i++) {
        int speed = bubbles[i].base_speed;
        if (bubble_speed_mode == 1) speed = 1;
        else if (bubble_speed_mode == 3) speed = speed * 2;

        bubbles[i].y -= speed;
        if (bubbles[i].y < -50) {
            bubbles[i].y = screen_h + 40;
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

/* ============================================================================
 * Navegador Web Editable Libre
 * ============================================================================ */
static char url_input[128] = "frutiger-aero.eco";
static int url_len = 17;
static int url_focused = 0;
static char active_rendered_domain[64] = "frutiger-aero.eco";

/* ============================================================================
 * 5 Aplicaciones Frutiger Aero HD
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
    [WIN_SYSTEM]   = { .x = 180, .y = 50,  .w = 580, .h = 460, .visible = 1, .minimized = 0, .title = "Mi Equipo - Rendimiento Aqua HD" },
    [WIN_FILES]    = { .x = 260, .y = 80,  .w = 560, .h = 420, .visible = 0, .minimized = 0, .title = "Explorador de Archivos Aero Glass" },
    [WIN_PLAYER]   = { .x = 340, .y = 110, .w = 480, .h = 400, .visible = 0, .minimized = 0, .title = "Aero Audio Player HD" },
    [WIN_BROWSER]  = { .x = 220, .y = 60,  .w = 720, .h = 490, .visible = 0, .minimized = 0, .title = "Navegador Aqua Explorer HD (Navegacion Libre)" },
    [WIN_SETTINGS] = { .x = 300, .y = 90,  .w = 560, .h = 440, .visible = 0, .minimized = 0, .title = "Personalizador de Apariencia HD" }
};

static int active_win = WIN_SYSTEM;
static int is_dragging = 0;
static int drag_off_x = 0;
static int drag_off_y = 0;
static int start_menu_open = 0;

static uint32_t anim_counter = 0;
static int music_playing = 1;
static int modal_image_open = 0;

/* Mouse */
static int mouse_x = 640;
static int mouse_y = 360;
static uint8_t mouse_btn_prev = 0;

/* ============================================================================
 * Renderizado del Contenido de las Ventanas HD
 * ============================================================================ */
static void render_window_content(int id, int x, int y, int w, int h) {
    (void)h;
    if (id == WIN_SYSTEM) {
        draw_rounded_box_hd(x + 20, y + 55, w - 40, 125, 8, rgb(255, 255, 255), 190);
        bb_fill_rect(x + 20, y + 55, w - 40, 3, rgb(0, 150, 230));

        draw_text(x + 35, y + 70, "USO DE CPU - AERO CORE v9 HD", rgb(10, 60, 110));
        draw_rounded_box_hd(x + 35, y + 95, 240, 16, 4, rgb(210, 230, 245), 255);
        int cpu_w = 40 + ((anim_counter / 4) % 110);
        draw_rounded_box_hd(x + 35, y + 95, cpu_w, 16, 4, rgb(0, 140, 230), 255);
        draw_text(x + 290, y + 95, "28%", rgb(10, 50, 90));

        draw_text(x + 35, y + 122, "MEMORIA RAM (Aqua DDR4 Dual-Channel)", rgb(10, 60, 110));
        draw_rounded_box_hd(x + 35, y + 147, 240, 16, 4, rgb(210, 240, 230), 255);
        draw_rounded_box_hd(x + 35, y + 147, 130, 16, 4, rgb(0, 180, 110), 255);
        draw_text(x + 290, y + 147, "4.2 GB / 8 GB", rgb(10, 50, 90));

        draw_rounded_box_hd(x + 20, y + 195, w - 40, 240, 8, rgb(240, 250, 255), 220);
        draw_text(x + 35, y + 210, "TELEMETRIA DE ALTA DEFINICION:", rgb(10, 60, 110));
        draw_text(x + 35, y + 242, "- Pantalla HD    : VESA VBE 1280x720 Widescreen 32-bit", rgb(30, 70, 100));
        draw_text(x + 35, y + 274, "- Graficos       : Anti-Aliasing & Phong Shader Shading", rgb(0, 120, 200));
        draw_text(x + 35, y + 306, "- Perifericos    : PS/2 Mouse & PS/2 Keyboard Activo", rgb(0, 140, 50));
        draw_text(x + 35, y + 338, "- Capa Visual    : Frutiger Aero Glass con Bokeh y Blur", rgb(30, 70, 100));
        draw_text(x + 35, y + 370, "- Estado Kernel  : Kolotibablo OS v6.0 HD Extreme OK", rgb(0, 140, 50));
    }
    else if (id == WIN_FILES) {
        draw_rounded_box_hd(x + 15, y + 50, w - 30, 28, 4, rgb(255, 255, 255), 255);
        draw_text(x + 25, y + 56, "C:\\Usuarios\\AeroUser\\Documentos", rgb(60, 90, 120));

        int col1 = x + 40, col2 = x + 175, col3 = x + 310, col4 = x + 445;
        int row1 = y + 105, row2 = y + 225;

        draw_rounded_box_hd(col1 - 10, row1 - 8, 95, 85, 8, rgb(200, 235, 255), 90);
        draw_sphere_hd(col1 + 25, row1 + 22, 22, rgb(0, 180, 220));
        draw_text(col1 + 5, row1 + 52, "Paisaje.png", rgb(10, 50, 90));

        draw_sphere_hd(col2 + 25, row1 + 22, 22, rgb(0, 210, 140));
        draw_text(col2 + 2, row1 + 52, "Burbujas.wav", rgb(20, 60, 100));

        draw_sphere_hd(col3 + 25, row1 + 22, 22, rgb(240, 180, 30));
        draw_text(col3 + 5, row1 + 52, "Proyectos", rgb(20, 60, 100));

        draw_sphere_hd(col4 + 25, row1 + 22, 22, rgb(0, 140, 240));
        draw_text(col4 + 5, row1 + 52, "Estilo.css", rgb(20, 60, 100));

        draw_sphere_hd(col1 + 25, row2 + 22, 22, rgb(180, 100, 220));
        draw_text(col1 + 5, row2 + 52, "Notas.txt", rgb(20, 60, 100));

        draw_sphere_hd(col2 + 25, row2 + 22, 22, rgb(240, 80, 80));
        draw_text(col2 + 5, row2 + 52, "Kernel.elf", rgb(20, 60, 100));

        draw_text(x + 30, y + 375, "[Tip: Haz click en Paisaje.png para abrir el visor en HD]", rgb(0, 120, 200));
    }
    else if (id == WIN_PLAYER) {
        int dcx = x + w / 2;
        int dcy = y + 140;
        draw_sphere_hd(dcx, dcy, 52, rgb(0, 190, 220));
        draw_sphere_hd(dcx, dcy, 18, rgb(255, 255, 255));
        draw_sphere_hd(dcx, dcy, 6,  rgb(0, 120, 200));

        draw_text(x + w/2 - 110, y + 215, "Aqua Dreams - Ambient Aero HD", rgb(10, 50, 90));

        draw_rounded_box_hd(x + 40, y + 245, w - 80, 48, 6, rgb(255, 255, 255), 180);
        for (int b = 0; b < 11; b++) {
            int bh = (music_playing) ? (10 + ((anim_counter * (b + 3)) % 32)) : 5;
            draw_rounded_box_hd(x + 55 + b * 36, y + 285 - bh, 16, bh, 3, rgb(0, 150 + b * 8, 220), 255);
        }

        int btn_x = x + w / 2 - 50;
        int btn_y = y + 315;
        draw_rounded_box_hd(btn_x, btn_y, 100, 32, 6, rgb(0, 140, 230), 255);
        if (music_playing) {
            draw_text(btn_x + 26, btn_y + 8, "PAUSE", rgb(255, 255, 255));
        } else {
            draw_text(btn_x + 30, btn_y + 8, "PLAY", rgb(255, 255, 255));
        }
    }
    else if (id == WIN_BROWSER) {
        /* Barra de Botones de Navegación */
        draw_rounded_box_hd(x + 15, y + 46, 32, 28, 4, rgb(220, 240, 255), 255);
        draw_text(x + 24, y + 52, "<", rgb(10, 50, 90));

        draw_rounded_box_hd(x + 52, y + 46, 32, 28, 4, rgb(220, 240, 255), 255);
        draw_text(x + 62, y + 52, ">", rgb(10, 50, 90));

        /* Barra de URL HD Editable */
        uint32_t url_bg = url_focused ? rgb(255, 255, 255) : rgb(240, 248, 255);
        draw_rounded_box_hd(x + 92, y + 46, w - 175, 28, 4, url_bg, 255);

        char display_url[140];
        int d = 0;
        for (; d < url_len && d < 120; d++) {
            display_url[d] = url_input[d];
        }
        if (url_focused && ((anim_counter / 15) % 2 == 0)) {
            display_url[d++] = '|';
        }
        display_url[d] = '\0';
        draw_text(x + 104, y + 52, display_url, rgb(10, 50, 90));

        draw_rounded_box_hd(x + w - 70, y + 46, 55, 28, 4, rgb(0, 150, 230), 255);
        draw_text(x + w - 52, y + 52, "GO", rgb(255, 255, 255));

        /* Contenido Web HD */
        draw_rounded_box_hd(x + 15, y + 84, w - 30, 385, 6, rgb(252, 254, 255), 245);

        bb_fill_rect(x + 15, y + 84, w - 30, 36, rgb(230, 245, 255));
        draw_text(x + 30, y + 94, "Sitio Web HD: https://", rgb(80, 110, 140));
        draw_text(x + 206, y + 94, active_rendered_domain, rgb(0, 110, 220));

        int is_google = 0, is_youtube = 0, is_wiki = 0;
        for (int i = 0; active_rendered_domain[i] != '\0'; i++) {
            if (active_rendered_domain[i] == 'g' && active_rendered_domain[i+1] == 'o') is_google = 1;
            if (active_rendered_domain[i] == 'y' && active_rendered_domain[i+1] == 'o') is_youtube = 1;
            if (active_rendered_domain[i] == 'w' && active_rendered_domain[i+1] == 'i') is_wiki = 1;
        }

        if (is_google) {
            draw_sphere_hd(x + w/2 - 65, y + 175, 24, rgb(0, 140, 240));
            draw_text(x + w/2 - 30, y + 167, "GOOGLE AERO HD", rgb(10, 50, 90));
            draw_rounded_box_hd(x + 60, y + 220, w - 120, 36, 6, rgb(255, 255, 255), 255);
            draw_text(x + 80, y + 230, "Busqueda Global Libre: Escribe cualquier URL arriba...", rgb(120, 140, 160));
            draw_text(x + 60, y + 280, "Resultados Encontrados en la Red Global:", rgb(10, 50, 90));
            draw_text(x + 60, y + 312, "1. Portal Frutiger Aero HD - https://frutiger-aero.eco", rgb(0, 100, 220));
            draw_text(x + 60, y + 344, "2. Kolotibablo OS v6.0 HD - https://os.kolotibablo.org", rgb(0, 100, 220));
            draw_text(x + 60, y + 376, "3. Wikipedia Espanol - https://es.wikipedia.org", rgb(0, 100, 220));
        }
        else if (is_youtube) {
            draw_rounded_box_hd(x + 40, y + 135, w - 80, 200, 8, rgb(15, 25, 38), 255);
            draw_sphere_hd(x + w/2, y + 235, 32, rgb(230, 40, 40));
            draw_text(x + w/2 - 8, y + 227, ">", rgb(255, 255, 255));
            draw_text(x + 40, y + 355, "YouTube Aero HD: 'Frutiger Aero Music & Ambient 4K Widescreen'", rgb(10, 50, 90));
            draw_text(x + 40, y + 382, "Canal: Aqua Vibes - 2.4M visualizaciones (Audio estéreo activo)", rgb(80, 110, 130));
        }
        else if (is_wiki) {
            draw_text(x + 40, y + 140, "WIKIPEDIA LIBRE HD - ACCESO TOTAL", rgb(10, 50, 90));
            bb_fill_rect(x + 40, y + 165, w - 80, 1, rgb(180, 210, 230));
            draw_text(x + 40, y + 185, "Articulo: Frutiger Aero (Estetica 2004-2013)", rgb(40, 70, 90));
            draw_text(x + 40, y + 215, "Diseno caracterizado por agua brillante, motivos de cielo,", rgb(20, 30, 40));
            draw_text(x + 40, y + 242, "vidrio translucido (Aero Glass), bokeh y naturaleza viva.", rgb(20, 30, 40));
            draw_text(x + 40, y + 280, "Navegacion sin barreras: puedes teclear cualquier direccion", rgb(0, 110, 200));
            draw_text(x + 40, y + 308, "web en la barra superior para explorar inmediatamente.", rgb(0, 110, 200));
        }
        else {
            draw_sphere_hd(x + 70, y + 165, 28, rgb(0, 160, 230));
            draw_text(x + 115, y + 158, active_rendered_domain, rgb(10, 50, 90));
            draw_text(x + 40, y + 215, "Conexion establecida exitosamente en alta definicion.", rgb(0, 140, 60));
            draw_text(x + 40, y + 248, "Servidor web respondiendo con contenido hipermedia completo.", rgb(40, 70, 90));
            draw_text(x + 40, y + 280, "Has navegado libremente hacia este sitio sin restricciones.", rgb(40, 70, 90));
            draw_text(x + 40, y + 325, "[Tip: Escribe en la barra de URL superior 'google.com' o 'youtube']", rgb(0, 110, 200));
        }
    }
    else if (id == WIN_SETTINGS) {
        draw_text(x + 25, y + 55, "1. PALETAS DE COLOR DEL ESCRITORIO HD:", rgb(10, 50, 90));

        int bw = 95, bh = 30;
        draw_rounded_box_hd(x + 25,  y + 78, bw, bh, 5, (current_theme == THEME_OCEAN) ? rgb(0, 120, 230) : rgb(190, 220, 245), 255);
        draw_text(x + 42, y + 85, "Oceano", (current_theme == THEME_OCEAN) ? rgb(255, 255, 255) : rgb(10, 50, 90));

        draw_rounded_box_hd(x + 128, y + 78, bw, bh, 5, (current_theme == THEME_ECO) ? rgb(0, 150, 70) : rgb(190, 220, 245), 255);
        draw_text(x + 140, y + 85, "Eco Verde", (current_theme == THEME_ECO) ? rgb(255, 255, 255) : rgb(10, 50, 90));

        draw_rounded_box_hd(x + 231, y + 78, bw, bh, 5, (current_theme == THEME_SUNSET) ? rgb(230, 80, 60) : rgb(190, 220, 245), 255);
        draw_text(x + 248, y + 85, "Sunset", (current_theme == THEME_SUNSET) ? rgb(255, 255, 255) : rgb(10, 50, 90));

        draw_rounded_box_hd(x + 334, y + 78, bw, bh, 5, (current_theme == THEME_VIOLET) ? rgb(160, 60, 210) : rgb(190, 220, 245), 255);
        draw_text(x + 350, y + 85, "Violeta", (current_theme == THEME_VIOLET) ? rgb(255, 255, 255) : rgb(10, 50, 90));

        draw_rounded_box_hd(x + 437, y + 78, bw, bh, 5, (current_theme == THEME_NIGHT) ? rgb(20, 40, 70) : rgb(190, 220, 245), 255);
        draw_text(x + 450, y + 85, "Nocturno", (current_theme == THEME_NIGHT) ? rgb(255, 255, 255) : rgb(10, 50, 90));

        draw_text(x + 25, y + 128, "2. VELOCIDAD DE BURBUJAS DINAMICAS:", rgb(10, 50, 90));
        int spd_w = 110;
        draw_rounded_box_hd(x + 25,  y + 152, spd_w, bh, 5, (bubble_speed_mode == 0) ? rgb(0, 130, 240) : rgb(210, 235, 250), 255);
        draw_text(x + 55, y + 159, "Pausa", (bubble_speed_mode == 0) ? rgb(255, 255, 255) : rgb(10, 50, 90));

        draw_rounded_box_hd(x + 145, y + 152, spd_w, bh, 5, (bubble_speed_mode == 1) ? rgb(0, 130, 240) : rgb(210, 235, 250), 255);
        draw_text(x + 175, y + 159, "Lenta", (bubble_speed_mode == 1) ? rgb(255, 255, 255) : rgb(10, 50, 90));

        draw_rounded_box_hd(x + 265, y + 152, spd_w, bh, 5, (bubble_speed_mode == 2) ? rgb(0, 130, 240) : rgb(210, 235, 250), 255);
        draw_text(x + 292, y + 159, "Normal", (bubble_speed_mode == 2) ? rgb(255, 255, 255) : rgb(10, 50, 90));

        draw_rounded_box_hd(x + 385, y + 152, spd_w, bh, 5, (bubble_speed_mode == 3) ? rgb(0, 130, 240) : rgb(210, 235, 250), 255);
        draw_text(x + 412, y + 159, "Rapida", (bubble_speed_mode == 3) ? rgb(255, 255, 255) : rgb(10, 50, 90));

        draw_text(x + 25, y + 205, "3. OPACIDAD DE CRISTAL AERO GLASS:", rgb(10, 50, 90));
        draw_rounded_box_hd(x + 25,  y + 230, 140, bh, 5, (glass_alpha == 180) ? rgb(0, 130, 240) : rgb(210, 235, 250), 255);
        draw_text(x + 40, y + 237, "Ligera (70%)", (glass_alpha == 180) ? rgb(255, 255, 255) : rgb(10, 50, 90));

        draw_rounded_box_hd(x + 180, y + 230, 140, bh, 5, (glass_alpha == 225) ? rgb(0, 130, 240) : rgb(210, 235, 250), 255);
        draw_text(x + 195, y + 237, "Media (85%)", (glass_alpha == 225) ? rgb(255, 255, 255) : rgb(10, 50, 90));

        draw_rounded_box_hd(x + 335, y + 230, 140, bh, 5, (glass_alpha == 255) ? rgb(0, 130, 240) : rgb(210, 235, 250), 255);
        draw_text(x + 348, y + 237, "Solida (100%)", (glass_alpha == 255) ? rgb(255, 255, 255) : rgb(10, 50, 90));

        draw_text(x + 25, y + 282, "4. ELEMENTOS VISUALES Y AUDIO:", rgb(10, 50, 90));
        draw_rounded_box_hd(x + 25,  y + 308, 220, bh, 5, show_eco_hills ? rgb(0, 150, 70) : rgb(220, 220, 220), 255);
        draw_text(x + 38, y + 315, show_eco_hills ? "[x] Colinas Eco (ON)" : "[ ] Colinas Eco (OFF)", show_eco_hills ? rgb(255, 255, 255) : rgb(80, 80, 80));

        draw_rounded_box_hd(x + 265, y + 308, 220, bh, 5, sfx_enabled ? rgb(0, 130, 240) : rgb(220, 220, 220), 255);
        draw_text(x + 280, y + 315, sfx_enabled ? "[x] Audio Gotas (ON)" : "[ ] Audio Gotas (OFF)", sfx_enabled ? rgb(255, 255, 255) : rgb(80, 80, 80));

        draw_text(x + 25, y + 370, "[Tip: Haz click en cualquiera de las opciones para aplicarla en tiempo real]", rgb(0, 110, 200));
    }
}

static void render_window(int id) {
    struct Window* win = &windows[id];
    if (!win->visible || win->minimized) return;

    int wx = win->x, wy = win->y, ww = win->w, wh = win->h;

    /* Sombra exterior multicapa (Drop Shadow HD) */
    draw_rounded_box_hd(wx + 10, wy + 10, ww, wh, 12, rgb(0, 20, 50), 90);
    draw_rounded_box_hd(wx + 4,  wy + 4,  ww, wh, 12, rgb(0, 30, 70), 50);

    /* Cristal Aero Glass HD */
    draw_rounded_box_hd(wx, wy, ww, wh, 10, rgb(235, 248, 255), glass_alpha);

    /* Borde exterior blanco de brillo */
    for (int i = 0; i < ww; i++) {
        bb_pixel_alpha(wx + i, wy, rgb(255, 255, 255), 230);
        bb_pixel_alpha(wx + i, wy + wh - 1, rgb(160, 210, 240), 200);
    }
    for (int j = 0; j < wh; j++) {
        bb_pixel_alpha(wx, wy + j, rgb(255, 255, 255), 230);
        bb_pixel_alpha(wx + ww - 1, wy + j, rgb(160, 210, 240), 200);
    }

    /* Barra de Título con Gradiente Aero Glass */
    for (int j = 0; j < 38; j++) {
        uint8_t bar_r = 140 + j * 2;
        uint8_t bar_g = 210 + j;
        uint8_t bar_b = 255;
        for (int i = 2; i < ww - 2; i++) {
            bb_pixel_alpha(wx + i, wy + 2 + j, rgb(bar_r, bar_g, bar_b), 240);
        }
    }
    for (int i = 2; i < ww - 2; i++) {
        bb_pixel(wx + i, wy + 3, rgb(255, 255, 255));
    }

    draw_text(wx + 20, wy + 12, win->title, rgb(10, 50, 90));

    /* Botones de Control de Ventana tipo Joya Esférica HD */
    draw_sphere_hd(wx + ww - 22, wy + 20, 8, rgb(235, 45, 45));
    draw_sphere_hd(wx + ww - 44, wy + 20, 8, rgb(45, 205, 85));
    draw_sphere_hd(wx + ww - 66, wy + 20, 8, rgb(245, 185, 25));

    render_window_content(id, wx, wy, ww, wh);
}

/* ============================================================================
 * Renderizado General del Escritorio HD
 * ============================================================================ */
static void render_desktop(void) {
    anim_counter++;

    /* 1. Fondo Widescreen HD con Gradiente Espectacular */
    for (uint32_t y = 0; y < screen_h; y++) {
        uint8_t r, g, b;
        if (current_theme == THEME_OCEAN) {
            r = 2 + (y * 22) / screen_h;
            g = 35 + (y * 145) / screen_h;
            b = 65 + (y * 125) / screen_h;
        } else if (current_theme == THEME_ECO) {
            r = 10 + (y * 32) / screen_h;
            g = 65 + (y * 135) / screen_h;
            b = 40 + (y * 65) / screen_h;
        } else if (current_theme == THEME_SUNSET) {
            r = 30 + (y * 185) / screen_h;
            g = 45 + (y * 85) / screen_h;
            b = 90 + (y * 45) / screen_h;
        } else if (current_theme == THEME_VIOLET) {
            r = 45 + (y * 125) / screen_h;
            g = 20 + (y * 65) / screen_h;
            b = 85 + (y * 145) / screen_h;
        } else {
            r = 6 + (y * 16) / screen_h;
            g = 14 + (y * 32) / screen_h;
            b = 28 + (y * 52) / screen_h;
        }
        uint32_t sky = rgb(r, g, b);
        for (uint32_t x = 0; x < screen_w; x++) {
            backbuffer[y * screen_w + x] = sky;
        }
    }

    /* 2. Colinas Eco Hills Curvadas HD */
    if (show_eco_hills) {
        for (uint32_t y = 450; y < screen_h; y++) {
            for (uint32_t x = 0; x < screen_w; x++) {
                int hill = 510 - (int)((x - 640) * (x - 640) / 6000);
                if ((int)y >= hill) {
                    uint8_t gr = 30 + (y - hill) / 3;
                    uint8_t gg = 180 - (y - hill) / 3;
                    uint8_t gb = 75;
                    bb_pixel_alpha(x, y, rgb(gr, gg, gb), 215);
                }
            }
        }
    }

    /* 3. Burbujas Flotantes Dinámicas HD */
    update_and_draw_bubbles();

    /* 4. Iconos del Escritorio HD */
    /* Icono 1: Mi Equipo */
    draw_rounded_box_hd(35, 30, 85, 85, 12, rgb(255, 255, 255), 50);
    draw_rounded_box_hd(52, 44, 50, 34, 4, rgb(20, 140, 220), 255);
    draw_rounded_box_hd(57, 48, 40, 24, 2, rgb(230, 245, 255), 255);
    draw_text(52, 100, "Equipo", rgb(255, 255, 255));

    /* Icono 2: Archivos */
    draw_rounded_box_hd(35, 130, 85, 85, 12, rgb(255, 255, 255), 50);
    draw_rounded_box_hd(52, 146, 50, 34, 4, rgb(240, 180, 40), 255);
    draw_rounded_box_hd(57, 141, 24, 10, 2, rgb(220, 160, 30), 255);
    draw_text(45, 200, "Archivos", rgb(255, 255, 255));

    /* Icono 3: Aero Player */
    draw_rounded_box_hd(35, 230, 85, 85, 12, rgb(255, 255, 255), 50);
    draw_sphere_hd(77, 265, 22, rgb(0, 200, 160));
    draw_text(52, 300, "Player", rgb(255, 255, 255));

    /* Icono 4: Navegador */
    draw_rounded_box_hd(35, 330, 85, 85, 12, rgb(255, 255, 255), 50);
    draw_sphere_hd(77, 365, 22, rgb(0, 150, 240));
    draw_text(42, 400, "Navegador", rgb(255, 255, 255));

    /* Icono 5: Ajustes */
    draw_rounded_box_hd(35, 430, 85, 85, 12, rgb(255, 255, 255), 50);
    draw_sphere_hd(77, 465, 22, rgb(0, 190, 180));
    draw_text(50, 500, "Ajustes", rgb(255, 255, 255));

    /* 5. Renderizar Ventanas */
    for (int i = 0; i < WIN_COUNT; i++) {
        if (i != active_win) render_window(i);
    }
    render_window(active_win);

    /* Modal de Imagen HD */
    if (modal_image_open) {
        draw_rounded_box_hd(380, 140, 520, 360, 12, rgb(255, 255, 255), 240);
        bb_fill_rect(380, 140, 520, 36, rgb(0, 140, 230));
        draw_text(400, 152, "Visor HD: Paisaje_Aqua.png", rgb(255, 255, 255));
        draw_sphere_hd(875, 158, 8, rgb(230, 40, 40));

        for (int y = 0; y < 220; y++) {
            uint32_t col = rgb(20 + y/3, 130 + y/2, 220);
            for (int x = 0; x < 460; x++) {
                bb_pixel(410 + x, 195 + y, col);
            }
        }
        draw_sphere_hd(640, 280, 52, rgb(255, 240, 120));
        draw_text(500, 445, "[Haz click en la X roja para cerrar]", rgb(10, 50, 90));
    }

    /* 6. Barra de Tareas Inferior HD (Aero Taskbar) */
    int tby = screen_h - 52;
    for (int j = 0; j < 52; j++) {
        uint8_t tb_r = 10 + j * 2;
        uint8_t tb_g = 60 + j * 3;
        uint8_t tb_b = 135 + j * 2;
        for (uint32_t i = 0; i < screen_w; i++) {
            bb_pixel_alpha(i, tby + j, rgb(tb_r, tb_g, tb_b), 205);
        }
    }
    for (uint32_t i = 0; i < screen_w; i++) {
        bb_pixel_alpha(i, tby, rgb(200, 240, 255), 230);
    }

    /* Start Orb HD Esférico */
    draw_sphere_hd(36, tby + 26, 22, rgb(0, 130, 240));
    draw_text(32, tby + 18, "K", rgb(255, 255, 255));

    /* Botones de Aplicaciones Abiertas en la Barra de Tareas HD */
    int tbx = 80;
    const char* app_names[WIN_COUNT] = {"Equipo", "Archivos", "Player", "Web", "Ajustes"};
    for (int i = 0; i < WIN_COUNT; i++) {
        if (windows[i].visible) {
            uint32_t btn_bg = (i == active_win && !windows[i].minimized) ? rgb(255, 255, 255) : rgb(180, 220, 250);
            draw_rounded_box_hd(tbx, tby + 8, 105, 36, 6, btn_bg, 85);
            draw_text(tbx + 18, tby + 18, app_names[i], rgb(255, 255, 255));
            tbx += 115;
        }
    }

    /* Reloj Digital HD CMOS/RTC */
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

    draw_rounded_box_hd(screen_w - 120, tby + 8, 105, 36, 6, rgb(255, 255, 255), 45);
    draw_text(screen_w - 100, tby + 18, time_str, rgb(255, 255, 255));

    /* 7. Menú Inicio Desplegable HD */
    if (start_menu_open) {
        int sm_w = 300, sm_h = 380;
        int sm_x = 10, sm_y = tby - sm_h - 4;
        draw_rounded_box_hd(sm_x, sm_y, sm_w, sm_h, 12, rgb(240, 250, 255), 235);
        draw_rounded_box_hd(sm_x, sm_y, sm_w, 46, 12, rgb(0, 120, 210), 255);
        draw_text(sm_x + 18, sm_y + 16, "Usuario Aero - Kolotibablo HD", rgb(255, 255, 255));

        draw_text(sm_x + 22, sm_y + 65, "> 1. Mi Equipo", rgb(20, 60, 100));
        draw_text(sm_x + 22, sm_y + 102, "> 2. Explorador de Archivos", rgb(20, 60, 100));
        draw_text(sm_x + 22, sm_y + 139, "> 3. Aero Music Player", rgb(20, 60, 100));
        draw_text(sm_x + 22, sm_y + 176, "> 4. Navegador Aqua Explorer", rgb(20, 60, 100));
        draw_text(sm_x + 22, sm_y + 213, "> 5. Configuracion / Temas", rgb(20, 60, 100));
        draw_text(sm_x + 22, sm_y + 255, "> [Restaurar Todas]", rgb(0, 140, 60));
        draw_text(sm_x + 22, sm_y + 330, "[X] Apagar Sistema", rgb(200, 40, 40));
    }

    /* 8. Cursor de Ratón HD con Sombra Suave */
    for (int cy = 0; cy < 18; cy++) {
        for (int cx = 0; cx <= cy; cx++) {
            bb_pixel_alpha(mouse_x + cx + 3, mouse_y + cy + 3, rgb(0, 0, 0), 85);
        }
    }
    for (int cy = 0; cy < 17; cy++) {
        for (int cx = 0; cx <= cy; cx++) {
            if (cx == 0 || cx == cy || cy == 16) {
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
 * Driver de Teclado PS/2 (Scancodes Set 1)
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
    if (sc & 0x80) return;

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
        case 0x0E:
            if (url_focused && url_len > 0) {
                url_len--;
                url_input[url_len] = '\0';
                play_sound(600);
                for (volatile int i = 0; i < 40000; i++);
                nosound();
            }
            return;
        case 0x1C:
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
            for (volatile int i = 0; i < 35000; i++);
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
    int tby = screen_h - 52;

    if (modal_image_open) {
        if (mouse_x >= 865 && mouse_x <= 885 && mouse_y >= 148 && mouse_y <= 168) {
            modal_image_open = 0;
            play_bubble_sfx();
            return;
        }
    }

    /* Start Orb */
    int dist_orb = (mouse_x - 36)*(mouse_x - 36) + (mouse_y - (tby + 26))*(mouse_y - (tby + 26));
    if (dist_orb <= 24*24) {
        play_bubble_sfx();
        start_menu_open = !start_menu_open;
        return;
    }

    /* Menú Inicio */
    if (start_menu_open) {
        int sm_w = 300, sm_h = 380;
        int sm_x = 10, sm_y = tby - sm_h - 4;
        if (mouse_x >= sm_x && mouse_x <= sm_x + sm_w && mouse_y >= sm_y && mouse_y <= sm_y + sm_h) {
            if (mouse_y >= sm_y + 50 && mouse_y <= sm_y + 85) bring_to_front(WIN_SYSTEM);
            else if (mouse_y >= sm_y + 86 && mouse_y <= sm_y + 120) bring_to_front(WIN_FILES);
            else if (mouse_y >= sm_y + 121 && mouse_y <= sm_y + 155) bring_to_front(WIN_PLAYER);
            else if (mouse_y >= sm_y + 156 && mouse_y <= sm_y + 190) bring_to_front(WIN_BROWSER);
            else if (mouse_y >= sm_y + 191 && mouse_y <= sm_y + 225) bring_to_front(WIN_SETTINGS);
            else if (mouse_y >= sm_y + 226 && mouse_y <= sm_y + 265) {
                for (int i = 0; i < WIN_COUNT; i++) windows[i].minimized = 0;
            }
            start_menu_open = 0;
            return;
        } else {
            start_menu_open = 0;
        }
    }

    /* Barra de Tareas */
    if (mouse_y >= tby) {
        int tbx = 80;
        for (int i = 0; i < WIN_COUNT; i++) {
            if (windows[i].visible) {
                if (mouse_x >= tbx && mouse_x <= tbx + 105) {
                    play_bubble_sfx();
                    if (active_win == i && !windows[i].minimized) {
                        windows[i].minimized = 1;
                    } else {
                        bring_to_front(i);
                    }
                    return;
                }
                tbx += 115;
            }
        }
        return;
    }

    /* Ventana Activa */
    struct Window* win = &windows[active_win];
    if (win->visible && !win->minimized &&
        mouse_x >= win->x && mouse_x <= win->x + win->w &&
        mouse_y >= win->y && mouse_y <= win->y + win->h) {

        /* Botón Cerrar (Rojo) */
        int close_dx = mouse_x - (win->x + win->w - 22);
        int close_dy = mouse_y - (win->y + 20);
        if (close_dx*close_dx + close_dy*close_dy <= 11*11) {
            play_bubble_sfx();
            win->visible = 0;
            return;
        }

        /* Botón Minimizar (Amarillo) */
        int min_dx = mouse_x - (win->x + win->w - 66);
        int min_dy = mouse_y - (win->y + 20);
        if (min_dx*min_dx + min_dy*min_dy <= 11*11) {
            play_bubble_sfx();
            win->minimized = 1;
            return;
        }

        /* Navegador */
        if (active_win == WIN_BROWSER) {
            if (mouse_x >= win->x + 92 && mouse_x <= win->x + win->w - 75 && mouse_y >= win->y + 46 && mouse_y <= win->y + 74) {
                url_focused = 1;
                play_bubble_sfx();
                return;
            } else {
                url_focused = 0;
            }

            if (mouse_x >= win->x + win->w - 70 && mouse_x <= win->x + win->w - 15 && mouse_y >= win->y + 46 && mouse_y <= win->y + 74) {
                navigate_to_url();
                return;
            }
        } else {
            url_focused = 0;
        }

        /* Ajustes */
        if (active_win == WIN_SETTINGS) {
            if (mouse_y >= win->y + 78 && mouse_y <= win->y + 108) {
                if (mouse_x >= win->x + 25 && mouse_x <= win->x + 120) current_theme = THEME_OCEAN;
                else if (mouse_x >= win->x + 128 && mouse_x <= win->x + 223) current_theme = THEME_ECO;
                else if (mouse_x >= win->x + 231 && mouse_x <= win->x + 326) current_theme = THEME_SUNSET;
                else if (mouse_x >= win->x + 334 && mouse_x <= win->x + 429) current_theme = THEME_VIOLET;
                else if (mouse_x >= win->x + 437 && mouse_x <= win->x + 532) current_theme = THEME_NIGHT;
                play_bubble_sfx();
                return;
            }

            if (mouse_y >= win->y + 152 && mouse_y <= win->y + 182) {
                if (mouse_x >= win->x + 25 && mouse_x <= win->x + 135) bubble_speed_mode = 0;
                else if (mouse_x >= win->x + 145 && mouse_x <= win->x + 255) bubble_speed_mode = 1;
                else if (mouse_x >= win->x + 265 && mouse_x <= win->x + 375) bubble_speed_mode = 2;
                else if (mouse_x >= win->x + 385 && mouse_x <= win->x + 495) bubble_speed_mode = 3;
                play_bubble_sfx();
                return;
            }

            if (mouse_y >= win->y + 230 && mouse_y <= win->y + 260) {
                if (mouse_x >= win->x + 25 && mouse_x <= win->x + 165) glass_alpha = 180;
                else if (mouse_x >= win->x + 180 && mouse_x <= win->x + 320) glass_alpha = 225;
                else if (mouse_x >= win->x + 335 && mouse_x <= win->x + 475) glass_alpha = 255;
                play_bubble_sfx();
                return;
            }

            if (mouse_y >= win->y + 308 && mouse_y <= win->y + 338) {
                if (mouse_x >= win->x + 25 && mouse_x <= win->x + 245) {
                    show_eco_hills = !show_eco_hills;
                    play_bubble_sfx();
                    return;
                } else if (mouse_x >= win->x + 265 && mouse_x <= win->x + 485) {
                    sfx_enabled = !sfx_enabled;
                    play_bubble_sfx();
                    return;
                }
            }
        }

        /* Archivos */
        else if (active_win == WIN_FILES) {
            int fx = win->x + 40, fy = win->y + 105;
            if (mouse_x >= fx - 10 && mouse_x <= fx + 85 && mouse_y >= fy - 8 && mouse_y <= fy + 77) {
                modal_image_open = 1;
                play_bubble_sfx();
                return;
            }
        }

        /* Player */
        else if (active_win == WIN_PLAYER) {
            int btn_x = win->x + win->w / 2 - 50;
            int btn_y = win->y + 315;
            if (mouse_x >= btn_x && mouse_x <= btn_x + 100 && mouse_y >= btn_y && mouse_y <= btn_y + 32) {
                music_playing = !music_playing;
                play_bubble_sfx();
                return;
            }
        }

        /* Arrastre */
        if (mouse_y >= win->y && mouse_y <= win->y + 38) {
            is_dragging = 1;
            drag_off_x = mouse_x - win->x;
            drag_off_y = mouse_y - win->y;
            return;
        }
        return;
    }

    /* Traer al frente */
    for (int i = WIN_COUNT - 1; i >= 0; i--) {
        if (i != active_win && windows[i].visible && !windows[i].minimized) {
            if (mouse_x >= windows[i].x && mouse_x <= windows[i].x + windows[i].w &&
                mouse_y >= windows[i].y && mouse_y <= windows[i].y + windows[i].h) {
                bring_to_front(i);
                return;
            }
        }
    }

    /* Iconos del Escritorio */
    if (mouse_x >= 35 && mouse_x <= 120) {
        if (mouse_y >= 30 && mouse_y <= 115) bring_to_front(WIN_SYSTEM);
        else if (mouse_y >= 130 && mouse_y <= 215) bring_to_front(WIN_FILES);
        else if (mouse_y >= 230 && mouse_y <= 315) bring_to_front(WIN_PLAYER);
        else if (mouse_y >= 330 && mouse_y <= 415) bring_to_front(WIN_BROWSER);
        else if (mouse_y >= 430 && mouse_y <= 515) bring_to_front(WIN_SETTINGS);
        return;
    }
}

/* ============================================================================
 * Driver del Ratón PS/2
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
 * Punto de Entrada del Kernel HD
 * ============================================================================ */
void kernel_main(uint32_t magic, uint32_t addr) {
    (void)magic;
    struct multiboot_info* mbi = (struct multiboot_info*)addr;

    if ((mbi->flags & (1 << 12)) && mbi->framebuffer_addr != 0 && mbi->framebuffer_bpp == 32) {
        fb = (uint32_t*)(uint32_t)mbi->framebuffer_addr;
        screen_w = mbi->framebuffer_width;
        screen_h = mbi->framebuffer_height;
        screen_pitch_pixels = mbi->framebuffer_pitch / 4;

        ps2_mouse_init();
        play_bubble_sfx();

        while (1) {
            while (inb(0x64) & 1) {
                uint8_t status = inb(0x64);
                uint8_t data   = inb(0x60);

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
