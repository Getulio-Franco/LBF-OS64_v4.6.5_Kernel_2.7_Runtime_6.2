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
 * SINTETIZADOR DE EFEITOS SONOROS (SFX) - ARKANOID
 * ============================================================================ */
#define SFX_SHORT_SAMPLES 4000     
#define SFX_LONG_SAMPLES  16000    

static int16_t sfx_paddle_hit[SFX_SHORT_SAMPLES * 2] __attribute__((aligned(4096)));
static int16_t sfx_block_hit[SFX_SHORT_SAMPLES * 2] __attribute__((aligned(4096)));
static int16_t sfx_wall_hit[SFX_SHORT_SAMPLES * 2] __attribute__((aligned(4096)));
static int16_t sfx_lose[SFX_LONG_SAMPLES * 2] __attribute__((aligned(4096)));
static int16_t sfx_win[SFX_LONG_SAMPLES * 2] __attribute__((aligned(4096)));

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
    
    // 1. Som Raquete (Ping médio)
    for (int i = 0; i < SFX_SHORT_SAMPLES; i++) {
        float freq = 600.0f;
        float rad_step = (2.0f * M_PI * freq) / sample_rate;
        float envelope = (1.0f - (float)i/SFX_SHORT_SAMPLES);
        int16_t val = (int16_t)(custom_sinf(i * rad_step) * 12000.0f * envelope);
        sfx_paddle_hit[i*2] = val; sfx_paddle_hit[i*2+1] = val;
    }
    
    // 2. Som Bloco (Ping agudo e metálico)
    for (int i = 0; i < SFX_SHORT_SAMPLES; i++) {
        float freq = 1200.0f - (200.0f * ((float)i / SFX_SHORT_SAMPLES));
        float rad_step = (2.0f * M_PI * freq) / sample_rate;
        float envelope = (1.0f - (float)i/SFX_SHORT_SAMPLES);
        int16_t val = (int16_t)(custom_sinf(i * rad_step) * 16000.0f * envelope);
        sfx_block_hit[i*2] = val; sfx_block_hit[i*2+1] = val;
    }

    // 3. Som Parede (Ping grave e seco)
    for (int i = 0; i < SFX_SHORT_SAMPLES; i++) {
        float freq = 300.0f;
        float rad_step = (2.0f * M_PI * freq) / sample_rate;
        float envelope = (1.0f - (float)i/SFX_SHORT_SAMPLES);
        int16_t val = (int16_t)(custom_sinf(i * rad_step) * 14000.0f * envelope);
        sfx_wall_hit[i*2] = val; sfx_wall_hit[i*2+1] = val;
    }

    // 4. Som Perder Vida (Descendente)
    for (int i = 0; i < SFX_LONG_SAMPLES; i++) {
        float freq = 400.0f - (300.0f * ((float)i / SFX_LONG_SAMPLES));
        if (freq < 50.0f) freq = 50.0f;
        float rad_step = (2.0f * M_PI * freq) / sample_rate;
        float envelope = (1.0f - (float)i/SFX_LONG_SAMPLES);
        int16_t val = (int16_t)(custom_sinf(i * rad_step) * 18000.0f * envelope);
        sfx_lose[i*2] = val; sfx_lose[i*2+1] = val;
    }

    // 5. Som Ganhar Fase (Ascendente)
    for (int i = 0; i < SFX_LONG_SAMPLES; i++) {
        float freq = 400.0f + (800.0f * ((float)i / SFX_LONG_SAMPLES));
        float rad_step = (2.0f * M_PI * freq) / sample_rate;
        float envelope = (1.0f - (float)i/SFX_LONG_SAMPLES);
        int16_t val = (int16_t)(custom_sinf(i * rad_step) * 18000.0f * envelope);
        sfx_win[i*2] = val; sfx_win[i*2+1] = val;
    }
}

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
        sound_cooldown = 4;
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
 * 👾 ARKANOID - LÓGICA E DADOS 
 * ============================================================================ */
#define ABS(x) ((x) < 0 ? -(x) : (x))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define OFFSET_X 25
#define OFFSET_Y 60  
#define GAME_W 500
#define GAME_H 320

#define BLOCK_ROWS 5
#define BLOCK_COLS 10
#define BLOCK_W 46
#define BLOCK_H 15
#define BLOCK_PAD 4

typedef struct { float x, y; int active; uint32_t color; } Block;

static float paddle_x = OFFSET_X + 220;
static float paddle_y = OFFSET_Y + 290;
static float paddle_w = 60;
static float paddle_h = 10;
static float paddle_speed = 12.0f;

