#include "sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"
#include "components/TOS_IPC.h"     
#include "components/TOSSerial.h"   

#define M_PI 3.14159265358979323846f

// ============================================================================
// CONFIGURAÇÕES DE ÁUDIO - VERSÃO SIMPLIFICADA
// ============================================================================
#define AUDIO_SAMPLES_COUNT 32000    // ~0.66 segundos a 48kHz
#define MAX_SOUND_CHANNELS 4         // Máximo de sons simultâneos
#define BUFFER_SIZE 4096             // Tamanho do buffer do DMA (4KB)

// ============================================================================
// BUFFERS DE ÁUDIO
// ============================================================================
static int16_t g_pcm_chime[AUDIO_SAMPLES_COUNT * 2] __attribute__((aligned(4096)));
static int16_t g_pcm_siren[AUDIO_SAMPLES_COUNT * 2] __attribute__((aligned(4096)));
static int16_t g_pcm_bass[AUDIO_SAMPLES_COUNT * 2] __attribute__((aligned(4096)));
static int16_t g_pcm_effect[AUDIO_SAMPLES_COUNT * 2] __attribute__((aligned(4096)));

// Buffer de mixagem final
static int16_t g_mix_buffer[AUDIO_SAMPLES_COUNT * 2] __attribute__((aligned(4096)));

// ============================================================================
// FUNÇÃO SENO OTIMIZADA
// ============================================================================
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
// GERADORES DE ÁUDIO
// ============================================================================

static uint32_t gerar_pcm_chime(int16_t* buffer, uint32_t samples, uint16_t amplitude) {
    uint32_t sample_rate = 48000;
    float freqs[3] = {523.25f, 659.25f, 783.99f};
    float rad_steps[3];
    float angles[3] = {0.0f, 0.0f, 0.0f};

    for(int i = 0; i < 3; i++) {
        rad_steps[i] = (2.0f * M_PI * freqs[i]) / (float)sample_rate;
    }

    for (uint32_t i = 0; i < samples; i++) {
        float envelope = 1.0f - ((float)i / (float)samples);
        float mix = 0.0f;
        for(int j = 0; j < 3; j++) {
            mix += custom_sinf(angles[j]);
            angles[j] += rad_steps[j];
            if (angles[j] > 2.0f * M_PI) angles[j] -= 2.0f * M_PI;
        }
        mix /= 3.0f;
        int16_t val = (int16_t)(mix * envelope * (float)amplitude);
        buffer[i*2] = val;
        buffer[i*2+1] = val;
    }
    return samples * 2;
}

static uint32_t gerar_pcm_siren(int16_t* buffer, uint32_t samples, uint16_t amplitude) {
    uint32_t sample_rate = 48000;
    float base_freq = 600.0f;
    float angle = 0.0f;
    float mod_angle = 0.0f;
    float mod_step = (2.0f * M_PI * 5.0f) / (float)sample_rate;

    for (uint32_t i = 0; i < samples; i++) {
        float current_freq = base_freq + (200.0f * custom_sinf(mod_angle));
        float rad_step = (2.0f * M_PI * current_freq) / (float)sample_rate;
        int16_t val = (int16_t)(custom_sinf(angle) * (float)amplitude);
        buffer[i*2] = val;
        buffer[i*2+1] = val;
        angle += rad_step;
        if (angle > 2.0f * M_PI) angle -= 2.0f * M_PI;
        mod_angle += mod_step;
        if (mod_angle > 2.0f * M_PI) mod_angle -= 2.0f * M_PI;
    }
    return samples * 2;
}

static uint32_t gerar_pcm_bass(int16_t* buffer, uint32_t samples, uint16_t amplitude) {
    uint32_t sample_rate = 48000;
    float freq = 80.0f;
    float rad_step = (2.0f * M_PI * freq) / (float)sample_rate;
    float angle = 0.0f;

    for (uint32_t i = 0; i < samples; i++) {
        float envelope = 1.0f - ((float)i / (float)samples);
        float pulse = 0.5f + 0.5f * custom_sinf(i * 0.005f);
        int16_t val = (int16_t)(custom_sinf(angle) * (float)amplitude * envelope * pulse);
        buffer[i*2] = val;
        buffer[i*2+1] = val;
        angle += rad_step;
        if (angle > 2.0f * M_PI) angle -= 2.0f * M_PI;
    }
    return samples * 2;
}

