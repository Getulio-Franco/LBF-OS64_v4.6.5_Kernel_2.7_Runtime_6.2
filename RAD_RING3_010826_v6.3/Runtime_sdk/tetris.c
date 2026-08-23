#include "sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"

// Componentes do Sistema encapsulados
#include "components/TOS_IPC.h"     
#include "components/TOSSerial.h"   

#define M_PI 3.14159265358979323846f

// Protótipos obrigatórios da GUI
void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;

// Variáveis da Janela (Largura ajustada para ser proporcional)
int my_app_slot = -1;
TGUIEnvironment MyApp;
const int winWidth = 360;   // Reduzido de 550 para 360
const int winHeight = 440;  // Leve ajuste na altura

/* ============================================================================
 * GERADOR DE NÚMEROS PSEUDO-ALEATÓRIOS (FREESTANDING PRNG)
 * ============================================================================ */
static uint32_t g_rand_seed = 0xA5A5A5A5;

static int custom_rand(void) {
    g_rand_seed = g_rand_seed * 1103515245 + 12345;
    return (int)((g_rand_seed / 65536) & 0x7FFF);
}

/* ============================================================================
 * SINTETIZADOR DE EFEITOS SONOROS (SFX)
 * ============================================================================ */
#define SFX_SHORT_SAMPLES 5000
#define SFX_MEDIUM_SAMPLES 12000
#define SFX_LONG_SAMPLES  20000

static int16_t sfx_move[SFX_SHORT_SAMPLES * 2] __attribute__((aligned(4096)));
static int16_t sfx_drop[SFX_SHORT_SAMPLES * 2] __attribute__((aligned(4096)));
static int16_t sfx_clear[SFX_MEDIUM_SAMPLES * 2] __attribute__((aligned(4096)));
static int16_t sfx_gameover[SFX_LONG_SAMPLES * 2] __attribute__((aligned(4096)));

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

    // 1. Som de Movimento / Rotação (Tique Agudo)
    for (int i = 0; i < SFX_SHORT_SAMPLES; i++) {
        float freq = 800.0f - (400.0f * ((float)i / SFX_SHORT_SAMPLES));
        float rad_step = (2.0f * M_PI * freq) / sample_rate;
        float envelope = 1.0f - ((float)i / SFX_SHORT_SAMPLES);
        int16_t val = (int16_t)(custom_sinf(i * rad_step) * 10000.0f * envelope);
        sfx_move[i*2] = val; sfx_move[i*2+1] = val;
    }

    // 2. Som de Encaixe / Drop (Grave e Curto)
    for (int i = 0; i < SFX_SHORT_SAMPLES; i++) {
        float freq = 300.0f - (200.0f * ((float)i / SFX_SHORT_SAMPLES));
        float rad_step = (2.0f * M_PI * freq) / sample_rate;
        float envelope = 1.0f - ((float)i / SFX_SHORT_SAMPLES);
        int16_t val = (int16_t)(custom_sinf(i * rad_step) * 14000.0f * envelope);
        sfx_drop[i*2] = val; sfx_drop[i*2+1] = val;
    }

    // 3. Som de Linha Completa (Sweep Ascendente de Vitória)
    for (int i = 0; i < SFX_MEDIUM_SAMPLES; i++) {
        float freq = 400.0f + (800.0f * ((float)i / SFX_MEDIUM_SAMPLES));
        float rad_step = (2.0f * M_PI * freq) / sample_rate;
        float envelope = 1.0f - ((float)i / SFX_MEDIUM_SAMPLES);
        int16_t val = (int16_t)(custom_sinf(i * rad_step) * 15000.0f * envelope);
        sfx_clear[i*2] = val; sfx_clear[i*2+1] = val;
    }

    // 4. Som de Game Over (Frequência em Queda)
    for (int i = 0; i < SFX_LONG_SAMPLES; i++) {
        float freq = 500.0f - (450.0f * ((float)i / SFX_LONG_SAMPLES));
        if (freq < 30.0f) freq = 30.0f;
        float rad_step = (2.0f * M_PI * freq) / sample_rate;
        float envelope = 1.0f - ((float)i / SFX_LONG_SAMPLES);
        int16_t val = (int16_t)(custom_sinf(i * rad_step) * 18000.0f * envelope);
        sfx_gameover[i*2] = val; sfx_gameover[i*2+1] = val;
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
 * ESTRUTURA AUXILIAR IPC DE TECLADO
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
 * TETRIS - CONSTANTES, ESTRUTURAS E LÓGICA DO JOGO
 * ============================================================================ */
#define A_WIDTH  10  // Largura da Arena (colunas)
#define A_HEIGHT 20  // Altura da Arena (linhas)
#define T_WIDTH  4   // Dimensão da matriz do Tetromino
#define T_HEIGHT 4

#define BLOCK_SIZE 18  // Tamanho do bloco em pixels
#define OFFSET_X   30  // Margem esquerda otimizada (era 40)
#define OFFSET_Y   45  // Margem superior otimizada (era 50)

// Matrizes dos 7 Tetrominos (I, O, S, Z, T, L, J)
const int tetrominoes[7][16] = {
    {0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0},  // I (Cyan)
    {0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0},  // O (Amarelo)
    {0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0},  // S (Verde)
    {0, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0},  // Z (Vermelho)
    {0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0},  // T (Roxo)
    {0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0},  // L (Laranja)
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 0, 0, 0}   // J (Azul)
};

