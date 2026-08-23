#include "sysutils.h"
#include "../system/string.h"
#include "assert.h"

assert_info_t g_last_assert_info = {0};

/* --- NÚMERO -> STRING --- */

void IntToStr(int value, char* dest) {
    int_to_string(value, dest);
}

void UIntToStr(uint32_t value, char* dest) {
    if (!dest) return;
    char temp[32];
    int i = 0;

    if (value == 0) {
        dest[0] = '0';
        dest[1] = '\0';
        return;
    }

    while (value > 0) {
        temp[i++] = (value % 10) + '0';
        value /= 10;
    }

    int j = 0;
    while (i > 0) {
        dest[j++] = temp[--i];
    }
    dest[j] = '\0';
}

void FloatToStr(float value, char* dest) {
    if (!dest) return;

    if (value < 0) {
        *dest++ = '-';
        value = -value;
    }

    int int_part = (int)value;
    float dec_part = value - (float)int_part;

    char temp[32];
    int_to_string(int_part, temp);
    strcpy(dest, temp);

    if (dec_part > 0.0001f) {
        strcat(dest, ".");
        int dec_int = (int)(dec_part * 10000.0f);
        char temp_dec[16];
        int_to_string(dec_int, temp_dec);

        int len_dec = strlen(temp_dec);
        while (len_dec < 4) {
            strcat(dest, "0");
            len_dec++;
        }
        strcat(dest, temp_dec);

        int len = strlen(dest);
        while (len > 0 && dest[len - 1] == '0') {
            dest[--len] = '\0';
        }
        if (len > 0 && dest[len - 1] == '.') {
            dest[len - 1] = '\0';
        }
    }
}

void IntToHex(uint32_t value, char* dest, int digits) {
    if (!dest) return;
    const char hex_chars[] = "0123456789ABCDEF";
    
    dest[0] = '0';
    dest[1] = 'x';
    
    if (digits < 1) digits = 1;
    if (digits > 8) digits = 8;

    for (int i = 0; i < digits; i++) {
        int shift = (digits - 1 - i) * 4;
        dest[2 + i] = hex_chars[(value >> shift) & 0xF];
    }
    dest[2 + digits] = '\0';
}

void BoolToStr(bool value, char* dest) {
    if (!dest) return;
    strcpy(dest, value ? "True" : "False");
}

/* --- STRING -> NÚMERO --- */

int StrToInt(const char* str) {
    return StrToIntDef(str, 0);
}

int StrToIntDef(const char* str, int defaultValue) {
    if (!str || *str == '\0') return defaultValue;

    int result = 0;
    bool negative = false;
    int i = 0;

    if (str[0] == '-') {
        negative = true;
        i++;
    }

    if (str[i] == '\0') return defaultValue;

    for (; str[i] != '\0'; i++) {
        if (str[i] >= '0' && str[i] <= '9') {
            result = (result * 10) + (str[i] - '0');
        } else {
            return defaultValue; // Retorna o valor padrão em caso de caractere inválido
        }
    }

    return negative ? -result : result;
}

float StrToFloat(const char* str) {
    return StrToFloatDef(str, 0.0f);
}

float StrToFloatDef(const char* str, float defaultValue) {
    if (!str || *str == '\0') return defaultValue;

    float result = 0.0f;
    float fraction = 0.1f;
    bool in_fraction = false;
    bool negative = false;
    int i = 0;

    if (str[0] == '-') {
        negative = true;
        i++;
    }

    if (str[i] == '\0') return defaultValue;

    for (; str[i] != '\0'; i++) {
        if (str[i] == '.' || str[i] == ',') {
            in_fraction = true;
        } else if (str[i] >= '0' && str[i] <= '9') {
            if (!in_fraction) {
                result = (result * 10.0f) + (str[i] - '0');
            } else {
                result += (str[i] - '0') * fraction;
                fraction *= 0.1f;
            }
        } else {
            return defaultValue; // Retorna padrão se encontrar caracteres inválidos
        }
    }

    return negative ? -result : result;
}

bool StrToBool(const char* str) {
    if (!str) return false;
    if (strcmp(str, "True") == 0 || strcmp(str, "true") == 0 || strcmp(str, "1") == 0) {
        return true;
    }
    return false;
}

/* --- CONCATENAÇÃO AUXILIAR --- */

void ConcatInt(char* dest, const char* prefixo, int valor, const char* sufixo) {
    char temp[32];
    IntToStr(valor, temp);
    strcpy(dest, prefixo);
    strcat(dest, temp);
    if (sufixo) strcat(dest, sufixo);
}

void ConcatFloat(char* dest, const char* prefixo, float valor, const char* sufixo) {
    char temp[32];
    FloatToStr(valor, temp);
    strcpy(dest, prefixo);
    strcat(dest, temp);
    if (sufixo) strcat(dest, sufixo);
}
