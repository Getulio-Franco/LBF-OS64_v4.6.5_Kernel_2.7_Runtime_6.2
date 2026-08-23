#ifndef AC97_H
#define AC97_H

#include <stdint.h>
#include <stdbool.h>

#define AC97_BUFFER_SIZE      4096
#define AC97_MAX_BDL_ENTRIES  32
#define AC97_DMA_TIMEOUT      1000

// Registradores Mixer
#define AC97_RESET            0x00
#define AC97_MASTER_VOL       0x02
#define AC97_HEADPHONE_VOL    0x04
#define AC97_PCMOUT_VOL       0x18
#define AC97_POWERDOWN        0x26
#define AC97_EXTENDED_ID      0x28
#define AC97_EXTENDED_STATUS  0x2A
#define AC97_PCM_FRONT_DAC_RATE 0x2C

// Registradores Bus Master
#define AC97_PO_BDBAR         0x10
#define AC97_PO_CIV           0x14
#define AC97_PO_LVI           0x15
#define AC97_PO_SR            0x16
#define AC97_PO_PICB          0x18
#define AC97_PO_CR            0x1B

// Bits
#define AC97_CR_RR            (1 << 0)
#define AC97_CR_IOCE          (1 << 3)
#define AC97_SR_BCS           (1 << 2)
#define AC97_SR_FIFOE         (1 << 3)
#define AC97_SR_LVBI          (1 << 4)
#define AC97_EI_VRA           (1 << 0)
#define AC97_EA_VRA           (1 << 0)

typedef struct {
    uint32_t buffer_addr;
    uint16_t length;
    uint16_t flags;
} __attribute__((packed)) ac97_bdl_entry_t;

typedef struct {
    bool is_playing;
    bool buffer_complete;
    bool error_occurred;
    uint32_t bytes_played;
    uint32_t current_bdl_index;
    uint32_t last_bdl_index;
} ac97_status_t;

// Funções Públicas
int ac97_init(uint16_t mixer_port, uint16_t bm_port);
int ac97_play_user_buffer(void* buffer, uint32_t size);
int ac97_stop(void);
void ac97_set_volume(uint8_t left, uint8_t right);
void ac97_irq_handler(void);
bool ac97_is_playing(void);
bool ac97_is_buffer_complete(void);
void ac97_clear_buffer_complete(void);
uint32_t ac97_get_bytes_played(void);
void ac97_reset_dma(void);
void ac97_dump_registers(void);

#endif // AC97_H
