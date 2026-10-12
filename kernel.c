/*
 * kernel.c — Kernel Gráfico "Frutiger Aero" para Kolotibablo OS
 *
 * Basado en la estética Frutiger Aero (Aero OS):
 * - Framebuffer Lineal VESA (1024x768x32bpp) vía Multiboot v1.
 * - Gradiente de cielo acuático, colinas verdes Frutiger Eco curvadas y burbujas.
 * - Barra de tareas translúcida con Start Orb esférico reflectante.
 * - Ventana de cristal estilo Aero Glass con botones rojo/amarillo/verde 3D y telemetría.
 * - Iconos de escritorio translúcidos y cursor de ratón aerodinámico.
 */

#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * Estructuras de Multiboot v1 para extraer el Framebuffer
 * ============================================================================ */
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

/* ============================================================================
 * Variables de Estado del Motor Gráfico
 * ============================================================================ */
static uint32_t* fb = NULL;
static uint32_t screen_w = 1024;
static uint32_t screen_h = 768;
static uint32_t screen_pitch_pixels = 1024;

/* ============================================================================
 * Primitivas de Color y Dibujo en Memoria de Video (ARGB 32-bit)
 * ============================================================================ */
static inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (0xFFU << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static inline uint8_t get_r(uint32_t c) { return (c >> 16) & 0xFF; }
static inline uint8_t get_g(uint32_t c) { return (c >> 8)  & 0xFF; }
static inline uint8_t get_b(uint32_t c) { return c & 0xFF; }

/* Alpha blending rápido: combina color frontal con fondo según alpha (0-255) */
static inline uint32_t blend(uint32_t fg, uint32_t bg, uint8_t a) {
    if (a == 255) return fg;
    if (a == 0) return bg;
    uint32_t inv_a = 255 - a;
    uint32_t r = (get_r(fg) * a + get_r(bg) * inv_a) >> 8;
    uint32_t g = (get_g(fg) * a + get_g(bg) * inv_a) >> 8;
    uint32_t b = (get_b(fg) * a + get_b(bg) * inv_a) >> 8;
    return rgb(r, g, b);
}

static inline void put_pixel(int x, int y, uint32_t color) {
    if (x >= 0 && (uint32_t)x < screen_w && y >= 0 && (uint32_t)y < screen_h) {
        fb[y * screen_pitch_pixels + x] = color;
    }
}

static inline void put_pixel_alpha(int x, int y, uint32_t color, uint8_t alpha) {
    if (x >= 0 && (uint32_t)x < screen_w && y >= 0 && (uint32_t)y < screen_h) {
        uint32_t bg = fb[y * screen_pitch_pixels + x];
        fb[y * screen_pitch_pixels + x] = blend(color, bg, alpha);
    }
}

static void fill_rect(int x, int y, int w, int h, uint32_t color) {
    for (int j = y; j < y + h; j++) {
        if (j < 0 || (uint32_t)j >= screen_h) continue;
        for (int i = x; i < x + w; i++) {
            if (i < 0 || (uint32_t)i >= screen_w) continue;
            fb[j * screen_pitch_pixels + i] = color;
        }
    }
}

static void fill_rect_alpha(int x, int y, int w, int h, uint32_t color, uint8_t alpha) {
    for (int j = y; j < y + h; j++) {
        if (j < 0 || (uint32_t)j >= screen_h) continue;
        for (int i = x; i < x + w; i++) {
            if (i < 0 || (uint32_t)i >= screen_w) continue;
            put_pixel_alpha(i, j, color, alpha);
        }
    }
}

/* ============================================================================
 * Fuente Bitmap 8x16 Incrustada (para texto en modo gráfico sin dependencias)
 * ============================================================================ */
static const uint8_t font_8x16_basic[128][16] = {
    ['A'] = {0x00,0x00,0x18,0x24,0x42,0x42,0x7E,0x42,0x42,0x42,0x42,0x00,0x00,0x00,0x00,0x00},
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
    ['i'] = {0x00,0x00,0x10,0x00,0x30,0x10,0x10,0x10,0x38,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['l'] = {0x00,0x00,0x30,0x10,0x10,0x10,0x10,0x10,0x38,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['m'] = {0x00,0x00,0x00,0x76,0x49,0x49,0x49,0x49,0x49,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['n'] = {0x00,0x00,0x00,0x5C,0x62,0x42,0x42,0x42,0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['o'] = {0x00,0x00,0x00,0x3C,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['p'] = {0x00,0x00,0x00,0x5C,0x62,0x42,0x62,0x5C,0x40,0x40,0x00,0x00,0x00,0x00,0x00,0x00},
    ['r'] = {0x00,0x00,0x00,0x56,0x68,0x40,0x40,0x40,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['s'] = {0x00,0x00,0x00,0x3E,0x40,0x3C,0x02,0x42,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['t'] = {0x00,0x00,0x20,0x78,0x20,0x20,0x20,0x24,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['u'] = {0x00,0x00,0x00,0x42,0x42,0x42,0x42,0x46,0x3A,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['v'] = {0x00,0x00,0x00,0x42,0x42,0x24,0x24,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['x'] = {0x00,0x00,0x00,0x42,0x24,0x18,0x24,0x42,0x42,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['y'] = {0x00,0x00,0x00,0x42,0x42,0x42,0x46,0x3A,0x02,0x3C,0x00,0x00,0x00,0x00,0x00,0x00},
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
    ['.'] = {0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['-'] = {0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    [' '] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

static void draw_text(int x, int y, const char* str, uint32_t color) {
    int cur_x = x;
    for (int i = 0; str[i] != '\0'; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c < 128) {
            for (int row = 0; row < 16; row++) {
                uint8_t line = font_8x16_basic[c][row];
                for (int col = 0; col < 8; col++) {
                    if (line & (0x80 >> col)) {
                        put_pixel(cur_x + col, y + row, color);
                    }
                }
            }
        }
        cur_x += 8;
    }
}

/* ============================================================================
 * Círculos y Burbujas con Brillo Especular Frutiger Aero
 * ============================================================================ */
static void draw_bubble(int cx, int cy, int radius) {
    int r2 = radius * radius;
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            int d2 = x * x + y * y;
            if (d2 <= r2) {
                /* Borde translúcido brillante */
                if (d2 > r2 - (radius * 3)) {
                    put_pixel_alpha(cx + x, cy + y, rgb(255, 255, 255), 180);
                } 
                /* Brillo especular blanco en la parte superior izquierda */
                else if (x < -radius/3 && y < -radius/3 && (x+radius/2)*(x+radius/2) + (y+radius/2)*(y+radius/2) < (radius/3)*(radius/3)) {
                    put_pixel_alpha(cx + x, cy + y, rgb(255, 255, 255), 230);
                }
                /* Relleno cristalino acuático */
                else {
                    put_pixel_alpha(cx + x, cy + y, rgb(160, 235, 255), 50);
                }
            }
        }
    }
}

static void draw_sphere(int cx, int cy, int r, uint32_t main_color) {
    int r2 = r * r;
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            int d2 = x * x + y * y;
            if (d2 <= r2) {
                /* Reflejo especular 3D */
                if (x < -r/4 && y < -r/4) {
                    put_pixel(cx + x, cy + y, rgb(255, 255, 255));
                } else if (d2 > r2 - (r * 2)) {
                    /* Borde oscuro */
                    put_pixel(cx + x, cy + y, rgb(get_r(main_color)/2, get_g(main_color)/2, get_b(main_color)/2));
                } else {
                    put_pixel(cx + x, cy + y, main_color);
                }
            }
        }
    }
}

/* ============================================================================
 * Generador del Entorno Frutiger Aero Completo
 * ============================================================================ */
static void render_frutiger_aero_os(void) {
    /* 1. Fondo Wallpaper: Cielo Acuático con Gradiente Suave */
    for (uint32_t y = 0; y < screen_h; y++) {
        /* Transición de azul marino profundo a cian brillante */
        uint8_t r = 2 + (y * 20) / screen_h;
        uint8_t g = 40 + (y * 140) / screen_h;
        uint8_t b = 64 + (y * 120) / screen_h;
        uint32_t sky_color = rgb(r, g, b);
        for (uint32_t x = 0; x < screen_w; x++) {
            fb[y * screen_pitch_pixels + x] = sky_color;
        }
    }

    /* 2. Colinas Verdes Curvadas Frutiger Eco Hills */
    for (uint32_t y = 480; y < screen_h; y++) {
        for (uint32_t x = 0; x < screen_w; x++) {
            /* Curvatura de la colina usando parábola suave */
            int hill_top = 530 - (int)((x - 512) * (x - 512) / 4500);
            if ((int)y >= hill_top) {
                uint8_t gr = 30 + (y - hill_top) / 3;
                uint8_t gg = 175 - (y - hill_top) / 3;
                uint8_t gb = 70;
                put_pixel_alpha(x, y, rgb(gr, gg, gb), 210);
            }
        }
    }

    /* 3. Burbujas Frutiger Aero Brillantes en Suspensión */
    draw_bubble(180, 220, 24);
    draw_bubble(240, 160, 38);
    draw_bubble(280, 250, 16);
    draw_bubble(840, 180, 42);
    draw_bubble(760, 260, 26);
    draw_bubble(880, 320, 20);

    /* 4. Iconos del Escritorio con Etiquetas Glass */
    /* Icono 1: Mi Equipo */
    fill_rect_alpha(35, 45, 75, 75, rgb(255, 255, 255), 45);
    fill_rect(50, 55, 45, 32, rgb(20, 140, 220));
    fill_rect(54, 59, 37, 24, rgb(230, 245, 255));
    fill_rect(68, 87, 9, 8, rgb(160, 180, 200));
    fill_rect(60, 95, 25, 4, rgb(120, 140, 160));
    draw_text(40, 126, "Equipo", rgb(255, 255, 255));

    /* Icono 2: Explorador de Archivos */
    fill_rect_alpha(35, 155, 75, 75, rgb(255, 255, 255), 45);
    fill_rect(50, 172, 45, 32, rgb(240, 180, 40));
    fill_rect(55, 166, 20, 8, rgb(220, 160, 30));
    draw_text(36, 236, "Archivos", rgb(255, 255, 255));

    /* Icono 3: Aero Player */
    fill_rect_alpha(35, 265, 75, 75, rgb(255, 255, 255), 45);
    draw_sphere(72, 300, 18, rgb(0, 200, 160));
    draw_text(44, 346, "Player", rgb(255, 255, 255));

    /* 5. Ventana Principal Central Estilo "Aero Glass" */
    int wx = 220, wy = 90, ww = 580, wh = 420;

    /* Sombra proyectada exterior de la ventana */
    fill_rect_alpha(wx + 8, wy + 8, ww, wh, rgb(0, 20, 50), 90);

    /* Cuerpo Translúcido de la Ventana (Glass Blur Effect) */
    fill_rect_alpha(wx, wy, ww, wh, rgb(235, 248, 255), 215);

    /* Borde exterior blanco de brillo de cristal */
    for (int i = 0; i < ww; i++) {
        put_pixel(wx + i, wy, rgb(255, 255, 255));
        put_pixel(wx + i, wy + wh - 1, rgb(160, 210, 240));
    }
    for (int j = 0; j < wh; j++) {
        put_pixel(wx, wy + j, rgb(255, 255, 255));
        put_pixel(wx + ww - 1, wy + j, rgb(160, 210, 240));
    }

    /* Barra de Título con Gradiente Aero Azul Cristalino */
    for (int j = 0; j < 36; j++) {
        uint8_t bar_r = 140 + j * 2;
        uint8_t bar_g = 210 + j;
        uint8_t bar_b = 255;
        for (int i = 1; i < ww - 1; i++) {
            put_pixel(wx + i, wy + 1 + j, rgb(bar_r, bar_g, bar_b));
        }
    }
    /* Brillo superior en la barra */
    for (int i = 1; i < ww - 1; i++) {
        put_pixel(wx + i, wy + 2, rgb(255, 255, 255));
    }

    /* Título de la ventana */
    draw_text(wx + 18, wy + 11, "Kolotibablo OS - Aero Console Experience", rgb(10, 50, 90));

    /* Botones de Control de Ventana (Estilo Esferas 3D) */
    draw_sphere(wx + ww - 20, wy + 18, 7, rgb(230, 40, 40));   /* Rojo (Cerrar) */
    draw_sphere(wx + ww - 40, wy + 18, 7, rgb(40, 200, 80));   /* Verde (Maximizar) */
    draw_sphere(wx + ww - 60, wy + 18, 7, rgb(240, 180, 20));  /* Amarillo (Minimizar) */

    /* Contenido de la Ventana */
    fill_rect(wx + 20, wy + 55, ww - 40, 75, rgb(255, 255, 255));
    fill_rect(wx + 20, wy + 55, ww - 40, 2, rgb(0, 160, 230));
    draw_text(wx + 35, wy + 72, "BIENVENIDO A KOLOTIBABLO OS", rgb(0, 120, 200));
    draw_text(wx + 35, wy + 98, "Entorno Frutiger Aero VBE 1024x768 True Color Activo", rgb(60, 80, 100));

    /* Panel de Estado / Diagnóstico */
    fill_rect_alpha(wx + 20, wy + 145, ww - 40, 240, rgb(220, 240, 250), 180);
    draw_text(wx + 35, wy + 165, "TELEMETRIA DEL SISTEMA:", rgb(10, 60, 110));
    draw_text(wx + 55, wy + 195, "- Nucleo de Kernel   : Kolotibablo v0.2 Release", rgb(20, 70, 100));
    draw_text(wx + 55, wy + 225, "- Modo de Video      : VESA VBE 32-bit (Linear Framebuffer)", rgb(20, 70, 100));
    draw_text(wx + 55, wy + 255, "- Resolucion Activa  : 1024 x 768 Pixeles", rgb(20, 70, 100));
    draw_text(wx + 55, wy + 285, "- Render Engine      : Software Alpha-Blending & Glass Shaders", rgb(20, 70, 100));
    draw_text(wx + 55, wy + 315, "- Arquitectura CPU   : Intel x86 Protected Mode (Ring 0)", rgb(20, 70, 100));
    draw_text(wx + 55, wy + 345, "- Estado de Energia  : CPU HLT Loop (Idle Eficiente)", rgb(0, 150, 70));

    /* 6. Barra de Tareas Inferior (Aero Taskbar) */
    int tby = screen_h - 48;
    for (int j = 0; j < 48; j++) {
        uint8_t tb_r = 10 + j * 2;
        uint8_t tb_g = 60 + j * 3;
        uint8_t tb_b = 130 + j * 2;
        for (uint32_t i = 0; i < screen_w; i++) {
            put_pixel_alpha(i, tby + j, rgb(tb_r, tb_g, tb_b), 200);
        }
    }
    /* Línea de brillo de cristal superior en la barra */
    for (uint32_t i = 0; i < screen_w; i++) {
        put_pixel(i, tby, rgb(200, 240, 255));
    }

    /* Start Orb (Botón de Inicio Esférico 3D Frutiger Aero) */
    draw_sphere(32, tby + 24, 20, rgb(0, 130, 240));
    draw_text(26, tby + 16, "K", rgb(255, 255, 255));

    /* Reloj / Bandeja del sistema en la barra */
    fill_rect_alpha(screen_w - 110, tby + 8, 95, 32, rgb(255, 255, 255), 40);
    draw_text(screen_w - 95, tby + 17, "12:00 PM", rgb(255, 255, 255));

    /* 7. Cursor de Ratón Aerodinámico */
    int mx = 540, my = 320;
    /* Sombra del cursor */
    for (int cy = 0; cy < 16; cy++) {
        for (int cx = 0; cx <= cy; cx++) {
            put_pixel_alpha(mx + cx + 3, my + cy + 3, rgb(0, 0, 0), 90);
        }
    }
    /* Flecha blanca con borde oscuro */
    for (int cy = 0; cy < 15; cy++) {
        for (int cx = 0; cx <= cy; cx++) {
            if (cx == 0 || cx == cy || cy == 14) {
                put_pixel(mx + cx, my + cy, rgb(10, 40, 70));
            } else {
                put_pixel(mx + cx, my + cy, rgb(255, 255, 255));
            }
        }
    }
}

/* ============================================================================
 * Fallback a Modo Texto VGA (por si la VM no soporta modo gráfico)
 * ============================================================================ */
static void render_text_fallback(void) {
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    for (int i = 0; i < 80 * 25; i++) {
        vga[i] = (uint16_t)' ' | (0x1F << 8);
    }
}

/* ============================================================================
 * Punto de Entrada del Kernel
 * ============================================================================ */
void kernel_main(uint32_t magic, uint32_t addr) {
    (void)magic;
    struct multiboot_info* mbi = (struct multiboot_info*)addr;

    /* Verificar si Multiboot configuró con éxito el framebuffer gráfico */
    if ((mbi->flags & (1 << 12)) && mbi->framebuffer_addr != 0 && mbi->framebuffer_bpp == 32) {
        fb = (uint32_t*)(uint32_t)mbi->framebuffer_addr;
        screen_w = mbi->framebuffer_width;
        screen_h = mbi->framebuffer_height;
        screen_pitch_pixels = mbi->framebuffer_pitch / 4;

        render_frutiger_aero_os();
    } else {
        /* Si falla el modo VBE, mostrar fallback seguro */
        render_text_fallback();
    }

    /* Bucle infinito de CPU en reposo */
    while (1) {
        __asm__ volatile ("hlt");
    }
}
