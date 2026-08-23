#include "sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"

// Componentes de IPC e Comunicação
#include "components/TOS_IPC.h"     
#include "components/TOSSerial.h"   

#define M_PI 3.14159265358979323846f

// Protótipos obrigatórios da GUI
void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;

// Dimensões da Janela e Display CHIP-8
int my_app_slot = -1;
TGUIEnvironment MyApp;
const int winWidth  = 360;   
const int winHeight = 310;

#define CHIP8_WIDTH  64
#define CHIP8_HEIGHT 32
#define CHIP8_SCALE  5
#define OFFSET_X     20
#define OFFSET_Y     40

// Ponteiros dos Controles RAD
TGUIControl* EditPath = NULL;
TGUIControl* BtnAbrir = NULL;

// Estados de Diagnóstico do Emulador
static bool rom_carregada = false;
static int status_sys_open = 0; // 0: Inicial, 1: Sucesso, -1: Erro sys_open, -2: Erro sys_read

/* ============================================================================
 * GERADOR DE NÚMEROS PSEUDO-ALEATÓRIOS
 * ============================================================================ */
static uint32_t g_rand_seed = 0x87654321;

static int custom_rand(void) {
    g_rand_seed = g_rand_seed * 1103515245 + 12345;
    return (int)((g_rand_seed / 65536) & 0x7FFF);
}

/* ============================================================================
 * GERADOR DE ÁUDIO DE BEEP
 * ============================================================================ */
#define SFX_BEEP_SAMPLES 4000
static int16_t sfx_beep[SFX_BEEP_SAMPLES * 2] __attribute__((aligned(4096)));

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
    float freq = 520.0f;
    float rad_step = (2.0f * M_PI * freq) / sample_rate;

    for (int i = 0; i < SFX_BEEP_SAMPLES; i++) {
        int16_t val = (int16_t)(custom_sinf(i * rad_step) * 12000.0f);
        sfx_beep[i * 2]     = val;
        sfx_beep[i * 2 + 1] = val;
    }
}

