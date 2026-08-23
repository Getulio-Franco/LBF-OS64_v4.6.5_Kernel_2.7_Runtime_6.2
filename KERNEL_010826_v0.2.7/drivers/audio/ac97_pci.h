#ifndef AC97_PCI_H
#define AC97_PCI_H

#include <stdint.h>
#include <stdbool.h>

// Registradores PCI Config Space
#define PCI_VENDOR_ID          0x00
#define PCI_COMMAND            0x04
#define PCI_BASE_CLASS         0x0B
#define PCI_SUBCLASS           0x0A
#define PCI_BAR0               0x10 // BAR0 (Mixer I/O)
#define PCI_BAR1               0x14 // BAR1 (Bus Master I/O)
#define PCI_INTERRUPT_LINE     0x3C

// Bits do PCI Command Register
#define PCI_CMD_IO_SPACE       (1 << 0) // AC97 usa I/O ports
#define PCI_CMD_BUS_MASTER     (1 << 2) // Necessário para o DMA funcionar

// Estrutura de Contexto do AC'97 PCI
typedef struct {
    // Endereçamento PCI
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  function;
    
    // Portas de Comunicação (I/O)
    uint16_t mixer_port;    // NAMBAR - Native Audio Mixer Base Address
    uint16_t bm_port;       // NABMBAR - Native Audio Bus Master Base Address
    
    // Interrupções
    uint8_t  irq_line;

    // Status
    bool     is_ready;
} ac97_hardware_context_t;

// Funções Públicas de inicialização PCI
bool ac97_pci_init(ac97_hardware_context_t* context);

// Protótipos utilitários PCI (Caso não estejam globais no seu barramento_pci.h)
uint32_t pci_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_read_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint8_t  pci_read_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void     pci_write_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value);

#endif // AC97_PCI_H
