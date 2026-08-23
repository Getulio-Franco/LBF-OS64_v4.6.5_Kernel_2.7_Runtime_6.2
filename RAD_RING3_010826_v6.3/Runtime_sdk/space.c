#include "sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"

// Componentes do Sistema encapsulados
#include "components/TOS_IPC.h"     
#include "components/TOSSerial.h"   

#define M_PI 3.14159265358979323846f

// Protótipos obrigatórios
void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;
extern void GUI_Memo_AddStr(TGUIControl* memo, const char* str);
extern void GUI_Memo_Clear(TGUIControl* memo);

// Variáveis da Janela
int my_app_slot = -1;
TGUIEnvironment MyApp;
const int winWidth = 550;
const int winHeight = 410;

/* ============================================================================
 * SINTETIZADOR DE EFEITOS SONOROS (SFX)
 * ============================================================================ */
#define SFX_SHORT_SAMPLES 6000     
#define SFX_MEDIUM_SAMPLES 12000   
#define SFX_LONG_SAMPLES  20000    

// Buffers para os sons (alinhados para DMA)
static int16_t sfx_shoot[SFX_SHORT_SAMPLES * 2] __attribute__((aligned(4096)));
static int16_t sfx_alien_shoot[SFX_MEDIUM_SAMPLES * 2] __attribute__((aligned(4096)));
static int16_t sfx_alien_hit[SFX_MEDIUM_SAMPLES * 2] __attribute__((aligned(4096)));
static int16_t sfx_barrier_hit[SFX_SHORT_SAMPLES * 2] __attribute__((aligned(4096)));
static int16_t sfx_player_hit[SFX_LONG_SAMPLES * 2] __attribute__((aligned(4096)));

// Função seno otimizada (série de Taylor)
static float custom_sinf(float x) {
    while (x > M_PI)  x -= 2.0f * M_PI;
    while (x < -M_PI) x += 2.0f * M_PI;
    float x2 = x * x;
    float x3 = x * x2;
    float x5 = x3 * x2;
    float x7 = x5 * x2;
    return x - (x3 / 6.0f) + (x5 / 120.0f) - (x7 / 5040.0f);
}

