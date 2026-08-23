#include "sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"
#include "../system/setjmp.h"
#include "../system/assert.h"

// Componentes do Sistema
#include "components/TOS_IPC.h"     

// Protótipos de renderização gráfica e SDK
void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);

extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;
extern void GUI_Memo_AddStr(TGUIControl* memo, const char* str);
extern void GUI_Memo_Clear(TGUIControl* memo);

// Variáveis de controle de janela
int my_app_slot = -1;
TGUIEnvironment MyApp;

const int winWidth = 550;
const int winHeight = 410;

// Ponteiros RAD
TGUIControl* ExeMemo          = NULL;
TGUIControl* BtnTesteDivisao  = NULL;
TGUIControl* BtnTestePonteiro = NULL;
TGUIControl* BtnTesteLimites  = NULL;

/* ============================================================================
 * ESTRUTURA EXTENDIDA DE EVENTOS IPC
 * ============================================================================ */
typedef struct {
    uint8_t dummy[sizeof(IPC_WINDOW_LIST[0])]; 
    volatile uint8_t fila_teclado_virtual;
    volatile uint8_t tem_evento_teclado;
} __attribute__((packed)) AppWindowInfoExtended;

/* ============================================================================
 * FUNÇÕES AUXILIARES DE TELA E EVENTOS
 * ============================================================================ */
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
 * FUNÇÕES SIMULADORAS UTILIZANDO ASSERT
 * ============================================================================ */
void executar_divisao_riscosa(int divisor) {
    GUI_Memo_AddStr(ExeMemo, "[EXEC] Testando divisor via assert(divisor != 0)...\n");
    assert(divisor != 0); // Valida a condição; se for falsa, dispara THROW(ERR_ASSERTION_FAILED)
    GUI_Memo_AddStr(ExeMemo, "[SUCESSO] Divisao concluida com sucesso.\n");
}

void processar_ponteiro_riscoso(void* ptr) {
    GUI_Memo_AddStr(ExeMemo, "[EXEC] Testando ponteiro via assert(ptr != NULL)...\n");
    assert(ptr != NULL);
    GUI_Memo_AddStr(ExeMemo, "[SUCESSO] Ponteiro valido consultado.\n");
}

void acessar_indice_array(int index) {
    GUI_Memo_AddStr(ExeMemo, "[EXEC] Testando limites via assert(index >= 0 && index < 10)...\n");
    assert(index >= 0 && index < 10);
    GUI_Memo_AddStr(ExeMemo, "[SUCESSO] Indice acessado com seguranca.\n");
}

/* ============================================================================
 * CALLBACKS DOS BOTÕES COM TRATAMENTO DE ASSERT NO EXCEPT
 * ============================================================================ */
void OnBtnTesteDivisaoClick(void* sender) {
    GUI_Memo_AddStr(ExeMemo, "\n--- INICIANDO TESTE ASSERT: DIVISAO POR ZERO ---\n");
    
    TRY {
        executar_divisao_riscosa(0);
        GUI_Memo_AddStr(ExeMemo, "[OK] Esta linha NUNCA sera impressa se a assercao falhar.\n");
    } 
    EXCEPT {
        if (GET_EXCEPTION() == ERR_ASSERTION_FAILED) {
            char linha_str[10];
            itoa(g_last_assert_info.line, linha_str, 10);

            GUI_Memo_AddStr(ExeMemo, "[ASSERT FALHOU] Expressao: ");
            GUI_Memo_AddStr(ExeMemo, g_last_assert_info.expression);
            GUI_Memo_AddStr(ExeMemo, "\nArquivo: ");
            GUI_Memo_AddStr(ExeMemo, g_last_assert_info.file);
            GUI_Memo_AddStr(ExeMemo, " | Linha: ");
            GUI_Memo_AddStr(ExeMemo, linha_str);
            GUI_Memo_AddStr(ExeMemo, "\nFuncao: ");
            GUI_Memo_AddStr(ExeMemo, g_last_assert_info.function);
            GUI_Memo_AddStr(ExeMemo, "\n[SISTEMA] O software.elf continuou ativo!\n");
        }
    }
}

void OnBtnTestePonteiroClick(void* sender) {
    GUI_Memo_AddStr(ExeMemo, "\n--- INICIANDO TESTE ASSERT: PONTEIRO NULO ---\n");
    
    TRY {
        processar_ponteiro_riscoso(NULL);
    } 
    EXCEPT {
        if (GET_EXCEPTION() == ERR_ASSERTION_FAILED) {
            char linha_str[10];
            itoa(g_last_assert_info.line, linha_str, 10);

            GUI_Memo_AddStr(ExeMemo, "[ASSERT FALHOU] Expressao: ");
            GUI_Memo_AddStr(ExeMemo, g_last_assert_info.expression);
            GUI_Memo_AddStr(ExeMemo, "\nArquivo: ");
            GUI_Memo_AddStr(ExeMemo, g_last_assert_info.file);
            GUI_Memo_AddStr(ExeMemo, " | Linha: ");
            GUI_Memo_AddStr(ExeMemo, linha_str);
            GUI_Memo_AddStr(ExeMemo, "\nFuncao: ");
            GUI_Memo_AddStr(ExeMemo, g_last_assert_info.function);
            GUI_Memo_AddStr(ExeMemo, "\n[SISTEMA] Falha contida no bloco EXCEPT.\n");
        }
    }
}

