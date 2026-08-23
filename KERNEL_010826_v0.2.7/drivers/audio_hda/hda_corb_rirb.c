#include "hda_corb_rirb.h"
#include "drivers/video.h"
#include "util/string.h"
#include "mem/pmm.h"

// ============================================================================
// REGISTRADORES HDA BAR0 - OFFSET CORB/RIRB
// ============================================================================
#define HDA_REG_CORBLBASE   0x40
#define HDA_REG_CORBUBASE   0x44
#define HDA_REG_CORBWP      0x48
#define HDA_REG_CORBRP      0x4A
#define HDA_REG_CORBCTL     0x4C
#define HDA_REG_CORBST      0x4D
#define HDA_REG_CORBSIZE    0x4E

#define HDA_REG_RIRBLBASE   0x50
#define HDA_REG_RIRBUBASE   0x54
#define HDA_REG_RIRBWP      0x58
#define HDA_REG_RINTCNT     0x5A
#define HDA_REG_RIRBCTL     0x5C
#define HDA_REG_RIRBST      0x5D
#define HDA_REG_RIRBSIZE    0x5E

#define CORB_RUN_BIT        0x02
#define RIRB_RUN_BIT        0x02
#define RIRB_INT_CTL        0x01

#define HDA_COMMAND_TIMEOUT 100000

// ============================================================================
// AUXILIARES DE LEITURA/ESCRITA MMIO LOCAIS
// ============================================================================
static inline void hda_write8(hda_corb_rirb_t* ctx, uint32_t reg, uint8_t val) {
    *(volatile uint8_t*)(ctx->hw->mmio_virt + reg) = val;
}
static inline uint8_t hda_read8(hda_corb_rirb_t* ctx, uint32_t reg) {
    return *(volatile uint8_t*)(ctx->hw->mmio_virt + reg);
}
static inline void hda_write16(hda_corb_rirb_t* ctx, uint32_t reg, uint16_t val) {
    *(volatile uint16_t*)(ctx->hw->mmio_virt + reg) = val;
}
static inline uint16_t hda_read16(hda_corb_rirb_t* ctx, uint32_t reg) {
    return *(volatile uint16_t*)(ctx->hw->mmio_virt + reg);
}
static inline void hda_write32(hda_corb_rirb_t* ctx, uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)(ctx->hw->mmio_virt + reg) = val;
}

// ============================================================================
// INICIALIZACAO CORB/RIRB
// ============================================================================

