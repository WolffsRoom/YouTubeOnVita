/* YouTube Vita - experimental native platform bring-up.
 * This is an unofficial interface prototype, not YouTube's proprietary app.
 * Engine and service integration are intentionally NOT mocked as working.
 */
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>
#include <stdio.h>
#include <string.h>

#define WW 960
#define HH 544
#define NAV_COUNT 5
#define MAX_NAV 5
#define TABS 4
#define COLUMNS 3
#define ROWS 2

unsigned int sceUserMainThreadStackSize = 1024 * 1024;
unsigned int _newlib_heap_size_user = 24 * 1024 * 1024;

static vita2d_font *font;
static int selected_nav = 1, selected_tab = 0, selected_tile = 0;
static int focus_tile = 0, show_diagnostics = 0;
static unsigned int frame_count = 0, prev_buttons = 0;
static int self_test_result = 0;

static const char *nav_labels[NAV_COUNT] = {
    "Buscar", "Inicio", "Inscricoes", "Biblioteca", "Configuracoes"
};
static const char *categories[TABS] = {
    "RECOMENDADOS", "MUSICA", "ENTRETENIMENTO", "JOGOS"
};

#define CLR(r,g,b) RGBA8(r,g,b,255)
static const unsigned int WHITE = CLR(247,247,247);
static const unsigned int MUTED = CLR(168,168,168);
static const unsigned int RED = CLR(255,0,0);
static const unsigned int BG = CLR(24,24,24);
static const unsigned int PANEL = CLR(34,34,34);

static void record(const char *s) {
    sceIoMkdir("ux0:data/youtubevita", 0777);
    SceUID f = sceIoOpen("ux0:data/youtubevita/boot.log",
                         SCE_O_CREAT | SCE_O_WRONLY | SCE_O_APPEND, 0666);
    if (f >= 0) {
        sceIoWrite(f, s, strlen(s));
        sceIoWrite(f, "\n", 1);
        sceIoClose(f);
    }
}

static void txt(int x, int y, int size, unsigned int color, const char *s) {
    if (font) vita2d_font_draw_text(font, x, y, color, size, s);
}

static void border(int x, int y, int w, int h, unsigned int color, int thick) {
    vita2d_draw_rectangle(x, y, w, thick, color);
    vita2d_draw_rectangle(x, y + h - thick, w, thick, color);
    vita2d_draw_rectangle(x, y, thick, h, color);
    vita2d_draw_rectangle(x + w - thick, y, thick, h, color);
}

static void draw_logo(int x, int y) {
    vita2d_draw_rectangle(x, y - 18, 35, 25, RED);
    txt(x + 11, y + 1, 21, WHITE, ">");
    txt(x + 44, y + 2, 21, WHITE, "YouTube");
}

static void draw_sidebar(void) {
    vita2d_draw_rectangle(0, 0, 126, HH, PANEL);
    draw_logo(15, 38);
    for (int i = 0; i < NAV_COUNT; i++) {
        int y = 87 + i*74;
        if (selected_nav == i && !focus_tile) {
            vita2d_draw_rectangle(0, y-23, 4, 55, RED);
            vita2d_draw_rectangle(6, y-23, 116, 55, CLR(67,67,67));
        }
        if (selected_nav == i && focus_tile)
            vita2d_draw_rectangle(6, y-23, 116, 55, CLR(48,48,48));
        const char *symbols[NAV_COUNT] = { "O", "^", "+", "=", "*" };
        txt(22, y + 1, 24, selected_nav==i ? WHITE : MUTED, symbols[i]);
        txt(22, y+21, 13, selected_nav==i ? WHITE : MUTED, nav_labels[i]);
    }
}

static void draw_tab_row(void) {
    const int xpos[TABS] = { 161, 360, 475, 688 };
    for (int i = 0; i < TABS; i++) {
        unsigned int c = (i == selected_tab && selected_nav == 1) ? WHITE : MUTED;
        txt(xpos[i], 60, 17, c, categories[i]);
        if (i == selected_tab && selected_nav == 1) {
            int width[TABS] = {151,72,175,59};
            vita2d_draw_rectangle(xpos[i], 74, width[i], 3, RED);
        }
    }
}

