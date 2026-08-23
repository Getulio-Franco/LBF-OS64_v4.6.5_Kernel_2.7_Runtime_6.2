#ifndef SETJMP_H
#define SETJMP_H

#include <stdint.h>

// Estrutura do contexto x86_64
typedef struct {
    uint64_t rbx;
    uint64_t rsp;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rip;
} jmp_buf[1];

// Declarações enviadas para o Assembly
int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

// Variáveis globais de controle do Try/Except
extern jmp_buf __exception_env;
extern int __last_exception_code;

// Macros para a sintaxe amigável
#define TRY \
    __last_exception_code = setjmp(__exception_env); \
    if (__last_exception_code == 0)

#define EXCEPT \
    else

#define THROW(code) \
    longjmp(__exception_env, (code) != 0 ? (code) : 1)

#define GET_EXCEPTION() (__last_exception_code)

#endif
