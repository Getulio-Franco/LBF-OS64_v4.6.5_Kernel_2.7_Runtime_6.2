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
#define SFX_MOVE_SAMPLES 6000     
#define SFX_EAT_SAMPLES  10000    
#define SFX_DIE_SAMPLES  20000    
#define SFX_WIN_SAMPLES  28000    
#define SFX_LEVELUP_SAMPLES 18000 

// Buffers para os sons (alinhados para DMA)
static int16_t sfx_move[SFX_MOVE_SAMPLES * 2] __attribute__((aligned(4096)));
static int16_t sfx_eat[SFX_EAT_SAMPLES * 2]   __attribute__((aligned(4096)));
static int16_t sfx_die[SFX_DIE_SAMPLES * 2]   __attribute__((aligned(4096)));
static int16_t sfx_win[SFX_WIN_SAMPLES * 2]   __attribute__((aligned(4096)));
static int16_t sfx_levelup[SFX_LEVELUP_SAMPLES * 2] __attribute__((aligned(4096)));

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

// ============================================================================
// INICIALIZAÇÃO DOS SONS - PRÉ-GERAÇÃO PARA RESPOSTA INSTANTÂNEA
// ============================================================================
void Init_Audio_SFX(void) {
    uint32_t sample_rate = 48000;
    
    // ============================================================
    // 1. Som de Movimento: Agora usando o tiro do Space Invaders
    // ============================================================
    for (int i = 0; i < SFX_MOVE_SAMPLES; i++) {
        float freq = 1200.0f - (800.0f * ((float)i / SFX_MOVE_SAMPLES));
        if (freq < 80.0f) freq = 80.0f;
        float rad_step = (2.0f * M_PI * freq) / sample_rate;
        float envelope = (i < 60) ? (float)i / 60.0f : (1.0f - (float)(i - 60) / (SFX_MOVE_SAMPLES - 60));
        if (envelope < 0) envelope = 0;
        float harmonic = 0.2f * custom_sinf(i * rad_step * 2.5f);
        int16_t val = (int16_t)((custom_sinf(i * rad_step) + harmonic) * 16000.0f * envelope * envelope);
        sfx_move[i*2] = val; 
        sfx_move[i*2+1] = val;
    }
    
    // ============================================================
    // 2. Som de Comer (Eat): Blip com harmônicos ricos (Bloco Vermelho)
    // ============================================================
    for (int i = 0; i < SFX_EAT_SAMPLES; i++) {
        float freq = 500.0f + (700.0f * ((float)i / SFX_EAT_SAMPLES));
        float rad_step = (2.0f * M_PI * freq) / sample_rate;
        float envelope;
        if (i < 200) envelope = (float)i / 200.0f;
        else envelope = (1.0f - (float)(i - 200) / (SFX_EAT_SAMPLES - 200));
        if (envelope < 0) envelope = 0;
        float harmonic1 = 0.4f * custom_sinf(i * rad_step * 1.5f);
        float harmonic2 = 0.2f * custom_sinf(i * rad_step * 2.5f);
        float harmonic3 = 0.1f * custom_sinf(i * rad_step * 4.0f);
        int16_t val = (int16_t)((custom_sinf(i * rad_step) + harmonic1 + harmonic2 + harmonic3) 
                              * 16000.0f * envelope);
        sfx_eat[i*2] = val; 
        sfx_eat[i*2+1] = val;
    }

    // ============================================================
    // 3. Som de Morte (Die): Grave e dramático
    // ============================================================
    for (int i = 0; i < SFX_DIE_SAMPLES; i++) {
        float freq = 500.0f - (450.0f * ((float)i / SFX_DIE_SAMPLES));
        if (freq < 30.0f) freq = 30.0f;
        float rad_step = (2.0f * M_PI * freq) / sample_rate;
        float envelope;
        if (i < 300) envelope = (float)i / 300.0f;
        else envelope = (1.0f - (float)(i - 300) / (SFX_DIE_SAMPLES - 300));
        if (envelope < 0) envelope = 0;
        float modulation = 1.0f + 0.3f * custom_sinf(i * 0.02f);
        float harmonic = 0.3f * custom_sinf(i * rad_step * 0.5f);
        int16_t val = (int16_t)((custom_sinf(i * rad_step) + harmonic) 
                              * 20000.0f * envelope * envelope * modulation);
        sfx_die[i*2] = val; 
        sfx_die[i*2+1] = val;
    }

    // ============================================================
    // 4. Som de Vitória (Win): Ascendente e épico
    // ============================================================
    for (int i = 0; i < SFX_WIN_SAMPLES; i++) {
        float freq = 400.0f + (600.0f * ((float)i / SFX_WIN_SAMPLES));
        float rad_step = (2.0f * M_PI * freq) / sample_rate;
        float envelope;
        if (i < 2000) envelope = (float)i / 2000.0f;
        else envelope = (1.0f - (float)(i - 2000) / (SFX_WIN_SAMPLES - 2000));
        if (envelope < 0) envelope = 0;
        float harmonic1 = 0.3f * custom_sinf(i * rad_step * 1.5f);
        float harmonic2 = 0.15f * custom_sinf(i * rad_step * 2.0f);
        float harmonic3 = 0.1f * custom_sinf(i * rad_step * 3.0f);
        int16_t val = (int16_t)((custom_sinf(i * rad_step) + harmonic1 + harmonic2 + harmonic3) 
                              * 15000.0f * envelope);
        sfx_win[i*2] = val; 
        sfx_win[i*2+1] = val;
    }

    // ============================================================
    // 5. Som de Mudança de Fase (Level Up): Energético
    // ============================================================
    for (int i = 0; i < SFX_LEVELUP_SAMPLES; i++) {
        float freq = 300.0f + (800.0f * ((float)i / SFX_LEVELUP_SAMPLES));
        float rad_step = (2.0f * M_PI * freq) / sample_rate;
        float envelope;
        if (i < 500) envelope = (float)i / 500.0f;
        else envelope = (1.0f - (float)(i - 500) / (SFX_LEVELUP_SAMPLES - 500));
        if (envelope < 0) envelope = 0;
        float modulation = 1.0f + 0.2f * custom_sinf(i * 0.015f);
        float harmonic = 0.25f * custom_sinf(i * rad_step * 2.0f);
        int16_t val = (int16_t)((custom_sinf(i * rad_step) + harmonic) 
                              * 16000.0f * envelope * envelope * modulation);
        sfx_levelup[i*2] = val; 
        sfx_levelup[i*2+1] = val;
    }
}