// Cores dos Tetrominos
const uint32_t piece_colors[8] = {
    0x000000,  // 0: Vazio
    0x00FFFF,  // 1: I - Cyan
    0xFFFF00,  // 2: O - Amarelo
    0x00FF00,  // 3: S - Verde
    0xFF0000,  // 4: Z - Vermelho
    0xAA00FF,  // 5: T - Roxo
    0xFF8800,  // 6: L - Laranja
    0x0044FF   // 7: J - Azul
};

// Arena guarda 0 para vazio ou o ID da cor (1 a 7)
static int arena[A_HEIGHT][A_WIDTH];

static uint32_t score = 0;
static uint32_t lines_cleared = 0;
static uint32_t level = 1;
static bool gameOver = false;

static int currTetrominoIdx;
static int nextTetrominoIdx;
static int currRotation = 0;
static int currX = A_WIDTH / 2 - 2;
static int currY = 0;

static int drop_timer = 0;
static int drop_speed = 20;  // Ticks antes de cair uma linha

// Protótipos das funções do Tetris
int rotate(int x, int y, int rotation);
bool validPos(int tetromino, int rotation, int posX, int posY);
void newTetromino(void);
bool moveDown(void);
void addToArena(void);
void checkLines(void);
void Reset_Game(void);

int rotate(int x, int y, int rotation) {
    switch (rotation % 4) {
    case 0: return x + y * T_WIDTH;
    case 1: return 12 + y - (x * T_WIDTH);
    case 2: return 15 - (y * T_WIDTH) - x;
    case 3: return 3 - y + (x * T_WIDTH);
    default: return 0;
    }
}

bool validPos(int tetromino, int rotation, int posX, int posY) {
    for (int x = 0; x < T_WIDTH; x++) {
        for (int y = 0; y < T_HEIGHT; y++) {
            int index = rotate(x, y, rotation);
            if (1 != tetrominoes[tetromino][index]) continue;

            int arenaX = x + posX;
            int arenaY = y + posY;

            if (arenaX < 0 || arenaX >= A_WIDTH || arenaY >= A_HEIGHT) {
                return false;
            }

            if (arenaY >= 0 && arena[arenaY][arenaX] != 0) {
                return false;
            }
        }
    }
    return true;
}

void newTetromino(void) {
    currTetrominoIdx = nextTetrominoIdx;
    nextTetrominoIdx = custom_rand() % 7;
    currRotation = 0;
    currX = (A_WIDTH / 2) - (T_WIDTH / 2);
    currY = 0;

    if (!validPos(currTetrominoIdx, currRotation, currX, currY)) {
        gameOver = true;
        play_sound(sfx_gameover, sizeof(sfx_gameover), true);
    }
}

bool moveDown(void) {
    if (validPos(currTetrominoIdx, currRotation, currX, currY + 1)) {
        currY++;
        return true;
    }
    return false;
}

void addToArena(void) {
    for (int y = 0; y < T_HEIGHT; y++) {
        for (int x = 0; x < T_WIDTH; x++) {
            int index = rotate(x, y, currRotation);
            if (1 != tetrominoes[currTetrominoIdx][index]) continue;

            int arenaX = currX + x;
            int arenaY = currY + y;
            if (arenaX >= 0 && arenaX < A_WIDTH && arenaY >= 0 && arenaY < A_HEIGHT) {
                arena[arenaY][arenaX] = currTetrominoIdx + 1;
            }
        }
    }
    play_sound(sfx_drop, sizeof(sfx_drop), false);
}