void Init_Audio_SFX(void) {
    uint32_t sample_rate = 48000;
    
    // 1. Som do Tiro (Jogador)
    for (int i = 0; i < SFX_SHORT_SAMPLES; i++) {
        float freq = 1200.0f - (800.0f * ((float)i / SFX_SHORT_SAMPLES));
        if (freq < 80.0f) freq = 80.0f;
        float rad_step = (2.0f * M_PI * freq) / sample_rate;
        float envelope = (i < 60) ? (float)i / 60.0f : (1.0f - (float)(i - 60) / (SFX_SHORT_SAMPLES - 60));
        if (envelope < 0) envelope = 0;
        float harmonic = 0.2f * custom_sinf(i * rad_step * 2.5f);
        int16_t val = (int16_t)((custom_sinf(i * rad_step) + harmonic) * 16000.0f * envelope * envelope);
        sfx_shoot[i*2] = val; sfx_shoot[i*2+1] = val;
    }

    // 2. Som do Tiro (Alien)
    for (int i = 0; i < SFX_MEDIUM_SAMPLES; i++) {
        float freq = 350.0f + (250.0f * custom_sinf(i * 0.025f));
        if (freq < 50.0f) freq = 50.0f;
        float rad_step = (2.0f * M_PI * freq) / sample_rate;
        float envelope = (i < 120) ? (float)i / 120.0f : (1.0f - (float)(i - 120) / (SFX_MEDIUM_SAMPLES - 120));
        if (envelope < 0) envelope = 0;
        float harmonic = 0.2f * custom_sinf(i * rad_step * 1.5f);
        int16_t val = (int16_t)((custom_sinf(i * rad_step) + harmonic) * 14000.0f * envelope * envelope);
        sfx_alien_shoot[i*2] = val; sfx_alien_shoot[i*2+1] = val;
    }

    // 3. Hit Alien & UFO
    for (int i = 0; i < SFX_MEDIUM_SAMPLES; i++) {
        float freq = 1500.0f - (800.0f * ((float)i / SFX_MEDIUM_SAMPLES));
        if (freq < 100.0f) freq = 100.0f;
        float rad_step = (2.0f * M_PI * freq) / sample_rate;
        float envelope = (i < 200) ? (float)i / 200.0f : (1.0f - (float)(i - 200) / (SFX_MEDIUM_SAMPLES - 200));
        if (envelope < 0) envelope = 0;
        float harmonic1 = 0.3f * custom_sinf(i * rad_step * 1.5f);
        float harmonic2 = 0.15f * custom_sinf(i * rad_step * 2.5f);
        int16_t val = (int16_t)((custom_sinf(i * rad_step) + harmonic1 + harmonic2) * 16000.0f * envelope * envelope);
        sfx_alien_hit[i*2] = val; sfx_alien_hit[i*2+1] = val;
    }

    // 4. Hit Barreira
    for (int i = 0; i < SFX_SHORT_SAMPLES; i++) {
        float freq = 250.0f - (150.0f * ((float)i / SFX_SHORT_SAMPLES));
        if (freq < 40.0f) freq = 40.0f;
        float rad_step = (2.0f * M_PI * freq) / sample_rate;
        float envelope = (1.0f - (float)i/SFX_SHORT_SAMPLES);
        if (envelope < 0) envelope = 0;
        float harmonic = 0.3f * custom_sinf(i * rad_step * 0.5f);
        int16_t val = (int16_t)((custom_sinf(i * rad_step) + harmonic) * 14000.0f * envelope * envelope);
        sfx_barrier_hit[i*2] = val; sfx_barrier_hit[i*2+1] = val;
    }

    // 5. Hit Jogador
    for (int i = 0; i < SFX_LONG_SAMPLES; i++) {
        float freq = 500.0f - (480.0f * ((float)i / SFX_LONG_SAMPLES));
        if (freq < 25.0f) freq = 25.0f;
        float rad_step = (2.0f * M_PI * freq) / sample_rate;
        float envelope = (i < 300) ? (float)i / 300.0f : (1.0f - (float)(i - 300) / (SFX_LONG_SAMPLES - 300));
        if (envelope < 0) envelope = 0;
        float modulation = 1.0f + 0.3f * custom_sinf(i * 0.025f);
        float harmonic = 0.2f * custom_sinf(i * rad_step * 0.5f);
        int16_t val = (int16_t)((custom_sinf(i * rad_step) + harmonic) * 20000.0f * envelope * envelope * modulation);
        sfx_player_hit[i*2] = val; sfx_player_hit[i*2+1] = val;
    }
}

// ============================================================================
// FUNÇÕES DE REPRODUÇÃO - OTIMIZADAS COM COOLDOWN E BUSY
// ============================================================================
static int sound_cooldown = 0;
static bool sound_busy = false;

static inline void play_sound(int16_t* buffer, uint32_t size, bool force_play) {
    if (sound_busy && !force_play) return;
    if (force_play || sound_cooldown == 0) {
        if (force_play) {
            sys_audio_stop();
            sound_busy = false;
        }
        sys_audio_play(buffer, size, 0);
        sound_busy = true;
        sound_cooldown = 3;
    }
}

static inline void play_shoot_sound(void) {
    if (!sound_busy && sound_cooldown == 0) {
        sys_audio_play(sfx_shoot, sizeof(sfx_shoot), 0);
        sound_busy = true;
        sound_cooldown = 3;
    }
}

static inline void update_audio_state(void) {
    if (sound_busy) {
        static int busy_timer = 0;
        if (busy_timer == 0) busy_timer = 2;
        else {
            busy_timer--;
            if (busy_timer == 0) sound_busy = false;
        }
    }
}

/* ============================================================================
 * 🛡️ ESTRUTURA AUXILIAR IPC
 * ============================================================================ */
