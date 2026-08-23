#include "hda_codec.h"
#include "util/string.h"
#include "drivers/video.h"

// Buffer para formatar strings hexadecimais
static char debug_buffer[32];

// ============================================================================
// CONSTRUÇÃO DE VERBOS HDA (ESPECIFICAÇÃO INTEL HDA)
// ============================================================================
uint32_t hda_make_verb(uint32_t node, uint32_t verb, uint32_t payload) {
    uint32_t formatted_node = (node & 0xFF) << 20;

    // Verbo curto (4 bits) - Payload de 16 bits
    if (verb <= 0xF) {
        return formatted_node | ((verb & 0x0F) << 16) | (payload & 0xFFFF);
    }
    
    // Verbo longo (12 bits) - Payload de 8 bits
    return formatted_node | ((verb & 0xFFF) << 8) | (payload & 0xFF);
}

// ============================================================================
// AUXILIAR: BUSCAR CONEXÃO
// ============================================================================
static int hda_find_connection_index(hda_codec_t* ctx, uint8_t pin_node, uint8_t dac_node) {
    // 1. Descobrir tamanho da lista de conexões do PIN
    uint32_t conn_len_resp = hda_send_verb(ctx->ring, ctx->codec_addr, hda_make_verb(pin_node, 0xF00, HDA_PAR_CONN_LIST));
    if (conn_len_resp == 0xFFFFFFFF) return 0;

    uint8_t conn_count = conn_len_resp & 0x7F;
    if (conn_count == 0) return 0;

    // 2. Ler as conexões (buscando o DAC na lista do PIN)
    uint32_t list_resp = hda_send_verb(ctx->ring, ctx->codec_addr, hda_make_verb(pin_node, HDA_VERB_GET_CONNECT, 0x00));
    for (uint8_t i = 0; i < conn_count && i < 4; i++) {
        uint8_t target_node = (list_resp >> (i * 8)) & 0xFF;
        if (target_node == dac_node) {
            return i; // Retorna o índice correto do MUX para conectar PIN -> DAC
        }
    }
    return 0;
}

// ============================================================================
// INICIALIZAÇÃO E VARREDURA DO CODEC
// ============================================================================

