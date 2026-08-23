#include "sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"
#include "components/TOS_IPC.h"

// Protótipos de renderização gráfica do ecossistema moderno
void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);

// Funções externas do sistema de foco e componentes
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;

// Protótipos de manipulação de Memo na SDK
extern void GUI_Memo_AddStr(TGUIControl* memo, const char* str);
extern void GUI_Memo_Clear(TGUIControl* memo);

// Variáveis de controle da aplicação
int my_app_slot = -1;
TGUIEnvironment MyApp;

// Configurações Globais de Dimensão
const int winWidth = 600;
const int winHeight = 500;

// Componentes RAD do Editor
TGUIControl* MemoTexto   = NULL;
TGUIControl* EditArquivo = NULL;
TGUIControl* BtnCarregar = NULL;
TGUIControl* BtnSalvar   = NULL;

/* ============================================================================
 * EXTRAÇÃO CORRETA DO TEXTO DO MEMO
 * ============================================================================ */
char* GUI_Memo_GetText(TGUIControl* memo) {
    if (!memo) return NULL;
    if (memo->Buffer && memo->Buffer[0] != '\0') return (char*)memo->Buffer;
    if (memo->Buffer) return (char*)memo->Buffer;
    return NULL; 
}

/* ============================================================================
 * ESTRUTURA AUXILIAR IPC
 * ============================================================================ */
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
    return get_key();
}

void Flush_Grafico_Janela(void) {
    gui_draw_form((TForm*)MyApp.MainWindow);
    gui_render_form((TForm*)MyApp.MainWindow);
    OS_IPC_FlipBuffers(my_app_slot, winWidth, winHeight);
}

void Tratar_Fechamento_Software(void) {
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_VISIBLE, 0);
    uint32_t* b0 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_0;
    uint32_t* b1 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_1;
    if (b0) memset(b0, 0, winWidth * winHeight * 4);
    if (b1) memset(b1, 0, winWidth * winHeight * 4);
    IPC_WINDOW_LIST[my_app_slot].is_active = 0;
    sys_sleep(50); 
}

/* ============================================================================
 * EVENTOS DO EDITOR DE TEXTO (I/O) - USANDO API DIRETA FAT32
 * ============================================================================ */
void OnBtnCarregarClick(void* sender) {
    char* caminho = GUI_Edit_GetText(EditArquivo);
    if (!caminho || caminho[0] == '\0') return;

    GUI_Memo_Clear(MemoTexto);

    // Aloca um buffer local limpo para receber o conteúdo do arquivo
    char buffer[4097];
    memset(buffer, 0, sizeof(buffer));

    // Usa a mesma syscall direta de leitura do FAT32 que funcionou no chip.c
    int bytes_lidos = sys_fat_read(caminho, (void*)buffer, 4096);

    if (bytes_lidos > 0) {
        // Protege o limite do buffer e garante o término de string
        if (bytes_lidos > 4096) bytes_lidos = 4096;
        buffer[bytes_lidos] = '\0';
        
        // Joga o texto recuperado para dentro do componente de texto (Memo)
        GUI_Memo_AddStr(MemoTexto, buffer);
    } else {
        // Feedback visual amigável caso o arquivo não exista ou esteja totalmente em branco
        GUI_Memo_AddStr(MemoTexto, "[Aviso] Arquivo vazio ou nao encontrado no disco.");
    }
}

void OnBtnSalvarClick(void* sender) {
    char* caminho = GUI_Edit_GetText(EditArquivo);
    char* conteudo = GUI_Memo_GetText(MemoTexto); 
    
    if (!caminho || caminho[0] == '\0') return;
    if (!conteudo) conteudo = ""; // Defesa contra ponteiro nulo (arquivo vazio)

    int tamanho = strlen(conteudo);

    // Escreve no disco FAT32
    int status = sys_fat_write(caminho, (void*)conteudo, (uint32_t)tamanho); 
    
    if (status == 0) { 
        // Sucesso: Altera temporariamente o texto do botão para dar feedback ao usuário
        if (BtnSalvar) GUI_Edit_SetText(BtnSalvar, "SALVO!"); 
    } else {
        // Erro: Mostra a mensagem no final do documento
        GUI_Memo_AddStr(MemoTexto, "\n[Erro] Falha ao gravar arquivo no disco.");
    }
}