static void draw_home(void) {
    draw_tab_row();
    txt(154, 112, 22, WHITE, "Recomendado");
    for (int i=0; i<COLUMNS*ROWS; i++) {
        int col=i%COLUMNS, row=i/COLUMNS;
        int x=154+col*265, y=133+row*191;
        int w=247, h=121;
        unsigned int color=(i%3==0) ? CLR(53,53,53) : (i%3==1 ? CLR(61,61,61):CLR(46,46,46));
        vita2d_draw_rectangle(x,y,w,h,color);
        vita2d_draw_rectangle(x + (w/2)-15, y + (h/2)-12, 30, 25, RED);
        txt(x+w/2-7,y+h/2+6,17,WHITE,">");
        vita2d_draw_rectangle(x, y+h+12, w-40, 8, CLR(87,87,87));
        vita2d_draw_rectangle(x, y+h+27, w-96, 6, CLR(74,74,74));
        if (selected_tile==i && focus_tile && selected_nav==1)
            border(x-3,y-3,w+6,h+43,WHITE,3);
    }
    txt(153, 526, 12, MUTED, "Estrutura visual | API e videos ainda nao integrados");
}

static void draw_other(void) {
    const char *title = nav_labels[selected_nav];
    txt(158, 63, 28, WHITE, title);
    vita2d_draw_rectangle(158,86,760,1,CLR(70,70,70));
    txt(158,148,22,WHITE,"YouTube Vita - fase de integracao");
    txt(158,184,18,MUTED,"A interface oficial e o mecanismo de reproducao");
    txt(158,207,18,MUTED,"ainda nao estao carregados neste prototipo.");
    if (selected_nav==0) txt(158,258,17,WHITE,"Busca: aguardando integracao da API.");
    if (selected_nav==2) txt(158,258,17,WHITE,"Inscricoes: aguardando autenticacao.");
    if (selected_nav==3) txt(158,258,17,WHITE,"Biblioteca: aguardando dados reais.");
    if (selected_nav==4) txt(158,258,17,WHITE,"Idioma: PT-BR    Qualidade alvo: H.264 480p/720p");
}

static void draw_screen(void) {
    if (!frame_count) record("DBG: before start drawing");
    vita2d_start_drawing();
    vita2d_set_clear_color(BG);
    if (!frame_count) record("DBG: after start drawing");
    vita2d_clear_screen();
    if (!frame_count) record("DBG: before sidebar");
    draw_sidebar();
    if (!frame_count) record("DBG: after sidebar");
    if (!frame_count) record("DBG: before content");
    if (selected_nav==1) draw_home();
    if (!frame_count) record("DBG: after content");
    else draw_other();
    vita2d_draw_rectangle(0,HH-28,WW,28,CLR(19,19,19));
    txt(13,HH-9,13,MUTED,"D-Pad: navegar    X: abrir    O: voltar    L/R: abas    START: sair");
    txt(740,HH-9,12,MUTED,"WIP 0.01 | NAO OFICIAL");
    if (show_diagnostics) {
        vita2d_draw_rectangle(365, 171, 394, 181, CLR(12,12,12));
        border(365,171,394,181,WHITE,2);
        txt(380,205,23,WHITE,"Diagnostico - Vita3K");
        char status[128];
        snprintf(status,sizeof(status),"Frames: %u  Menu: %d  Aba: %d",frame_count,selected_nav,selected_tab);
        txt(380,249,17,MUTED,status);
        txt(380,288,16,WHITE,"GXM / vita2d: ativo");
        txt(380,317,15,MUTED,"SELECT: fechar");
    }
    if (!frame_count) record("DBG: before end drawing");
    vita2d_end_drawing();
    if (!frame_count) record("DBG: after end drawing");
    if (!frame_count) record("DBG: before swap");
    vita2d_swap_buffers();
    if (!frame_count) record("DBG: after swap");
}