// ============================================================================
// FUNÇÕES DE REPRODUÇÃO - COM TRAVA DE SINCRONIZAÇÃO (AUDIO LOCK)
// ============================================================================

static int audio_lock_frames = 0; // Quantos frames a fila de áudio ficará bloqueada
static bool app_closing = false;  // Flag inquebrável para impedir sons ao fechar

static inline void play_sound(int16_t* buffer, uint32_t size, bool force_play, int lock_frames) {
    if (app_closing) return; // Segurança contra eventos fantasmas ao fechar a janela
    
    // Se o áudio estiver travado (lock > 0) e não for reprodução forçada, ignoramos o pedido
    if (audio_lock_frames > 0 && !force_play) return;
    
    if (force_play || audio_lock_frames == 0) {
        if (force_play) {
            sys_audio_stop();
        }
        sys_audio_play(buffer, size, 0);
        audio_lock_frames = lock_frames; // Aplica a trava pelo tempo estipulado
    }
}

static inline void play_move_sound(void) {
    if (app_closing) return;
    
    // O som de movimento respeita estritamente a trava de áudio global
    if (audio_lock_frames == 0) {
        sys_audio_play(sfx_move, sizeof(sfx_move), 0);
        audio_lock_frames = 3; // Trava curtinha (~48ms) apenas para evitar spam de teclas
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
 * 🐍 LÓGICA DO JOGO DA COBRINHA
 * ============================================================================ */
#define OFFSET_X 25
#define OFFSET_Y 60  
#define TILE_SIZE 20
#define GRID_W 25
#define GRID_H 16
#define GAME_W (GRID_W * TILE_SIZE)
#define GAME_H (GRID_H * TILE_SIZE)

static int snake_x[100], snake_y[100];
static int snake_len = 4;
static int dir_x = 1, dir_y = 0;
static bool dir_changed = false; 
static int food_x = 10, food_y = 10;
static int obs_x[2], obs_y[2];
static int num_obs = 0;
static int player_lives = 3;
static int score = 0;
static int fase = 1;
static int foods_collected = 0;
static int game_over = 0;
static int game_won = 0;
static int speed_threshold = 110; 

static uint32_t rng_seed = 12345; 
int lcg_rand() {
    rng_seed = (1103515245 * rng_seed + 12345) % 2147483648;
    return rng_seed;
}

bool Is_Occupied(int x, int y) {
    for (int i = 0; i < snake_len; i++) {
        if (snake_x[i] == x && snake_y[i] == y) return true;
    }
    return false;
}

void Spawn_Items(void) {
    do {
        food_x = lcg_rand() % GRID_W;
        food_y = lcg_rand() % GRID_H;
    } while (Is_Occupied(food_x, food_y));

    if (fase >= 3) {
        num_obs = 2;
        for (int i = 0; i < num_obs; i++) {
            do {
                obs_x[i] = lcg_rand() % GRID_W;
                obs_y[i] = lcg_rand() % GRID_H;
            } while (Is_Occupied(obs_x[i], obs_y[i]) || 
                     (obs_x[i] == food_x && obs_y[i] == food_y) || 
                     (i == 1 && obs_x[0] == obs_x[1] && obs_y[0] == obs_y[1]));
        }
    } else {
        num_obs = 0;
    }
}

void Init_Snake(void) {
    snake_len = 4;
    for (int i = 0; i < snake_len; i++) {
        snake_x[i] = 12 - i;
        snake_y[i] = 8;
    }
    dir_x = 1; dir_y = 0;
    dir_changed = false;
    Spawn_Items();
}

void Reset_Full_Game(void) {
    player_lives = 3;
    score = 0;
    fase = 1;
    foods_collected = 0;
    game_over = 0;
    game_won = 0;
    speed_threshold = 110; 
    audio_lock_frames = 0;
    Init_Snake();
    if (!app_closing) sys_audio_stop();  
}

void Next_Fase(void) {
    if (fase < 3) fase++;
    foods_collected = 0;
    game_won = 0;
    speed_threshold = (fase >= 2) ? 95 : 110; 
    play_sound(sfx_levelup, sizeof(sfx_levelup), true, 25);
    Init_Snake();
}

void Update_Game(void) {
    if (game_over || game_won) return;

    for (int i = snake_len - 1; i > 0; i--) {
        snake_x[i] = snake_x[i-1];
        snake_y[i] = snake_y[i-1];
    }
    snake_x[0] += dir_x;
    snake_y[0] += dir_y;
    dir_changed = false;

    bool died = false;
    if (snake_x[0] < 0 || snake_x[0] >= GRID_W || snake_y[0] < 0 || snake_y[0] >= GRID_H) died = true;
    for (int i = 1; i < snake_len; i++) {
        if (snake_x[0] == snake_x[i] && snake_y[0] == snake_y[i]) died = true;
    }
    for (int i = 0; i < num_obs; i++) {
        if (snake_x[0] == obs_x[i] && snake_y[0] == obs_y[i]) died = true;
    }

    if (died) {
        play_sound(sfx_die, sizeof(sfx_die), true, 40);
        player_lives--;
        if (player_lives <= 0) {
            game_over = 1;
        } else {
            sys_sleep(300);
            Init_Snake(); 
        }
        return;
    }

    // Coleta do Bloco Vermelho (Comida)
    if (snake_x[0] == food_x && snake_y[0] == food_y) {
        if (snake_len < 100) snake_len++;
        score += 10 * fase;
        foods_collected++;

        if (foods_collected >= 16) {
            play_sound(sfx_win, sizeof(sfx_win), true, 50);
            if (fase == 1) {
                sys_sleep(200);
                Next_Fase();
            } else {
                game_won = 1;
            }
        } else {
            // TRAVA DE ÁUDIO APLICADA: Forçamos o som do bloco vermelho e 
            // bloqueamos outros sons por 14 frames (~224ms)
            play_sound(sfx_eat, sizeof(sfx_eat), true, 14); 
            Spawn_Items(); 
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

    // Comida (Bloco Vermelho)
    graphics_fill_rect(OFFSET_X + food_x * TILE_SIZE + 1, OFFSET_Y + food_y * TILE_SIZE + 1, TILE_SIZE - 2, TILE_SIZE - 2, 0xFF0000);
    graphics_fill_rect(OFFSET_X + food_x * TILE_SIZE + 7, OFFSET_Y + food_y * TILE_SIZE + 7, 6, 6, 0xFF4444);

    for (int i = 0; i < num_obs; i++) {
        graphics_fill_rect(OFFSET_X + obs_x[i] * TILE_SIZE + 1, OFFSET_Y + obs_y[i] * TILE_SIZE + 1, TILE_SIZE - 2, TILE_SIZE - 2, 0x00FFFF);
    }

    for (int i = 0; i < snake_len; i++) {
        uint32_t color;
        if (i == 0) color = (game_over) ? 0x888888 : 0x00FF00;
        else if (i < 3) color = (game_over) ? 0x666666 : 0x00CC00;
        else color = (game_over) ? 0x444444 : 0x008800;
        
        graphics_fill_rect(OFFSET_X + snake_x[i] * TILE_SIZE + 1, 
                          OFFSET_Y + snake_y[i] * TILE_SIZE + 1, 
                          TILE_SIZE - 2, TILE_SIZE - 2, color);
    }

    if (game_over) {
        graphics_fill_rect(OFFSET_X + 100, OFFSET_Y + 130, 300, 80, 0x440000); 
        sys_draw_string(OFFSET_X + 170, OFFSET_Y + 150, "GAME OVER!", 0xFFFFFF, 1);
        sys_draw_string(OFFSET_X + 120, OFFSET_Y + 180, "Pressione 'R' para Reiniciar", 0xFFFF00, 1);
    } else if (game_won) {
        graphics_fill_rect(OFFSET_X + 100, OFFSET_Y + 130, 300, 80, 0x004400); 
        sys_draw_string(OFFSET_X + 150, OFFSET_Y + 150, "FASE CONCLUIDA!", 0xFFFFFF, 1);
        sys_draw_string(OFFSET_X + 115, OFFSET_Y + 180, "Pressione 'ESPACO' p/ proxima", 0xFFFF00, 1);
    }
}

/* ============================================================================
 * FLUSH E FECHAMENTO
 * ============================================================================ */
void Flush_Grafico_Janela(void) {
    if (my_app_slot == -1) return;
    gui_draw_form((TForm*)MyApp.MainWindow);
    gui_render_form((TForm*)MyApp.MainWindow);
    Render_Game();
    OS_IPC_FlipBuffers(my_app_slot, winWidth, winHeight);
}

void Tratar_Fechamento_Software(void) {
    app_closing = true; // Impede qualquer lógica de som dali em diante
    if (my_app_slot == -1) return;
    
    sys_audio_stop();
    
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_VISIBLE, 0);
    uint32_t* b0 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_0;
    uint32_t* b1 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_1;
    
    if (b0) memset(b0, 0, winWidth * winHeight * 4);
    if (b1) memset(b1, 0, winWidth * winHeight * 4);
    
    IPC_WINDOW_LIST[my_app_slot].is_active = 0;
    
    sys_sleep(50); 
    sys_audio_stop(); // Envio extra para garantir o silêncio final
}

/* ============================================================================
 * FUNÇÃO PRINCIPAL (MAIN)
 * ============================================================================ */
int main(int argc, char* argv[]) {
    static bool primeiro_desenho = true;
    static bool ultimo_estado_foco = false;
    int game_tick_timer = 0;

    graphics_init_app(winWidth, winHeight);
    wm_init();
    
    my_app_slot = OS_IPC_RegisterApp("SnakeGame Premium Audio", winWidth, winHeight);
    if (my_app_slot == -1) return -1; 
    
    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "LBF Snake V5 - Audio Sync", winWidth, winHeight);

    if (MyApp.MainWindow) {
        gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0x000000); 
    }
   
    Init_Audio_SFX();
    Reset_Full_Game();
    Flush_Grafico_Janela();

    while(1) {
        if (IPC_WINDOW_LIST[my_app_slot].is_active == 0) {
            app_closing = true;
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
            if (MyApp.MainWindow) {
                ((TForm*)MyApp.MainWindow)->ActiveFocus = euTenhoFoco;
            }
            precisa_redesenhar = true;
        }

        if (euTenhoFoco) {
            // Atualiza a trava de áudio dinamicamente
            if (audio_lock_frames > 0) {
                audio_lock_frames--;
            }

            char key = Obter_Tecla_Entrada();
            if (key != 0 && !app_closing) {
                if ((key == 'w' || key == 'W' || key == '8') && dir_y == 0 && !dir_changed) { 
                    dir_x = 0; dir_y = -1; dir_changed = true; 
                    play_move_sound();
                }
                else if ((key == 's' || key == 'S' || key == '2') && dir_y == 0 && !dir_changed) { 
                    dir_x = 0; dir_y = 1; dir_changed = true; 
                    play_move_sound();
                }
                else if ((key == 'a' || key == 'A' || key == '4') && dir_x == 0 && !dir_changed) { 
                    dir_x = -1; dir_y = 0; dir_changed = true; 
                    play_move_sound();
                }
                else if ((key == 'd' || key == 'D' || key == '6') && dir_x == 0 && !dir_changed) { 
                    dir_x = 1; dir_y = 0; dir_changed = true; 
                    play_move_sound();
                }
                else if (key == ' ' || key == '5') {
                    if (game_won) {
                        if (fase < 3) play_sound(sfx_levelup, sizeof(sfx_levelup), true, 25);
                        Next_Fase();
                    }
                }
                else if (key == 'r' || key == 'R') {
                    Reset_Full_Game();
                }
                precisa_redesenhar = true; 
            }

            if (!game_over && !game_won) {
                game_tick_timer += 16;
                if (game_tick_timer >= speed_threshold) {
                    game_tick_timer = 0;
                    Update_Game();
                    precisa_redesenhar = true;
                }
            }
        }

        if (precisa_redesenhar) {
            Flush_Grafico_Janela();
        }
        
        sys_sleep(16);
    }

    sys_exit(); 
    return 0;
}