/* ============================================================================
 * FUNÇÃO PRINCIPAL
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
    
    my_app_slot = OS_IPC_RegisterApp("Bloco de Notas LBF", winWidth, winHeight);
    if (my_app_slot == -1) return -1; 
    
    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "Bloco de Notas v1.1", winWidth, winHeight);
    
    if (MyApp.MainWindow) {
        gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0x1E1E1E); 
    }

    // Design do Layout
    GUI_CreateLabel(&MyApp, 10, 42, "Arquivo:");
    EditArquivo = GUI_CreateEdit(&MyApp, 70, 38, 300, 25, "0:/nota.txt", NULL);
    
    BtnCarregar = GUI_CreateButton(&MyApp, 380, 38, 100, 25, "CARREGAR", OnBtnCarregarClick);
    BtnSalvar   = GUI_CreateButton(&MyApp, 490, 38, 100, 25, "SALVAR", OnBtnSalvarClick);

    MemoTexto = GUI_CreateMemo(&MyApp, 10, 75, 580, 415);
    gui_set_prop(MemoTexto, PROP_COLOR, 0x000000); 

    // Configuração inicial de foco
    g_focused_control = (void*)MemoTexto; // Foca no texto direto para agilizar a digitação
    ultimo_controle_focado = (void*)MemoTexto;
    gui_set_prop(MemoTexto, PROP_SET_FOCUS, 1);

    Flush_Grafico_Janela();

    /* =========================================================================
     * LOOP DE EVENTOS REATIVO
     * ========================================================================= */
    while(1) {
        if (IPC_WINDOW_LIST[my_app_slot].is_active == 0) {
            Tratar_Fechamento_Software();
            break;
        }

        bool euTenhoFoco = (IPC_CONTROL->active_focus_slot == my_app_slot);
        if (MyApp.MainWindow) {
            ((TForm*)MyApp.MainWindow)->ActiveFocus = euTenhoFoco;
        }

        bool precisa_redesenhar = false;

        if (primeiro_desenho) {
            primeiro_desenho = false;
            precisa_redesenhar = true;
        }

        if (euTenhoFoco != ultimo_estado_foco) {
            ultimo_estado_foco = euTenhoFoco;
            precisa_redesenhar = true;
        }

        // Restaura foco local se necessário
        if (g_focused_control != NULL) {
            ultimo_controle_focado = g_focused_control;
        } else if (ultimo_controle_focado != NULL) {
            g_focused_control = ultimo_controle_focado;
            gui_set_prop((TGUIControl*)ultimo_controle_focado, PROP_SET_FOCUS, 1);
        }

        if (euTenhoFoco) {
            char key = Obter_Tecla_Entrada();
            if (key != 0) {
                // Restaura o nome do botão caso o usuário volte a digitar após salvar
                if (BtnSalvar && strcmp(GUI_Edit_GetText(BtnSalvar), "SALVO!") == 0) {
                    GUI_Edit_SetText(BtnSalvar, "SALVAR");
                }
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

                    events_process_mouse(rel_x, rel_y, 1, 0);
                    
                    // A SDK processa o clique em botões e altera o foco, se necessário
                    if (GUI_ProcessMouseClick(&MyApp, rel_x, rel_y)) {
                        precisa_redesenhar = true;
                        if (g_focused_control != NULL) ultimo_controle_focado = g_focused_control;
                    }
                }
                IPC_WINDOW_LIST[my_app_slot].has_click_event = 0;
            }

            if (mouse_hold_timer > 0) {
                mouse_hold_timer--; 
                if (mouse_hold_timer == 0) {
                    events_process_mouse(ultimo_x, ultimo_y, 0, 0); 
                    precisa_redesenhar = true;
                }
            }
        }

        if (precisa_redesenhar) {
            Flush_Grafico_Janela();
        }
        
        sys_sleep(euTenhoFoco ? 16 : 32);
    }

    sys_exit(); 
    return 0;
}
