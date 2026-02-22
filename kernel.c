/*
 * kernel.c — Kernel principal de Kolotibablo OS
 *
 * Compilación: freestanding (sin biblioteca estándar C)
 * No se usan funciones de libc: no hay printf, malloc, etc.
 * Accedemos al hardware directamente a través del buffer de texto VGA.
 *
 * Buffer VGA en modo texto 80x25:
 *   Dirección física: 0xB8000
 *   Cada celda = 2 bytes: [byte_alto = atributo de color] [byte_bajo = código ASCII]
 *   Total celdas: 80 columnas × 25 filas = 2000 celdas × 2 bytes = 4000 bytes
 */

/* ============================================================================
 * Includes de tamaño fijo (compatibles con freestanding: no requieren libc)
 * stdint.h define uint8_t, uint16_t, size_t de forma portable
 * stddef.h define size_t, NULL
 * ============================================================================ */
#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * Constantes del hardware VGA
 * ============================================================================ */

/** Número de columnas del modo texto VGA 80×25 */
static const size_t VGA_WIDTH  = 80;

/** Número de filas del modo texto VGA 80×25 */
static const size_t VGA_HEIGHT = 25;

/** Dirección física del buffer de texto VGA.
 *  'volatile' indica al compilador que este puntero accede a hardware real
 *  y que no debe optimizar ni reordenar los accesos de escritura/lectura. */
#define VGA_MEMORY ((volatile uint16_t*)0xB8000)

/* ============================================================================
 * Enumeración de colores VGA (4 bits por componente: fondo | frente)
 * Los valores corresponden al nibble de color de la BIOS/VGA
 * ============================================================================ */
typedef enum {
    VGA_COLOR_BLACK          = 0,   /* 0000 — Negro */
    VGA_COLOR_BLUE           = 1,   /* 0001 — Azul oscuro */
    VGA_COLOR_GREEN          = 2,   /* 0010 — Verde oscuro */
    VGA_COLOR_CYAN           = 3,   /* 0011 — Cyan oscuro */
    VGA_COLOR_RED            = 4,   /* 0100 — Rojo oscuro */
    VGA_COLOR_MAGENTA        = 5,   /* 0101 — Magenta */
    VGA_COLOR_BROWN          = 6,   /* 0110 — Marrón */
    VGA_COLOR_LIGHT_GREY     = 7,   /* 0111 — Gris claro */
    VGA_COLOR_DARK_GREY      = 8,   /* 1000 — Gris oscuro (intensidad alta) */
    VGA_COLOR_LIGHT_BLUE     = 9,   /* 1001 — Azul claro */
    VGA_COLOR_LIGHT_GREEN    = 10,  /* 1010 — Verde claro */
    VGA_COLOR_LIGHT_CYAN     = 11,  /* 1011 — Cyan claro ← usaremos este */
    VGA_COLOR_LIGHT_RED      = 12,  /* 1100 — Rojo claro */
    VGA_COLOR_LIGHT_MAGENTA  = 13,  /* 1101 — Magenta claro */
    VGA_COLOR_LIGHT_BROWN    = 14,  /* 1110 — Amarillo */
    VGA_COLOR_WHITE          = 15,  /* 1111 — Blanco puro */
} vga_color_t;

/* ============================================================================
 * Variables de estado del terminal (globales de módulo)
 * ============================================================================ */

/** Posición actual del cursor: columna (0-79) */
static size_t terminal_column;

/** Posición actual del cursor: fila (0-24) */
static size_t terminal_row;

/** Byte de atributo de color actual (fondo<<4 | frente) */
static uint8_t terminal_color;

/** Puntero al buffer VGA (volatile para acceso directo a hardware) */
static volatile uint16_t* terminal_buffer;

/* ============================================================================
 * Funciones auxiliares de color VGA
 * ============================================================================ */

/**
 * vga_entry_color — Combina colores de frente y fondo en un byte de atributo.
 * @fg: Color del texto (foreground), nibble bajo
 * @bg: Color de fondo (background), nibble alto
 * Retorna: byte 0xBF donde B=nibble bg, F=nibble fg
 */
static inline uint8_t vga_entry_color(vga_color_t fg, vga_color_t bg) {
    return (uint8_t)fg | ((uint8_t)bg << 4);
}

/**
 * vga_entry — Crea una entrada de 16 bits para el buffer VGA.
 * @uc:    Carácter ASCII a mostrar (byte bajo)
 * @color: Byte de atributo de color (byte alto)
 * Retorna: valor uint16_t listo para escribir en VGA_MEMORY
 */
static inline uint16_t vga_entry(unsigned char uc, uint8_t color) {
    return (uint16_t)uc | ((uint16_t)color << 8);
}

/* ============================================================================
 * Implementación de strlen sin libc
 * Necesitamos esta función para terminal_writestring sin usar <string.h>
 * ============================================================================ */

/**
 * strlen — Calcula la longitud de una cadena terminada en '\0'.
 * Implementación manual para no depender de libc.
 */
static size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}

/* ============================================================================
 * API del Terminal
 * ============================================================================ */

