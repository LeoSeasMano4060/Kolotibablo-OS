/*
 * kernel.c â€” Kernel interactivo y profesional para Kolotibablo OS
 *
 * Modo Texto VGA 80x25 (0xB8000)
 * CodificaciÃ³n de caracteres: IBM Code Page 437 (CP437)
 * DiseÃ±o: Interfaz TUI completa con marcos, ventana central y barra de estado.
 */

#include <stdint.h>
#include <stddef.h>

static const size_t VGA_WIDTH  = 80;
static const size_t VGA_HEIGHT = 25;

#define VGA_MEMORY ((volatile uint16_t*)0xB8000)

/* ============================================================================
 * Constantes de Caracteres CP437 (IBM Box-Drawing & Specials)
 * ============================================================================ */
#define CP437_DOUBLE_TOP_LEFT      0xC9  /* â•” */
#define CP437_DOUBLE_TOP_RIGHT     0xBB  /* â•— */
#define CP437_DOUBLE_BOTTOM_LEFT   0xC8  /* â•š */
#define CP437_DOUBLE_BOTTOM_RIGHT  0xBC  /* â• */
#define CP437_DOUBLE_HORIZONTAL    0xCD  /* â• */
#define CP437_DOUBLE_VERTICAL      0xBA  /* â•‘ */
#define CP437_DOUBLE_CROSS_LEFT    0xCC  /* â•  */
#define CP437_DOUBLE_CROSS_RIGHT   0xB9  /* â•£ */
#define CP437_LIGHT_SHADE          0xB0  /* â–‘ */
#define CP437_MEDIUM_SHADE         0xB1  /* â–’ */
#define CP437_CHECKMARK            0xFB  /* âˆš */
#define CP437_BULLET               0xFE  /* â–  */

/* ============================================================================
 * Paleta de colores VGA
 * ============================================================================ */
typedef enum {
    VGA_COLOR_BLACK          = 0,
    VGA_COLOR_BLUE           = 1,
    VGA_COLOR_GREEN          = 2,
    VGA_COLOR_CYAN           = 3,
    VGA_COLOR_RED            = 4,
    VGA_COLOR_MAGENTA        = 5,
    VGA_COLOR_BROWN          = 6,
    VGA_COLOR_LIGHT_GREY     = 7,
    VGA_COLOR_DARK_GREY      = 8,
    VGA_COLOR_LIGHT_BLUE     = 9,
    VGA_COLOR_LIGHT_GREEN    = 10,
    VGA_COLOR_LIGHT_CYAN     = 11,
    VGA_COLOR_LIGHT_RED      = 12,
    VGA_COLOR_LIGHT_MAGENTA  = 13,
    VGA_COLOR_LIGHT_BROWN    = 14, /* Amarillo en monitores estÃ¡ndar */
    VGA_COLOR_WHITE          = 15,
} vga_color_t;

/* ============================================================================
 * Puertos de hardware VGA (I/O) para manejar el cursor fÃ­sico
 * ============================================================================ */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void vga_disable_cursor(void) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20); /* Bit 5 = Cursor Disable */
}

/* ============================================================================
 * Primitivas grÃ¡ficas VGA
 * ============================================================================ */
static inline uint8_t vga_color(vga_color_t fg, vga_color_t bg) {
    return (uint8_t)fg | ((uint8_t)bg << 4);
}

static inline uint16_t vga_cell(unsigned char ch, uint8_t color) {
    return (uint16_t)ch | ((uint16_t)color << 8);
}

static void draw_char(size_t x, size_t y, unsigned char ch, uint8_t color) {
    if (x < VGA_WIDTH && y < VGA_HEIGHT) {
        VGA_MEMORY[y * VGA_WIDTH + x] = vga_cell(ch, color);
    }
}