typedef struct {
    uint8_t dummy[sizeof(IPC_WINDOW_LIST[0])]; 
    volatile uint8_t fila_teclado_virtual;
    volatile uint8_t tem_evento_teclado;
} __attribute__((packed)) AppWindowInfoExtended;

char Obter_Tecla_Entrada(void) {
    if (my_app_slot < 0) return 0;
    AppWindowInfoExtended* ext_slot = (AppWindowInfoExtended*)&IPC_WINDOW_LIST[my_app_slot];

    if (ext_slot->tem_evento_teclado == 1) {
        char key = (char)ext_slot->fila_teclado_virtual;
        ext_slot->tem_evento_teclado = 0; 
        return key;
    }
    return 0;
}

/* ============================================================================
 * 👾 SPACE INVADERS - LÓGICA E DADOS 
 * ============================================================================ */
#define OFFSET_X 25
#define OFFSET_Y 60  
#define GAME_W 500
#define GAME_H 320

#define ALIEN_ROWS 3
#define ALIEN_COLS 8
#define ALIEN_W 20
#define ALIEN_H 15

#define UFO_W 32
#define UFO_H 12

#define MAX_ALIEN_LASERS 5
#define NUM_BARRIERS 4
#define BARRIER_BLOCKS_X 5
#define BARRIER_BLOCKS_Y 3
#define BLOCK_SIZE 6

typedef struct { int x, y; int w, h; int alive; } Alien;
typedef struct { int x, y; int active; } Laser;
typedef struct { int x, y; int hp; } BarrierBlock;

static int player_x = OFFSET_X + 230;
static int player_y = OFFSET_Y + 290;
static int player_w = 30;
static int player_h = 12;
static int player_lives = 3;
static int score = 0;
static int fase = 1;

static Laser player_laser = {0, 0, 0};
static Laser alien_lasers[MAX_ALIEN_LASERS];
static Alien aliens[ALIEN_ROWS][ALIEN_COLS];
static BarrierBlock barriers[NUM_BARRIERS][BARRIER_BLOCKS_Y][BARRIER_BLOCKS_X];

// Variáveis do UFO
static int ufo_active = 0;
static int ufo_x = 0;
static int ufo_y = OFFSET_Y + 5;
static int ufo_dir = 1;
static Laser ufo_laser = {0, 0, 0};

static int alien_dir = 1; 
static int alien_move_timer = 0;
static int alien_move_speed = 15; 
static int game_over = 0;
static int game_won = 0;

static uint32_t game_ticks = 0;

/* ============================================================================
 * FUNÇÕES DE INICIALIZAÇÃO
 * ============================================================================ */
void Init_Barriers(void) {
    if (fase == 1) {
        for (int b = 0; b < NUM_BARRIERS; b++)
            for (int r = 0; r < BARRIER_BLOCKS_Y; r++)
                for (int c = 0; c < BARRIER_BLOCKS_X; c++)
                    barriers[b][r][c].hp = 0;
        return;
    }

    int spacing = (GAME_W - (NUM_BARRIERS * BARRIER_BLOCKS_X * BLOCK_SIZE)) / (NUM_BARRIERS + 1);
    for (int b = 0; b < NUM_BARRIERS; b++) {
        int start_x = OFFSET_X + spacing + b * (BARRIER_BLOCKS_X * BLOCK_SIZE + spacing);
        int start_y = OFFSET_Y + 220;
        for (int r = 0; r < BARRIER_BLOCKS_Y; r++) {
            for (int c = 0; c < BARRIER_BLOCKS_X; c++) {
                if (r == BARRIER_BLOCKS_Y - 1 && (c == 1 || c == 2 || c == 3)) {
                    barriers[b][r][c].hp = 0; 
                } else {
                    barriers[b][r][c].x = start_x + c * BLOCK_SIZE;
                    barriers[b][r][c].y = start_y + r * BLOCK_SIZE;
                    barriers[b][r][c].hp = 3;
                }
            }
        }
    }
}

