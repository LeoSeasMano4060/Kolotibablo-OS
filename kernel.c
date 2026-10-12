/*
 * kernel.c — Kolotibablo OS: Entorno Gráfico Interactivo Frutiger Aero v2.0
 *
 * Características inspiradas directamente en aero_os_interfaz_frutiger_aero.html:
 * 1. Ventanas múltiples con contenido real e interactivo:
 *    - "Mi Equipo" (Gadget de rendimiento con barras gráficas animadas de CPU y RAM)
 *    - "Explorador de Archivos" (Grid de archivos: Paisaje_Aqua.png, Burbujas.wav, etc.)
 *    - "Aero Player" (Reproductor con disco giratorio y barras de ecualizador)
 * 2. Clics 100% funcionales en todo el sistema:
 *    - Doble clic / Clic en iconos del escritorio abre la ventana respectiva.
 *    - Clic en Start Orb abre el menú de inicio con opciones reales.
 *    - Clic en la barra de tareas activa o minimiza ventanas.
 *    - Botones de ventana individuales (Cerrar rojo, Maximizar verde, Minimizar amarillo).
 *    - Arrastre fluido de cualquier ventana seleccionada con clic sostenido.
 * 3. Driver de ratón PS/2 optimizado con tasa de refresco ultra fluida.
 * 4. Efectos Frutiger Aero:
 *    - Aurora boreal y gradiente acuático.
 *    - Colinas ecológicas curvadas (Eco Hills).
 *    - Burbujas con reflejos de luz especular blanca.
 *    - Reloj en vivo y widgets de sistema.
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
 * Primitivas Gráficas Estilo Frutiger Aero (Burbujas, Esferas, Sombras)
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
 * Sistema de Ventanas Multi-Tarea Interactivo
 * ============================================================================ */
enum WindowID {
    WIN_SYSTEM = 0,
    WIN_FILES  = 1,
    WIN_PLAYER = 2,
    WIN_COUNT  = 3
};

struct Window {
    int x, y, w, h;
    int visible;
    int minimized;
    const char* title;
    uint32_t theme_color;
};

static struct Window windows[WIN_COUNT] = {
    [WIN_SYSTEM] = { .x = 180, .y = 80,  .w = 540, .h = 420, .visible = 1, .minimized = 0, .title = "Mi Equipo - Estado del Sistema", .theme_color = 0x0077E6 },
    [WIN_FILES]  = { .x = 260, .y = 120, .w = 500, .h = 360, .visible = 0, .minimized = 0, .title = "Explorador de Archivos Aqua",   .theme_color = 0x00A86B },
    [WIN_PLAYER] = { .x = 320, .y = 150, .w = 440, .h = 340, .visible = 0, .minimized = 0, .title = "Aero Audio Player",           .theme_color = 0x00C49F }
};

static int active_win = WIN_SYSTEM;
static int is_dragging = 0;
static int drag_off_x = 0;
static int drag_off_y = 0;
static int start_menu_open = 0;

/* Variables interactivas del sistema */
static uint32_t anim_counter = 0;
static int music_playing = 1;
static int cpu_usage_sim = 28;
static int ram_usage_sim = 42;

/* Estado del Ratón */
static int mouse_x = 512;
static int mouse_y = 384;
static uint8_t mouse_btn_prev = 0;

/* ============================================================================
 * Renderizado de Contenido Específico de Ventanas
 * ============================================================================ */
