#include "libgui.h"

TGUIControl* GUI_CreateScrollBar(TGUIEnvironment* app, int x, int y, int w, int h, int orientation) {
    if (!app) return NULL;

    TGUIControl* sb = (TGUIControl*)malloc(sizeof(TGUIControl));
    if (!sb) return NULL;
    memset(sb, 0, sizeof(TGUIControl));

    sb->Type = TYPE_SCROLLBAR;
    sb->Left = x;
    sb->Top = y;
    sb->Width = w;
    sb->Height = h;
    sb->Orientation = orientation;
    sb->Min = 0;
    sb->Max = 100;
    sb->Position = 0;
    sb->PageSize = 10;

    GUI_RegisterControl(app, sb, "ScrollBar");

    sb->KernelHandle = (uint64_t)gui_create_scrollbar((TWinControl*)app->MainWindow, sb->Name, orientation);
    if (sb->KernelHandle == 0) {
        free(sb);
        return NULL;
    }

    gui_set_prop((void*)sb->KernelHandle, PROP_LEFT,   (uint64_t)sb->Left);
    gui_set_prop((void*)sb->KernelHandle, PROP_TOP,    (uint64_t)sb->Top);
    gui_set_prop((void*)sb->KernelHandle, PROP_WIDTH,  (uint64_t)sb->Width);
    gui_set_prop((void*)sb->KernelHandle, PROP_HEIGHT, (uint64_t)sb->Height);

    return sb;
}

/* ============================================================================
 * FUNÇÃO CORRIGIDA: GUI_ScrollBar_SetPosition
 * ============================================================================ */
void GUI_ScrollBar_SetPosition(TGUIControl* sb, int position) {
    if (!sb || !sb->KernelHandle) return;

    if (position < sb->Min) position = sb->Min;
    if (position > sb->Max) position = sb->Max;

    if (sb->Position != position) {
        sb->Position = position;

        // GARANTIA: Re-envia o estado de Max e PageSize para o Kernel 
        // para que a alça da barra nunca perca a proporção ao se mover!
        uint64_t state_val = ((uint64_t)sb->PageSize << 32) | (uint32_t)sb->Max;
        gui_set_prop((void*)sb->KernelHandle, PROP_STATE, state_val);

        // Atualiza a posição de rolagem no Kernel
        gui_set_prop((void*)sb->KernelHandle, PROP_SCROLL_Y, (uint64_t)sb->Position);

        if (sb->OnScroll) {
            sb->OnScroll(sb, sb->Position);
        }
    }
}

void GUI_ScrollBar_SetRange(TGUIControl* sb, int min, int max, int pagesize) {
    if (!sb || !sb->KernelHandle) return;

    sb->Min = min;
    sb->Max = (max < 0) ? 0 : max;
    sb->PageSize = (pagesize < 1) ? 1 : pagesize;

    // Empacota o Max e PageSize de forma segura para o Kernel
    uint64_t state_val = ((uint64_t)sb->PageSize << 32) | (uint32_t)sb->Max;
    gui_set_prop((void*)sb->KernelHandle, PROP_STATE, state_val);

    if (sb->Position > sb->Max) {
        GUI_ScrollBar_SetPosition(sb, sb->Max);
    }
}