void Init_Aliens(void) {
    for (int r = 0; r < ALIEN_ROWS; r++) {
        for (int c = 0; c < ALIEN_COLS; c++) {
            aliens[r][c].x = OFFSET_X + 20 + c * (ALIEN_W + 15);
            aliens[r][c].y = OFFSET_Y + 25 + r * (ALIEN_H + 15);
            aliens[r][c].w = ALIEN_W;
            aliens[r][c].h = ALIEN_H;
            aliens[r][c].alive = 1;
        }
    }
    alien_move_speed = 15 - (fase * 2);
    if (alien_move_speed < 3) alien_move_speed = 3; 
    
    alien_dir = 1;
    player_laser.active = 0;
    for (int i = 0; i < MAX_ALIEN_LASERS; i++) alien_lasers[i].active = 0;
}

void Reset_Full_Game(void) {
    player_lives = 3;
    score = 0;
    fase = 1;
    game_over = 0;
    game_won = 0;
    player_x = OFFSET_X + 230;
    sound_cooldown = 0;
    sound_busy = false;
    game_ticks = 0;
    ufo_active = 0;
    ufo_laser.active = 0;
    Init_Barriers();
    Init_Aliens();
    sys_audio_stop();
}

void Next_Fase(void) {
    fase++;
    game_won = 0;
    player_x = OFFSET_X + 230;
    sound_cooldown = 0;
    game_ticks = 0;
    ufo_active = 0;
    ufo_laser.active = 0;
    Init_Barriers(); 
    Init_Aliens();
}

/* ============================================================================
 * LÓGICA DE ATUALIZAÇÃO E COLISÕES
 * ============================================================================ */