void checkLines(void) {
    int clearedInThisStep = 0;

    for (int y = A_HEIGHT - 1; y >= 0; y--) {
        bool lineFull = true;
        for (int x = 0; x < A_WIDTH; x++) {
            if (arena[y][x] == 0) {
                lineFull = false;
                break;
            }
        }

        if (!lineFull) continue;

        clearedInThisStep++;
        for (int yy = y; yy > 0; yy--) {
            for (int xx = 0; xx < A_WIDTH; xx++) {
                arena[yy][xx] = arena[yy - 1][xx];
            }
        }

        for (int xx = 0; xx < A_WIDTH; xx++) {
            arena[0][xx] = 0;
        }
        y++; 
    }

    if (clearedInThisStep > 0) {
        lines_cleared += clearedInThisStep;
        score += clearedInThisStep * 100 * level;
        level = (lines_cleared / 10) + 1;
        
        drop_speed = 20 - (level * 2);
        if (drop_speed < 3) drop_speed = 3;

        play_sound(sfx_clear, sizeof(sfx_clear), false);
    }
}

void Reset_Game(void) {
    memset(arena, 0, sizeof(arena));
    score = 0;
    lines_cleared = 0;
    level = 1;
    drop_speed = 20;
    drop_timer = 0;
    gameOver = false;
    sound_cooldown = 0;
    sound_busy = false;

    nextTetrominoIdx = custom_rand() % 7;
    newTetromino();
    sys_audio_stop();
}

/* ============================================================================
 * ATUALIZAÇÃO E RENDERIZAÇÃO
 * ============================================================================ */
void Update_Game(void) {
    if (gameOver) return;

    if (sound_cooldown > 0) sound_cooldown--;
    update_audio_state();

    drop_timer++;
    if (drop_timer >= drop_speed) {
        drop_timer = 0;
        if (!moveDown()) {
            addToArena();
            checkLines();
            newTetromino();
        }
    }
}