static float ball_x = 0;
static float ball_y = 0;
static float ball_dx = 0;
static float ball_dy = 0;
static float ball_size = 8;
static int ball_active = 0;

static Block blocks[BLOCK_ROWS][BLOCK_COLS];

static int player_lives = 3;
static int score = 0;
static int fase = 1;
static int game_over = 0;
static int game_won = 0;

void Init_Blocks(void) {
    uint32_t row_colors[5] = {0xFF0000, 0xFFFF00, 0x0055FF, 0xFF00FF, 0x00FF00}; // Cores clássicas

    int start_x = OFFSET_X + (GAME_W - (BLOCK_COLS * BLOCK_W)) / 2;
    int start_y = OFFSET_Y + 30;

    for (int r = 0; r < BLOCK_ROWS; r++) {
        for (int c = 0; c < BLOCK_COLS; c++) {
            blocks[r][c].x = start_x + c * BLOCK_W;
            blocks[r][c].y = start_y + r * BLOCK_H;
            blocks[r][c].active = 1;
            blocks[r][c].color = row_colors[r % 5];
        }
    }
}

void Reset_Ball(void) {
    ball_active = 0;
    ball_x = paddle_x + paddle_w / 2 - ball_size / 2;
    ball_y = paddle_y - ball_size - 1;
    ball_dx = 0;
    ball_dy = 0;
}

void Reset_Full_Game(void) {
    player_lives = 3;
    score = 0;
    fase = 1;
    game_over = 0;
    game_won = 0;
    paddle_x = OFFSET_X + (GAME_W / 2) - (paddle_w / 2);
    sound_cooldown = 0;
    sound_busy = false;
    sys_audio_stop();
    Init_Blocks();
    Reset_Ball();
}

void Next_Fase(void) {
    fase++;
    game_won = 0;
    paddle_x = OFFSET_X + (GAME_W / 2) - (paddle_w / 2);
    Init_Blocks();
    Reset_Ball();
}

/* ============================================================================
 * LÓGICA DE ATUALIZAÇÃO E COLISÕES
 * ============================================================================ */
void Update_Game(void) {
    if (game_over || game_won) return;

    if (sound_cooldown > 0) sound_cooldown--;
    update_audio_state();

    if (!ball_active) {
        // Bola presa na raquete
        ball_x = paddle_x + paddle_w / 2 - ball_size / 2;
        ball_y = paddle_y - ball_size - 1;
        return;
    }

    // Move a bola
    ball_x += ball_dx;
    ball_y += ball_dy;

    // 1. Colisão com Paredes
    if (ball_x <= OFFSET_X) {
        ball_x = OFFSET_X;
        ball_dx = -ball_dx;
        play_sound(sfx_wall_hit, sizeof(sfx_wall_hit), false);
    } else if (ball_x + ball_size >= OFFSET_X + GAME_W) {
        ball_x = OFFSET_X + GAME_W - ball_size;
        ball_dx = -ball_dx;
        play_sound(sfx_wall_hit, sizeof(sfx_wall_hit), false);
    }

    if (ball_y <= OFFSET_Y) {
        ball_y = OFFSET_Y;
        ball_dy = -ball_dy;
        play_sound(sfx_wall_hit, sizeof(sfx_wall_hit), false);
    }

    // 2. Colisão com o Fundo (Perde Vida)
    if (ball_y >= OFFSET_Y + GAME_H) {
        player_lives--;
        play_sound(sfx_lose, sizeof(sfx_lose), true);
        if (player_lives <= 0) {
            game_over = 1;
        } else {
            Reset_Ball();
        }
        return;
    }

    // 3. Colisão com a Raquete
    if (ball_dy > 0 && ball_y + ball_size >= paddle_y && ball_y <= paddle_y + paddle_h) {
        if (ball_x + ball_size >= paddle_x && ball_x <= paddle_x + paddle_w) {
            ball_y = paddle_y - ball_size;
            ball_dy = -ball_dy;
            
            // Calcula o ângulo baseado no local da batida na raquete
            float hit_pos = (ball_x + ball_size / 2.0f) - (paddle_x + paddle_w / 2.0f);
            ball_dx = (hit_pos / (paddle_w / 2.0f)) * 5.0f; 
            
            play_sound(sfx_paddle_hit, sizeof(sfx_paddle_hit), false);
        }
    }

    // 4. Colisão com Blocos (AABB Simples com detecção de eixo)
    int blocks_remaining = 0;
    for (int r = 0; r < BLOCK_ROWS; r++) {
        for (int c = 0; c < BLOCK_COLS; c++) {
            Block* b = &blocks[r][c];
            if (b->active) {
                blocks_remaining++;
                // Verifica intersecção
                if (ball_x < b->x + BLOCK_W && ball_x + ball_size > b->x &&
                    ball_y < b->y + BLOCK_H && ball_y + ball_size > b->y) {
                    
                    b->active = 0;
                    score += 10 * fase;
                    play_sound(sfx_block_hit, sizeof(sfx_block_hit), true);

                    // Determina de qual lado bateu calculando a área de sobreposição (overlap)
                    float overlap_x = MIN(ball_x + ball_size, b->x + BLOCK_W) - MAX(ball_x, b->x);
                    float overlap_y = MIN(ball_y + ball_size, b->y + BLOCK_H) - MAX(ball_y, b->y);

                    if (overlap_x < overlap_y) {
                        ball_dx = -ball_dx; // Bateu na lateral
                    } else {
                        ball_dy = -ball_dy; // Bateu em cima/embaixo
                    }
                    goto end_collision_check; // Previne múltiplas colisões no mesmo frame
                }
            }
        }
    }
end_collision_check:

    // 5. Condição de Vitória
    if (blocks_remaining == 0) {
        game_won = 1;
        play_sound(sfx_win, sizeof(sfx_win), true);
    }
}