void Update_Game(void) {
    if (game_over || game_won) return;

    game_ticks++;

    if (sound_cooldown > 0) sound_cooldown--;
    update_audio_state();

    // 1. Atualizar Laser do Jogador
    if (player_laser.active) {
        player_laser.y -= 10;
        if (player_laser.y < OFFSET_Y) player_laser.active = 0;

        // Colisão Laser Jogador vs Barreiras
        if (player_laser.active && fase > 1) {
            for (int b = 0; b < NUM_BARRIERS; b++) {
                for (int r = 0; r < BARRIER_BLOCKS_Y; r++) {
                    for (int c = 0; c < BARRIER_BLOCKS_X; c++) {
                        BarrierBlock* block = &barriers[b][r][c];
                        if (block->hp > 0 &&
                            player_laser.x >= block->x && player_laser.x <= block->x + BLOCK_SIZE &&
                            player_laser.y >= block->y && player_laser.y <= block->y + BLOCK_SIZE) {
                            block->hp--;
                            player_laser.active = 0;
                            play_sound(sfx_barrier_hit, sizeof(sfx_barrier_hit), false);
                            break;
                        }
                    }
                    if (!player_laser.active) break;
                }
                if (!player_laser.active) break;
            }
        }

        // Colisão Laser Jogador vs UFO
        if (player_laser.active && ufo_active) {
            if (player_laser.x >= ufo_x && player_laser.x <= ufo_x + UFO_W &&
                player_laser.y >= ufo_y && player_laser.y <= ufo_y + UFO_H) {
                ufo_active = 0;
                player_laser.active = 0;
                score += 300; // Bônus!
                play_sound(sfx_alien_hit, sizeof(sfx_alien_hit), false);
            }
        }

        // Colisão Laser Jogador vs Aliens
        if (player_laser.active) {
            for (int r = 0; r < ALIEN_ROWS; r++) {
                for (int c = 0; c < ALIEN_COLS; c++) {
                    Alien* a = &aliens[r][c];
                    if (a->alive &&
                        player_laser.x >= a->x && player_laser.x <= a->x + a->w &&
                        player_laser.y >= a->y && player_laser.y <= a->y + a->h) {
                        a->alive = 0;
                        player_laser.active = 0;
                        score += 100 * fase;
                        play_sound(sfx_alien_hit, sizeof(sfx_alien_hit), false);

                        int remaining = 0;
                        for (int xr = 0; xr < ALIEN_ROWS; xr++)
                            for (int xc = 0; xc < ALIEN_COLS; xc++)
                                if (aliens[xr][xc].alive) remaining++;
                        
                        if (remaining == 0) game_won = 1;
                        break;
                    }
                }
                if (!player_laser.active) break;
            }
        }
    }

    // 2. Lógica do UFO Misterioso
    if (!ufo_active) {
        // Nasce a cada ~10 segundos aleatoriamente
        if (game_ticks % 600 == 0) {
            ufo_active = 1;
            ufo_dir = (game_ticks % 2 == 0) ? 1 : -1;
            ufo_x = (ufo_dir == 1) ? OFFSET_X : OFFSET_X + GAME_W - UFO_W;
        }
    } else {
        // Move o UFO
        if (game_ticks % 2 == 0) ufo_x += ufo_dir * 3;
        
        // Desativa se sair da tela
        if (ufo_x < OFFSET_X - UFO_W || ufo_x > OFFSET_X + GAME_W) {
            ufo_active = 0;
        }

        // UFO joga Bomba! (quando passa perto de você)
        if (!ufo_laser.active && (game_ticks % 60 == 0) && ufo_x > player_x - 40 && ufo_x < player_x + 40) {
            ufo_laser.x = ufo_x + UFO_W/2;
            ufo_laser.y = ufo_y + UFO_H;
            ufo_laser.active = 1;
            play_sound(sfx_alien_shoot, sizeof(sfx_alien_shoot), false);
        }
    }

    // Atualiza Laser do UFO
    if (ufo_laser.active) {
        ufo_laser.y += 8; // Bomba rápida
        if (ufo_laser.y > OFFSET_Y + GAME_H) ufo_laser.active = 0;

        // Bomba UFO vs Barreiras
        if (ufo_laser.active && fase > 1) {
            for (int b = 0; b < NUM_BARRIERS; b++) {
                for (int r = 0; r < BARRIER_BLOCKS_Y; r++) {
                    for (int c = 0; c < BARRIER_BLOCKS_X; c++) {
                        BarrierBlock* block = &barriers[b][r][c];
                        if (block->hp > 0 &&
                            ufo_laser.x >= block->x && ufo_laser.x <= block->x + BLOCK_SIZE &&
                            ufo_laser.y >= block->y && ufo_laser.y <= block->y + BLOCK_SIZE) {
                            block->hp--;
                            ufo_laser.active = 0;
                            play_sound(sfx_barrier_hit, sizeof(sfx_barrier_hit), false);
                            break;
                        }
                    }
                    if (!ufo_laser.active) break;
                }
                if (!ufo_laser.active) break;
            }
        }

        // Bomba UFO vs Jogador
        if (ufo_laser.active) {
            if (ufo_laser.x >= player_x && ufo_laser.x <= player_x + player_w &&
                ufo_laser.y >= player_y && ufo_laser.y <= player_y + player_h) {
                ufo_laser.active = 0;
                player_lives--;
                play_sound(sfx_player_hit, sizeof(sfx_player_hit), true);
                if (player_lives <= 0) game_over = 1;
            }
        }
    }

    // 3. Movimentação e Tiro dos Aliens
    alien_move_timer++;
    if (alien_move_timer >= alien_move_speed) {
        alien_move_timer = 0;
        int touch_edge = 0;
        for (int r = 0; r < ALIEN_ROWS; r++) {
            for (int c = 0; c < ALIEN_COLS; c++) {
                if (aliens[r][c].alive) {
                    if ((alien_dir == 1 && aliens[r][c].x + ALIEN_W >= OFFSET_X + GAME_W - 10) ||
                        (alien_dir == -1 && aliens[r][c].x <= OFFSET_X + 10)) {
                        touch_edge = 1;
                        break;
                    }
                }
            }
            if (touch_edge) break;
        }

        if (touch_edge) {
            alien_dir = -alien_dir;
            for (int r = 0; r < ALIEN_ROWS; r++) {
                for (int c = 0; c < ALIEN_COLS; c++) {
                    aliens[r][c].y += 10;
                    if (aliens[r][c].alive && aliens[r][c].y + ALIEN_H >= player_y - 10) {
                        game_over = 1;
                    }
                }
            }
        } else {
            for (int r = 0; r < ALIEN_ROWS; r++) {
                for (int c = 0; c < ALIEN_COLS; c++) {
                    aliens[r][c].x += alien_dir * 6;
                }
            }
        }

        int max_allowed_lasers = (fase >= 3) ? MAX_ALIEN_LASERS : 1;
        if (max_allowed_lasers > 0) {
            for (int c = 0; c < ALIEN_COLS; c++) {
                for (int r = ALIEN_ROWS - 1; r >= 0; r--) {
                    if (aliens[r][c].alive) {
                        if ((game_ticks % 150) < (8 + fase * 2)) {  
                            int current_active = 0;
                            for (int i = 0; i < MAX_ALIEN_LASERS; i++) {
                                if (alien_lasers[i].active) current_active++;
                            }

                            if (current_active < max_allowed_lasers) {
                                for (int i = 0; i < MAX_ALIEN_LASERS; i++) {
                                    if (!alien_lasers[i].active) {
                                        alien_lasers[i].x = aliens[r][c].x + ALIEN_W / 2;
                                        alien_lasers[i].y = aliens[r][c].y + ALIEN_H;
                                        alien_lasers[i].active = 1;
                                        play_sound(sfx_alien_shoot, sizeof(sfx_alien_shoot), false);
                                        break;
                                    }
                                }
                            }
                        }
                        break; 
                    }
                }
            }
        }
    }

    // 4. Atualizar Lasers dos Aliens
    for (int i = 0; i < MAX_ALIEN_LASERS; i++) {
        if (alien_lasers[i].active) {
            alien_lasers[i].y += 6;
            if (alien_lasers[i].y > OFFSET_Y + GAME_H) alien_lasers[i].active = 0;

            if (alien_lasers[i].active && fase > 1) {
                for (int b = 0; b < NUM_BARRIERS; b++) {
                    for (int r = 0; r < BARRIER_BLOCKS_Y; r++) {
                        for (int c = 0; c < BARRIER_BLOCKS_X; c++) {
                            BarrierBlock* block = &barriers[b][r][c];
                            if (block->hp > 0 &&
                                alien_lasers[i].x >= block->x && alien_lasers[i].x <= block->x + BLOCK_SIZE &&
                                alien_lasers[i].y >= block->y && alien_lasers[i].y <= block->y + BLOCK_SIZE) {
                                block->hp--;
                                alien_lasers[i].active = 0;
                                play_sound(sfx_barrier_hit, sizeof(sfx_barrier_hit), false);
                                break;
                            }
                        }
                        if (!alien_lasers[i].active) break;
                    }
                    if (!alien_lasers[i].active) break;
                }
            }

            // Colisão com Jogador
            if (alien_lasers[i].active) {
                if (alien_lasers[i].x >= player_x && alien_lasers[i].x <= player_x + player_w &&
                    alien_lasers[i].y >= player_y && alien_lasers[i].y <= player_y + player_h) {
                    alien_lasers[i].active = 0;
                    player_lives--;
                    play_sound(sfx_player_hit, sizeof(sfx_player_hit), true);
                    if (player_lives <= 0) game_over = 1;
                }
            }
        }
    }
}

