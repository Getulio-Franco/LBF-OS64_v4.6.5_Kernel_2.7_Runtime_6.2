#include "hda_dma.h"
#include "hda_pci.h"
#include "mem/pmm.h"
#include "util/string.h"
#include "drivers/video.h"
#include "drivers/audio/hda_codec.h"

// Registradores do Stream
#define SD_CTL          0x00
#define SD_STS          0x03
#define SD_LPIB         0x04
#define SD_CBP          0x08
#define SD_LBL          0x0C
#define SD_LVI          0x0E
#define SD_FMT          0x12
#define SD_BDLPL        0x18
#define SD_BDLPU        0x1C

#define SD_CTL_RUN      (1 << 1)
#define SD_CTL_SRST     (1 << 0)

#define HDA_FMT_48KHZ_16BIT_STEREO 0x0011

static inline void stream_write8(hda_dma_stream_t* s, uint32_t reg, uint8_t val) {
    *(volatile uint8_t*)(s->hw->mmio_virt + s->stream_offset + reg) = val;
}
static inline void stream_write16(hda_dma_stream_t* s, uint32_t reg, uint16_t val) {
    *(volatile uint16_t*)(s->hw->mmio_virt + s->stream_offset + reg) = val;
}
static inline void stream_write32(hda_dma_stream_t* s, uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)(s->hw->mmio_virt + s->stream_offset + reg) = val;
}
static inline uint8_t stream_read8(hda_dma_stream_t* s, uint32_t reg) {
    return *(volatile uint8_t*)(s->hw->mmio_virt + s->stream_offset + reg);
}
static inline uint16_t stream_read16(hda_dma_stream_t* s, uint32_t reg) {
    return *(volatile uint16_t*)(s->hw->mmio_virt + s->stream_offset + reg);
}
static inline uint32_t stream_read32(hda_dma_stream_t* s, uint32_t reg) {
    return *(volatile uint32_t*)(s->hw->mmio_virt + s->stream_offset + reg);
}

bool hda_dma_init(hda_dma_stream_t* stream, hda_hardware_context_t* hw, hda_codec_t* codec) {
    if (!stream || !hw || !codec || !codec->is_initialized) return false;

    memset(stream, 0, sizeof(hda_dma_stream_t));
    stream->hw = hw;
    stream->codec = codec;
    stream->stream_tag = 1;
    stream->stream_offset = 0x80 + (hw->num_input_streams * 0x20);
    stream->pcm_buffer_size = HDA_TOTAL_BUFFER; // 32768 bytes

    vga_print_string("[HDA_DMA] Alocando BDL e 8 paginas PCM (32KB)...", 0, 21);

    // 1. Alocar 1 página para a tabela BDL
    void* bdl_page = pmm_alloc_block();
    if (!bdl_page) {
        vga_print_string("[HDA_DMA] ERRO: Falha ao alocar pagina BDL.", 0, 21);
        return false;
    }
    memset(bdl_page, 0, 4096);
    stream->bdl_table = (hda_bdl_entry_t*)bdl_page;
    stream->bdl_phys_addr = (uint64_t)(uintptr_t)bdl_page;

    // 2. Alocar as 8 páginas de 4 KB individualmente e preencher o BDL dinamicamente
    for (int i = 0; i < HDA_BUFFER_PAGES; i++) {
        void* pcm_page = pmm_alloc_block();
        if (!pcm_page) {
            vga_print_string("[HDA_DMA] ERRO: Falha ao alocar pagina PCM.", 0, 21);
            return false;
        }
        memset(pcm_page, 0, HDA_PAGE_SIZE);
        
        stream->pcm_virtual_buffers[i] = (int16_t*)pcm_page;

        // Configura cada entrada na tabela BDL do controlador HDA
        stream->bdl_table[i].phys_addr = (uint64_t)(uintptr_t)pcm_page;
        stream->bdl_table[i].length = HDA_PAGE_SIZE; // 4096 bytes por entrada
        stream->bdl_table[i].flags = 0x00; // Sem IOC nesta etapa
    }

    // 3. Resetar e Parar o Stream DMA
    stream_write32(stream, SD_CTL, 0);
    while ((stream_read32(stream, SD_CTL) & SD_CTL_RUN) != 0);

    stream_write32(stream, SD_CTL, SD_CTL_SRST);
    while ((stream_read32(stream, SD_CTL) & SD_CTL_SRST) == 0);
    stream_write32(stream, SD_CTL, 0);
    while ((stream_read32(stream, SD_CTL) & SD_CTL_SRST) != 0);

    // 4. Configurar Registradores DMA
    stream_write16(stream, SD_FMT, HDA_FMT_48KHZ_16BIT_STEREO);
    stream_write32(stream, SD_BDLPL, (uint32_t)(stream->bdl_phys_addr & 0xFFFFFFFF));
    stream_write32(stream, SD_BDLPU, (uint32_t)(stream->bdl_phys_addr >> 32));
    stream_write32(stream, SD_LBL, stream->pcm_buffer_size); // 32768 total bytes
    stream_write16(stream, SD_LVI, HDA_BUFFER_PAGES - 1);    // Índice 7 (última das 8 entradas)

    uint32_t ctl = stream_read32(stream, SD_CTL);
    ctl = (ctl & 0xFF0FFFFF) | ((uint32_t)stream->stream_tag << 20);
    stream_write32(stream, SD_CTL, ctl);

    // 5. Associar o DAC ao Stream Tag
    hda_setup_dac_stream(codec, stream->stream_tag, HDA_FMT_48KHZ_16BIT_STEREO);

    vga_print_string("[HDA_DMA] Motor DMA configurado via BDL com sucesso!", 0, 21);
    return true;
}

void hda_generate_test_tone(hda_dma_stream_t* stream, uint32_t frequency_hz) {
    if (!stream) return;

    uint32_t sample_rate = 48000;
    uint32_t half_period = sample_rate / (frequency_hz * 2);
    int16_t amplitude = 8000;

    uint32_t samples_per_page = HDA_PAGE_SIZE / sizeof(int16_t); // 2048 amostras por página

    // Preenche cada uma das 8 páginas encadeadas na BDL
    for (int p = 0; p < HDA_BUFFER_PAGES; p++) {
        int16_t* page_buffer = stream->pcm_virtual_buffers[p];
        if (!page_buffer) continue;

        for (uint32_t i = 0; i < samples_per_page; i += 2) {
            uint32_t global_sample_index = (p * samples_per_page) + i;
            int16_t sample_val = ((global_sample_index / 2 / half_period) % 2 == 0) ? amplitude : -amplitude;
            
            page_buffer[i]     = sample_val; // Esquerdo
            page_buffer[i + 1] = sample_val; // Direito
        }
    }
}

void hda_dma_start(hda_dma_stream_t* stream) {
    if (!stream) return;
    uint32_t ctl = stream_read32(stream, SD_CTL);
    stream_write32(stream, SD_CTL, ctl | SD_CTL_RUN);
    stream->is_running = true;
}

void hda_dma_stop(hda_dma_stream_t* stream) {
    if (!stream) return;
    uint32_t ctl = stream_read32(stream, SD_CTL);
    stream_write32(stream, SD_CTL, ctl & ~SD_CTL_RUN);
    stream->is_running = false;
}

void hda_dma_handle_irq(hda_dma_stream_t* stream) {
    if (!stream) return;
    uint8_t status = stream_read8(stream, SD_STS);
    if (status & (1 << 2)) {
        stream_write8(stream, SD_STS, status | (1 << 2));
    }
}
