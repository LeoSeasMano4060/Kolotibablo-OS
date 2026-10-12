section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM
    
    ; Campos de modo de video de la especificación Multiboot v1
    dd 0, 0, 0, 0, 0                ; Offsets de aout kludge (no usados en ELF)
    dd 0                            ; Modo 0 = Modo gráfico lineal (no texto)
    dd 1024                         ; Ancho preferido: 1024 píxeles
    dd 768                          ; Alto preferido: 768 píxeles
    dd 32                           ; Profundidad de color: 32 bits (ARGB/RGBA)
section .bss
align 16
stack_bottom:
    resb 32768                      ; Pila de 32 KiB
stack_top:
section .text
global _start
_start:
    ; Configurar el puntero de pila (ESP)
    mov esp, stack_top
    ; En Multiboot v1:
    ; EAX = 0x2BADB002 (número mágico de validación)
    ; EBX = Puntero a la estructura multiboot_info_t
    ; Pasamos ambos registros como argumentos de cdecl a kernel_main(magic, mboot_ptr)
    push ebx
    push eax
    extern kernel_main
    call kernel_main
.hang:
    cli
    hlt
    jmp .hang