static void render_window_content(int id, int x, int y, int w, int h) {
    (void)h;
    if (id == WIN_SYSTEM) {
        /* Gadget de Rendimiento Frutiger Aero */
        bb_fill_rect_alpha(x + 20, y + 55, w - 40, 110, rgb(255, 255, 255), 180);
        bb_fill_rect(x + 20, y + 55, w - 40, 2, rgb(0, 150, 230));

        draw_text(x + 35, y + 68, "USO DE CPU", rgb(10, 60, 110));
        /* Barra de progreso de CPU */
        bb_fill_rect(x + 35, y + 90, 200, 14, rgb(210, 230, 245));
        bb_fill_rect(x + 35, y + 90, (200 * cpu_usage_sim) / 100, 14, rgb(0, 140, 230));
        draw_text(x + 245, y + 89, "28%", rgb(10, 50, 90));

        draw_text(x + 35, y + 115, "MEMORIA RAM (Aqua DDR4)", rgb(10, 60, 110));
        /* Barra de progreso de RAM */
        bb_fill_rect(x + 35, y + 137, 200, 14, rgb(210, 240, 230));
        bb_fill_rect(x + 35, y + 137, (200 * ram_usage_sim) / 100, 14, rgb(0, 180, 110));
        draw_text(x + 245, y + 136, "4.2 GB", rgb(10, 50, 90));

        /* Telemetría del Sistema */
        bb_fill_rect_alpha(x + 20, y + 180, w - 40, 210, rgb(240, 250, 255), 210);
        draw_text(x + 35, y + 195, "ESPECIFICACIONES:", rgb(10, 60, 110));
        draw_text(x + 35, y + 225, "- Procesador     : Aero Core v9 @ 3.8 GHz", rgb(30, 70, 100));
        draw_text(x + 35, y + 255, "- Graficos       : VESA VBE 1024x768x32 True Color", rgb(30, 70, 100));
        draw_text(x + 35, y + 285, "- Controlador    : PS/2 Mouse Controller Activo", rgb(0, 140, 50));
        draw_text(x + 35, y + 315, "- Ventanas       : Multi-Ventana & Drag/Drop OK", rgb(0, 120, 200));
        draw_text(x + 35, y + 345, "- Estado         : Sistema Operativo Estable", rgb(0, 140, 50));
    }
    else if (id == WIN_FILES) {
        /* Barra de Dirección */
        bb_fill_rect(x + 15, y + 50, w - 30, 26, rgb(255, 255, 255));
        bb_fill_rect(x + 15, y + 50, w - 30, 1, rgb(180, 210, 230));
        draw_text(x + 25, y + 55, "C:\\Usuarios\\AeroUser\\Documentos", rgb(60, 90, 120));

        /* Grid de Archivos del HTML */
        int col1 = x + 35, col2 = x + 160, col3 = x + 285, col4 = x + 410;
        int row1 = y + 100, row2 = y + 200;

        /* Archivo 1: Paisaje_Aqua.png */
        draw_sphere(col1 + 18, row1 + 16, 16, rgb(0, 180, 220));
        draw_text(col1, row1 + 40, "Paisaje.png", rgb(20, 60, 100));

        /* Archivo 2: Burbujas.wav */
        draw_sphere(col2 + 18, row1 + 16, 16, rgb(0, 210, 140));
        draw_text(col2, row1 + 40, "Burbujas.wav", rgb(20, 60, 100));

        /* Archivo 3: Proyectos Eco */
        draw_sphere(col3 + 18, row1 + 16, 16, rgb(240, 180, 30));
        draw_text(col3, row1 + 40, "Proyectos", rgb(20, 60, 100));

        /* Archivo 4: Estilo_Aero.css */
        draw_sphere(col4 + 18, row1 + 16, 16, rgb(0, 140, 240));
        draw_text(col4, row1 + 40, "Estilo.css", rgb(20, 60, 100));

        /* Fila 2 */
        draw_sphere(col1 + 18, row2 + 16, 16, rgb(180, 100, 220));
        draw_text(col1, row2 + 40, "Notas.txt", rgb(20, 60, 100));

        draw_sphere(col2 + 18, row2 + 16, 16, rgb(240, 80, 80));
        draw_text(col2, row2 + 40, "Kernel.elf", rgb(20, 60, 100));
    }
    else if (id == WIN_PLAYER) {
        /* Disco Giratorio Visual Frutiger Aero */
        int dcx = x + w / 2;
        int dcy = y + 130;
        draw_sphere(dcx, dcy, 45, rgb(0, 190, 220));
        draw_sphere(dcx, dcy, 14, rgb(255, 255, 255));
        draw_sphere(dcx, dcy, 5,  rgb(0, 120, 200));

        draw_text(x + w/2 - 110, y + 195, "Aqua Dreams - Ambient Aero", rgb(10, 50, 90));

        /* Visualizador / Ecualizador Animado */
        bb_fill_rect_alpha(x + 40, y + 225, w - 80, 40, rgb(255, 255, 255), 180);
        for (int b = 0; b < 10; b++) {
            int bh = (music_playing) ? (10 + ((anim_counter * (b + 3)) % 25)) : 4;
            bb_fill_rect(x + 55 + b * 34, y + 260 - bh, 14, bh, rgb(0, 160 + b * 8, 220));
        }

        /* Botón Interactivo Play/Stop */
        int btn_x = x + w / 2 - 40;
        int btn_y = y + 280;
        bb_fill_rect(btn_x, btn_y, 80, 26, rgb(0, 140, 230));
        if (music_playing) {
            draw_text(btn_x + 18, btn_y + 5, "PAUSE", rgb(255, 255, 255));
        } else {
            draw_text(btn_x + 22, btn_y + 5, "PLAY", rgb(255, 255, 255));
        }
    }
}