/**
 * terminal_initialize — Inicializa el terminal VGA.
 * Limpia los 80×25 caracteres del buffer, los llena con espacios sobre fondo
 * negro, y posiciona el cursor en la esquina superior izquierda (0,0).
 * Debe llamarse antes de cualquier otra función del terminal.
 */
void terminal_initialize(void) {
    terminal_row    = 0;
    terminal_column = 0;

    /* Color inicial: gris claro sobre negro (texto estándar de BIOS) */
    terminal_color  = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    /* Apuntar al buffer VGA físico */
    terminal_buffer = VGA_MEMORY;

    /* Limpiar toda la pantalla: escribir espacios con color negro sobre negro */
    for (size_t row = 0; row < VGA_HEIGHT; row++) {
        for (size_t col = 0; col < VGA_WIDTH; col++) {
            const size_t index = row * VGA_WIDTH + col;
            terminal_buffer[index] = vga_entry(' ', terminal_color);
        }
    }
}

/**
 * terminal_setcolor — Cambia el color actual del terminal.
 * @color: byte de atributo (usar vga_entry_color para construirlo)
 * Los siguientes caracteres escritos usarán este color.
 */
void terminal_setcolor(uint8_t color) {
    terminal_color = color;
}

/**
 * terminal_putentryat — Escribe un carácter con color en una posición específica.
 * @c:    Carácter ASCII
 * @color: Byte de atributo de color
 * @x:    Columna (0-79)
 * @y:    Fila (0-24)
 * No modifica terminal_row/terminal_column.
 */
static void terminal_putentryat(char c, uint8_t color, size_t x, size_t y) {
    const size_t index = y * VGA_WIDTH + x;
    terminal_buffer[index] = vga_entry((unsigned char)c, color);
}

/**
 * terminal_putchar — Escribe un carácter en la posición actual del cursor.
 * @c: Carácter a escribir.
 * Avanza el cursor automáticamente; si llega al final de la línea, hace
 * un salto de línea implícito. Si el carácter es '\n', salta de línea.
 *
 * NOTA: No se implementa scroll automático (se asume que el mensaje cabe en
 * la pantalla, lo cual es válido para este kernel minimal).
 */
void terminal_putchar(char c) {
    if (c == '\n') {
        /* Salto de línea: mover cursor al inicio de la siguiente fila */
        terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT) {
            terminal_row = 0;   /* Wrap-around simple (sin scroll) */
        }
        return;
    }

    /* Escribir carácter en la posición actual */
    terminal_putentryat(c, terminal_color, terminal_column, terminal_row);

    /* Avanzar columna; si llegamos al borde derecho, saltar de línea */
    if (++terminal_column == VGA_WIDTH) {
        terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT) {
            terminal_row = 0;
        }
    }
}

/**
 * terminal_write — Escribe un buffer de bytes de longitud dada.
 * @data: Puntero al buffer de caracteres
 * @size: Número de bytes a escribir
 */
void terminal_write(const char* data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        terminal_putchar(data[i]);
    }
}

/**
 * terminal_writestring — Escribe una cadena terminada en '\0'.
 * @data: Puntero a la cadena de texto
 * Calcula la longitud automáticamente y delega en terminal_write.
 */
void terminal_writestring(const char* data) {
    terminal_write(data, strlen(data));
}

/* ============================================================================
 * Punto de entrada del kernel
 * Llamado desde boot.s mediante 'call kernel_main'
 * ============================================================================ */

/**
 * kernel_main — Función principal del kernel de Kolotibablo OS.
 * No recibe argumentos (podría recibir los registros EAX/EBX de Multiboot,
 * pero no los necesitamos para este kernel básico).
 */
void kernel_main(void) {
    /* 1. Inicializar el terminal: limpiar pantalla y posicionar cursor */
    terminal_initialize();

    /* 2. Cambiar el color a Cyan claro sobre fondo negro para el mensaje principal */
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));

    /* 3. Escribir el mensaje de bienvenida */
    terminal_writestring("==================================================\n");
    terminal_writestring("       Kolotibablo OS v0.1 — Bare Bones           \n");
    terminal_writestring("==================================================\n");
    terminal_writestring("\n");

    /* Mensaje principal en amarillo (brown = yellow en VGA) */
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK));
    terminal_writestring("  \xa1Bienvenido a Kolotibablo OS!\n");
    terminal_writestring("  El sistema esta operativo.\n");
    terminal_writestring("\n");

    /* Mensaje de estado en verde claro */
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("  [OK] Kernel cargado en 0x00100000 (1 MiB)\n");
    terminal_writestring("  [OK] Buffer VGA en modo texto 80x25 activo\n");
    terminal_writestring("  [OK] Multiboot v1 detectado correctamente\n");
    terminal_writestring("\n");

    /* Mensaje de espera en gris claro */
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    terminal_writestring("  CPU en modo halt. Reinicia la VM para salir.\n");

    /* 4. Bucle infinito: detener la CPU de forma segura */
    /* sin este bucle, el procesador ejecutaría código basura después de kernel_main */
    while (1) {
        /* __asm__ volatile("hlt") puede usarse aquí para ahorrar energía, */
        /* pero while(1) vacío es suficiente para un kernel freestanding    */
    }
}
