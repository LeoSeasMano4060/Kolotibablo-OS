/*
 * linker.ld — Script de enlazado para Kolotibablo OS
 *
 * Este script controla cómo el linker (ld) combina los archivos objeto
 * (.o) y produce el ejecutable ELF final (kolotibablo.elf).
 *
 * Dirección de carga: 0x00100000 (1 MiB)
 * Razón: Los kernels Multiboot se cargan convencionalmente en 1 MiB.
 * GRUB garantiza que la memoria 0x00100000–0x00400000 está disponible
 * cuando se carga un kernel Multiboot (el BIOS reserva el primer 1 MiB).
 *
 * Formato de salida: ELF de 32 bits para Intel i386
 */

/* Definir el símbolo de punto de entrada del programa.
 * _start está definido en boot.s como símbolo global.
 * El linker necesita saber cuál es la primera instrucción a ejecutar. */
ENTRY(_start)

/* Secciones del binary ELF resultante */
SECTIONS
{
    /*
     * El punto (.) representa el "contador de posición actual" (location counter).
     * Lo establecemos en 1 MiB = 0x00100000 como dirección de carga base.
     * Toda la memoria del kernel se colocará a partir de esta dirección.
     */
    . = 0x00100000;

    /*
     * .text — Sección de código ejecutable
     *
     * Contiene:
     *   - El encabezado Multiboot (.multiboot de boot.s) — DEBE ir PRIMERO
     *     para que GRUB lo encuentre en los primeros 8 KiB del binario.
     *   - El código de arranque (_start de boot.s)
     *   - Las funciones del kernel (kernel.c compilado)
     *
     * ALIGN(4): alinear sección a múltiplos de 4 bytes (requisito Multiboot).
     */
    .text ALIGN(4) :
    {
        /* Primero: el encabezado Multiboot (sección .multiboot de boot.s) */
        *(.multiboot)

        /* Luego: todo el resto del código de todos los archivos objeto */
        *(.text)
    }

    /*
     * .rodata — Sección de datos de sólo lectura (read-only data)
     *
     * Contiene:
     *   - Cadenas literales (strings), tablas de constantes
     *   - Declaradas con 'const' en C o en secciones .rodata del ensamblador
     *
     * ALIGN(4096): alinear a página completa (4 KiB = 0x1000) para que
     * un futuro sistema de paginación pueda marcar esta región como read-only.
     */
    .rodata ALIGN(4096) :
    {
        *(.rodata)
    }

    /*
     * .data — Sección de datos inicializados
     *
     * Contiene variables globales/estáticas con valor inicial definido en el código.
     * Ejemplos: int x = 5; static char buf[] = "hola";
     *
     * ALIGN(4096): misma razón que .rodata (alineación de página).
     */
    .data ALIGN(4096) :
    {
        *(.data)
    }

    /*
     * .bss — Sección de datos sin inicializar (Block Started by Symbol)
     *
     * Contiene variables globales/estáticas sin valor inicial, o inicializadas a 0.
     * En el archivo ELF, NOBITS: NO ocupa espacio en disco, sólo en memoria.
     * El bootloader (o el kernel) debe poner a cero esta región antes de usarla.
     *
     * GRUB cumple con el estándar Multiboot y ya inicializa .bss a cero.
     *
     * Símbolo __bss_start: marcador del inicio de .bss (útil si el kernel
     * quisiera limpiar esta sección manualmente en boot.s).
     * Símbolo __bss_end / _end: marcador del final del kernel en memoria.
     */
    .bss ALIGN(4096) :
    {
        __bss_start = .;    /* Exportar símbolo de inicio de BSS */
        *(.bss)             /* Todas las secciones .bss de todos los .o */
        *(COMMON)           /* Variables "comunes" no inicializadas (FORTRAN/C) */
        __bss_end = .;      /* Exportar símbolo de fin de BSS */
    }

    /* _end: dirección final del kernel, después de todas las secciones.
     * Útil para un allocator de memoria simple que empiece después del kernel. */
    _end = .;
}
