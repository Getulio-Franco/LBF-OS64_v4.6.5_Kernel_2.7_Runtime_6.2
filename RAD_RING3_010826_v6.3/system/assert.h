#ifndef ASSERT_H
#define ASSERT_H

#include "setjmp.h"

// Código de erro reservado para falhas de asserção
#define ERR_ASSERTION_FAILED 109

// Estrutura para armazenar os detalhes da última asserção que falhou
typedef struct {
    const char* file;
    int line;
    const char* function;
    const char* expression;
} assert_info_t;

extern assert_info_t g_last_assert_info;

// Macro assert personalizada para Ring 3
#define assert(condition) \
    do { \
        if (!(condition)) { \
            g_last_assert_info.file = __FILE__; \
            g_last_assert_info.line = __LINE__; \
            g_last_assert_info.function = __func__; \
            g_last_assert_info.expression = #condition; \
            THROW(ERR_ASSERTION_FAILED); \
        } \
    } while (0)

#endif

/*
Para implementar uma macro assert integrada ao seu sistema de TRY / EXCEPT, utilizamos as macros nativas do pré-processador C (__FILE__, __LINE__, __func__). Dessa forma, quando uma validação falha, em vez de derrubar a interface ou travar a execução, a macro captura as informações exatas de onde ocorreu o erro e dispara um THROW.

1. Definição do Código de Erro e da Macro (sysutils.h ou assert.h)Adicione ao seu cabeçalho a definição do código de erro e a macro de checagem:
*/
