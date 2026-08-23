global setjmp
global longjmp

section .text

setjmp:
    mov [rdi + 0],  rbx
    mov [rdi + 8],  rsp
    mov [rdi + 16], rbp
    mov [rdi + 24], r12
    mov [rdi + 32], r13
    mov [rdi + 40], r14
    mov [rdi + 48], r15
    
    mov rax, [rsp]
    mov [rdi + 56], rax

    xor rax, rax
    ret

longjmp:
    mov rax, rsi
    test rax, rax
    jnz .skip_zero
    mov rax, 1

.skip_zero:
    mov rbx, [rdi + 0]
    mov rsp, [rdi + 8]
    mov rbp, [rdi + 16]
    mov r12, [rdi + 24]
    mov r13, [rdi + 32]
    mov r14, [rdi + 40]
    mov r15, [rdi + 48]
    
    mov rdx, [rdi + 56]
    mov [rsp], rdx
    
    ret

; Desativa explicitamente a permissão de execução na pilha para este objeto
section .note.GNU-stack noalloc noexec nowrite progbits