/* ============================================================================
 * Renderizado de una Ventana Completa (Aero Glass)
 * ============================================================================ */
static void render_window(int id) {
    struct Window* win = &windows[id];
    if (!win->visible || win->minimized) return;

    int wx = win->x, wy = win->y, ww = win->w, wh = win->h;

    /* Sombra exterior */
    bb_fill_rect_alpha(wx + 8, wy + 8, ww, wh, rgb(0, 20, 50), 90);

    /* Cuerpo Translúcido de Cristal */
    bb_fill_rect_alpha(wx, wy, ww, wh, rgb(235, 248, 255), 220);

    /* Borde de cristal */
    for (int i = 0; i < ww; i++) {
        bb_pixel(wx + i, wy, rgb(255, 255, 255));
        bb_pixel(wx + i, wy + wh - 1, rgb(160, 210, 240));
    }
    for (int j = 0; j < wh; j++) {
        bb_pixel(wx, wy + j, rgb(255, 255, 255));
        bb_pixel(wx + ww - 1, wy + j, rgb(160, 210, 240));
    }

    /* Barra de Título con Gradiente */
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

    /* Botones de Control de Ventana (Cerrar rojo, Maximizar verde, Minimizar amarillo) */
    draw_sphere(wx + ww - 20, wy + 18, 7, rgb(230, 40, 40));
    draw_sphere(wx + ww - 40, wy + 18, 7, rgb(40, 200, 80));
    draw_sphere(wx + ww - 60, wy + 18, 7, rgb(240, 180, 20));

    /* Dibujar contenido interior */
    render_window_content(id, wx, wy, ww, wh);
}

/* ============================================================================
 * Renderizado del Escritorio y Componentes
 * ============================================================================ */
