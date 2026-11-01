; =============================================================================
; boot.s — Multiboot v1 Bootloader con soporte para Framebuffer Gráfico VBE HD
; =============================================================================

MB_MAGIC    equ 0x1BADB002
MB_FLAGS    equ 0x00000007
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)

section .multiboot
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM

    ; Estos campos deben seguir al checksum cuando el bit 2 está activado.
    dd 0                    ; Modo gráfico lineal
    dd 1280                 ; Ancho HD: 1280
    dd 720                  ; Alto HD: 720
    dd 32                   ; BPP: 32 (ARGB True Color)

section .bss
align 16
stack_bottom:
    resb 65536              ; Pila de 64 KiB
stack_top:

section .text
global _start

_start:
    ; Configurar puntero de pila (ESP)
    mov esp, stack_top

    ; Pasar argumentos a kernel_main(magic, multiboot_info)
    push ebx                ; Puntero a la estructura multiboot_info
    push eax                ; Magic (0x2BADB002)

    extern kernel_main
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang
