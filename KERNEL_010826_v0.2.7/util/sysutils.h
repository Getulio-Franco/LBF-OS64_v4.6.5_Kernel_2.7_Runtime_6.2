#ifndef SYSUTILS_H
#define SYSUTILS_H

#include <stdbool.h>
#include <stdint.h>
#include "setjmp.h" // tratamento em tempo de execução try-except

// Declarações 'extern' para evitar múltiplas definições no Linker
extern jmp_buf __exception_env;
extern int __last_exception_code;

/* ============================================================================
 * CONVERSÕES: NÚMERO -> STRING
 * ============================================================================ */
void IntToStr(int value, char* dest);
void UIntToStr(uint32_t value, char* dest);
void FloatToStr(float value, char* dest);
void IntToHex(uint32_t value, char* dest, int digits);
void BoolToStr(bool value, char* dest);

/* ============================================================================
 * CONVERSÕES: STRING -> NÚMERO
 * ============================================================================ */
int StrToInt(const char* str);
int StrToIntDef(const char* str, int defaultValue);

float StrToFloat(const char* str);
float StrToFloatDef(const char* str, float defaultValue);

bool StrToBool(const char* str);

/* ============================================================================
 * CONCATENAÇÃO AUXILIAR DE STRING (ESTILO DELPHI)
 * ============================================================================ */
void ConcatInt(char* dest, const char* prefixo, int valor, const char* sufixo);
void ConcatFloat(char* dest, const char* prefixo, float valor, const char* sufixo);

#endif // SYSUTILS_H