/* ============================================================================
 * RENDERING GRÁFICO
 * ============================================================================ */
void Render_Game(void) {
    uint32_t* buf = (uint32_t*)graphics_get_buffer();
    if (!buf) return;

    graphics_fill_rect(OFFSET_X, OFFSET_Y, GAME_W, GAME_H, 0x000001); 
    graphics_draw_rect(OFFSET_X - 1, OFFSET_Y - 1, GAME_W + 2, GAME_H + 2, 0x222222);

    char hud_buf[64];
    itoa(score, hud_buf, 10);
    sys_draw_string(OFFSET_X + 10, OFFSET_Y - 25, "SCORE:", 0xFFFFFF, 1);
    sys_draw_string(OFFSET_X + 70, OFFSET_Y - 25, hud_buf, 0xFFFF00, 1);

    itoa(player_lives, hud_buf, 10);
    sys_draw_string(OFFSET_X + 200, OFFSET_Y - 25, "VIDAS:", 0xFFFFFF, 1);
    sys_draw_string(OFFSET_X + 260, OFFSET_Y - 25, hud_buf, 0xFF0000, 1);

    itoa(fase, hud_buf, 10);
    sys_draw_string(OFFSET_X + 380, OFFSET_Y - 25, "FASE:", 0xFFFFFF, 1);
    sys_draw_string(OFFSET_X + 430, OFFSET_Y - 25, hud_buf, 0x00FFFF, 1);

    // Jogador
    graphics_fill_rect(player_x, player_y, player_w, player_h, 0x00FF00);
    graphics_fill_rect(player_x + player_w / 2 - 2, player_y - 4, 4, 4, 0x00FF00);

    // Laser Jogador
    if (player_laser.active) {
        graphics_fill_rect(player_laser.x - 1, player_laser.y, 3, 8, 0xFFFF00);
    }

    // Desenhar UFO (Disco Voador)
    if (ufo_active) {
        graphics_fill_rect(ufo_x, ufo_y, UFO_W, UFO_H, 0xFF00FF);
        graphics_fill_rect(ufo_x + 6, ufo_y - 4, UFO_W - 12, 4, 0x00FFFF); // Cabine do UFO
    }

    // Bomba do UFO
    if (ufo_laser.active) {
        graphics_fill_rect(ufo_laser.x - 2, ufo_laser.y, 5, 8, 0xFF00FF);
    }

    // Aliens
    for (int r = 0; r < ALIEN_ROWS; r++) {
        uint32_t color = (r == 0) ? 0xFF0055 : (r == 1) ? 0xFFAA00 : 0x00CCFF;
        for (int c = 0; c < ALIEN_COLS; c++) {
            if (aliens[r][c].alive) {
                graphics_fill_rect(aliens[r][c].x, aliens[r][c].y, ALIEN_W, ALIEN_H, color);
                graphics_fill_rect(aliens[r][c].x + 4, aliens[r][c].y + 4, 3, 3, 0x000001);
                graphics_fill_rect(aliens[r][c].x + ALIEN_W - 7, aliens[r][c].y + 4, 3, 3, 0x000001);
            }
        }
    }

    // Lasers Aliens
    for (int i = 0; i < MAX_ALIEN_LASERS; i++) {
        if (alien_lasers[i].active) {
            graphics_fill_rect(alien_lasers[i].x - 1, alien_lasers[i].y, 3, 6, 0xFF0000);
        }
    }

    // Barreiras
    for (int b = 0; b < NUM_BARRIERS; b++) {
        for (int r = 0; r < BARRIER_BLOCKS_Y; r++) {
            for (int c = 0; c < BARRIER_BLOCKS_X; c++) {
                BarrierBlock* block = &barriers[b][r][c];
                if (block->hp > 0) {
                    uint32_t b_color = (block->hp == 3) ? 0x00FFFF : (block->hp == 2) ? 0x00AAAA : 0x005555;
                    graphics_fill_rect(block->x, block->y, BLOCK_SIZE, BLOCK_SIZE, b_color);
                }
            }
        }
    }

    // Overlays
    if (game_over) {
        graphics_fill_rect(OFFSET_X + 100, OFFSET_Y + 120, 300, 80, 0x440000); 
        sys_draw_string(OFFSET_X + 170, OFFSET_Y + 140, "GAME OVER!", 0xFFFFFF, 1);
        sys_draw_string(OFFSET_X + 120, OFFSET_Y + 170, "Pressione 'R' para Reiniciar", 0xFFFF00, 1);
    } else if (game_won) {
        graphics_fill_rect(OFFSET_X + 100, OFFSET_Y + 120, 300, 80, 0x004400); 
        sys_draw_string(OFFSET_X + 150, OFFSET_Y + 140, "FASE CONCLUIDA!", 0xFFFFFF, 1);
        sys_draw_string(OFFSET_X + 115, OFFSET_Y + 170, "Pressione 'ESPACO' p/ proxima", 0xFFFF00, 1);
    }
}

