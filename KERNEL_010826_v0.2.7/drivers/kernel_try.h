// kernel_try.h
#ifndef KERNEL_TRY_H
#define KERNEL_TRY_H

#include <stdint.h>
#include "idt.h"

typedef struct {
    uint64_t rsp;
    uint64_t rbp;
    uint64_t rip;
    uint64_t rbx;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint32_t active;      // 1 se estiver dentro de um K_TRY
    uint32_t last_error;  // Código da exceção IDT (ex: 14 para #PF)
} k_exception_frame_t;

// Contexto global do Kernel para exceções de Ring 0
extern k_exception_frame_t g_k_exception_env;

// Salva o contexto atual da CPU
int k_setjmp(k_exception_frame_t* env);

#define K_TRY \
    if (k_setjmp(&g_k_exception_env) == 0) { \
        g_k_exception_env.active = 1;

#define K_EXCEPT \
        g_k_exception_env.active = 0; \
    } else { \
        g_k_exception_env.active = 0;

#define K_END_TRY }

#endif