static uint32_t gerar_pcm_effect(int16_t* buffer, uint32_t samples, uint16_t amplitude) {
    uint32_t sample_rate = 48000;
    float freq = 1200.0f;
    float rad_step = (2.0f * M_PI * freq) / (float)sample_rate;
    float angle = 0.0f;
    float mod_angle = 0.0f;

    for (uint32_t i = 0; i < samples; i++) {
        float envelope = 1.0f - ((float)i / (float)samples);
        envelope = envelope * envelope;
        float modulation = 1.0f + 0.3f * custom_sinf(mod_angle);
        int16_t val = (int16_t)(custom_sinf(angle) * (float)amplitude * envelope * modulation);
        buffer[i*2] = val;
        buffer[i*2+1] = val;
        angle += rad_step;
        if (angle > 2.0f * M_PI) angle -= 2.0f * M_PI;
        mod_angle += 0.05f;
        if (mod_angle > 2.0f * M_PI) mod_angle -= 2.0f * M_PI;
    }
    return samples * 2;
}

// ============================================================================
// FUNÇÃO DE MIXAGEM - SIMPLES E DIRETA
// ============================================================================

void mix_all_sounds_and_play(void) {
    // Limpa o buffer de mixagem
    memset(g_mix_buffer, 0, AUDIO_SAMPLES_COUNT * 2 * sizeof(int16_t));
    
    // Lista de sons para mixar (todos com volume reduzido para não saturar)
    // Sino: volume 80%
    int16_t* sounds[] = {g_pcm_chime, g_pcm_siren, g_pcm_bass, g_pcm_effect};
    float volumes[] = {0.8f, 0.6f, 1.0f, 0.7f};
    int num_sounds = 4;
    
    for (int s = 0; s < num_sounds; s++) {
        int16_t* src = sounds[s];
        float vol = volumes[s];
        
        for (uint32_t i = 0; i < AUDIO_SAMPLES_COUNT * 2; i++) {
            int32_t mixed = g_mix_buffer[i] + (int32_t)(src[i] * vol);
            if (mixed > 32767) mixed = 32767;
            if (mixed < -32768) mixed = -32768;
            g_mix_buffer[i] = (int16_t)mixed;
        }
    }
}

// ============================================================================
// FUNÇÕES DE TESTE
// ============================================================================

void play_chime(void) {
    gerar_pcm_chime(g_pcm_chime, AUDIO_SAMPLES_COUNT, 16000);
    sys_audio_play(g_pcm_chime, AUDIO_SAMPLES_COUNT * 2 * sizeof(int16_t), 0);
}

void play_siren(void) {
    gerar_pcm_siren(g_pcm_siren, AUDIO_SAMPLES_COUNT, 12000);
    sys_audio_play(g_pcm_siren, AUDIO_SAMPLES_COUNT * 2 * sizeof(int16_t), 0);
}

void play_bass(void) {
    gerar_pcm_bass(g_pcm_bass, AUDIO_SAMPLES_COUNT, 18000);
    sys_audio_play(g_pcm_bass, AUDIO_SAMPLES_COUNT * 2 * sizeof(int16_t), 0);
}

void play_effect(void) {
    gerar_pcm_effect(g_pcm_effect, AUDIO_SAMPLES_COUNT, 14000);
    sys_audio_play(g_pcm_effect, AUDIO_SAMPLES_COUNT * 2 * sizeof(int16_t), 0);
}

void play_mix_all(void) {
    // Gera todos os sons
    gerar_pcm_chime(g_pcm_chime, AUDIO_SAMPLES_COUNT, 16000);
    gerar_pcm_siren(g_pcm_siren, AUDIO_SAMPLES_COUNT, 12000);
    gerar_pcm_bass(g_pcm_bass, AUDIO_SAMPLES_COUNT, 18000);
    gerar_pcm_effect(g_pcm_effect, AUDIO_SAMPLES_COUNT, 14000);
    
    // Mixa todos no buffer final
    mix_all_sounds_and_play();
    
    // Toca o buffer mixado como um único som
    sys_audio_play(g_mix_buffer, AUDIO_SAMPLES_COUNT * 2 * sizeof(int16_t), 0);
}

// ============================================================================
// GUI E IPC
// ============================================================================

void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;
extern void GUI_Memo_AddStr(TGUIControl* memo, const char* str);
extern void GUI_Memo_Clear(TGUIControl* memo);

int my_app_slot = -1;
TGUIEnvironment MyApp;
const int winWidth = 620;
const int winHeight = 440;

TGUIControl* BtnChime = NULL;
TGUIControl* BtnSiren = NULL;
TGUIControl* BtnBass = NULL;
TGUIControl* BtnEffect = NULL;
TGUIControl* BtnStopAll = NULL;
TGUIControl* BtnMixTest = NULL;
TGUIControl* ExeMemo = NULL;

typedef struct {
    uint8_t dummy[sizeof(IPC_WINDOW_LIST[0])]; 
    volatile uint8_t fila_teclado_virtual;
    volatile uint8_t tem_evento_teclado;
} __attribute__((packed)) AppWindowInfoExtended;

