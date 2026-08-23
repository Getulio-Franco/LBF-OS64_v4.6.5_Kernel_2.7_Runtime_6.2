#include "sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"

// Componentes do Sistema encapsulados
#include "components/TOS_IPC.h"     
#include "components/TOSSerial.h"   

// Protótipos de renderização gráfica
void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);

extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;

extern char* GUI_Edit_GetText(TGUIControl* edit);
extern void GUI_Edit_SetText(TGUIControl* edit, const char* text); 

// Variáveis de controle do ambiente da aplicação
int my_app_slot = -1;
TGUIEnvironment MyApp;

const int winWidth = 260;
const int winHeight = 350;

// Ponteiros dos Controles
TGUIControl* CalcDisplay = NULL;
TGUIControl* Btn[10]; 
TGUIControl* BtnAdd, *BtnSub, *BtnMul, *BtnDiv, *BtnEq, *BtnClear;
TGUIControl* BackButton = NULL; 

// Variáveis de Lógica Matemática com Ponto Flutuante (float)
char display_buffer[64] = "0";
float operand1 = 0.0f;
char current_operator = 0;
bool is_new_number = true;

typedef struct {
    uint8_t dummy[sizeof(IPC_WINDOW_LIST[0])]; 
    volatile uint8_t fila_teclado_virtual;
    volatile uint8_t tem_evento_teclado;
} __attribute__((packed)) AppWindowInfoExtended;

/* ============================================================================
 * CONVERSORES AUXILIARES (FLOAT <-> STRING)
 * ============================================================================ */
static float string_to_float(const char* str) {
    if (!str) return 0.0f;
    float result = 0.0f;
    float fraction = 0.1f;
    bool in_fraction = false;
    bool negative = false;

    if (*str == '-') {
        negative = true;
        str++;
    }

    while (*str) {
        if (*str == '.' || *str == ',') {
            in_fraction = true;
        } else if (*str >= '0' && *str <= '9') {
            if (!in_fraction) {
                result = (result * 10.0f) + (*str - '0');
            } else {
                result += (*str - '0') * fraction;
                fraction *= 0.1f;
            }
        }
        str++;
    }
    return negative ? -result : result;
}

static void float_to_string(float val, char* buf) {
    if (val < 0) {
        *buf++ = '-';
        val = -val;
    }

    int int_part = (int)val;
    float dec_part = val - (float)int_part;

    char temp[32];
    int_to_string(int_part, temp);
    strcpy(buf, temp);

    if (dec_part > 0.0001f) {
        strcat(buf, ".");
        int dec_int = (int)(dec_part * 10000.0f);
        char temp_dec[16];
        int_to_string(dec_int, temp_dec);
        
        int len_dec = strlen(temp_dec);
        while (len_dec < 4) {
            strcat(buf, "0");
            len_dec++;
        }
        strcat(buf, temp_dec);

        int len = strlen(buf);
        while (len > 0 && buf[len - 1] == '0') {
            buf[--len] = '\0';
        }
        if (len > 0 && buf[len - 1] == '.') {
            buf[len - 1] = '\0';
        }
    }
}

