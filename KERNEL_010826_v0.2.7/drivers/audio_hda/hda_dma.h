#ifndef HDA_DMA_H
#define HDA_DMA_H

#include <stdint.h>
#include <stdbool.h>
#include "hda_codec.h"

// ============================================================================
// CONFIGURAÇÕES DO BUFFER DE ÁUDIO (32 KB / 8 PÁGINAS DE 4 KB)
// ============================================================================
#define HDA_BUFFER_PAGES   8        // 8 entradas na BDL (Total: 32 KB)
#define HDA_PAGE_SIZE      4096     // 4 KB por página
#define HDA_TOTAL_BUFFER   (HDA_BUFFER_PAGES * HDA_PAGE_SIZE)

// ============================================================================
// ESTRUTURA BDL (BUFFER DESCRIPTOR LIST - 16 BYTES POR ENTRADA)
// ============================================================================
typedef struct hda_bdl_entry {
    uint64_t phys_addr; // Endereço físico de cada página de 4 KB
    uint32_t length;    // Tamanho da página (4096 bytes)
    uint32_t flags;     // Bit 0 = IOC (Interrupt on Completion)
} __attribute__((packed)) hda_bdl_entry_t;

// ============================================================================
// CONTEXTO DO MOTOR DE STREAM DMA
// ============================================================================
typedef struct {
    hda_hardware_context_t* hw;
    hda_codec_t* codec;
    
    uint32_t stream_offset;
    uint8_t  stream_tag;
    
    hda_bdl_entry_t* bdl_table;
    uint64_t bdl_phys_addr;
    
    int16_t* pcm_virtual_buffers[HDA_BUFFER_PAGES]; // Vetor com os ponteiros virtuais das 8 páginas
    uint32_t pcm_buffer_size;

    bool is_running;
} hda_dma_stream_t;

// ============================================================================
// FUNÇÕES PÚBLICAS
// ============================================================================
bool hda_dma_init(hda_dma_stream_t* stream, hda_hardware_context_t* hw, hda_codec_t* codec);
void hda_generate_test_tone(hda_dma_stream_t* stream, uint32_t frequency_hz);
void hda_dma_start(hda_dma_stream_t* stream);
void hda_dma_stop(hda_dma_stream_t* stream);
void hda_dma_handle_irq(hda_dma_stream_t* stream);

#endif // HDA_DMA_H