void OnBtnTesteLimitesClick(void* sender) {
    GUI_Memo_AddStr(ExeMemo, "\n--- INICIANDO TESTE ASSERT: INDICE FORA DOS LIMITES ---\n");
    
    TRY {
        acessar_indice_array(99);
    } 
    EXCEPT {
        if (GET_EXCEPTION() == ERR_ASSERTION_FAILED) {
            char linha_str[10];
            itoa(g_last_assert_info.line, linha_str, 10);

            GUI_Memo_AddStr(ExeMemo, "[ASSERT FALHOU] Expressao: ");
            GUI_Memo_AddStr(ExeMemo, g_last_assert_info.expression);
            GUI_Memo_AddStr(ExeMemo, "\nArquivo: ");
            GUI_Memo_AddStr(ExeMemo, g_last_assert_info.file);
            GUI_Memo_AddStr(ExeMemo, " | Linha: ");
            GUI_Memo_AddStr(ExeMemo, linha_str);
            GUI_Memo_AddStr(ExeMemo, "\nFuncao: ");
            GUI_Memo_AddStr(ExeMemo, g_last_assert_info.function);
            GUI_Memo_AddStr(ExeMemo, "\n[SISTEMA] Execucao recuperada com sucesso!\n");
        }
    }
}

/* ============================================================================
 * MAIN
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
    
    my_app_slot = OS_IPC_RegisterApp("Teste Assert LBF", winWidth, winHeight);
    if (my_app_slot == -1) return -1; 
    
    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "Teste de Assercoes e Try/Except v1.1", winWidth, winHeight);

    if (MyApp.MainWindow) {
        gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0x000000);
    }

    int btnW = 160; 
    int ctrlH = 30;

    // Criando os 3 botões de teste
    BtnTesteDivisao  = GUI_CreateButton(&MyApp, 10,  40, btnW, ctrlH, "ASSERT DIV / 0",  OnBtnTesteDivisaoClick);
    BtnTestePonteiro = GUI_CreateButton(&MyApp, 180, 40, btnW, ctrlH, "ASSERT NULL PTR", OnBtnTestePonteiroClick);
    BtnTesteLimites  = GUI_CreateButton(&MyApp, 350, 40, btnW, ctrlH, "ASSERT OVERFLOW", OnBtnTesteLimitesClick);
    
    // Memo central para logs
    ExeMemo = GUI_CreateMemo(&MyApp, 10, 80, 530, 310);
    gui_set_prop(ExeMemo, PROP_COLOR, 0x000000);
    GUI_Memo_AddStr(ExeMemo, "=== AMBIENTE DE TESTE ASSERT + TRY/EXCEPT INICIALIZADO ===\n");
    GUI_Memo_AddStr(ExeMemo, "Clique em um dos botoes acima para testar o assert.h\n");

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

        if (IPC_WINDOW_LIST[my_app_slot].has_click_event == 1) {
            if (mouse_hold_timer == 0) {
                int rel_x = IPC_WINDOW_LIST[my_app_slot].local_click_x;
                int rel_y = IPC_WINDOW_LIST[my_app_slot].local_click_y;
                ultimo_x = rel_x;
                ultimo_y = rel_y;
                mouse_hold_timer = 2; 

                if (BtnTesteDivisao && rel_x >= BtnTesteDivisao->Left && rel_x < (BtnTesteDivisao->Left + BtnTesteDivisao->Width) &&
                    rel_y >= BtnTesteDivisao->Top && rel_y < (BtnTesteDivisao->Top + BtnTesteDivisao->Height)) {
                    gui_set_prop(BtnTesteDivisao, PROP_STATE, 2);
                }
                else if (BtnTestePonteiro && rel_x >= BtnTestePonteiro->Left && rel_x < (BtnTestePonteiro->Left + BtnTestePonteiro->Width) &&
                         rel_y >= BtnTestePonteiro->Top && rel_y < (BtnTestePonteiro->Top + BtnTestePonteiro->Height)) {
                    gui_set_prop(BtnTestePonteiro, PROP_STATE, 2);
                }
                else if (BtnTesteLimites && rel_x >= BtnTesteLimites->Left && rel_x < (BtnTesteLimites->Left + BtnTesteLimites->Width) &&
                         rel_y >= BtnTesteLimites->Top && rel_y < (BtnTesteLimites->Top + BtnTesteLimites->Height)) {
                    gui_set_prop(BtnTesteLimites, PROP_STATE, 2);
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
                if (BtnTesteDivisao)  gui_set_prop(BtnTesteDivisao,  PROP_STATE, 0); 
                if (BtnTestePonteiro) gui_set_prop(BtnTestePonteiro, PROP_STATE, 0); 
                if (BtnTesteLimites)  gui_set_prop(BtnTesteLimites,  PROP_STATE, 0); 
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