/* ============================================================================
 * RENDERING GRÁFICO
 * ============================================================================ */
void Render_Game(void) {
    uint32_t* buf = (uint32_t*)graphics_get_buffer();
    if (!buf) return;

    // Fundo azul estilo clássico Arkanoid
    graphics_fill_rect(OFFSET_X, OFFSET_Y, GAME_W, GAME_H, 0x000044); 
    graphics_draw_rect(OFFSET_X - 1, OFFSET_Y - 1, GAME_W + 2, GAME_H + 2, 0x888888);

    // HUD Superior
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

    // Blocos
    for (int r = 0; r < BLOCK_ROWS; r++) {
        for (int c = 0; c < BLOCK_COLS; c++) {
            if (blocks[r][c].active) {
                // Preenchimento do bloco com uma borda leve para destacar os tijolos
                graphics_fill_rect(blocks[r][c].x + 1, blocks[r][c].y + 1, 
                                   BLOCK_W - 2, BLOCK_H - 2, blocks[r][c].color);
                
                // Detalhe de "brilho/sombra" clássico
                graphics_fill_rect(blocks[r][c].x + 1, blocks[r][c].y + 1, BLOCK_W - 2, 2, 0xFFFFFF); // topo claro
            }
        }
    }

    // Raquete
    graphics_fill_rect(paddle_x, paddle_y, paddle_w, paddle_h, 0xAAAAAA);
    graphics_fill_rect(paddle_x + 2, paddle_y + 2, paddle_w - 4, paddle_h - 4, 0xDD0000);

    // Bola
    graphics_fill_rect(ball_x, ball_y, ball_size, ball_size, 0xFFFFFF);

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
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_VISIBLE, 0);
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

    graphics_init_app(winWidth, winHeight);
    wm_init();

    my_app_slot = OS_IPC_RegisterApp("Arkanoid LBF", winWidth, winHeight);
    if (my_app_slot == -1) return -1;

    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "Arkanoid V1 - Retro Breakout", winWidth, winHeight);

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
            char key = Obter_Tecla_Entrada();
            if (key != 0) {
                // Mover Raquete para Esquerda
                if (key == 'a' || key == 'A' || key == '4') {
                    if (paddle_x > OFFSET_X + 5) paddle_x -= paddle_speed;
                } 
                // Mover Raquete para Direita
                else if (key == 'd' || key == 'D' || key == '6') {
                    if (paddle_x + paddle_w < OFFSET_X + GAME_W - 5) paddle_x += paddle_speed;
                } 
                // Lançar bola ou Avançar telas
                else if (key == ' ' || key == 'w' || key == 'W' || key == '5') {
                    if (game_won) {
                        Next_Fase();
                    } else if (game_over) {
                        Reset_Full_Game();
                    } else if (!ball_active) {
                        ball_active = 1;
                        ball_dx = 3.0f; // Direção inicial
                        ball_dy = -(4.0f + (fase * 0.5f)); // Fica mais rápido a cada fase
                    }
                } 
                // Reiniciar
                else if (key == 'r' || key == 'R') {
                    Reset_Full_Game();
                }
                precisa_redesenhar = true;
            }

            Update_Game();
            precisa_redesenhar = true; // Força redesenhar contínuo para o movimento da bola
        }

        if (precisa_redesenhar) Flush_Grafico_Janela();
        
        sys_sleep(16); // ~60 FPS
    }

    sys_exit();
    return 0;
}