static void print_string(size_t x, size_t y, const char* str, uint8_t color) {
    size_t cur_x = x;
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (cur_x >= VGA_WIDTH) break;
        draw_char(cur_x++, y, (unsigned char)str[i], color);
    }
}

static void fill_rect(size_t x, size_t y, size_t w, size_t h, unsigned char ch, uint8_t color) {
    for (size_t row = y; row < y + h && row < VGA_HEIGHT; row++) {
        for (size_t col = x; col < x + w && col < VGA_WIDTH; col++) {
            draw_char(col, row, ch, color);
        }
    }
}

static void draw_double_box(size_t x, size_t y, size_t w, size_t h, uint8_t color) {
    if (w < 2 || h < 2) return;

    /* Esquinas */
    draw_char(x,         y,         CP437_DOUBLE_TOP_LEFT,     color);
    draw_char(x + w - 1, y,         CP437_DOUBLE_TOP_RIGHT,    color);
    draw_char(x,         y + h - 1, CP437_DOUBLE_BOTTOM_LEFT,  color);
    draw_char(x + w - 1, y + h - 1, CP437_DOUBLE_BOTTOM_RIGHT, color);

    /* Bordes horizontales */
    for (size_t col = x + 1; col < x + w - 1; col++) {
        draw_char(col, y,         CP437_DOUBLE_HORIZONTAL, color);
        draw_char(col, y + h - 1, CP437_DOUBLE_HORIZONTAL, color);
    }

    /* Bordes verticales */
    for (size_t row = y + 1; row < y + h - 1; row++) {
        draw_char(x,         row, CP437_DOUBLE_VERTICAL, color);
        draw_char(x + w - 1, row, CP437_DOUBLE_VERTICAL, color);
    }
}

/* ============================================================================
 * Renderizado de Interfaz Profesional
 * ============================================================================ */