void Render_Game(void) {
    uint32_t* buf = (uint32_t*)graphics_get_buffer();
    if (!buf) return;

    // Fundo da Arena
    graphics_fill_rect(OFFSET_X, OFFSET_Y, A_WIDTH * BLOCK_SIZE, A_HEIGHT * BLOCK_SIZE, 0x111111);
    graphics_draw_rect(OFFSET_X - 2, OFFSET_Y - 2, (A_WIDTH * BLOCK_SIZE) + 4, (A_HEIGHT * BLOCK_SIZE) + 4, 0x555555);

    // Desenhar Blocos Fixos da Arena
    for (int y = 0; y < A_HEIGHT; y++) {
        for (int x = 0; x < A_WIDTH; x++) {
            int colorIdx = arena[y][x];
            if (colorIdx > 0) {
                int px = OFFSET_X + (x * BLOCK_SIZE);
                int py = OFFSET_Y + (y * BLOCK_SIZE);
                graphics_fill_rect(px + 1, py + 1, BLOCK_SIZE - 2, BLOCK_SIZE - 2, piece_colors[colorIdx]);
            }
        }
    }

    // Desenhar Tetromino Atual em Queda
    if (!gameOver) {
        for (int y = 0; y < T_HEIGHT; y++) {
            for (int x = 0; x < T_WIDTH; x++) {
                int index = rotate(x, y, currRotation);
                if (1 == tetrominoes[currTetrominoIdx][index]) {
                    int arenaX = currX + x;
                    int arenaY = currY + y;
                    if (arenaX >= 0 && arenaX < A_WIDTH && arenaY >= 0 && arenaY < A_HEIGHT) {
                        int px = OFFSET_X + (arenaX * BLOCK_SIZE);
                        int py = OFFSET_Y + (arenaY * BLOCK_SIZE);
                        graphics_fill_rect(px + 1, py + 1, BLOCK_SIZE - 2, BLOCK_SIZE - 2, piece_colors[currTetrominoIdx + 1]);
                    }
                }
            }
        }
    }

    // Painel Lateral (HUD) - Posicionado logo ao lado da arena
    int hudX = OFFSET_X + (A_WIDTH * BLOCK_SIZE) + 25;
    char strBuf[32];

    sys_draw_string(hudX, OFFSET_Y, "TETRIS OS", 0x00FFFF, 1);

    sys_draw_string(hudX, OFFSET_Y + 35, "SCORE:", 0xFFFFFF, 1);
    itoa(score, strBuf, 10);
    sys_draw_string(hudX, OFFSET_Y + 50, strBuf, 0xFFFF00, 1);

    sys_draw_string(hudX, OFFSET_Y + 75, "LINHAS:", 0xFFFFFF, 1);
    itoa(lines_cleared, strBuf, 10);
    sys_draw_string(hudX, OFFSET_Y + 90, strBuf, 0x00FF00, 1);

    sys_draw_string(hudX, OFFSET_Y + 115, "NIVEL:", 0xFFFFFF, 1);
    itoa(level, strBuf, 10);
    sys_draw_string(hudX, OFFSET_Y + 130, strBuf, 0xFF8800, 1);

    // Visualização da Próxima Peça
    sys_draw_string(hudX, OFFSET_Y + 160, "PROXIMA:", 0xAAAAAA, 1);
    graphics_fill_rect(hudX, OFFSET_Y + 180, 4 * BLOCK_SIZE, 4 * BLOCK_SIZE, 0x1A1A1A);
    graphics_draw_rect(hudX - 1, OFFSET_Y + 179, (4 * BLOCK_SIZE) + 2, (4 * BLOCK_SIZE) + 2, 0x333333);

    for (int y = 0; y < T_HEIGHT; y++) {
        for (int x = 0; x < T_WIDTH; x++) {
            int index = rotate(x, y, 0);
            if (1 == tetrominoes[nextTetrominoIdx][index]) {
                int px = hudX + (x * BLOCK_SIZE);
                int py = OFFSET_Y + 180 + (y * BLOCK_SIZE);
                graphics_fill_rect(px + 1, py + 1, BLOCK_SIZE - 2, BLOCK_SIZE - 2, piece_colors[nextTetrominoIdx + 1]);
            }
        }
    }

    // Overlay de Game Over
    if (gameOver) {
        graphics_fill_rect(OFFSET_X - 5, OFFSET_Y + 130, 220, 90, 0x440000);
        graphics_draw_rect(OFFSET_X - 5, OFFSET_Y + 130, 220, 90, 0xFF0000);
        sys_draw_string(OFFSET_X + 50, OFFSET_Y + 150, "GAME OVER!", 0xFFFFFF, 1);
        sys_draw_string(OFFSET_X + 10, OFFSET_Y + 180, "Pressione 'R' p/ reiniciar", 0xFFFF00, 1);
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

    graphics_init_app(winWidth, winHeight);
    wm_init();

    my_app_slot = OS_IPC_RegisterApp("Tetris TOS App", winWidth, winHeight);
    if (my_app_slot == -1) return -1;

    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "Tetris - TOS Graphics", winWidth, winHeight);

    if (MyApp.MainWindow) {
        gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0x000000);
    }

    Init_Audio_SFX();
    Reset_Game();
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
                // Mistura a entrada do usuário para variar a semente do gerador
                g_rand_seed ^= (uint32_t)key;

                if (key == 'a' || key == 'A' || key == '4') {  // Esquerda
                    if (!gameOver && validPos(currTetrominoIdx, currRotation, currX - 1, currY)) {
                        currX--;
                        play_sound(sfx_move, sizeof(sfx_move), false);
                    }
                } else if (key == 'd' || key == 'D' || key == '6') {  // Direita
                    if (!gameOver && validPos(currTetrominoIdx, currRotation, currX + 1, currY)) {
                        currX++;
                        play_sound(sfx_move, sizeof(sfx_move), false);
                    }
                } else if (key == 's' || key == 'S' || key == '8') {  // Acelerar Queda (Soft Drop)
                    if (!gameOver) {
                        if (moveDown()) {
                            score += 1;
                        } else {
                            addToArena();
                            checkLines();
                            newTetromino();
                        }
                    }
                } else if (key == 'w' || key == 'W' || key == ' ' || key == '5') {  // Rotacionar
                    if (!gameOver) {
                        int nextRot = (currRotation + 1) % 4;
                        if (validPos(currTetrominoIdx, nextRot, currX, currY)) {
                            currRotation = nextRot;
                            play_sound(sfx_move, sizeof(sfx_move), false);
                        }
                    }
                } else if (key == 'r' || key == 'R') {  // Reiniciar
                    Reset_Game();
                }
                precisa_redesenhar = true;
            }

            Update_Game();
            precisa_redesenhar = true;
        }

        if (precisa_redesenhar) Flush_Grafico_Janela();

        sys_sleep(16);  // ~60 FPS
    }

    sys_exit();
    return 0;
}
