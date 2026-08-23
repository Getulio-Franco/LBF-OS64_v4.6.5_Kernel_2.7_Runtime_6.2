#ifndef HDA_PCI_H
#define HDA_PCI_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// REGISTRADORES PCI CONFIG SPACE
// ============================================================================
#define PCI_VENDOR_ID          0x00
#define PCI_DEVICE_ID          0x02
#define PCI_COMMAND            0x04
#define PCI_STATUS             0x06
#define PCI_REVISION_ID        0x08
#define PCI_SUBCLASS           0x0A
#define PCI_BASE_CLASS         0x0B
#define PCI_BAR0               0x10
#define PCI_INTERRUPT_LINE     0x3C

// Bits do PCI Command
#define PCI_CMD_MEMORY_SPACE   (1 << 1)
#define PCI_CMD_BUS_MASTER     (1 << 2)
#define PCI_CMD_INT_DISABLE    (1 << 10)

// ============================================================================
// CONSTANTES BASE DO CONTROLADOR INTEL HDA
// ============================================================================
#define HDA_REG_GCAP           0x00   // Global Capabilities
#define HDA_REG_GCTL           0x08   // Global Control
#define HDA_REG_STATESTS       0x0E   // State Change Status (Codecs detectados)

#define HDA_GCTL_CRST          (1 << 0)  // Controller Reset

// ============================================================================
// ESTRUTURA DE CONTEXTO
// ============================================================================
typedef struct {
    // Endereçamento PCI
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  function;
    
    // Mapeamento de Memória (BAR0)
    uint64_t mmio_phys;     // Endereço Físico
    uintptr_t mmio_virt;    // Endereço Virtual
    uint32_t mmio_size;     // Tamanho do BAR0
    
    // Capacidades de Hardware (Lidas do GCAP)
    uint8_t  num_input_streams;
    uint8_t  num_output_streams;
    uint8_t  num_bidir_streams;
    uint8_t  num_sdo;       // Serial Data Outputs
    bool     supports_64bit;
    
    // Codecs Detectados (Lido do STATESTS - Offset 0x0E)
    uint16_t codec_mask;    // Bitmask dos codecs ativos (Bit 0 = Codec 0, Bit 1 = Codec 1, etc.)

    // Interrupções
    uint8_t  irq_line;
    bool     using_msi;

    // Status
    bool     is_ready;      // true se hardware inicializado com sucesso
} hda_hardware_context_t;

// ============================================================================
// FUNÇÕES PÚBLICAS
// ============================================================================
uint32_t pci_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_read_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint8_t  pci_read_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void     pci_write_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value);

bool hda_pci_init(hda_hardware_context_t* context);

#endif // HDA_PCI_H
