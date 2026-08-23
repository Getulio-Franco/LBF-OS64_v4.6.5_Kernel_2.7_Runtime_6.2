#include "hda_pci.h"
#include "drivers/proc.h"
#include "util/string.h"
#include "drivers/video.h"

#define TIMEOUT_LOOPS 1000

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

// ============================================================================
// FUNÇÕES DE I/O ASSEMBLY E ACESSO PCI NATIVO
// ============================================================================

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

uint32_t pci_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_read_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t dword = pci_read_dword(bus, slot, func, offset);
    return (uint16_t)((dword >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t pci_read_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t dword = pci_read_dword(bus, slot, func, offset);
    return (uint8_t)((dword >> ((offset & 3) * 8)) & 0xFF);
}

void pci_write_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value) {
    uint32_t address = (1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    
    uint32_t dword = inl(PCI_CONFIG_DATA);
    int shift = (offset & 2) * 8;
    dword &= ~(0xFFFF << shift);
    dword |= ((uint32_t)value << shift);
    
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, dword);
}

// ============================================================================
// FUNÇÕES AUXILIARES MMIO
// ============================================================================

static inline uint16_t mmio_read16(uintptr_t base, uint32_t offset) {
    return *(volatile uint16_t*)(base + offset);
}

static inline void mmio_write16(uintptr_t base, uint32_t offset, uint16_t value) {
    *(volatile uint16_t*)(base + offset) = value;
}

static inline uint32_t mmio_read32(uintptr_t base, uint32_t offset) {
    return *(volatile uint32_t*)(base + offset);
}

static inline void mmio_write32(uintptr_t base, uint32_t offset, uint32_t value) {
    *(volatile uint32_t*)(base + offset) = value;
}

// ============================================================================
// FLUXO PRINCIPAL DO HDA_PCI
// ============================================================================

bool hda_pci_init(hda_hardware_context_t* context) {
    if (!context) return false;
    memset(context, 0, sizeof(hda_hardware_context_t));

    vga_print_string("[HDA_PCI] Iniciando busca pelo controlador Intel HDA...\n", 0, 38);

    // 1. Procurar o controlador (Class 0x04, Subclass 0x03)
    bool found = false;
    for (uint16_t bus = 0; bus < 256 && !found; bus++) {
        for (uint8_t slot = 0; slot < 32 && !found; slot++) {
            for (uint8_t func = 0; func < 8 && !found; func++) {
                if (pci_read_word((uint8_t)bus, slot, func, PCI_VENDOR_ID) == 0xFFFF) continue;
                
                if (pci_read_byte((uint8_t)bus, slot, func, PCI_BASE_CLASS) == 0x04 && 
                    pci_read_byte((uint8_t)bus, slot, func, PCI_SUBCLASS) == 0x03) {
                    context->bus = (uint8_t)bus;
                    context->slot = slot;
                    context->function = func;
                    found = true;
                }
            }
        }
    }

    if (!found) {
        vga_print_string("[HDA_PCI] Controlador HDA nao encontrado. Abortando audio.\n", 0, 38);
        return false;
    }

    // 2. Habilitar Memory Space e Bus Master
    uint16_t cmd = pci_read_word(context->bus, context->slot, context->function, PCI_COMMAND);
    cmd |= PCI_CMD_MEMORY_SPACE | PCI_CMD_BUS_MASTER;
    cmd |= PCI_CMD_INT_DISABLE;
    pci_write_word(context->bus, context->slot, context->function, PCI_COMMAND, cmd);

    // 3. Ler e Validar BAR0
    uint32_t bar0_low = pci_read_dword(context->bus, context->slot, context->function, PCI_BAR0);
    uint32_t bar0_high = pci_read_dword(context->bus, context->slot, context->function, PCI_BAR0 + 4);
    
    bool is_64bit_bar = ((bar0_low & 0x06) == 0x04);
    context->mmio_phys = bar0_low & ~0x0F;
    if (is_64bit_bar) {
        context->mmio_phys |= ((uint64_t)bar0_high << 32);
    }

    if (context->mmio_phys == 0 || context->mmio_phys == 0xFFFFFFFF) {
        vga_print_string("[HDA_PCI] BAR0 invalida. Abortando.\n", 0, 38);
        return false;
    }

    context->mmio_virt = (uintptr_t)context->mmio_phys;

    // 4. Executar Global Reset (GCTL.CRST)
    vga_print_string("[HDA_PCI] Executando Global Reset...\n", 0, 38);
        
    uint32_t gctl = mmio_read32(context->mmio_virt, HDA_REG_GCTL);
    mmio_write32(context->mmio_virt, HDA_REG_GCTL, gctl & ~HDA_GCTL_CRST);
    
    int timeout = TIMEOUT_LOOPS;
    while ((mmio_read32(context->mmio_virt, HDA_REG_GCTL) & HDA_GCTL_CRST) && --timeout > 0) {
        sys_sleep(10);
    }
    
    if (timeout <= 0) {
        vga_print_string("[HDA_PCI] Timeout ao entrar em reset. Hardware travado.\n", 0, 38);
        return false;
    }

    sys_sleep(5);

    mmio_write32(context->mmio_virt, HDA_REG_GCTL, mmio_read32(context->mmio_virt, HDA_REG_GCTL) | HDA_GCTL_CRST);

    timeout = TIMEOUT_LOOPS;
    while (!(mmio_read32(context->mmio_virt, HDA_REG_GCTL) & HDA_GCTL_CRST) && --timeout > 0) {
        sys_sleep(10);
    }

    if (timeout <= 0) {
        vga_print_string("[HDA_PCI] Timeout ao sair do reset. Controlador morto.\n", 0, 38);
        return false;
    }

    // Aguardar estabilização do barramento SDIN para resposta dos Codecs (mínimo 1ms)
    sys_sleep(1000);

    // Capturar a máscara de codecs detectados (STATESTS - 0x0E)
    uint16_t statests = mmio_read16(context->mmio_virt, HDA_REG_STATESTS);
    context->codec_mask = statests & 0x7FFF; // Bits 0 a 14

    // Limpar o registrador STATESTS escrevendo os próprios bits lidos de volta (Write 1 to Clear)
    mmio_write16(context->mmio_virt, HDA_REG_STATESTS, statests);

    // 5. Ler parâmetros básicos do hardware (GCAP)
    uint32_t gcap = mmio_read32(context->mmio_virt, HDA_REG_GCAP);
    context->supports_64bit     = (gcap & (1 << 0)) != 0;
    context->num_sdo            = (gcap >> 1) & 0x03;
    context->num_bidir_streams  = (gcap >> 3) & 0x1F; // Bits 7:3
    context->num_input_streams  = (gcap >> 8) & 0x0F; // Bits 11:8 (Corrigido)
    context->num_output_streams = (gcap >> 12) & 0x0F; // Bits 15:12

    // 6. Configurar Interrupção
    context->irq_line = pci_read_byte(context->bus, context->slot, context->function, PCI_INTERRUPT_LINE);
    context->using_msi = false;

    // 7. Status final
    context->is_ready = true;
    vga_print_string("[HDA_PCI] Controlador HDA inicializado com sucesso.\n", 0, 38);
    
    return true;
}
