; =============================================================================
; boot.s — Punto de entrada Multiboot para Kolotibablo OS
; Ensamblador: NASM (sintaxis Intel)
; Especificación: Multiboot v1 (compatible con GRUB Legacy y GRUB2 en modo BIOS)
; =============================================================================

; -----------------------------------------------------------------------------
; Sección Multiboot Header
; GRUB busca esta firma en los primeros 8 KiB del archivo ELF binario.
; Los tres campos deben cumplir: MAGIC + FLAGS + CHECKSUM == 0 (mod 2^32)
; -----------------------------------------------------------------------------

MBALIGN  equ  1 << 0        ; Bit 0: alinear módulos cargados en límites de página (4 KiB)
MEMINFO  equ  1 << 1        ; Bit 1: solicitar mapa de memoria al bootloader
FLAGS    equ  MBALIGN | MEMINFO  ; Combinación de flags requeridos

MAGIC    equ  0x1BADB002    ; Número mágico que identifica el encabezado Multiboot v1
CHECKSUM equ -(MAGIC + FLAGS)    ; Checksum: los tres campos deben sumar 0 en 32 bits

; -----------------------------------------------------------------------------
; Sección .multiboot — debe estar en los primeros 8 KiB del binary
; PROGBITS indica que contiene datos reales (no es BSS)
; alloc,exec indica que se carga en memoria y es ejecutable
; -----------------------------------------------------------------------------
section .multiboot
align 4                     ; El encabezado Multiboot debe estar alineado a 4 bytes
    dd MAGIC                ; Campo 1: número mágico (4 bytes)
    dd FLAGS                ; Campo 2: flags de características requeridas (4 bytes)
    dd CHECKSUM             ; Campo 3: complemento para que la suma sea 0 (4 bytes)

; -----------------------------------------------------------------------------
; Sección .bss — espacio reservado para la pila (stack)
; La pila crece hacia abajo en x86, por eso definimos el tope al final.
; NOBITS: ocupa espacio en memoria pero NO en el archivo .elf
; -----------------------------------------------------------------------------
section .bss
align 16                    ; La pila debe estar alineada a 16 bytes (convención SysV ABI)
stack_bottom:               ; Dirección base de la pila (punto más bajo)
    resb 16384              ; Reservar 16 KiB (16 * 1024 bytes) para la pila
stack_top:                  ; Dirección tope de la pila (donde arranca el puntero ESP)

; -----------------------------------------------------------------------------
; Sección .text — código ejecutable del bootloader
; _start: símbolo de punto de entrada definido en linker.ld
; global _start: exporta el símbolo para que el linker pueda encontrarlo
; -----------------------------------------------------------------------------
section .text
global _start               ; Exportar _start como símbolo global (requerido por el linker)

_start:
    ; -------------------------------------------------------------------------
    ; Al llegar aquí, GRUB ya ha:
    ;   - Cargado el kernel en memoria (dirección definida en linker.ld, 1 MiB)
    ;   - Puesto EAX = 0x2BADB002 (magic de confirmación Multiboot)
    ;   - Puesto EBX = dirección de la estructura multiboot_info_t
    ;   - Desactivado interrupciones (IF=0 en EFLAGS)
    ;   - Puesto la CPU en modo protegido de 32 bits (PM, sin paginación)
    ; -------------------------------------------------------------------------

    ; Configurar el puntero de pila (ESP) al tope de nuestra pila reservada
    ; Usamos 'mov' porque ESP debe ser un valor absoluto, no relativo
    mov esp, stack_top       ; ESP apunta al tope de los 16 KiB reservados

    ; -------------------------------------------------------------------------
    ; Llamar al kernel principal escrito en C
    ; kernel_main() está definido en kernel.c y tiene prototipo: void kernel_main(void)
    ; El convenio de llamada cdecl (usado por GCC para x86) espera:
    ;   - Argumentos en la pila (ninguno en este caso)
    ;   - Valor de retorno en EAX (ignoramos el retorno ya que es void)
    ; -------------------------------------------------------------------------
    extern kernel_main       ; Declarar kernel_main como símbolo externo (de kernel.c)
    call kernel_main         ; Saltar al kernel C

    ; -------------------------------------------------------------------------
    ; Si kernel_main retorna (no debería), detenemos la CPU de forma segura:
    ;   cli  — deshabilita interrupciones (Clear Interrupt Flag)
    ;   hlt  — detiene la CPU hasta la próxima interrupción (no habrá ninguna)
    ;   jmp  — por si hlt se interrumpe de algún modo, volvemos a hlt
    ; -------------------------------------------------------------------------
.hang:
    cli                      ; Deshabilitar interrupciones enmascarables
    hlt                      ; Detener la CPU (halt)
    jmp .hang                ; Bucle infinito de seguridad