/* ============================================================================
 * INTERFACE GRÁFICA & FLUXO
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
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_VISIBLE, 0);
    uint32_t* b0 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_0;
    uint32_t* b1 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_1;
    if (b0) memset(b0, 0, winWidth * winHeight * 4);
    if (b1) memset(b1, 0, winWidth * winHeight * 4);
    IPC_WINDOW_LIST[my_app_slot].is_active = 0;
    sys_sleep(50); 
}

void AtualizarVisor() {
    GUI_Edit_SetText(CalcDisplay, display_buffer);
}

void ProcessarDigito(int digito) {
    if (is_new_number) {
        display_buffer[0] = digito + '0';
        display_buffer[1] = '\0';
        is_new_number = false;
    } else {
        int len = strlen(display_buffer);
        if (len < 15) { 
            display_buffer[len] = digito + '0';
            display_buffer[len + 1] = '\0';
        }
    }
    AtualizarVisor();
}

void ProcessarOperador(char op) {
    operand1 = string_to_float(display_buffer);
    current_operator = op;
    is_new_number = true;
}

void CalcularResultado() {
    if (current_operator == 0) return;
    
    float operand2 = string_to_float(display_buffer);
    float resultado = 0.0f;

    switch (current_operator) {
        case '+': resultado = operand1 + operand2; break;
        case '-': resultado = operand1 - operand2; break;
        case '*': resultado = operand1 * operand2; break;
        case '/': 
            if (operand2 != 0.0f) {
                resultado = operand1 / operand2;
            } else {
                strcpy(display_buffer, "Erro");
                AtualizarVisor();
                is_new_number = true;
                current_operator = 0;
                return;
            }
            break;
    }
    
    float_to_string(resultado, display_buffer);
    AtualizarVisor();
    is_new_number = true;
    current_operator = 0;
}

void LimparCalculadora() {
    strcpy(display_buffer, "0");
    operand1 = 0.0f;
    current_operator = 0;
    is_new_number = true;
    AtualizarVisor();
}

void ProcessarBackspace() {
    int len = strlen(display_buffer);
    if (len > 0 && !is_new_number) {
        display_buffer[len - 1] = '\0';
        if (strlen(display_buffer) == 0 || (strlen(display_buffer) == 1 && display_buffer[0] == '-')) {
            strcpy(display_buffer, "0");
            is_new_number = true;
        }
        AtualizarVisor();
    }
}

// Callbacks dos Botões
void OnBtn0Click(void* s) { ProcessarDigito(0); }
void OnBtn1Click(void* s) { ProcessarDigito(1); }
void OnBtn2Click(void* s) { ProcessarDigito(2); }
void OnBtn3Click(void* s) { ProcessarDigito(3); }
void OnBtn4Click(void* s) { ProcessarDigito(4); }
void OnBtn5Click(void* s) { ProcessarDigito(5); }
void OnBtn6Click(void* s) { ProcessarDigito(6); }
void OnBtn7Click(void* s) { ProcessarDigito(7); }
void OnBtn8Click(void* s) { ProcessarDigito(8); }
void OnBtn9Click(void* s) { ProcessarDigito(9); }

void OnBtnAddClick(void* s) { ProcessarOperador('+'); }
void OnBtnSubClick(void* s) { ProcessarOperador('-'); }
void OnBtnMulClick(void* s) { ProcessarOperador('*'); }
void OnBtnDivClick(void* s) { ProcessarOperador('/'); }

void OnBtnEqClick(void* s)  { CalcularResultado(); }
void OnBtnClearClick(void* s){ LimparCalculadora(); }
void OnBtnBackClick(void* s) { ProcessarBackspace(); } 

int main(int argc, char* argv[]) {
    static int ultimo_x = 0, ultimo_y = 0;
    static bool mouse_was_pressed = false; // Controle de estado para Debounce
    static bool primeiro_desenho = true;
    static bool ultimo_estado_foco = false;
    void* ultimo_controle_focado = NULL;

    graphics_init_app(winWidth, winHeight);
    wm_init();
    
    my_app_slot = OS_IPC_RegisterApp("Calculadora Float", winWidth, winHeight);
    if (my_app_slot == -1) return -1; 
    
    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "Calculadora Float", winWidth, winHeight);

    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0x1E1E1E); 

    int btnW = 50, btnH = 40, sp = 8; 
    int col1 = 15, col2 = col1 + btnW + sp, col3 = col2 + btnW + sp, col4 = col3 + btnW + sp;
    int row1 = 90, row2 = row1 + btnH + sp, row3 = row2 + btnH + sp, row4 = row3 + btnH + sp;

    GUI_CreateLabel(&MyApp, 15, 40, "LBF OS - Float Calc");
    CalcDisplay = GUI_CreateEdit(&MyApp, 15, 55, 225, 30, "0", NULL);

    Btn[7]  = GUI_CreateButton(&MyApp, col1, row1, btnW, btnH, "7", OnBtn7Click);
    Btn[8]  = GUI_CreateButton(&MyApp, col2, row1, btnW, btnH, "8", OnBtn8Click);
    Btn[9]  = GUI_CreateButton(&MyApp, col3, row1, btnW, btnH, "9", OnBtn9Click);
    BtnDiv  = GUI_CreateButton(&MyApp, col4, row1, btnW, btnH, "/", OnBtnDivClick);

    Btn[4]  = GUI_CreateButton(&MyApp, col1, row2, btnW, btnH, "4", OnBtn4Click);
    Btn[5]  = GUI_CreateButton(&MyApp, col2, row2, btnW, btnH, "5", OnBtn5Click);
    Btn[6]  = GUI_CreateButton(&MyApp, col3, row2, btnW, btnH, "6", OnBtn6Click);
    BtnMul  = GUI_CreateButton(&MyApp, col4, row2, btnW, btnH, "*", OnBtnMulClick);

    Btn[1]  = GUI_CreateButton(&MyApp, col1, row3, btnW, btnH, "1", OnBtn1Click);
    Btn[2]  = GUI_CreateButton(&MyApp, col2, row3, btnW, btnH, "2", OnBtn2Click);
    Btn[3]  = GUI_CreateButton(&MyApp, col3, row3, btnW, btnH, "3", OnBtn3Click);
    BtnSub  = GUI_CreateButton(&MyApp, col4, row3, btnW, btnH, "-", OnBtnSubClick);

    BtnClear = GUI_CreateButton(&MyApp, col1, row4, btnW, btnH, "C", OnBtnClearClick);
    Btn[0]   = GUI_CreateButton(&MyApp, col2, row4, btnW, btnH, "0", OnBtn0Click);
    BtnEq    = GUI_CreateButton(&MyApp, col3, row4, btnW, btnH, "=", OnBtnEqClick);
    BtnAdd   = GUI_CreateButton(&MyApp, col4, row4, btnW, btnH, "+", OnBtnAddClick);

    BackButton = GUI_CreateButton(&MyApp, 15, row4 + btnH + 15, 225, 30, "Backspace", OnBtnBackClick);

    g_focused_control = (void*)CalcDisplay;
    ultimo_controle_focado = (void*)CalcDisplay;
    gui_set_prop(CalcDisplay, PROP_SET_FOCUS, 1);

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
            if (MyApp.MainWindow) ((TForm*)MyApp.MainWindow)->ActiveFocus = euTenhoFocoJanelaReal; 
            precisa_redesenhar = true;
        }

        if (g_focused_control != NULL) {
            ultimo_controle_focado = g_focused_control;
        } else if (ultimo_controle_focado != NULL) {
            g_focused_control = ultimo_controle_focado;
            gui_set_prop((TGUIControl*)ultimo_controle_focado, PROP_SET_FOCUS, 1);
        }

        char key = Obter_Tecla_Entrada();
        if (key == 0 && euTenhoFocoJanelaReal) key = get_key();

        if (key != 0) {
            if (key >= '0' && key <= '9') ProcessarDigito(key - '0');
            else if (key == '+' || key == '-' || key == '*' || key == '/') ProcessarOperador(key);
            else if (key == '=' || key == '\n' || key == '\r') CalcularResultado();
            else if (key == 8 || key == 127) ProcessarBackspace();
            else if (key == 'c' || key == 'C') LimparCalculadora();
            else GUI_ProcessKeyboard(&MyApp, key);
            precisa_redesenhar = true; 
        }

        // --- SISTEMA DE DEBOUNCE DO MOUSE (BORDA DE SUBIDA) ---
        if (IPC_WINDOW_LIST[my_app_slot].has_click_event == 1) {
            int rel_x = IPC_WINDOW_LIST[my_app_slot].local_click_x;
            int rel_y = IPC_WINDOW_LIST[my_app_slot].local_click_y;
            ultimo_x = rel_x; 
            ultimo_y = rel_y;

            // Dispara a ação APENAS no momento em que o clique inicia (Edge Trigger)
            if (!mouse_was_pressed) {
                mouse_was_pressed = true;

                events_process_mouse(rel_x, rel_y, 1, 0);
                if (GUI_ProcessMouseClick(&MyApp, rel_x, rel_y)) {
                    precisa_redesenhar = true;
                    if (g_focused_control != NULL) ultimo_controle_focado = g_focused_control;
                }
            }
            IPC_WINDOW_LIST[my_app_slot].has_click_event = 0;
        } else {
            // Quando a flag de clique apaga no IPC, liberamos o estado do mouse (Release/Debounce concluído)
            if (mouse_was_pressed) {
                mouse_was_pressed = false;
                events_process_mouse(ultimo_x, ultimo_y, 0, 0);
                precisa_redesenhar = true;
            }
        }

        if (precisa_redesenhar) Flush_Grafico_Janela();
        sys_sleep(euTenhoFocoJanelaReal ? 16 : 32); 
    }

    sys_exit(); 
    return 0;
}
