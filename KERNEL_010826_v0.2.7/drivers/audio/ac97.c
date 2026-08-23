#include "ac97.h"
#include "drivers/io.h"
#include <string.h>
#include <stddef.h>

static uint16_t ac97_mixer_io = 0;
static uint16_t ac97_bm_io = 0;
static bool g_is_initialized = false;

static ac97_bdl_entry_t g_bdl_table[AC97_MAX_BDL_ENTRIES] __attribute__((aligned(8)));
static uint8_t g_bounce_buffer[AC97_BUFFER_SIZE * AC97_MAX_BDL_ENTRIES] __attribute__((aligned(4096)));
static ac97_status_t g_status = {0};

static inline uint32_t virt_to_phys(void* virt_addr) {
    return (uint32_t)(uintptr_t)virt_addr; 
}

static inline void ac97_delay_us(uint32_t microseconds) {
    volatile uint32_t count = microseconds * 100;
    while (count--) { __asm__ volatile ("nop"); }
}

static inline void ac97_delay_ms(uint32_t milliseconds) {
    for (uint32_t i = 0; i < milliseconds; i++) {
        ac97_delay_us(1000);
    }
}

static void ac97_reset_bus_master(void) {
    outb(ac97_bm_io + AC97_PO_CR, 0);
    ac97_delay_us(50);

    outb(ac97_bm_io + AC97_PO_CR, AC97_CR_RR);
    ac97_delay_us(100);

    outb(ac97_bm_io + AC97_PO_CR, 0);
    ac97_delay_us(50);

    outw(ac97_bm_io + AC97_PO_SR, 0x001F);
    
    outb(ac97_bm_io + AC97_PO_CIV, 0);
    outb(ac97_bm_io + AC97_PO_LVI, 0);
    outw(ac97_bm_io + AC97_PO_PICB, 0);
}

int ac97_init(uint16_t mixer_port, uint16_t bm_port) {
    ac97_mixer_io = mixer_port;
    ac97_bm_io = bm_port;

    uint16_t ext_id = inw(ac97_mixer_io + AC97_EXTENDED_ID);
    if (ext_id == 0xFFFF) return -1;

    outw(ac97_mixer_io + AC97_RESET, 0x0000);
    ac97_delay_ms(10);

    outw(ac97_mixer_io + AC97_MASTER_VOL, 0x0000);
    outw(ac97_mixer_io + AC97_PCMOUT_VOL, 0x0000);
    outw(ac97_mixer_io + AC97_HEADPHONE_VOL, 0x0000);

    outw(ac97_mixer_io + AC97_POWERDOWN, 0x0000);
    ac97_delay_ms(10);
    
    if (ext_id & AC97_EI_VRA) {
        uint16_t ext_status = inw(ac97_mixer_io + AC97_EXTENDED_STATUS);
        ext_status |= AC97_EA_VRA;
        outw(ac97_mixer_io + AC97_EXTENDED_STATUS, ext_status);
        ac97_delay_us(100);
        outw(ac97_mixer_io + AC97_PCM_FRONT_DAC_RATE, 48000);
        ac97_delay_us(100);
    }
    
    ac97_reset_bus_master();

    memset(g_bounce_buffer, 0, sizeof(g_bounce_buffer));
    memset(g_bdl_table, 0, sizeof(g_bdl_table));

    g_status.is_playing = false;
    g_status.buffer_complete = false;
    g_status.error_occurred = false;
    g_status.bytes_played = 0;

    g_is_initialized = true;

    return 0;
}