bool hda_codec_init(hda_codec_t* codec_ctx, hda_corb_rirb_t* ring_ctx, uint8_t codec_addr) {
    if (!codec_ctx || !ring_ctx || !ring_ctx->is_initialized) return false;

    codec_ctx->ring = ring_ctx;
    codec_ctx->codec_addr = codec_addr;
    codec_ctx->is_initialized = false;

    // 1. Identificar Vendor ID (Node 0x00)
    uint32_t vendor = hda_send_verb(ring_ctx, codec_addr, hda_make_verb(0x00, 0xF00, HDA_PAR_VENDOR_ID));
    if (vendor == 0xFFFFFFFF || vendor == 0) {
        vga_print_string("[HDA_CODEC] ERRO: Codec nao respondeu no Node 0x00.", 0, 18);
        return false;
    }
    codec_ctx->vendor_id = vendor;

    // 2. Buscar Grupo de Funcao de Audio (AFG)
    uint32_t node_count = hda_send_verb(ring_ctx, codec_addr, hda_make_verb(0x00, 0xF00, HDA_PAR_NODE_COUNT));
    uint8_t start_node = (node_count >> 16) & 0xFF;
    uint8_t total_nodes = node_count & 0xFF;
    
    codec_ctx->afg_node = 0;
    for (uint8_t i = 0; i < total_nodes; i++) {
        uint8_t node_id = start_node + i;
        uint32_t fg_resp = hda_send_verb(ring_ctx, codec_addr, hda_make_verb(node_id, 0xF00, HDA_PAR_FG_TYPE));
        if ((fg_resp & 0xFF) == 0x01) { // 0x01 = Audio Function Group
            codec_ctx->afg_node = node_id;
            break;
        }
    }

    if (codec_ctx->afg_node == 0) {
        vga_print_string("[HDA_CODEC] ERRO: AFG nao encontrado.", 0, 18);
        return false;
    }

    // 3. Varredura dos Widgets dentro do AFG
    uint32_t afg_resp = hda_send_verb(ring_ctx, codec_addr, hda_make_verb(codec_ctx->afg_node, 0xF00, HDA_PAR_NODE_COUNT));
    uint8_t widget_start = (afg_resp >> 16) & 0xFF;
    uint8_t widget_count = afg_resp & 0xFF;

    codec_ctx->dac_node = 0;
    codec_ctx->pin_node = 0;

    for (uint8_t i = 0; i < widget_count; i++) {
        uint8_t widget_id = widget_start + i;
        uint32_t wcaps = hda_send_verb(ring_ctx, codec_addr, hda_make_verb(widget_id, 0xF00, HDA_PAR_WIDGET_CAP));
        
        if (wcaps == 0xFFFFFFFF) continue;
        
        uint8_t wtype = (wcaps >> 20) & 0xF;

        // Tipo 0x0: Audio Output (DAC)
        if (wtype == WIDGET_TYPE_AUDIO_OUTPUT && codec_ctx->dac_node == 0) {
            codec_ctx->dac_node = widget_id;
        } 
        // Tipo 0x4: Pin Complex (Saida de Audio/Speaker/Headphone)
        else if (wtype == WIDGET_TYPE_PIN_COMPLEX && codec_ctx->pin_node == 0) {
            codec_ctx->pin_node = widget_id;
        }
    }

    // Fallback: Caso o controlador nao categorize explicitamente como Pin 0x4,
    // atribui o no consecutivo ao DAC
    if (codec_ctx->dac_node != 0 && codec_ctx->pin_node == 0) {
        codec_ctx->pin_node = codec_ctx->dac_node + 1;
    }

    if (codec_ctx->dac_node == 0 || codec_ctx->pin_node == 0) {
        vga_print_string("[HDA_CODEC] ERRO: DAC ou PIN nao localizados.", 0, 18);
        return false;
    }

    // 4. Ativacao de Energia (Power State D0 = 0x00)
    hda_send_verb(ring_ctx, codec_addr, hda_make_verb(codec_ctx->afg_node, HDA_VERB_SET_POWER_STATE, 0x00));
    hda_send_verb(ring_ctx, codec_addr, hda_make_verb(codec_ctx->dac_node, HDA_VERB_SET_POWER_STATE, 0x00));
    hda_send_verb(ring_ctx, codec_addr, hda_make_verb(codec_ctx->pin_node, HDA_VERB_SET_POWER_STATE, 0x00));

    // 5. Configurar controle de PIN e Amplificador Externo (EAPD)
    hda_send_verb(ring_ctx, codec_addr, hda_make_verb(codec_ctx->pin_node, HDA_VERB_SET_PIN_CTL, 0x40));
    hda_send_verb(ring_ctx, codec_addr, hda_make_verb(codec_ctx->pin_node, HDA_VERB_SET_EAPD, 0x02));

    // 6. Configurar Roteamento e Amplificador de Saida
    int conn_index = hda_find_connection_index(codec_ctx, codec_ctx->pin_node, codec_ctx->dac_node);
    hda_send_verb(ring_ctx, codec_addr, hda_make_verb(codec_ctx->pin_node, HDA_VERB_SET_CONNECT, conn_index));

    uint32_t amp_payload = 0xB07F; // Desmuta e ajusta ganho ao maximo
    hda_send_verb(ring_ctx, codec_addr, hda_make_verb(codec_ctx->dac_node, HDA_VERB_SET_AMP_GAIN, amp_payload));
    hda_send_verb(ring_ctx, codec_addr, hda_make_verb(codec_ctx->pin_node, HDA_VERB_SET_AMP_GAIN, amp_payload));

    codec_ctx->is_initialized = true;
    return true;
}

bool hda_setup_dac_stream(hda_codec_t* codec, uint8_t stream_id, uint16_t format) {
    if (!codec || !codec->ring || codec->dac_node == 0) return false;

    // Verbo 0x2 (SET_STREAM_FORMAT): define o formato PCM (ex: 48kHz, 16-bit, estéreo)
    hda_send_verb(codec->ring, codec->codec_addr, hda_make_verb(codec->dac_node, HDA_VERB_SET_FORMAT, format));

    // Verbo 0x706 (SET_CONVERTER_STREAM_CHANNEL): define o Stream Tag nos bits [7:4]
    hda_send_verb(codec->ring, codec->codec_addr, hda_make_verb(codec->dac_node, HDA_VERB_SET_STREAM, (stream_id & 0x0F) << 4));

    return true;
}