static inline void play_beep(void) {
    sys_audio_play(sfx_beep, sizeof(sfx_beep), 0);
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
 * ESTRUTURA DO CHIP-8
 * ============================================================================ */
typedef struct {
    uint8_t  display[CHIP8_WIDTH * CHIP8_HEIGHT];
    uint8_t  memory[4096];
    uint8_t  regs[16];
    uint16_t reg_I;
    uint8_t  DT;
    uint8_t  ST;
    bool     keypad[16];
    bool     waiting_for_keypress;
    uint8_t  waiting_for_keypress_reg;
    uint16_t pc;
    uint8_t  sp;
    uint16_t stack[16];
} Chip8;

static Chip8 chip;

static const uint8_t chip8_fontset[80] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
    0x20, 0x60, 0x20, 0x20, 0x70, // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

void Chip8_Init(void) {
    memset(&chip, 0, sizeof(Chip8));

    for (int i = 0; i < 80; i++) {
        chip.memory[i] = chip8_fontset[i];
    }

    chip.pc = 0x200;
}

void Chip8_Cycle(void) {
    if (chip.waiting_for_keypress) return;

    uint16_t op = (chip.memory[chip.pc] << 8) | chip.memory[chip.pc + 1];

    uint16_t arg_nnn = op & 0x0FFF;
    uint8_t  arg_kk  = op & 0x00FF;
    uint8_t  arg_x   = (op & 0x0F00) >> 8;
    uint8_t  arg_y   = (op & 0x00F0) >> 4;
    uint8_t  arg_n   = (op & 0x000F);

    chip.pc += 2;

    if (op == 0x00E0) {
        memset(chip.display, 0, sizeof(chip.display));
    } else if (op == 0x00EE) {
        chip.sp--;
        chip.pc = chip.stack[chip.sp];
    } else if ((op & 0xF000) == 0x1000) {
        chip.pc = arg_nnn;
    } else if ((op & 0xF000) == 0x2000) {
        chip.stack[chip.sp] = chip.pc;
        chip.sp++;
        chip.pc = arg_nnn;
    } else if ((op & 0xF000) == 0x3000) {
        if (chip.regs[arg_x] == arg_kk) chip.pc += 2;
    } else if ((op & 0xF000) == 0x4000) {
        if (chip.regs[arg_x] != arg_kk) chip.pc += 2;
    } else if ((op & 0xF000) == 0x5000) {
        if (chip.regs[arg_x] == chip.regs[arg_y]) chip.pc += 2;
    } else if ((op & 0xF000) == 0x6000) {
        chip.regs[arg_x] = arg_kk;
    } else if ((op & 0xF000) == 0x7000) {
        chip.regs[arg_x] += arg_kk;
    } else if ((op & 0xF00F) == 0x8000) {
        chip.regs[arg_x] = chip.regs[arg_y];
    } else if ((op & 0xF00F) == 0x8001) {
        chip.regs[arg_x] |= chip.regs[arg_y];
    } else if ((op & 0xF00F) == 0x8002) {
        chip.regs[arg_x] &= chip.regs[arg_y];
    } else if ((op & 0xF00F) == 0x8003) {
        chip.regs[arg_x] ^= chip.regs[arg_y];
    } else if ((op & 0xF00F) == 0x8004) {
        uint16_t sum = chip.regs[arg_x] + chip.regs[arg_y];
        chip.regs[0xF] = (sum > 255) ? 1 : 0;
        chip.regs[arg_x] = sum & 0xFF;
    } else if ((op & 0xF00F) == 0x8005) {
        chip.regs[0xF] = (chip.regs[arg_x] > chip.regs[arg_y]) ? 1 : 0;
        chip.regs[arg_x] -= chip.regs[arg_y];
    } else if ((op & 0xF00F) == 0x8006) {
        chip.regs[0xF] = chip.regs[arg_x] & 0x1;
        chip.regs[arg_x] >>= 1;
    } else if ((op & 0xF00F) == 0x8007) {
        chip.regs[0xF] = (chip.regs[arg_y] > chip.regs[arg_x]) ? 1 : 0;
        chip.regs[arg_x] = chip.regs[arg_y] - chip.regs[arg_x];
    } else if ((op & 0xF00F) == 0x800E) {
        chip.regs[0xF] = (chip.regs[arg_x] & 0x80) ? 1 : 0;
        chip.regs[arg_x] <<= 1;
    } else if ((op & 0xF00F) == 0x9000) {
        if (chip.regs[arg_x] != chip.regs[arg_y]) chip.pc += 2;
    } else if ((op & 0xF000) == 0xA000) {
        chip.reg_I = arg_nnn;
    } else if ((op & 0xF000) == 0xB000) {
        chip.pc = chip.regs[0] + arg_nnn;
    } else if ((op & 0xF000) == 0xC000) {
        chip.regs[arg_x] = (custom_rand() & 0xFF) & arg_kk;
    } else if ((op & 0xF000) == 0xD000) {
        uint8_t x_loc = chip.regs[arg_x] % CHIP8_WIDTH;
        uint8_t y_loc = chip.regs[arg_y] % CHIP8_HEIGHT;
        chip.regs[0xF] = 0;

        for (int yline = 0; yline < arg_n; yline++) {
            if (y_loc + yline >= CHIP8_HEIGHT) break;
            uint8_t pixel = chip.memory[chip.reg_I + yline];

            for (int xline = 0; xline < 8; xline++) {
                if (x_loc + xline >= CHIP8_WIDTH) break;

                if ((pixel & (0x80 >> xline)) != 0) {
                    int idx = (y_loc + yline) * CHIP8_WIDTH + (x_loc + xline);
                    if (chip.display[idx] == 1) {
                        chip.regs[0xF] = 1;
                    }
                    chip.display[idx] ^= 1;
                }
            }
        }
    } else if ((op & 0xF0FF) == 0xE09E) {
        if (chip.keypad[chip.regs[arg_x] & 0xF]) chip.pc += 2;
    } else if ((op & 0xF0FF) == 0xE0A1) {
        if (!chip.keypad[chip.regs[arg_x] & 0xF]) chip.pc += 2;
    } else if ((op & 0xF0FF) == 0xF007) {
        chip.regs[arg_x] = chip.DT;
    } else if ((op & 0xF0FF) == 0xF00A) {
        chip.waiting_for_keypress = true;
        chip.waiting_for_keypress_reg = arg_x;
    } else if ((op & 0xF0FF) == 0xF015) {
        chip.DT = chip.regs[arg_x];
    } else if ((op & 0xF0FF) == 0xF018) {
        chip.ST = chip.regs[arg_x];
    } else if ((op & 0xF0FF) == 0xF01E) {
        chip.reg_I += chip.regs[arg_x];
    } else if ((op & 0xF0FF) == 0xF029) {
        chip.reg_I = (chip.regs[arg_x] & 0xF) * 5;
    } else if ((op & 0xF0FF) == 0xF033) {
        chip.memory[chip.reg_I]     = chip.regs[arg_x] / 100;
        chip.memory[chip.reg_I + 1] = (chip.regs[arg_x] / 10) % 10;
        chip.memory[chip.reg_I + 2] = chip.regs[arg_x] % 10;
    } else if ((op & 0xF0FF) == 0xF055) {
        for (int i = 0; i <= arg_x; i++) {
            chip.memory[chip.reg_I + i] = chip.regs[i];
        }
    } else if ((op & 0xF0FF) == 0xF065) {
        for (int i = 0; i <= arg_x; i++) {
            chip.regs[i] = chip.memory[chip.reg_I + i];
        }
    }
}

void Chip8_UpdateTimers(void) {
    if (chip.DT > 0) chip.DT--;
    if (chip.ST > 0) {
        play_beep();
        chip.ST--;
    }
}

void Process_KeyInput(char key) {
    g_rand_seed ^= (uint32_t)key;

    int key_idx = -1;
    switch (key) {
        case '1': key_idx = 0x1; break;
        case '2': key_idx = 0x2; break;
        case '3': key_idx = 0x3; break;
        case '4': key_idx = 0xC; break;
        case 'q': case 'Q': key_idx = 0x4; break;
        case 'w': case 'W': key_idx = 0x5; break;
        case 'e': case 'E': key_idx = 0x6; break;
        case 'r': case 'R': key_idx = 0xD; break;
        case 'a': case 'A': key_idx = 0x7; break;
        case 's': case 'S': key_idx = 0x8; break;
        case 'd': case 'D': key_idx = 0x9; break;
        case 'f': case 'F': key_idx = 0xE; break;
        case 'z': case 'Z': key_idx = 0xA; break;
        case 'x': case 'X': key_idx = 0x0; break;
        case 'c': case 'C': key_idx = 0xB; break;
        case 'v': case 'V': key_idx = 0xF; break;
    }

    if (key_idx != -1) {
        if (chip.waiting_for_keypress) {
            chip.waiting_for_keypress = false;
            chip.regs[chip.waiting_for_keypress_reg] = (uint8_t)key_idx;
        }
        chip.keypad[key_idx] = true;
    }
}

/* ============================================================================
 * CALLBACK DO BOTÃO: ABRIR ROM COM TRATAMENTO API DIRETA FAT32
 * ============================================================================ */
void OnBtnAbrirClick(void* sender) {
    char* caminho_rom = GUI_Edit_GetText(EditPath);
    if (!caminho_rom || caminho_rom[0] == '\0') return;

    Chip8_Init();

    int bytes_lidos = sys_fat_read(caminho_rom, &chip.memory[0x200], 4096 - 0x200);

    if (bytes_lidos > 0) {
        chip.pc = 0x200;
        rom_carregada = true;
        status_sys_open = 1; // Sucesso
        
        // Remove o foco do Edit e passa para o jogo
        g_focused_control = NULL;
        if (EditPath) {
            gui_set_prop(EditPath, PROP_SET_FOCUS, 0);
            gui_set_prop(EditPath, PROP_STATE, 0); 
        }
        
        if (MyApp.MainWindow) {
            ((TForm*)MyApp.MainWindow)->ActiveFocus = true;
        }
    } else {
        rom_carregada = false;
        status_sys_open = -2;
    }
}

/* ============================================================================
 * RENDERIZAÇÃO E HUD DE DIAGNÓSTICO
 * ============================================================================ */
void Render_Game(void) {
    uint32_t* buf = (uint32_t*)graphics_get_buffer();
    if (!buf) return;

    graphics_fill_rect(OFFSET_X, OFFSET_Y, CHIP8_WIDTH * CHIP8_SCALE, CHIP8_HEIGHT * CHIP8_SCALE, 0x000000);
    graphics_draw_rect(OFFSET_X - 2, OFFSET_Y - 2, (CHIP8_WIDTH * CHIP8_SCALE) + 4, (CHIP8_HEIGHT * CHIP8_SCALE) + 4, 0x555555);

    if (rom_carregada) {
        for (int y = 0; y < CHIP8_HEIGHT; y++) {
            for (int x = 0; x < CHIP8_WIDTH; x++) {
                if (chip.display[y * CHIP8_WIDTH + x] > 0) {
                    int px = OFFSET_X + (x * CHIP8_SCALE);
                    int py = OFFSET_Y + (y * CHIP8_SCALE);
                    graphics_fill_rect(px, py, CHIP8_SCALE, CHIP8_SCALE, 0x00FF66);
                }
            }
        }
    }

    char strBuf[32];
    int hudY = OFFSET_Y + (CHIP8_HEIGHT * CHIP8_SCALE) + 48;

    sys_draw_string(OFFSET_X, hudY, "CHIP-8 OS EMULATOR", 0x00FFFF, 1);

    if (status_sys_open == -1) {
        sys_draw_string(OFFSET_X, hudY + 12, "[ERRO: FALHA AO ABRIR (SYS_OPEN)]", 0xFF0000, 1);
    } else if (status_sys_open == -2) {
        sys_draw_string(OFFSET_X, hudY + 12, "[ERRO: ROM VAZIA OU ERRO READ]", 0xFF5500, 1);
    } else if (!rom_carregada) {
        sys_draw_string(OFFSET_X, hudY + 12, "[CARREGUE UMA ROM PARA JOGAR]", 0xAAAAAA, 1);
    } else if (g_focused_control == (void*)EditPath) {
        sys_draw_string(OFFSET_X, hudY + 12, "[MODO: EDITAR CAMINHO (PAUSADO)]", 0xFF8800, 1);
    } else {
        sys_draw_string(OFFSET_X, hudY + 12, "[MODO: JOGO ATIVO]", 0x00FF00, 1);
    }

    sys_draw_string(OFFSET_X + 230, hudY, "PC:", 0xAAAAAA, 1);
    itoa(chip.pc, strBuf, 16);
    sys_draw_string(OFFSET_X + 260, hudY, strBuf, 0xFFFF00, 1);
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
 * MAIN LOOP
 * ============================================================================ */
int main(int argc, char* argv[]) {
    static int ultimo_x = 0;
    static int ultimo_y = 0;
    static int mouse_hold_timer = 0;

    static bool primeiro_desenho = true;
    static bool ultimo_estado_foco = false;
    void* ultimo_controle_focado = NULL;

    graphics_init_app(winWidth, winHeight);
    wm_init();

    my_app_slot = OS_IPC_RegisterApp("CHIP-8 Emulator", winWidth, winHeight);
    if (my_app_slot == -1) return -1;

    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "Chip-8 - TOS Graphics", winWidth, winHeight);

    if (MyApp.MainWindow) {
        gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0x111111);
    }

    int ctrlY = OFFSET_Y + (CHIP8_HEIGHT * CHIP8_SCALE) + 10;
    
    // 1. Cria o Edit vazio (igual ao tarefa.c) e injeta o texto via SetText logo em seguida
    EditPath = GUI_CreateEdit(&MyApp, OFFSET_X, ctrlY, 210, 28, "", NULL);
    GUI_Edit_SetText(EditPath, "0:/INVADERS.CH8");

    BtnAbrir = GUI_CreateButton(&MyApp, OFFSET_X + 220, ctrlY, 100, 28, "ABRIR ROM", OnBtnAbrirClick);

    // Foco inicial no Edit
    g_focused_control = (void*)EditPath;
    ultimo_controle_focado = (void*)EditPath;
    gui_set_prop(EditPath, PROP_SET_FOCUS, 1);

    Init_Audio_SFX();
    Chip8_Init();
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

        // --- SISTEMA DE MANUTENÇÃO DE FOCO (IGUAL TAREFA.C) ---
        if (g_focused_control != NULL) {
            ultimo_controle_focado = g_focused_control;
        } else if (ultimo_controle_focado != NULL && g_focused_control == NULL && !rom_carregada) {
            g_focused_control = ultimo_controle_focado;
            gui_set_prop((TGUIControl*)ultimo_controle_focado, PROP_SET_FOCUS, 1);
        }

        // --- GERENCIAMENTO DE MODO E TECLADO ---
        if (euTenhoFoco) {
            static int key_hold[16] = {0};
            bool modo_edicao = (g_focused_control == (void*)EditPath);
            char key = Obter_Tecla_Entrada();

            if (key != 0) {
                if (modo_edicao) {
                    // Delega inteiramente para a SDK gerenciar a digitação e backspace
                    GUI_ProcessKeyboard(&MyApp, key);
                } else if (rom_carregada) {
                    Process_KeyInput(key);
                    for (int k = 0; k < 16; k++) {
                        if (chip.keypad[k]) key_hold[k] = 6;
                    }
                }
                precisa_redesenhar = true;
            }

            if (!modo_edicao && rom_carregada) {
                for (int k = 0; k < 16; k++) {
                    if (key_hold[k] > 0) {
                        chip.keypad[k] = true;
                        key_hold[k]--;
                    } else {
                        chip.keypad[k] = false;
                    }
                }

                for (int i = 0; i < 10; i++) {
                    Chip8_Cycle();
                }

                Chip8_UpdateTimers();
                precisa_redesenhar = true;
            }
        }

        // --- ROTEAMENTO DE EVENTOS DE MOUSE (CORRIGIDO) ---
        if (IPC_WINDOW_LIST[my_app_slot].has_click_event == 1) {
            if (mouse_hold_timer == 0) {
                int rel_x = IPC_WINDOW_LIST[my_app_slot].local_click_x;
                int rel_y = IPC_WINDOW_LIST[my_app_slot].local_click_y;
                ultimo_x = rel_x;
                ultimo_y = rel_y;
                mouse_hold_timer = 2;

                if (BtnAbrir && rel_x >= BtnAbrir->Left && rel_x < (BtnAbrir->Left + BtnAbrir->Width) &&
                    rel_y >= BtnAbrir->Top && rel_y < (BtnAbrir->Top + BtnAbrir->Height)) {
                    gui_set_prop(BtnAbrir, PROP_STATE, 2);
                }

                events_process_mouse(rel_x, rel_y, 1, 0);

                // 1. Deixa a SDK processar o clique primeiro
                GUI_ProcessMouseClick(&MyApp, rel_x, rel_y);

                // 2. FORÇA O FOCO NO EDIT POR ÚLTIMO (para anular qualquer reset da SDK)
                if (EditPath && rel_x >= EditPath->Left && rel_x < (EditPath->Left + EditPath->Width) &&
                    rel_y >= EditPath->Top && rel_y < (EditPath->Top + EditPath->Height)) {
                    g_focused_control = (void*)EditPath;
                    ultimo_controle_focado = (void*)EditPath;
                    gui_set_prop(EditPath, PROP_SET_FOCUS, 1);
                }

                precisa_redesenhar = true;
            }
            IPC_WINDOW_LIST[my_app_slot].has_click_event = 0;
        }

        if (mouse_hold_timer > 0) {
            mouse_hold_timer--;
            if (mouse_hold_timer == 0) {
                if (BtnAbrir) gui_set_prop(BtnAbrir, PROP_STATE, 0);
                events_process_mouse(ultimo_x, ultimo_y, 0, 0);
                precisa_redesenhar = true;
            }
        }

        if (precisa_redesenhar) Flush_Grafico_Janela();

        sys_sleep(16);
    }

    sys_exit();
    return 0;
}
