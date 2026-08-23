#ifndef HDA_CODEC_H
#define HDA_CODEC_H

#include <stdint.h>
#include <stdbool.h>
#include "hda_corb_rirb.h" // Importa a camada de transporte

// ============================================================================
// PARÂMETROS DO CODEC (GET PARAMETER - VERBO 0xF00)
// ============================================================================
#define HDA_PAR_VENDOR_ID        0x00
#define HDA_PAR_REVISION_ID      0x02
#define HDA_PAR_NODE_COUNT       0x04
#define HDA_PAR_FG_TYPE          0x05
#define HDA_PAR_WIDGET_CAP       0x09
#define HDA_PAR_PIN_CAP          0x0C
#define HDA_PAR_AMP_CAP          0x0D
#define HDA_PAR_CONN_LIST        0x0E
#define HDA_PAR_POWER_STATE      0x0F

// ============================================================================
// VERBOS DO CODEC (ESPECIFICAÇÃO INTEL HDA)
// ============================================================================
// Verbos curtos (4 bits)
#define HDA_VERB_SET_FORMAT      0x2
#define HDA_VERB_SET_AMP_GAIN    0x3

// Verbos longos (12 bits)
#define HDA_VERB_SET_CONNECT     0x701
#define HDA_VERB_SET_POWER_STATE 0x705
#define HDA_VERB_SET_STREAM      0x706
#define HDA_VERB_SET_PIN_CTL     0x707
#define HDA_VERB_SET_EAPD        0x70C
#define HDA_VERB_GET_CONNECT     0xF02

// ============================================================================
// TIPOS DE WIDGETS
// ============================================================================
#define WIDGET_TYPE_AUDIO_OUTPUT 0x0
#define WIDGET_TYPE_AUDIO_INPUT  0x1
#define WIDGET_TYPE_AUDIO_MIXER  0x2
#define WIDGET_TYPE_PIN_COMPLEX  0x4

// ============================================================================
// CONTEXTO DO CODEC
// ============================================================================
typedef struct {
    hda_corb_rirb_t* ring; // Referência à camada CORB/RIRB inferior
    
    uint8_t  codec_addr;
    uint32_t vendor_id;
    
    // Árvore de Nós (Nodes)
    uint8_t  afg_node; // Audio Function Group
    uint8_t  dac_node; // Conversor Digital-Analógico (Saída)
    uint8_t  pin_node; // Pino físico (Jack/Speaker)
    
    bool     is_initialized;
} hda_codec_t;

// ============================================================================
// FUNÇÕES PÚBLICAS
// ============================================================================

/**
 * Formata um comando HDA de 32 bits.
 */
uint32_t hda_make_verb(uint32_t node, uint32_t verb, uint32_t payload);

/**
 * Varre os nós do codec, encontra o DAC/PIN, liga a energia e configura o roteamento.
 */
bool hda_codec_init(hda_codec_t* codec_ctx, hda_corb_rirb_t* ring_ctx, uint8_t codec_addr);

/**
 * Configura o formato PCM e o Stream Tag no nó do DAC.
 */
bool hda_setup_dac_stream(hda_codec_t* codec, uint8_t stream_id, uint16_t format);

#endif // HDA_CODEC_H