int ac97_play_user_buffer(void* buffer, uint32_t size) {
    if (!g_is_initialized || !buffer || size == 0) return -1;
    
    if (size % 2 != 0) size--;
    if (size == 0) return -1;
    
    uint32_t max_size = AC97_BUFFER_SIZE * AC97_MAX_BDL_ENTRIES;
    if (size > max_size) size = max_size;

    // 1. PARA O DMA
    outb(ac97_bm_io + AC97_PO_CR, 0);
    ac97_delay_us(100);
    
    // 2. RESET
    ac97_reset_bus_master();
    ac97_delay_us(100);

    // 3. LIMPA
    memset(g_bounce_buffer, 0, sizeof(g_bounce_buffer));
    memset(g_bdl_table, 0, sizeof(g_bdl_table));

    // 4. COPIA
    memcpy(g_bounce_buffer, buffer, size);

    // 5. PREENCHE BDL
    uint32_t bytes_copied = 0;
    uint32_t bdl_index = 0;

    while (bytes_copied < size && bdl_index < AC97_MAX_BDL_ENTRIES) {
        uint32_t remaining = size - bytes_copied;
        uint32_t chunk = (remaining > AC97_BUFFER_SIZE) ? AC97_BUFFER_SIZE : remaining;
        
        g_bdl_table[bdl_index].buffer_addr = virt_to_phys(&g_bounce_buffer[bytes_copied]);
        g_bdl_table[bdl_index].length = (uint16_t)(chunk / 2);
        
        if (bytes_copied + chunk >= size) {
            g_bdl_table[bdl_index].flags = 0x8000 | 0x4000;
        } else {
            g_bdl_table[bdl_index].flags = 0x0000;
        }

        bytes_copied += chunk;
        bdl_index++;
    }

    // 6. CONFIGURA DMA
    outl(ac97_bm_io + AC97_PO_BDBAR, virt_to_phys(g_bdl_table));
    outb(ac97_bm_io + AC97_PO_LVI, (uint8_t)(bdl_index - 1));
    outb(ac97_bm_io + AC97_PO_CIV, 0);
    outw(ac97_bm_io + AC97_PO_PICB, 0);
    outw(ac97_bm_io + AC97_PO_SR, 0x001F);
    ac97_delay_us(10);

    // 7. ATUALIZA STATUS
    g_status.is_playing = true;
    g_status.buffer_complete = false;
    g_status.error_occurred = false;
    g_status.bytes_played = 0;
    g_status.current_bdl_index = 0;
    g_status.last_bdl_index = bdl_index - 1;

    // 8. INICIA DMA
    outb(ac97_bm_io + AC97_PO_CR, AC97_CR_RR | AC97_CR_IOCE);
    ac97_delay_us(50);

    // 9. VERIFICA
    uint8_t cr_check = inb(ac97_bm_io + AC97_PO_CR);
    if (!(cr_check & AC97_CR_RR)) {
        outb(ac97_bm_io + AC97_PO_CR, AC97_CR_RR | AC97_CR_IOCE);
        ac97_delay_us(50);
    }

    return 0;
}

int ac97_stop(void) {
    if (!g_is_initialized) return -1;
    
    outb(ac97_bm_io + AC97_PO_CR, 0);
    ac97_delay_us(100);
    ac97_reset_bus_master();
    memset(g_bounce_buffer, 0, sizeof(g_bounce_buffer));
    memset(g_bdl_table, 0, sizeof(g_bdl_table));
    g_status.is_playing = false;
    g_status.buffer_complete = false;
    g_status.error_occurred = false;
    g_status.bytes_played = 0;
    return 0;
}

void ac97_set_volume(uint8_t left, uint8_t right) {
    if (!g_is_initialized) return;
    uint8_t left_val = (left > 100) ? 0 : (31 - (left * 31 / 100));
    uint8_t right_val = (right > 100) ? 0 : (31 - (right * 31 / 100));
    outw(ac97_mixer_io + AC97_MASTER_VOL, (right_val << 8) | left_val);
    outw(ac97_mixer_io + AC97_PCMOUT_VOL, (right_val << 8) | left_val);
}

void ac97_irq_handler(void) {
    if (!g_is_initialized) return;
    uint16_t status = inw(ac97_bm_io + AC97_PO_SR);

    if (status & AC97_SR_LVBI) {
        g_status.buffer_complete = true;
        g_status.is_playing = false;
        outb(ac97_bm_io + AC97_PO_CR, 0);
        ac97_delay_us(10);
        outw(ac97_bm_io + AC97_PO_SR, AC97_SR_LVBI);
    }

    if (status & AC97_SR_BCS) {
        outw(ac97_bm_io + AC97_PO_SR, AC97_SR_BCS);
    }

    if (status & AC97_SR_FIFOE) {
        g_status.error_occurred = true;
        outw(ac97_bm_io + AC97_PO_SR, AC97_SR_FIFOE);
    }
}

bool ac97_is_playing(void) {
    if (!g_is_initialized) return false;
    return g_status.is_playing;
}

bool ac97_is_buffer_complete(void) {
    if (!g_is_initialized) return false;
    return g_status.buffer_complete;
}

void ac97_clear_buffer_complete(void) {
    if (!g_is_initialized) return;
    g_status.buffer_complete = false;
}

uint32_t ac97_get_bytes_played(void) {
    if (!g_is_initialized) return 0;
    return g_status.bytes_played;
}

void ac97_reset_dma(void) {
    if (!g_is_initialized) return;
    ac97_reset_bus_master();
    memset(&g_status, 0, sizeof(g_status));
}

void ac97_dump_registers(void) {
    if (!g_is_initialized) return;
}