char Obter_Tecla_Entrada(void) {
    AppWindowInfoExtended* ext_slot = (AppWindowInfoExtended*)&IPC_WINDOW_LIST[my_app_slot];
    if (ext_slot->tem_evento_teclado == 1) {
        char key = (char)ext_slot->fila_teclado_virtual;
        ext_slot->tem_evento_teclado = 0; 
        return key;
    }
    return 0;
}

void Flush_Grafico_Janela(void) {
    gui_draw_form((TForm*)MyApp.MainWindow);
    gui_render_form((TForm*)MyApp.MainWindow);
    OS_IPC_FlipBuffers(my_app_slot, winWidth, winHeight);
}

void Tratar_Fechamento_Software(void) {
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
 * CALLBACKS DOS BOTÕES
 * ============================================================================ */

void OnBtnChimeClick(void* sender) {
    GUI_Memo_AddStr(ExeMemo, "[Áudio] Tocando Sino...\n");
    sys_audio_stop();
    play_chime();
}

void OnBtnSirenClick(void* sender) {
    GUI_Memo_AddStr(ExeMemo, "[Áudio] Tocando Sirene...\n");
    sys_audio_stop();
    play_siren();
}

void OnBtnBassClick(void* sender) {
    GUI_Memo_AddStr(ExeMemo, "[Áudio] Tocando Baixo...\n");
    sys_audio_stop();
    play_bass();
}

void OnBtnEffectClick(void* sender) {
    GUI_Memo_AddStr(ExeMemo, "[Áudio] Tocando Efeito...\n");
    sys_audio_stop();
    play_effect();
}

void OnBtnMixTestClick(void* sender) {
    GUI_Memo_AddStr(ExeMemo, "[Áudio] TESTE DE MIXAGEM - Tocando todos os sons juntos!\n");
    sys_audio_stop();
    play_mix_all();
    GUI_Memo_AddStr(ExeMemo, "[Mixer] 4 sons mixados em um único buffer!\n");
}

void OnBtnStopAllClick(void* sender) {
    GUI_Memo_AddStr(ExeMemo, "[Controle] Parando o áudio...\n");
    sys_audio_stop();
    GUI_Memo_AddStr(ExeMemo, "[AC'97] Áudio parado.\n");
}

/* ============================================================================
 * FUNÇÃO PRINCIPAL (MAIN)
 * ============================================================================ */
int main(int argc, char* argv[]) {
    static int ultimo_x = 0;
    static int ultimo_y = 0;
    static int mouse_hold_timer = 0; 
    static bool primeiro_desenho = true;
    static bool ultimo_estado_foco = false;

    graphics_init_app(winWidth, winHeight);
    wm_init();
    
    my_app_slot = OS_IPC_RegisterApp("Teste Mixagem AC97", winWidth, winHeight);
    if (my_app_slot == -1) return -1; 
    
    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "LBF-OS Mixer AC'97 - Versão Estável", winWidth, winHeight);

    if (MyApp.MainWindow) {
        gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0x000000);
    }

    // Botões
    int btnW = 140;
    int btnH = 30;
    int startY = 35;
    int spacing = 10;

    BtnChime = GUI_CreateButton(&MyApp, 10,  startY, btnW, btnH, "1. Sino", OnBtnChimeClick);
    BtnSiren = GUI_CreateButton(&MyApp, 10 + btnW + spacing, startY, btnW, btnH, "2. Sirene", OnBtnSirenClick);
    BtnBass  = GUI_CreateButton(&MyApp, 10 + (btnW + spacing) * 2, startY, btnW, btnH, "3. Baixo", OnBtnBassClick);
    BtnEffect = GUI_CreateButton(&MyApp, 10 + (btnW + spacing) * 3, startY, btnW, btnH, "4. Efeito", OnBtnEffectClick);
    
    int startY2 = startY + btnH + 10;
    BtnMixTest = GUI_CreateButton(&MyApp, 10, startY2, 200, btnH, "🔊 TESTE MIXAGEM", OnBtnMixTestClick);
    BtnStopAll = GUI_CreateButton(&MyApp, 220, startY2, 150, btnH, "⏹ PARAR TUDO", OnBtnStopAllClick);
    
    GUI_CreateLabel(&MyApp, 10, startY2 + btnH + 15, "Console:");
    ExeMemo = GUI_CreateMemo(&MyApp, 10, startY2 + btnH + 35, 600, 180);
    gui_set_prop(ExeMemo, PROP_COLOR, 0x000000);
    
    GUI_Memo_AddStr(ExeMemo, "[Sistema] Mixer de Áudio Inicializado.\n");
    GUI_Memo_AddStr(ExeMemo, "[Sistema] Versão Estável - Buffer único mixado.\n");
    GUI_Memo_AddStr(ExeMemo, "[Sistema] Clique em 'TESTE MIXAGEM' para ouvir todos juntos.\n");

    Flush_Grafico_Janela();

    while(1) {
        if (IPC_WINDOW_LIST[my_app_slot].is_active == 0) {
            Tratar_Fechamento_Software();
            break;
        }

        bool precisa_redesenhar = false;

        if (primeiro_desenho) {
            primeiro_desenho = false;
            precisa_redesenhar = true;
        }

        bool euTenhoFocoJanelaReal = (IPC_CONTROL->active_focus_slot == my_app_slot);
        if (euTenhoFocoJanelaReal != ultimo_estado_foco) {
            ultimo_estado_foco = euTenhoFocoJanelaReal;
            if (MyApp.MainWindow) {
                ((TForm*)MyApp.MainWindow)->ActiveFocus = euTenhoFocoJanelaReal; 
            }
            precisa_redesenhar = true;
        }

        char key = Obter_Tecla_Entrada();
        if (key != 0) {
            GUI_ProcessKeyboard(&MyApp, key); 
            precisa_redesenhar = true; 
        }

        // Processamento do Mouse
        if (IPC_WINDOW_LIST[my_app_slot].has_click_event == 1) {
            if (mouse_hold_timer == 0) {
                int rel_x = IPC_WINDOW_LIST[my_app_slot].local_click_x;
                int rel_y = IPC_WINDOW_LIST[my_app_slot].local_click_y;
                ultimo_x = rel_x;
                ultimo_y = rel_y;
                mouse_hold_timer = 2;

                // Verifica cliques nos botões
                if (BtnChime && rel_x >= BtnChime->Left && rel_x < (BtnChime->Left + BtnChime->Width) &&
                    rel_y >= BtnChime->Top && rel_y < (BtnChime->Top + BtnChime->Height)) {
                    gui_set_prop(BtnChime, PROP_STATE, 2);
                }
                else if (BtnSiren && rel_x >= BtnSiren->Left && rel_x < (BtnSiren->Left + BtnSiren->Width) &&
                         rel_y >= BtnSiren->Top && rel_y < (BtnSiren->Top + BtnSiren->Height)) {
                    gui_set_prop(BtnSiren, PROP_STATE, 2);
                }
                else if (BtnBass && rel_x >= BtnBass->Left && rel_x < (BtnBass->Left + BtnBass->Width) &&
                         rel_y >= BtnBass->Top && rel_y < (BtnBass->Top + BtnBass->Height)) {
                    gui_set_prop(BtnBass, PROP_STATE, 2);
                }
                else if (BtnEffect && rel_x >= BtnEffect->Left && rel_x < (BtnEffect->Left + BtnEffect->Width) &&
                         rel_y >= BtnEffect->Top && rel_y < (BtnEffect->Top + BtnEffect->Height)) {
                    gui_set_prop(BtnEffect, PROP_STATE, 2);
                }
                else if (BtnMixTest && rel_x >= BtnMixTest->Left && rel_x < (BtnMixTest->Left + BtnMixTest->Width) &&
                         rel_y >= BtnMixTest->Top && rel_y < (BtnMixTest->Top + BtnMixTest->Height)) {
                    gui_set_prop(BtnMixTest, PROP_STATE, 2);
                }
                else if (BtnStopAll && rel_x >= BtnStopAll->Left && rel_x < (BtnStopAll->Left + BtnStopAll->Width) &&
                         rel_y >= BtnStopAll->Top && rel_y < (BtnStopAll->Top + BtnStopAll->Height)) {
                    gui_set_prop(BtnStopAll, PROP_STATE, 2);
                }

                events_process_mouse(rel_x, rel_y, 1, 0);
                
                if (GUI_ProcessMouseClick(&MyApp, rel_x, rel_y)) {
                    precisa_redesenhar = true;
                }
            }
            IPC_WINDOW_LIST[my_app_slot].has_click_event = 0;
        }

        if (mouse_hold_timer > 0) {
            mouse_hold_timer--; 
            if (mouse_hold_timer == 0) {
                if (BtnChime)  gui_set_prop(BtnChime, PROP_STATE, 0); 
                if (BtnSiren)  gui_set_prop(BtnSiren, PROP_STATE, 0); 
                if (BtnBass)   gui_set_prop(BtnBass, PROP_STATE, 0); 
                if (BtnEffect) gui_set_prop(BtnEffect, PROP_STATE, 0); 
                if (BtnMixTest) gui_set_prop(BtnMixTest, PROP_STATE, 0); 
                if (BtnStopAll) gui_set_prop(BtnStopAll, PROP_STATE, 0); 
                events_process_mouse(ultimo_x, ultimo_y, 0, 0); 
                precisa_redesenhar = true;
            }
        }

        if (precisa_redesenhar) {
            Flush_Grafico_Janela();
        }
        
        sys_sleep(euTenhoFocoJanelaReal ? 16 : 32);
    }

    sys_exit(); 
    return 0;
}
