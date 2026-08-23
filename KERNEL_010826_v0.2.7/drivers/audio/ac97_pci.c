#include "ac97_pci.h"
#include "ac97.h"
#include "drivers/proc.h"
#include "util/string.h"
#include "drivers/video.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

// ============================================================================
// I/O E ACESSO PCI NATIVO (Se você já tiver isso global, pode remover daqui)
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
// FLUXO PRINCIPAL DO AC97_PCI
// ============================================================================

bool ac97_pci_init(ac97_hardware_context_t* context) {
    if (!context) return false;
    memset(context, 0, sizeof(ac97_hardware_context_t));

    vga_print_string("[AC97_PCI] Buscando controlador AC'97...\n", 0, 38);

    // 1. Procurar o controlador (Class 0x04 - Multimedia, Subclass 0x01 - Audio)
    bool found = false;
    for (uint16_t bus = 0; bus < 256 && !found; bus++) {
        for (uint8_t slot = 0; slot < 32 && !found; slot++) {
            for (uint8_t func = 0; func < 8 && !found; func++) {
                if (pci_read_word((uint8_t)bus, slot, func, PCI_VENDOR_ID) == 0xFFFF) continue;
                
                if (pci_read_byte((uint8_t)bus, slot, func, PCI_BASE_CLASS) == 0x04 && 
                    pci_read_byte((uint8_t)bus, slot, func, PCI_SUBCLASS) == 0x01) {
                    context->bus = (uint8_t)bus;
                    context->slot = slot;
                    context->function = func;
                    found = true;
                }
            }
        }
    }

    if (!found) {
        vga_print_string("[AC97_PCI] Controlador AC'97 nao encontrado.\n", 0, 38);
        return false;
    }

    // 2. Habilitar I/O Space e Bus Master no comando PCI
    uint16_t cmd = pci_read_word(context->bus, context->slot, context->function, PCI_COMMAND);
    cmd |= PCI_CMD_IO_SPACE | PCI_CMD_BUS_MASTER;
    pci_write_word(context->bus, context->slot, context->function, PCI_COMMAND, cmd);

    // 3. Ler BAR0 (Mixer Base Address) e BAR1 (Bus Master Base Address)
    uint32_t bar0 = pci_read_dword(context->bus, context->slot, context->function, PCI_BAR0);
    uint32_t bar1 = pci_read_dword(context->bus, context->slot, context->function, PCI_BAR1);

    // AC'97 usa as portas de E/S. O bit 0 de uma BAR de I/O é sempre 1. Removemos ele (~3).
    context->mixer_port = (uint16_t)(bar0 & ~0x3);
    context->bm_port    = (uint16_t)(bar1 & ~0x3);

    if (context->mixer_port == 0 || context->bm_port == 0) {
        vga_print_string("[AC97_PCI] Erro ao ler BARs do AC'97. Abortando.\n", 0, 38);
        return false;
    }

    // 4. Ler linha de interrupção
    context->irq_line = pci_read_byte(context->bus, context->slot, context->function, PCI_INTERRUPT_LINE);

    context->is_ready = true;
    
    // 5. Acorda o driver AC'97 em si!
    if (ac97_init(context->mixer_port, context->bm_port) == 0) {
        vga_print_string("[AC97_PCI] Driver AC'97 carregado com sucesso!\n", 0, 38);
        return true;
    } else {
        vga_print_string("[AC97_PCI] Falha no Hardware AC'97.\n", 0, 38);
        return false;
    }
}