void Flush_Grafico_Janela(void) {
    if (my_app_slot == -1) return;
    gui_draw_form((TForm*)MyApp.MainWindow);
    gui_render_form((TForm*)MyApp.MainWindow);
    Render_Game();
    OS_IPC_FlipBuffers(my_app_slot, winWidth, winHeight);
}

void Tratar_Fechamento_Software(void) {
    if (my_app_slot == -1) return;
    sys_audio_stop();
    sound_busy = false;
    if (MyApp.MainWindow) {
        gui_set_prop(MyApp.MainWindow, PROP_VISIBLE, 0);
    }
    uint32_t* b0 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_0;
    uint32_t* b1 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_1;
    if (b0) memset(b0, 0, winWidth * winHeight * 4);
    if (b1) memset(b1, 0, winWidth * winHeight * 4);
    IPC_WINDOW_LIST[my_app_slot].is_active = 0;
    sys_sleep(50); 
}

/* ============================================================================
 * MAIN (LOOP PRINCIPAL)
 * ============================================================================ */
int main(int argc, char* argv[]) {
    static bool primeiro_desenho = true;
    static bool ultimo_estado_foco = false;
    int audio_update_timer = 0;

    graphics_init_app(winWidth, winHeight);
    wm_init();

    my_app_slot = OS_IPC_RegisterApp("Space Invaders LBF Audio", winWidth, winHeight);
    if (my_app_slot == -1) return -1;

    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "Space Invaders V8 - Audio HD", winWidth, winHeight);

    if (MyApp.MainWindow) {
        gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0x000000); 
    }

    Init_Audio_SFX();
    Reset_Full_Game();
    Flush_Grafico_Janela();

    while (1) {
        if (IPC_WINDOW_LIST[my_app_slot].is_active == 0) {
            Tratar_Fechamento_Software();
            break;
        }

        bool precisa_redesenhar = false;

        if (primeiro_desenho) {
            primeiro_desenho = false;
            precisa_redesenhar = true;
        }

        bool euTenhoFoco = (IPC_CONTROL->active_focus_slot == my_app_slot);
        if (euTenhoFoco != ultimo_estado_foco) {
            ultimo_estado_foco = euTenhoFoco;
            if (MyApp.MainWindow) { ((TForm*)MyApp.MainWindow)->ActiveFocus = euTenhoFoco; }
            precisa_redesenhar = true;
        }

        if (euTenhoFoco) {
            if (sound_cooldown > 0) sound_cooldown--;
            
            audio_update_timer++;
            if (audio_update_timer >= 3) {
                audio_update_timer = 0;
                if (sound_busy) {
                    sound_busy = false;
                }
            }

            char key = Obter_Tecla_Entrada();
            if (key != 0) {
                if (key == 'a' || key == 'A' || key == '4') {
                    if (player_x > OFFSET_X + 5) {
                        player_x -= 12;
                        // Removido som de movimento!
                    }
                } else if (key == 'd' || key == 'D' || key == '6') {
                    if (player_x + player_w < OFFSET_X + GAME_W - 5) {
                        player_x += 12;
                        // Removido som de movimento!
                    }
                } else if (key == ' ' || key == 'w' || key == 'W' || key == '5') {
                    if (game_won) {
                        Next_Fase();
                    } else if (game_over) {
                        Reset_Full_Game();
                    } else if (!player_laser.active) {
                        player_laser.x = player_x + player_w / 2;
                        player_laser.y = player_y - 4;
                        player_laser.active = 1;
                        play_shoot_sound();
                    }
                } else if (key == 'r' || key == 'R') {
                    Reset_Full_Game();
                }
                precisa_redesenhar = true;
            }

            Update_Game();
            precisa_redesenhar = true; // Atualiza a tela a cada frame para as animações fluidas
        }

        if (precisa_redesenhar) Flush_Grafico_Janela();
        
        sys_sleep(16);
    }

    sys_exit();
    return 0;
}