bool hda_corb_rirb_init(hda_corb_rirb_t* ring_ctx, hda_hardware_context_t* hw_ctx) {
    if (!ring_ctx || !hw_ctx || !hw_ctx->is_ready) return false;
    
    memset(ring_ctx, 0, sizeof(hda_corb_rirb_t));
    ring_ctx->hw = hw_ctx;

    vga_print_string("[HDA_RING] Alocando memoria para CORB e RIRB...\n", 0, 38);

    // 1. Alocar bloco PMM e configurar Identity Mapping
    void* block = pmm_alloc_block(); 
    if (!block) {
        vga_print_string("[HDA_RING] Erro: Falha de alocacao PMM.\n", 0, 38);
        return false;
    }

    uintptr_t virt_addr = (uintptr_t)block;
    uint64_t phys_addr = (uint64_t)virt_addr; // Mapeamento 1:1
    memset((void*)virt_addr, 0, 4096);

    // Configurar CORB (0 a 1024 bytes) e RIRB (2048 a 4096 bytes)
    ring_ctx->corb_buffer = (uint32_t*)virt_addr;
    ring_ctx->corb_phys_addr = phys_addr;
    ring_ctx->corb_size = 256;

    ring_ctx->rirb_buffer = (hda_rirb_entry_t*)(virt_addr + 2048);
    ring_ctx->rirb_phys_addr = phys_addr + 2048;
    ring_ctx->rirb_size = 256;

    // 2. Parar motores DMA do CORB e RIRB
    hda_write8(ring_ctx, HDA_REG_CORBCTL, 0x00);
    hda_write8(ring_ctx, HDA_REG_RIRBCTL, 0x00);

    uint32_t timeout = HDA_COMMAND_TIMEOUT;
    while ((hda_read8(ring_ctx, HDA_REG_CORBCTL) & CORB_RUN_BIT) || 
           (hda_read8(ring_ctx, HDA_REG_RIRBCTL) & RIRB_RUN_BIT)) {
        if (--timeout == 0) return false;
    }

    // 3. Programar base de enderecos do CORB
    hda_write32(ring_ctx, HDA_REG_CORBLBASE, (uint32_t)(ring_ctx->corb_phys_addr & 0xFFFFFFFF));
    hda_write32(ring_ctx, HDA_REG_CORBUBASE, (uint32_t)(ring_ctx->corb_phys_addr >> 32));
    hda_write8(ring_ctx, HDA_REG_CORBSIZE, 0x02); // 256 entradas

    // Reset dos ponteiros do CORB
    hda_write16(ring_ctx, HDA_REG_CORBRP, 0x8000); 
    while ((hda_read16(ring_ctx, HDA_REG_CORBRP) & 0x8000) == 0); 
    hda_write16(ring_ctx, HDA_REG_CORBRP, 0x0000);
    hda_write16(ring_ctx, HDA_REG_CORBWP, 0x0000);

    // 4. Programar base de enderecos do RIRB
    hda_write32(ring_ctx, HDA_REG_RIRBLBASE, (uint32_t)(ring_ctx->rirb_phys_addr & 0xFFFFFFFF));
    hda_write32(ring_ctx, HDA_REG_RIRBUBASE, (uint32_t)(ring_ctx->rirb_phys_addr >> 32));
    hda_write8(ring_ctx, HDA_REG_RIRBSIZE, 0x02); // 256 entradas

    // Reset do ponteiro de escrita do RIRB
    hda_write16(ring_ctx, HDA_REG_RIRBWP, 0x8000);

    ring_ctx->corb_write_pos = 0;
    ring_ctx->rirb_read_pos = 0;

    // 5. Ativar os motores DMA
    hda_write8(ring_ctx, HDA_REG_CORBCTL, CORB_RUN_BIT);
    hda_write8(ring_ctx, HDA_REG_RIRBCTL, RIRB_RUN_BIT | RIRB_INT_CTL);

    ring_ctx->is_initialized = true;
    vga_print_string("[HDA_RING] CORB e RIRB inicializados e rodando!\n", 0, 38);
    return true;
}

// ============================================================================
// ENVIO E RESPOSTA
// ============================================================================

uint32_t hda_send_verb(hda_corb_rirb_t* ring_ctx, uint8_t codec_addr, uint32_t verb_data) {
    if (!ring_ctx || !ring_ctx->is_initialized) return 0xFFFFFFFF;

    // Montar comando: Codec ID no topo (bits 31:28) + Payload do verbo
    uint32_t corb_entry = ((uint32_t)(codec_addr & 0x0F) << 28) | (verb_data & 0x0FFFFFFF);

    // Escrever no proximo espaco livre do CORB (buffer circular)
    ring_ctx->corb_write_pos = (ring_ctx->corb_write_pos + 1) % ring_ctx->corb_size;
    ring_ctx->corb_buffer[ring_ctx->corb_write_pos] = corb_entry;

    // Notificar o controlador atualizando o Write Pointer
    hda_write16(ring_ctx, HDA_REG_CORBWP, ring_ctx->corb_write_pos);

    // Aguardar o hardware escrever a resposta no RIRB
    uint32_t timeout = HDA_COMMAND_TIMEOUT;
    uint16_t rirb_wp = hda_read16(ring_ctx, HDA_REG_RIRBWP);

    while (ring_ctx->rirb_read_pos == rirb_wp) {
        if (--timeout == 0) {
            vga_print_string("[HDA_RING] Timeout de hardware aguardando resposta!\n", 0, 38);
            return 0xFFFFFFFF;
        }
        rirb_wp = hda_read16(ring_ctx, HDA_REG_RIRBWP);
    }

    // Ler a resposta e avancar nosso ponteiro de leitura
    ring_ctx->rirb_read_pos = (ring_ctx->rirb_read_pos + 1) % ring_ctx->rirb_size;
    hda_rirb_entry_t rirb_entry = ring_ctx->rirb_buffer[ring_ctx->rirb_read_pos];

    // Limpar flag de status de mudanca
    hda_write8(ring_ctx, HDA_REG_RIRBST, 0x01);

    return rirb_entry.response;
}