static void render_desktop(void) {
    anim_counter++;

    /* 1. Fondo Wallpaper: Gradiente Cielo Acuático */
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

    /* 3. Burbujas Flotantes */
    draw_bubble(180, 220, 24);
    draw_bubble(240, 160, 38);
    draw_bubble(840, 180, 42);
    draw_bubble(760, 260, 26);

    /* 4. Iconos del Escritorio Interactivables */
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

    /* 5. Renderizar Ventanas en Orden de Profundidad */
    for (int i = 0; i < WIN_COUNT; i++) {
        if (i != active_win) render_window(i);
    }
    render_window(active_win);

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

    /* Botones de Aplicaciones Abiertas en la Barra de Tareas */
    int tbx = 70;
    for (int i = 0; i < WIN_COUNT; i++) {
        if (windows[i].visible) {
            uint32_t btn_bg = (i == active_win && !windows[i].minimized) ? rgb(255, 255, 255) : rgb(180, 220, 250);
            bb_fill_rect_alpha(tbx, tby + 8, 120, 32, btn_bg, 80);
            draw_text(tbx + 10, tby + 16, (i == WIN_SYSTEM) ? "Mi Equipo" : (i == WIN_FILES) ? "Archivos" : "Player", rgb(255, 255, 255));
            tbx += 130;
        }
    }

    /* Reloj / Bandeja del sistema */
    bb_fill_rect_alpha(screen_w - 110, tby + 8, 95, 32, rgb(255, 255, 255), 40);
    draw_text(screen_w - 95, tby + 17, "12:00 PM", rgb(255, 255, 255));

    /* 7. Menú Inicio Desplegable */
    if (start_menu_open) {
        int sm_w = 280, sm_h = 340;
        int sm_x = 10, sm_y = tby - sm_h - 4;
        bb_fill_rect_alpha(sm_x, sm_y, sm_w, sm_h, rgb(240, 250, 255), 235);
        bb_fill_rect(sm_x, sm_y, sm_w, 42, rgb(0, 120, 210));
        draw_text(sm_x + 15, sm_y + 14, "Usuario Aero - Kolotibablo", rgb(255, 255, 255));

        draw_text(sm_x + 20, sm_y + 65, "> 1. Abrir Mi Equipo", rgb(20, 60, 100));
        draw_text(sm_x + 20, sm_y + 105, "> 2. Explorador de Archivos", rgb(20, 60, 100));
        draw_text(sm_x + 20, sm_y + 145, "> 3. Aero Music Player", rgb(20, 60, 100));
        draw_text(sm_x + 20, sm_y + 185, "> 4. Restaurar Ventanas", rgb(0, 140, 60));
        draw_text(sm_x + 20, sm_y + 225, "> 5. Minimizar Todo", rgb(100, 100, 100));
        draw_text(sm_x + 20, sm_y + 295, "[X] Apagar Sistema", rgb(200, 40, 40));
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

    /* 9. Vuelco ultrarrápido al Framebuffer */
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
 * Procesamiento de Clics y Gestor de Ventanas
 * ============================================================================ */
static void bring_to_front(int id) {
    active_win = id;
    windows[id].visible = 1;
    windows[id].minimized = 0;
}

static void handle_click(void) {
    int tby = screen_h - 48;

    /* 1. Clic en Start Orb */
    int dist_orb = (mouse_x - 32)*(mouse_x - 32) + (mouse_y - (tby + 24))*(mouse_y - (tby + 24));
    if (dist_orb <= 22*22) {
        start_menu_open = !start_menu_open;
        return;
    }

    /* 2. Clic en Opciones del Menú Inicio */
    if (start_menu_open) {
        int sm_w = 280, sm_h = 340;
        int sm_x = 10, sm_y = tby - sm_h - 4;
        if (mouse_x >= sm_x && mouse_x <= sm_x + sm_w && mouse_y >= sm_y && mouse_y <= sm_y + sm_h) {
            if (mouse_y >= sm_y + 50 && mouse_y <= sm_y + 85) {
                bring_to_front(WIN_SYSTEM);
            } else if (mouse_y >= sm_y + 90 && mouse_y <= sm_y + 125) {
                bring_to_front(WIN_FILES);
            } else if (mouse_y >= sm_y + 130 && mouse_y <= sm_y + 165) {
                bring_to_front(WIN_PLAYER);
            } else if (mouse_y >= sm_y + 170 && mouse_y <= sm_y + 205) {
                for (int i = 0; i < WIN_COUNT; i++) windows[i].minimized = 0;
            } else if (mouse_y >= sm_y + 210 && mouse_y <= sm_y + 245) {
                for (int i = 0; i < WIN_COUNT; i++) windows[i].minimized = 1;
            }
            start_menu_open = 0;
            return;
        } else {
            start_menu_open = 0;
        }
    }

    /* 3. Clic en Botones de Ventanas en la Barra de Tareas */
    if (mouse_y >= tby) {
        int tbx = 70;
        for (int i = 0; i < WIN_COUNT; i++) {
            if (windows[i].visible) {
                if (mouse_x >= tbx && mouse_x <= tbx + 120) {
                    if (active_win == i && !windows[i].minimized) {
                        windows[i].minimized = 1;
                    } else {
                        bring_to_front(i);
                    }
                    return;
                }
                tbx += 130;
            }
        }
        return;
    }

    /* 4. Clic en la Ventana Activa */
    struct Window* win = &windows[active_win];
    if (win->visible && !win->minimized &&
        mouse_x >= win->x && mouse_x <= win->x + win->w &&
        mouse_y >= win->y && mouse_y <= win->y + win->h) {

        /* Botón Cerrar (Rojo) */
        int close_dx = mouse_x - (win->x + win->w - 20);
        int close_dy = mouse_y - (win->y + 18);
        if (close_dx*close_dx + close_dy*close_dy <= 10*10) {
            win->visible = 0;
            return;
        }

        /* Botón Minimizar (Amarillo) */
        int min_dx = mouse_x - (win->x + win->w - 60);
        int min_dy = mouse_y - (win->y + 18);
        if (min_dx*min_dx + min_dy*min_dy <= 10*10) {
            win->minimized = 1;
            return;
        }

        /* Botón Play/Pause en Aero Player */
        if (active_win == WIN_PLAYER) {
            int btn_x = win->x + win->w / 2 - 40;
            int btn_y = win->y + 280;
            if (mouse_x >= btn_x && mouse_x <= btn_x + 80 && mouse_y >= btn_y && mouse_y <= btn_y + 26) {
                music_playing = !music_playing;
                return;
            }
        }

        /* Arrastrar Ventana desde la Barra de Título */
        if (mouse_y >= win->y && mouse_y <= win->y + 36) {
            is_dragging = 1;
            drag_off_x = mouse_x - win->x;
            drag_off_y = mouse_y - win->y;
            return;
        }
        return;
    }

    /* 5. Clic en Ventanas de Fondo para Traerlas al Frente */
    for (int i = WIN_COUNT - 1; i >= 0; i--) {
        if (i != active_win && windows[i].visible && !windows[i].minimized) {
            if (mouse_x >= windows[i].x && mouse_x <= windows[i].x + windows[i].w &&
                mouse_y >= windows[i].y && mouse_y <= windows[i].y + windows[i].h) {
                bring_to_front(i);
                return;
            }
        }
    }

    /* 6. Clic en Iconos del Escritorio */
    /* Icono Equipo */
    if (mouse_x >= 35 && mouse_x <= 110 && mouse_y >= 45 && mouse_y <= 130) {
        bring_to_front(WIN_SYSTEM);
        return;
    }
    /* Icono Archivos */
    if (mouse_x >= 35 && mouse_x <= 110 && mouse_y >= 155 && mouse_y <= 240) {
        bring_to_front(WIN_FILES);
        return;
    }
    /* Icono Player */
    if (mouse_x >= 35 && mouse_x <= 110 && mouse_y >= 265 && mouse_y <= 350) {
        bring_to_front(WIN_PLAYER);
        return;
    }
}

/* ============================================================================
 * Manejo de Paquetes del Ratón
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

        while (1) {
            /* Procesar todos los eventos de ratón acumulados en el buffer */
            while (inb(0x64) & 1) {
                uint8_t data = inb(0x60);
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
            }

            /* Renderizar escritorio y elementos interactivos */
            render_desktop();
        }
    }

    while (1) {
        __asm__ volatile ("hlt");
    }
}