static void input_buttons(unsigned int buttons) {
    if (buttons & SCE_CTRL_START) return;
    if (buttons & SCE_CTRL_SELECT) show_diagnostics=!show_diagnostics;
    if (buttons & SCE_CTRL_CIRCLE) {focus_tile=0;selected_nav=1;return;}
    if (buttons & SCE_CTRL_LTRIGGER) {if(selected_nav==1) selected_tab=(selected_tab+TABS-1)%TABS;}
    if (buttons & SCE_CTRL_RTRIGGER) {if(selected_nav==1) selected_tab=(selected_tab+1)%TABS;}
    if (buttons & SCE_CTRL_TRIANGLE) {selected_nav=0;focus_tile=0;return;}
    if (!focus_tile) {
        if (buttons & SCE_CTRL_UP) selected_nav=(selected_nav+NAV_COUNT-1)%NAV_COUNT;
        if (buttons & SCE_CTRL_DOWN) selected_nav=(selected_nav+1)%NAV_COUNT;
        if ((buttons & SCE_CTRL_CROSS) && selected_nav==1) focus_tile=1;
    } else {
        if (buttons & SCE_CTRL_LEFT) selected_tile=(selected_tile+COLUMNS*ROWS-1)%(COLUMNS*ROWS);
        if (buttons & SCE_CTRL_RIGHT) selected_tile=(selected_tile+1)%(COLUMNS*ROWS);
        if (buttons & SCE_CTRL_UP) selected_tile=(selected_tile+COLUMNS*ROWS-COLUMNS)%(COLUMNS*ROWS);
        if (buttons & SCE_CTRL_DOWN) selected_tile=(selected_tile+COLUMNS)%(COLUMNS*ROWS);
        if (buttons & SCE_CTRL_CROSS) record("INFO: playback not yet integrated; no fake player");
    }
}

static int run_model_self_test(void) {
    selected_nav=1; selected_tab=0; selected_tile=0; focus_tile=0;
    input_buttons(SCE_CTRL_RTRIGGER);
    if (selected_tab!=1) return 1;
    input_buttons(SCE_CTRL_LTRIGGER);
    if (selected_tab!=0) return 2;
    input_buttons(SCE_CTRL_CROSS);
    if (!focus_tile) return 3;
    input_buttons(SCE_CTRL_RIGHT);
    if (selected_tile!=1) return 4;
    input_buttons(SCE_CTRL_DOWN);
    if (selected_tile!=4) return 5;
    input_buttons(SCE_CTRL_CIRCLE);
    if (focus_tile || selected_nav!=1) return 6;
    input_buttons(SCE_CTRL_TRIANGLE);
    if (selected_nav!=0) return 7;
    input_buttons(SCE_CTRL_CIRCLE);
    if (selected_nav!=1) return 8;
    return 0;
}

int main(void) {
    record("BOOT: YouTubeVita 0.01");
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    vita2d_set_clear_color(BG);
    if (vita2d_init()<0) {
        record("FAIL: vita2d_init");
        sceKernelExitProcess(1);
        return 1;
    }
    record("PASS: vita2d_init");
    font=vita2d_load_font_file("app0:font.ttf");
    if (!font) {
        record("FAIL: font not loaded");
        vita2d_fini();
        sceKernelExitProcess(2);
        return 2;
    }
    record("PASS: font load");
#ifdef YTV_AUTOTEST
    self_test_result = run_model_self_test();
    if (self_test_result) record("FAIL: model navigation");
    else record("PASS: navigation model");
#else
    (void)run_model_self_test; /* reserved for deterministic emulator self-test */
    (void)self_test_result;
#endif
    SceCtrlData pad={0};
    int running=1;
    while (running) {
        sceCtrlPeekBufferPositive(0,&pad,1);
        unsigned int pressed=pad.buttons&(~prev_buttons);
        prev_buttons=pad.buttons;
        if (pressed) { char message[128]; snprintf(message, sizeof(message), "INPUT: buttons=0x%08X nav=%d focus=%d tab=%d", pressed, selected_nav, focus_tile, selected_tab); record(message); }
        if (pressed&SCE_CTRL_START) running=0;
        else if (pressed) input_buttons(pressed);
        draw_screen();
        frame_count++;
        if (frame_count==10) record("PASS: 10 rendered frames");
#ifdef YTV_AUTOTEST
        if (frame_count>=120) {
            record(self_test_result ? "RESULT: FAIL" : "RESULT: PASS");
            running=0;
        }
#endif
    }
    record("EXIT: normal shutdown");
    vita2d_free_font(font);
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}