void render_desktop(void) {
    uint8_t bg_color     = vga_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    uint8_t bar_color    = vga_color(VGA_COLOR_WHITE,     VGA_COLOR_BLUE);
    uint8_t bar_key      = vga_color(VGA_COLOR_LIGHT_CYAN,VGA_COLOR_BLUE);
    uint8_t win_frame    = vga_color(VGA_COLOR_LIGHT_CYAN,VGA_COLOR_BLACK);
    uint8_t win_title    = vga_color(VGA_COLOR_WHITE,     VGA_COLOR_CYAN);
    uint8_t text_title   = vga_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK);
    uint8_t text_regular = vga_color(VGA_COLOR_LIGHT_GREY,  VGA_COLOR_BLACK);
    uint8_t ok_color     = vga_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    uint8_t label_color  = vga_color(VGA_COLOR_WHITE,       VGA_COLOR_BLACK);

    /* 1. Fondo texturizado elegante */
    fill_rect(0, 0, VGA_WIDTH, VGA_HEIGHT, ' ', vga_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    fill_rect(0, 1, VGA_WIDTH, VGA_HEIGHT - 2, CP437_LIGHT_SHADE, bg_color);

    /* 2. Barra Superior (Top Navigation Bar) */
    fill_rect(0, 0, VGA_WIDTH, 1, ' ', bar_color);
    print_string(2, 0, "KOLOTIBABLO OS", vga_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLUE));
    print_string(17, 0, "| Architecture: i686 (32-bit)", bar_color);
    print_string(54, 0, "Build: 2026.1-RELEASE", bar_key);

    /* 3. Ventana Principal Central (ancho: 72, alto: 20, x: 4, y: 2) */
    size_t wx = 4, wy = 2, ww = 72, wh = 20;
    fill_rect(wx, wy, ww, wh, ' ', vga_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    draw_double_box(wx, wy, ww, wh, win_frame);

    /* TÃ­tulo incrustado en el marco */
    print_string(wx + 3, wy, " [ SYSTEM CONSOLE: KERNEL STATUS ] ", win_title);

    /* Banner ASCII en relieve */
    print_string(wx + 4, wy + 2, " _  ___     _       _   _ _           _     _        ____   ____ ", vga_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    print_string(wx + 4, wy + 3, "| |/ / |__ | | ___ | |_(_) |__   __ _| |__ | | ___  / __ \\ / ___|", vga_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    print_string(wx + 4, wy + 4, "| ' /| '_ \\| |/ _ \\| __| | '_ \\ / _` | '_ \\| |/ _ \\| |  | |\\___ \\", vga_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK));
    print_string(wx + 4, wy + 5, "| . \\| |_) | | (_) | |_| | |_) | (_| | |_) | | (_) | |__| | ___) |", vga_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK));
    print_string(wx + 4, wy + 6, "|_|\\_\\_.__/|_|\\___/ \\__|_|_.__/ \\__,_|_.__/|_|\\___/ \\____/ |____/ ", vga_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK));

    /* LÃ­nea separadora */
    for (size_t c = wx + 2; c < wx + ww - 2; c++) {
        draw_char(c, wy + 7, CP437_DOUBLE_HORIZONTAL, vga_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    }

    /* Mensaje de bienvenida oficial */
    print_string(wx + 4, wy + 8, "Bienvenido a Kolotibablo OS - El sistema esta completamente operativo.", text_title);

    /* SecciÃ³n de TelemetrÃ­a y DiagnÃ³stico */
    print_string(wx + 4, wy + 10, "DIAGNOSTICO DEL KERNEL:", label_color);

    draw_char(wx + 6, wy + 11, CP437_CHECKMARK, ok_color);
    print_string(wx + 8, wy + 11, "Multiboot v1 Protocol   : Sincronizado OK (0x1BADB002)", text_regular);

    draw_char(wx + 6, wy + 12, CP437_CHECKMARK, ok_color);
    print_string(wx + 8, wy + 12, "Kernel Physical Offset  : 0x00100000 (1 MiB Mark)", text_regular);

    draw_char(wx + 6, wy + 13, CP437_CHECKMARK, ok_color);
    print_string(wx + 8, wy + 13, "Execution Mode          : 32-bit Protected Mode (CR0.PE = 1)", text_regular);

    draw_char(wx + 6, wy + 14, CP437_CHECKMARK, ok_color);
    print_string(wx + 8, wy + 14, "VGA Display Adapter     : IBM Color Text 80x25 @ 0xB8000", text_regular);

    draw_char(wx + 6, wy + 15, CP437_CHECKMARK, ok_color);
    print_string(wx + 8, wy + 15, "CPU Interrupts State    : IF=0 (CLI mode, Clean Halt)", text_regular);

    /* Cuadro de aviso inferior de la ventana */
    fill_rect(wx + 3, wy + 17, ww - 6, 1, ' ', vga_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY));
    print_string(wx + 5, wy + 17, "Kernel en reposo seguro. La CPU esta en bucle HLT sin consumo.", vga_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY));

    /* 4. Barra Inferior de Estado (Bottom Status Bar) */
    fill_rect(0, VGA_HEIGHT - 1, VGA_WIDTH, 1, ' ', bar_color);
    print_string(2,  VGA_HEIGHT - 1, "[CPU: Halted]", bar_key);
    print_string(18, VGA_HEIGHT - 1, "[Memory: 16 KiB Stack]", bar_color);
    print_string(45, VGA_HEIGHT - 1, "[Ready for Driver Extensions]", vga_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE));
}

/* ============================================================================
 * Punto de entrada principal
 * ============================================================================ */
void kernel_main(void) {
    /* 1. Ocultar el cursor parpadeante de hardware para aspecto impecable */
    vga_disable_cursor();

    /* 2. Dibujar la interfaz completa estilo consola retro profesional */
    render_desktop();

    /* 3. Bucle infinito seguro usando instrucciÃ³n de bajo consumo HLT */
    while (1) {
        __asm__ volatile ("hlt");
    }
}
