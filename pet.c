/* pet.c - 像素风小恐龙桌宠 2.0 (原生 Win32 GDI 版本)
 * 帧资源已离线转为 raw BGRA (白键透明已处理), 运行时只用 BitBlt/StretchBlt.
 * 编译: 见 build.bat (注意: 已不需要 -lgdiplus)
 */
#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <windowsx.h>
#include <imm.h>          /* ImmAssociateContext: 关闭本窗口的输入法关联 */
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WM_TRAYICON         (WM_USER + 1)
#define ID_TRAY_AUTOSTART   1001
#define ID_TRAY_EXIT        1002
#define ID_TRAY_SLEEP       1003
#define ID_TRAY_REMIND_OFF  1004
#define ID_TRAY_REMIND_30   1005
#define ID_TRAY_REMIND_45   1006
#define ID_TRAY_REMIND_60   1007

#define PET_W               110
#define PET_H               110

#define IDLE_MS             1800
#define WALK_MS             180
#define WALK_PX             6
#define WALK_RETURN_PX      24
#define WALK_TARGET_PX      30
#define WALK_REACT_MS       1000
#define IDLE_HOME_MIN_MS    300000

#define CLICK_DRAG_THRESH   6
#define ANIM_MS             180
#define SLEEP_MS         1500   /* 睡觉帧切换 0.6s/帧 (慢节奏) */
#define DRAG_TICK_MS        16

#define TIMER_ANIM          1
#define TIMER_CLOCK         2
#define TIMER_CLICK_WAIT    3
#define TIMER_PRESS_POLL    4
#define TIMER_DIZZY_TICK  5
#define TIMER_KEYBOARD_CHECK  6
#define CLICK_WAIT_MS       300
#define PRESS_POLL_MS       30

#define TOPMOST_INTERVAL_MS 600000
#define DIZZY_FRAME_MS 300   /* 每帧间隔 */
#define DIZZY_HOLD_MS  1500  /* 最后帧停留 */
#define DIZZY_TOTAL_MS  (5 * DIZZY_FRAME_MS + DIZZY_HOLD_MS)  /* 5*300+1500 = 3000 */
#define DIZZY_TICK_MS   350   /* timer 周期 (> DIZZY_FRAME_MS 留缓冲, 触发帧切换) */

/* DBG_LOG disabled */
#define DBG_LOG(tag, v1, v2) do { } while(0)

enum {
    RID_FIRST = 101,
    RID_IDLE_01 = 101, RID_IDLE_02, RID_IDLE_03, RID_IDLE_04,
    RID_IDLE_ANGRY, RID_IDLE_CONFUSED, RID_IDLE_SHY, RID_IDLE_SURPRISE,
    RID_WALK_RIGHT_01 = 111, RID_WALK_RIGHT_02, RID_WALK_RIGHT_03, RID_WALK_RIGHT_04,
    RID_WALK_LEFT_01, RID_WALK_LEFT_02, RID_WALK_LEFT_03, RID_WALK_LEFT_04,
    RID_WALK_UP_01, RID_WALK_UP_02,
    RID_WALK_DOWN_01, RID_WALK_DOWN_02,
    RID_EXP_FOCUS = 131, RID_EXP_HAPPY, RID_EXP_LOVE, RID_EXP_MIRROR,
    RID_EXP_NOSE, RID_EXP_SNACK, RID_EXP_WINTER,
    RID_DRAG_HEAD = 141, RID_DRAG_LEFT, RID_DRAG_RIGHT, RID_DRAG_TAIL,
    RID_SIGN_01 = 151, RID_SIGN_02, RID_SIGN_03,
    RID_SLEEP_01 = 161, RID_SLEEP_02, RID_SLEEP_03, RID_SLEEP_04,
    RID_SHAKE_SHOCK = 165,
    RID_DIZZY_00 = 166, RID_DIZZY_01, RID_DIZZY_02, RID_DIZZY_03, RID_DIZZY_04,
    RID_TYPING_LEFT_UP = 171, RID_TYPING_RIGHT_UP = 172,
    RID_ROAR = 173,
    RID_LAST
};

static const int IDLE_NUM_FRAMES[]   = { RID_IDLE_01, RID_IDLE_02, RID_IDLE_03, RID_IDLE_04 };
static const int IDLE_WORD_FRAMES[]  = { RID_IDLE_ANGRY, RID_IDLE_CONFUSED, RID_IDLE_SHY, RID_IDLE_SURPRISE };
static const int WALK_RIGHT_FRAMES[] = { RID_WALK_RIGHT_01, RID_WALK_RIGHT_02, RID_WALK_RIGHT_03, RID_WALK_RIGHT_04 };
static const int WALK_LEFT_FRAMES[]  = { RID_WALK_LEFT_01, RID_WALK_LEFT_02, RID_WALK_LEFT_03, RID_WALK_LEFT_04 };
static const int WALK_UP_FRAMES[]    = { RID_WALK_UP_01, RID_WALK_UP_02 };
static const int WALK_DOWN_FRAMES[]  = { RID_WALK_DOWN_01, RID_WALK_DOWN_02 };
static const int DRAG_FRAMES[]       = { RID_DRAG_HEAD, RID_DRAG_LEFT, RID_DRAG_RIGHT, RID_DRAG_TAIL };
static const int EXP_FRAMES[]        = { RID_EXP_FOCUS, RID_EXP_HAPPY, RID_EXP_LOVE, RID_EXP_MIRROR,
                                         RID_EXP_NOSE, RID_EXP_SNACK, RID_EXP_WINTER };
static const int SIGN_FRAMES[]       = { RID_SIGN_01, RID_SIGN_02, RID_SIGN_03 };
static const int SLEEP_FRAMES[]      = { RID_SLEEP_01, RID_SLEEP_02, RID_SLEEP_03, RID_SLEEP_04 };
static const int DIZZY_FRAMES[]      = { RID_DIZZY_00, RID_DIZZY_01, RID_DIZZY_02, RID_DIZZY_03, RID_DIZZY_04 };
static const int TYPING_FRAMES[]   = { RID_TYPING_LEFT_UP, RID_TYPING_RIGHT_UP };
#define TYPING_FRAMES_N 2
/* 打字检测白名单: 等价于原 0x08..0xFE 扫描后 is_real_key 通过的键集,
 * 直接把结果列成表, 把每 tick 的 GetAsyncKeyState 调用从 247 次降到 55 次. */
static const BYTE g_typing_keys[] = {
    0x08, 0x09, 0x0D, 0x1B, 0x20, 0x2E, 0x2F,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
    0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A,
    0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54,
    0x55, 0x56, 0x57, 0x58, 0x59, 0x5A,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B
};
#define TYPING_KEYS_N ((int)(sizeof(g_typing_keys) / sizeof(g_typing_keys[0])))
#define DIZZY_FRAMES_N 5

#define IDLE_NUM_N   4
#define IDLE_WORD_N  4
#define WALK_RIGHT_N 4
#define WALK_LEFT_N  4
#define WALK_UP_N    2
#define WALK_DOWN_N  2
#define DRAG_N       4
#define EXP_N        7
#define SIGN_N       3
#define SLEEP_N      4

enum {
    ST_IDLE = 0, ST_WALK_AUTO, ST_WALK_TARGET, ST_RETURN_HOME,
    ST_SIGN, ST_SLEEP, ST_DRAG, ST_AWAIT_TARGET,
ST_ENTERING,  /* 入场动画 */
};

static HWND g_hwnd = NULL;
static int  g_x = 0, g_y = 0;
static int  g_home_x = 0, g_home_y = 0;
static int  g_screen_w = 0, g_screen_h = 0;

static int  g_state = ST_IDLE;
static int  g_frame_idx = 0;
static int  g_drag_variant = 0;
static int  g_idle_phase = 0;
static int  g_idle_lap = 0;
static DWORD g_idle_since_tick = 0;

/* #6 键盘监听: 用户打字时恐龙转头看 */
static BOOL  g_typing_phase = FALSE;
static DWORD g_typing_last_tick = 0;
static int g_typing_frame_idx = 0;
static DWORD g_typing_frame_tick = 0;
#define KEYBOARD_IDLE_MS  1500   /* 1.5秒无键盘活动回 idle */
#define KEYBOARD_CHECK_MS 100   /* timer 周期 = 帧切换周期 */
static DWORD g_last_input_tick = 0;

static int  g_walk_dir_x = 0;
static int  g_walk_dir_y = 0;
static int  g_walk_origin_x = 0;
static int  g_walk_origin_y = 0;
static int  g_walk_target_x = 0;
static int  g_walk_target_y = 0;
static int  g_walk_phase = 0;
static DWORD g_walk_react_until = 0;
static int  g_walk_react_rid = 0;

static int  g_pre_sign_x = 0;
static int  g_pre_sign_y = 0;
static DWORD g_last_topmost_tick = 0;

static BOOL  g_exp_active = FALSE;
static DWORD g_exp_until = 0;
static int   g_exp_rid = 0;

static DWORD g_await_target_until = 0;
static int   g_await_click_count = 0;

static DWORD g_sign_until = 0;
static DWORD g_sign_duration_ms = 2000;   /* 举牌停留时长(久坐提醒会加长) */

/* ---- 久坐提醒 ----
 * 语义: 连续"坐着"的时间。只要没有连续休息够 ACTIVITY_BREAK_MS 就一直在累计,
 * 不看这段时间有没有在打字(看文档也是坐着)。累计到 ACTIVITY_REMIND_MS 提醒一次。
 * 注意: 举牌已完全由这里的累计时间驱动(原来的"半点"硬触发已删)。 */
#define ACTIVITY_REMIND_MS  (45 * 60 * 1000)   /* 默认 45 分钟(可被 pos.ini 覆盖) */
#define ACTIVITY_BREAK_MS   (5 * 60 * 1000)    /* 连续 5 分钟无键鼠 = 算休息过 */
#define REMIND_QUIET_MS     1000               /* 到点后先等 1 秒"手头空档"再动 */
#define REMIND_WAIT_MAX_MS  60000              /* 最多等 60 秒, 免得连续码字时收不到 */
static DWORD g_active_ms = 0;
static DWORD g_last_activity_tick = 0;
static POINT g_last_mouse_pos = { 0, 0 };
static BOOL  g_mouse_pos_valid = FALSE;
static int   g_remind_min = 45;        /* 0 = 关闭; 单位分钟 */
static DWORD g_remind_since = 0;       /* 首次达到阈值的时刻(用于 60s 上限) */
static DWORD g_quiet_since = 0;        /* 本次"安静"起点(用于 1s 空档) */

static POINT g_drag_off = { 0, 0 };
/* 入场动画 */
static int  g_enter_phase = 0;
static int  g_enter_target_x = 0;
static int  g_enter_target_y = 0;
static DWORD g_enter_roar_until = 0;  /* roar 阶段结束时间 */
static POINT g_drag_prev_pt = { 0, 0 };
/* #2 惊吓: 上一次的拖动速度. 原来是 tick_anim 里的 static 局部变量, 会跨两次拖动
 * 残留, 导致新一次拖动刚起步就可能被判定为"反向"而误触发惊吓/晕眩. 改为文件级,
 * 并在每次按下时清零. */
static int   g_drag_prev_vx = 0, g_drag_prev_vy = 0;
static BOOL  g_dragging = FALSE;
static POINT g_press_pt = { 0, 0 };
static BOOL  g_press_pending = FALSE;

static BOOL  g_prev_lbutton_down = FALSE;

static UINT  g_last_tick_ms = 0;
static int   g_last_minute = -1;
static int   g_last_hour = -1;

/* #2 惊吓: 仅在 ST_DRAG 期间检测方向反转 */
static int  g_scare_phase = 0;   /* 0=none, 1=shake, 2=dizzy */
static DWORD g_scare_until = 0;
static int g_dizzy_frame_idx = 0;  /* dizzy 动画帧索引 */
static DWORD g_dizzy_last_tick = 0;

static HBITMAP g_bmp[RID_LAST - RID_FIRST] = { NULL };

static NOTIFYICONDATAA g_nid;
static BOOL g_tray_added = FALSE;

static void enter_idle(void);
static void enter_sleep(void);
static void enter_await_target(void);
static void render_now(void);
static void render_full(void);
static void reset_anim_timer(void);
static void start_walk_auto(void);
static void start_entering(void);
static void start_walk_to_target(int tx, int ty);
static void start_return_home(void);
static void start_return_to_origin(void);
static void start_sign(void);
static void start_walk_to_sign(void);
static void start_walk_to_sign_ex(DWORD sign_ms);
static void click_to_expression(void);
static void switch_to_next_expression(void);
static void preload_startup(void);
static void add_tray_icon(void);
static void remove_tray_icon(void);
static void show_tray_menu(void);
static void cleanup_pet_run_keys(void);
static void get_autostart_enabled(BOOL* enabled);
static void set_autostart_enabled(BOOL enabled);
static int  pick_drag_frame_by_velocity(int vx, int vy);
static void utf8_to_gbk(const char* utf8, char* gbk, int gbk_size);

/* #6 键盘监听函数 */
static void check_keyboard_activity(void);

/* 久坐提醒: 每 100ms 由 TIMER_KEYBOARD_CHECK 驱动.
 * 累计"连续坐着"的时间, 达到阈值就走到屏幕中间举牌提醒, 然后重新计时. */
static void check_activity(void) {
    DWORD now = GetTickCount();
    static DWORD s_last_tick = 0;
    BOOL moved = FALSE;
    POINT pt;
    DWORD dt;

    if (s_last_tick == 0) {
        s_last_tick = now;
        g_last_activity_tick = now;
        if (GetCursorPos(&pt)) { g_last_mouse_pos = pt; g_mouse_pos_valid = TRUE; }
        return;
    }
    dt = now - s_last_tick;
    s_last_tick = now;
    if (dt > 1000) dt = 1000;   /* 定时器被系统延迟时, 一次别加太多 */

    if (GetCursorPos(&pt)) {
        if (g_mouse_pos_valid &&
            (abs(pt.x - g_last_mouse_pos.x) + abs(pt.y - g_last_mouse_pos.y)) >= 3) {
            moved = TRUE;
        }
        g_last_mouse_pos = pt;
        g_mouse_pos_valid = TRUE;
    }
    if (g_typing_phase || moved) g_last_activity_tick = now;

    if (now - g_last_activity_tick > ACTIVITY_BREAK_MS) {
        g_active_ms = 0;          /* 连续 5 分钟没动过 = 算休息过, 重新累计 */
    } else {
        g_active_ms += dt;
    }

    /* 到点后不立刻动: 先等 1 秒"手头空档"(没在打字 / 没动鼠标 / 没在拖它),
     * 免得在你打得最投入的时候突然走开; 最多等 60 秒, 保证提醒不会永远收不到. */
    {
        DWORD limit = (DWORD)g_remind_min * 60u * 1000u;
        BOOL busy = (g_typing_phase || moved || g_dragging);
        if (g_remind_min <= 0 || g_active_ms < limit) {
            g_remind_since = 0;
            g_quiet_since = 0;
            return;
        }
        if (g_remind_since == 0) g_remind_since = now;
        if (busy || g_quiet_since == 0) g_quiet_since = now;
        if (g_state != ST_IDLE) return;   /* 在走/在睡/在拖, 等它空闲 */
        if ((now - g_quiet_since >= REMIND_QUIET_MS) ||
            (now - g_remind_since >= REMIND_WAIT_MAX_MS)) {
            g_active_ms = 0;
            g_remind_since = 0;
            g_quiet_since = 0;
            start_walk_to_sign_ex(6000);   /* 举牌停 6 秒 */
        }
    }
}

/* #6 键盘监听 */
static void check_keyboard_activity(void) {
    DWORD now = GetTickCount();
    static WORD s_prev[256] = {0};
    static BOOL s_was_typing = FALSE;  /* 状态切换检测 */
    BOOL any_pressed = FALSE;
    /* 只检测会真正打字的键: ASCII 可打印字符 + 必要功能键 */
    /* 跳过 IME / OEM / 扩展键 (中文 IME 会持续触发 0xE5 等特殊键) */
    /* 只轮询白名单键 (与原逻辑等价, 见 g_typing_keys 注释) */
    for (int i = 0; i < TYPING_KEYS_N; i++) {
        int vk = g_typing_keys[i];
        WORD cur = (WORD)GetAsyncKeyState(vk);
        WORD prev = s_prev[vk - 0x08];
        if ((cur & 0x8000) && !(prev & 0x8000)) {
            any_pressed = TRUE;
        }
        s_prev[vk - 0x08] = cur;
    }

    if (any_pressed) {
        g_typing_phase = TRUE;
        g_typing_last_tick = now;
    } else if (g_typing_phase && (now - g_typing_last_tick) > KEYBOARD_IDLE_MS) {
        g_typing_phase = FALSE;
    }
    /* 帧切换: 每次 typing timer tick 切一帧 (timer 100ms 周期) */
    if (g_typing_phase) {
        g_typing_frame_idx = (g_typing_frame_idx + 1) % TYPING_FRAMES_N;
    }
    /* Bug #1: 睡觉时被打字唤醒(只在边沿触发,避免重复 enter_idle) */
    if (g_state == ST_SLEEP && g_typing_phase && !s_was_typing) {
        enter_idle();
    }
    /* Bug #2: 打字也算操作,持续重置 idle_since_tick 避免 5 分钟回窝 */
    if (g_typing_phase && g_state == ST_IDLE) {
        g_idle_since_tick = GetTickCount();
    }

    /* 关键: 只在 typing 状态切换时改 TIMER_ANIM (避免频繁 reset) */
    /* ST_ENTERING 期间保持入场动画 ANIM_MS, 不加速 tick_anim (避免动画变快/卡) */
    if (g_typing_phase != s_was_typing) {
        s_was_typing = g_typing_phase;
        if (g_state == ST_ENTERING) {
            /* 不 SetTimer, 保持 ANIM_MS */
        } else {
            /* Fix A: the typing -> not-typing transition used to hardcode
             * SetTimer(TIMER_ANIM, IDLE_MS=1800) and ignore g_state, so a
             * walking pet (180ms) was switched to 1800ms -> visible stutter;
             * dragging (16ms) was even worse. reset_anim_timer() derives the
             * period from g_state and already carries the typing->100ms rule. */
            reset_anim_timer();
        }
    }
}

/* duplicate check_keyboard_activity removed */

/* #5 记忆功能: 保存/读取 home 位置 */
static char g_pos_ini_path[MAX_PATH];
static void get_pos_ini_path(void);
static void save_position(void);
static void load_position(int def_x, int def_y, int* out_x, int* out_y);

static void utf8_to_gbk(const char* utf8, char* gbk, int gbk_size) {
    if (!utf8 || !gbk || gbk_size <= 0) return;
    wchar_t wbuf[128];
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wbuf, 128);
    if (wlen > 0) {
        WideCharToMultiByte(CP_ACP, 0, wbuf, -1, gbk, gbk_size, NULL, NULL);
    } else {
        strncpy(gbk, utf8, gbk_size - 1);
        gbk[gbk_size - 1] = '\0';
    }
}

/* duplicate check_keyboard_activity removed */

/* #5 记忆功能: 读写 %APPDATA%\\PetDinoPet\\pos.ini */
static void get_pos_ini_path(void) {
    if (g_pos_ini_path[0]) return;
    char appdata[MAX_PATH];
    SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata);
    char dir[MAX_PATH];
    wsprintfA(dir, "%s\\PetDinoPet", appdata);
    CreateDirectoryA(dir, NULL);
    wsprintfA(g_pos_ini_path, "%s\\pos.ini", dir);
}

static void save_position(void) {
    get_pos_ini_path();
    char buf[16];
    wsprintfA(buf, "%d", g_x);
    WritePrivateProfileStringA("PetDino", "X", buf, g_pos_ini_path);
    wsprintfA(buf, "%d", g_y);
    WritePrivateProfileStringA("PetDino", "Y", buf, g_pos_ini_path);
}

static void load_position(int def_x, int def_y, int* out_x, int* out_y) {
    get_pos_ini_path();
    *out_x = GetPrivateProfileIntA("PetDino", "X", def_x, g_pos_ini_path);
    *out_y = GetPrivateProfileIntA("PetDino", "Y", def_y, g_pos_ini_path);
}

/* 久坐提醒间隔(分钟), 0 = 关闭. 持久化到 pos.ini 的 RemindMin. */
static void set_remind_min(int m) {
    g_remind_min = m;
    g_active_ms = 0;
    g_remind_since = 0;
    g_quiet_since = 0;
    get_pos_ini_path();
    char buf[16];
    wsprintfA(buf, "%d", m);
    WritePrivateProfileStringA("PetDino", "RemindMin", buf, g_pos_ini_path);
}

static int load_remind_min(void) {
    get_pos_ini_path();
    return GetPrivateProfileIntA("PetDino", "RemindMin", 45, g_pos_ini_path);
}

/* 加载 raw BGRA 资源: 文件格式 [4B W][4B H][BGRA 像素] */
static HBITMAP load_raw_resource(int rid) {
    HRSRC hr = FindResourceA(NULL, MAKEINTRESOURCEA(rid), RT_RCDATA);
    if (!hr) return NULL;
    HGLOBAL hg = LoadResource(NULL, hr);
    if (!hg) return NULL;
    LPVOID data = LockResource(hg);
    DWORD sz = SizeofResource(NULL, hr);
    if (!data || sz < 8) { DBG_LOG("LOAD_FAIL1", rid, sz); return NULL; }
    DWORD w = *(DWORD*)data;
    DWORD h = *((DWORD*)data + 1);
    if (w != PET_W || h != PET_H || sz < 8 + w*h*4) { DBG_LOG("LOAD_FAIL2", rid, (int)sz); return NULL; }
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)w;
    bmi.bmiHeader.biHeight = -(LONG)h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    HDC screen_dc = GetDC(NULL);
    LPVOID bits = NULL;
    HBITMAP hbmp = CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, screen_dc);
    if (!hbmp || !bits) { if (hbmp) DeleteObject(hbmp); return NULL; }
    memcpy(bits, (BYTE*)data + 8, w * h * 4);
    return hbmp;
}

static int hit_test_pet(int px, int py) {
    return px >= g_x && px < g_x + PET_W && py >= g_y && py < g_y + PET_H;
}

static void clamp_position(void) {
    if (g_x < 0) g_x = 0;
    if (g_y < 0) g_y = 0;
    if (g_x + PET_W > g_screen_w) g_x = g_screen_w - PET_W;
    if (g_y + PET_H > g_screen_h) g_y = g_screen_h - PET_H;
}

/* ---- 帧缓存: 按需加载 + LRU 淘汰 ----
 * 原来启动时把 47 帧全部解码成 DIB(47 x 110x110x4 = 2.17 MB 私有内存),
 * 但任一时刻最多只用 2-3 帧. 改成上限 FRAME_CACHE_MAX 张, 其余按需加载.
 * 注意: 取帧函数只在这里被 draw_frame_to_dib 调用, 且 bitmap 用完立刻
 * SelectObject 还原, 所以淘汰时不会有位图仍被选进 DC. */
#define FRAME_CACHE_MAX 24
static int g_frame_tick[RID_LAST - RID_FIRST];   /* 最近使用序号, 越大越新 */
static int g_frame_seq = 0;
static int g_frame_count = 0;

static void evict_one_frame(void) {
    int i, victim = -1, oldest = 0x7fffffff;
    for (i = 0; i < RID_LAST - RID_FIRST; i++) {
        if (g_bmp[i] && g_frame_tick[i] < oldest) { oldest = g_frame_tick[i]; victim = i; }
    }
    if (victim >= 0) {
        DeleteObject(g_bmp[victim]);
        g_bmp[victim] = NULL;
        g_frame_tick[victim] = 0;
        g_frame_count--;
    }
}

static HBITMAP get_bmp(int rid) {
    int i;
    HBITMAP b;
    if (rid < RID_FIRST || rid >= RID_LAST) return NULL;
    i = rid - RID_FIRST;
    if (g_bmp[i]) { g_frame_tick[i] = ++g_frame_seq; return g_bmp[i]; }
    if (g_frame_count >= FRAME_CACHE_MAX) evict_one_frame();
    b = load_raw_resource(rid);
    if (!b) return NULL;
    g_bmp[i] = b;
    g_frame_tick[i] = ++g_frame_seq;
    g_frame_count++;
    return b;
}

static int pick_drag_frame_by_velocity(int vx, int vy) {
    int avx = abs(vx), avy = abs(vy);
    if (avx < 1 && avy < 1) return -1;
    if (avy >= avx * 2)  return vy > 0 ? 3 : 0;
    if (avx >= avy * 2)  return vx > 0 ? 2 : 1;
    return avy > avx ? (vy > 0 ? 3 : 0) : (vx > 0 ? 2 : 1);
}

/* ---- 常驻渲染表面 ----------------------------------------------------
 * 原实现每渲染一帧都要: CreateCompatibleDC + CreateDIBSection + SelectObject
 * + SelectObject(还原) + DeleteObject + DeleteDC (外加 draw_frame 里再建一个 DC).
 * 拖动时 16ms 一帧, 相当于每秒上百次 GDI 对象创建/销毁.
 * 这里改为一次创建、反复复用, 每帧只做 memset + 两次 BitBlt/StretchBlt.
 * -------------------------------------------------------------------- */
static HDC     g_render_dc   = NULL;
static HBITMAP g_render_bmp  = NULL;
static LPVOID  g_render_bits = NULL;
static HDC     g_src_dc      = NULL;

static BOOL ensure_render_surfaces(HDC screen_dc) {
    if (g_render_bmp) return TRUE;
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = PET_W;
    bmi.bmiHeader.biHeight = -PET_H;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    g_render_dc = CreateCompatibleDC(screen_dc);
    if (!g_render_dc) return FALSE;
    g_render_bmp = CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS, &g_render_bits, NULL, 0);
    if (!g_render_bmp) { DeleteDC(g_render_dc); g_render_dc = NULL; return FALSE; }
    SelectObject(g_render_dc, g_render_bmp);
    g_src_dc = CreateCompatibleDC(screen_dc);
    return TRUE;
}

static void release_render_surfaces(void) {
    if (g_src_dc)     { DeleteDC(g_src_dc);         g_src_dc = NULL; }
    if (g_render_bmp) { DeleteObject(g_render_bmp); g_render_bmp = NULL; }
    if (g_render_dc)  { DeleteDC(g_render_dc);      g_render_dc = NULL; }
    g_render_bits = NULL;
}

/* 渲染当前帧到 DIB (drag_tail 用 StretchBlt 负高度实现垂直翻转) */
static void draw_frame_to_dib(HDC mem_dc, int w, int h, LPVOID bits) {
    (void)bits;
    int rid = RID_IDLE_01;
    BOOL need_flip_tail = FALSE;
    DWORD now = GetTickCount();

    switch (g_state) {
        case ST_IDLE:
            {
                static DWORD s_last_idle_log = 0;
                DWORD _in = GetTickCount();
                if (_in - s_last_idle_log > 200) {
                    s_last_idle_log = _in;
                    DBG_LOG("IDLE_CK", g_scare_phase, (int)(g_scare_until - _in));
                }
            }
            /* #6 键盘监听优先: 用户打字时显示 surprise (恐龙在看屏幕) */
            /* Bug 修复: 只查 g_scare_phase, 不查时间. 原先 "&& GetTickCount() < g_scare_until" 在 _now == g_scare_until 时为 false, 回落 IDLE 帧 */
            if (g_scare_phase == 2) {
                rid = DIZZY_FRAMES[g_dizzy_frame_idx];
            } else if (g_typing_phase) {
                /* #6 打字动画: 2 帧交替 */
                rid = TYPING_FRAMES[g_typing_frame_idx];
            } else if (g_idle_phase == 0) rid = IDLE_NUM_FRAMES[g_frame_idx % IDLE_NUM_N];
            else                   rid = IDLE_WORD_FRAMES[g_idle_lap % IDLE_WORD_N];
            break;
        case ST_WALK_AUTO:
        case ST_WALK_TARGET:
        case ST_RETURN_HOME:
            if (now < g_walk_react_until && g_walk_react_rid) {
                rid = g_walk_react_rid;
                break;
            }
            if (g_walk_dir_x != 0) {
                if (g_walk_dir_x > 0) rid = WALK_RIGHT_FRAMES[g_frame_idx % WALK_RIGHT_N];
                else                  rid = WALK_LEFT_FRAMES[g_frame_idx % WALK_LEFT_N];
            } else if (g_walk_dir_y != 0) {
                if (g_walk_dir_y < 0) rid = WALK_UP_FRAMES[g_frame_idx % WALK_UP_N];
                else                  rid = WALK_DOWN_FRAMES[g_frame_idx % WALK_DOWN_N];
            } else {
                rid = WALK_RIGHT_FRAMES[0];
            }
            break;
        case ST_SIGN:
            rid = SIGN_FRAMES[g_frame_idx % SIGN_N];
            break;
        case ST_SLEEP:
            rid = SLEEP_FRAMES[g_frame_idx % SLEEP_N];
            break;
        case ST_DRAG:
            /* #2 shake 期间显示 shake_shock 帧 */
            if (g_scare_phase == 1) {
                rid = RID_SHAKE_SHOCK;
            } else {
                rid = DRAG_FRAMES[g_drag_variant % DRAG_N];
                if (rid == RID_DRAG_TAIL) need_flip_tail = TRUE;
            }
            break;
        case ST_ENTERING:
            /* roar 阶段: 显示 roar 帧 */
            if (g_enter_roar_until > 0 && GetTickCount() < g_enter_roar_until) {
                rid = RID_ROAR;
                break;
            }
            /* 入场动画用 walk 帧 (根据方向) */
            if (g_walk_dir_y < 0) rid = WALK_UP_FRAMES[g_frame_idx % WALK_UP_N];
            else if (g_walk_dir_y > 0) rid = WALK_DOWN_FRAMES[g_frame_idx % WALK_DOWN_N];
            else if (g_walk_dir_x > 0) rid = WALK_RIGHT_FRAMES[g_frame_idx % WALK_RIGHT_N];
            else rid = WALK_LEFT_FRAMES[g_frame_idx % WALK_LEFT_N];
            break;
        case ST_AWAIT_TARGET:
            if (g_exp_active && g_exp_until > now) {
                rid = g_exp_rid;
            } else if (g_idle_phase == 0) {
                rid = IDLE_NUM_FRAMES[g_frame_idx % IDLE_NUM_N];
            } else {
                rid = IDLE_WORD_FRAMES[g_idle_lap % IDLE_WORD_N];
            }
            break;
    }

    {
        static DWORD s_last_render_log = 0;
        DWORD _rn = GetTickCount();
        if (_rn - s_last_render_log > 200) {
            s_last_render_log = _rn;
            DBG_LOG("RENDER", g_state, rid);
        }
    }
    HBITMAP src_bmp = get_bmp(rid);
    if (!src_bmp) return;
    HDC src_dc = g_src_dc;
    BOOL own_dc = FALSE;
    if (!src_dc) { src_dc = CreateCompatibleDC(mem_dc); own_dc = TRUE; }
    HBITMAP old_src = (HBITMAP)SelectObject(src_dc, src_bmp);
    if (need_flip_tail) {
        StretchBlt(mem_dc, 0, h, w, -h, src_dc, 0, 0, w, h, SRCCOPY);
    } else {
        BitBlt(mem_dc, 0, 0, w, h, src_dc, 0, 0, SRCCOPY);
    }
    SelectObject(src_dc, old_src);
    if (own_dc) DeleteDC(src_dc);
}

static void render_now(void) {
    HDC screen_dc = GetDC(NULL);
    if (!screen_dc) return;
    if (ensure_render_surfaces(screen_dc)) {
        memset(g_render_bits, 0, PET_W * PET_H * 4);
        draw_frame_to_dib(g_render_dc, PET_W, PET_H, g_render_bits);
        POINT ptSrc = { 0, 0 };
        POINT ptDst = { g_x, g_y };
        SIZE  szWnd = { PET_W, PET_H };
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        UpdateLayeredWindow(g_hwnd, screen_dc, &ptDst, &szWnd, g_render_dc, &ptSrc, 0, &bf, ULW_ALPHA);
    }
    ReleaseDC(NULL, screen_dc);
}

/* 说明: UpdateLayeredWindow 的 ptDst 已经负责把窗口移到 (g_x,g_y),
 * 原 render_full 每帧再调一次 SetWindowPos(HWND_TOPMOST, SWP_NOMOVE|SWP_NOSIZE)
 * 只是重复刷新 z-order, 属于纯开销. 置顶改由两处负责, 行为不变:
 *   1) 每次按下宠物 (WM_LBUTTONDOWN)
 *   2) TIMER_CLOCK 每 10 分钟一次
 * 窗口本身带 WS_EX_TOPMOST, 不会被普通窗口盖住. */
static void render_full(void) {
    render_now();
}

static void start_walk_dir_internal(int dir_x, int dir_y) {
    g_state = ST_WALK_AUTO;
    g_walk_dir_x = dir_x;
    g_walk_dir_y = dir_y;
    g_walk_origin_x = g_x;
    g_walk_origin_y = g_y;
    int range = g_screen_w / 3;
    if (dir_x > 0)      g_walk_target_x = g_x + range;
    else if (dir_x < 0) g_walk_target_x = g_x - range;
    else                g_walk_target_x = g_x;
    if (dir_y < 0)      g_walk_target_y = g_y - 60;
    else if (dir_y > 0) g_walk_target_y = g_y + 60;
    else                g_walk_target_y = g_y;
    if (g_walk_target_x < 0) g_walk_target_x = 0;
    if (g_walk_target_x > g_screen_w - PET_W) g_walk_target_x = g_screen_w - PET_W;
    if (g_walk_target_y < 0) g_walk_target_y = 0;
    if (g_walk_target_y > g_screen_h - PET_H) g_walk_target_y = g_screen_h - PET_H;
    g_walk_phase = 0;
    g_walk_react_until = 0;
    g_walk_react_rid = 0;
    g_frame_idx = 0;
    reset_anim_timer();
}

static void start_entering(void) {
    g_state = ST_ENTERING;
    g_enter_phase = 0;
    /* 起始: 屏幕底部下方 (x=中央, y=screen_h 屏幕底部) */
    g_x = (g_screen_w - PET_W) / 2;
    g_y = g_screen_h + 10;  /* 屏幕底部, 完全隐藏 */
    /* 目标: 屏幕中央 */
    g_enter_target_x = (g_screen_w - PET_W) / 2;
    g_enter_target_y = (g_screen_h - PET_H) / 2;
    g_frame_idx = 0;
    reset_anim_timer();
}

static void start_walk_auto(void) {
    int space_right = (g_screen_w - PET_W) - g_x;
    int space_left = g_x;
    int dir_x;
    if (space_right > space_left) dir_x = 1;
    else if (space_left > space_right) dir_x = -1;
    else dir_x = (rand() & 1) ? 1 : -1;
    start_walk_dir_internal(dir_x, 0);
}

static void start_walk_to_target(int tx, int ty) {
    g_state = ST_WALK_TARGET;
    g_walk_origin_x = g_x;
    g_walk_origin_y = g_y;
    g_walk_target_x = tx;
    g_walk_target_y = ty;
    if (g_walk_target_x > g_screen_w - PET_W) g_walk_target_x = g_screen_w - PET_W;
    if (g_walk_target_y > g_screen_h - PET_H) g_walk_target_y = g_screen_h - PET_H;
    if (g_walk_target_x < 0) g_walk_target_x = 0;
    if (g_walk_target_y < 0) g_walk_target_y = 0;
    {
        int idx = tx - g_x;
        int idy = ty - g_y;
        if (abs(idx) > 0) { g_walk_dir_x = (idx > 0) ? 1 : -1; g_walk_dir_y = 0; }
        else              { g_walk_dir_x = 0; g_walk_dir_y = (idy > 0) ? 1 : -1; }
    }
    g_walk_phase = 0;
    g_frame_idx = 0;
    g_walk_react_until = 0;
    g_walk_react_rid = 0;
    reset_anim_timer();
}

static void start_return_home(void) {
    g_state = ST_RETURN_HOME;
    g_walk_origin_x = g_x;
    g_walk_origin_y = g_y;
    g_walk_target_x = g_home_x;
    g_walk_target_y = g_home_y;
    g_walk_dir_x = 0;
    g_walk_dir_y = 0;
    g_walk_phase = 0;
    g_frame_idx = 0;
    reset_anim_timer();
}

static void start_return_to_origin(void) {
    g_state = ST_RETURN_HOME;
    g_walk_origin_x = g_x;
    g_walk_origin_y = g_y;
    g_walk_target_x = g_pre_sign_x;
    g_walk_target_y = g_pre_sign_y;
    g_walk_dir_x = 0;
    g_walk_dir_y = 0;
    g_walk_phase = 0;
    g_frame_idx = 0;
    reset_anim_timer();
}

static void start_sign(void) {
    g_state = ST_SIGN;
    g_sign_until = GetTickCount() + g_sign_duration_ms;
    g_frame_idx = 0;
    reset_anim_timer();
}

static void start_walk_to_sign_ex(DWORD sign_ms) {
    g_sign_duration_ms = sign_ms;
    int tx = g_screen_w / 2 - PET_W / 2;
    int ty = g_screen_h / 2 - PET_H / 2;
    if (abs(tx - g_x) <= WALK_TARGET_PX && abs(ty - g_y) <= WALK_TARGET_PX) {
        g_x = tx; g_y = ty;
        start_sign();
        return;
    }
    g_state = ST_WALK_TARGET;
    g_pre_sign_x = g_x;
    g_pre_sign_y = g_y;
    g_walk_origin_x = g_x;
    g_walk_origin_y = g_y;
    g_walk_target_x = tx;
    g_walk_target_y = ty;
    g_walk_dir_x = 0;
    g_walk_dir_y = 0;
    g_walk_phase = 0;
    g_frame_idx = 0;
    g_sign_until = 0;
    reset_anim_timer();
}

static void start_walk_to_sign(void) {
    start_walk_to_sign_ex(2000);   /* 举牌: 停留 2 秒(连点三下触发) */
}

static void click_to_expression(void) {
    int new_rid;
    do { new_rid = EXP_FRAMES[rand() % EXP_N]; } while (new_rid == g_exp_rid && EXP_N > 1);
    g_exp_rid = new_rid;
    g_exp_active = TRUE;
    g_exp_until = GetTickCount() + 2000;
    enter_await_target();
    render_full();
}

static void switch_to_next_expression(void) {
    int new_rid;
    do { new_rid = EXP_FRAMES[rand() % EXP_N]; } while (new_rid == g_exp_rid && EXP_N > 1);
    g_exp_rid = new_rid;
    g_exp_active = TRUE;
    g_exp_until = GetTickCount() + 2000;
    g_await_target_until = GetTickCount() + 3000;
    reset_anim_timer();
    render_full();
}

static void enter_idle(void) {
    g_state = ST_IDLE;
    g_frame_idx = 0;
    g_idle_phase = 0;
    g_idle_since_tick = GetTickCount();
    g_last_input_tick = g_idle_since_tick;
    reset_anim_timer();
}

static void enter_sleep(void) {
    g_state = ST_SLEEP;
    g_frame_idx = 0;
    reset_anim_timer();
    /* 内存优化: 睡觉时把工作集还给系统(帧位图仍在, 唤醒时按需缺页).
     * 任务管理器里能直接看到占用下降, 功能不受影响. */
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
}

static void enter_await_target(void) {
    g_state = ST_AWAIT_TARGET;
    g_await_target_until = GetTickCount() + 3000;
    g_prev_lbutton_down = FALSE;
    reset_anim_timer();
}

static void reset_anim_timer(void) {
    UINT want_ms;
    switch (g_state) {
        case ST_IDLE:         want_ms = IDLE_MS; break;
        case ST_WALK_AUTO:
        case ST_WALK_TARGET:
        case ST_RETURN_HOME:  want_ms = WALK_MS; break;
        case ST_SIGN:         want_ms = ANIM_MS; break;
        case ST_SLEEP:        want_ms = SLEEP_MS; break;
        case ST_DRAG:         want_ms = DRAG_TICK_MS; break;
        case ST_ENTERING:    want_ms = ANIM_MS; break;
        case ST_AWAIT_TARGET: want_ms = 100; break;
        default:              want_ms = ANIM_MS; break;
    }
    /* #6 typing 期间: 强制 100ms 渲染, 避免 enter_idle 把 TIMER_ANIM 设回 1800ms 导致 typing 卡住 */
    /* 只在待机时生效: 走路(180ms)/拖动(16ms)/举牌/睡觉/入场 的节奏是各自的设计,
     * 不能被"正在打字"改掉 —— 走路会被 100ms 拉快 1.8 倍, 拖动会从 60fps 掉到 10fps. */
    if (g_typing_phase && g_state == ST_IDLE) want_ms = 100;
    KillTimer(g_hwnd, TIMER_ANIM);
    SetTimer(g_hwnd, TIMER_ANIM, want_ms, NULL);
    g_last_tick_ms = 0;
}

static void tick_anim(void) {
    DWORD now = GetTickCount();
    if (g_last_tick_ms == 0) g_last_tick_ms = now - 16;  /* 第一帧渲染 (避免 200ms 空白) */
    g_last_tick_ms = now;

    switch (g_state) {
        case ST_IDLE: {
            /* Bug 修复: dizzy 期间不推 idle 帧, 避免 g_frame_idx 推到 IDLE_04, 后续 TIMER_ANIM 在 TIMER_DIZZY_TICK 之前触发时回落到 IDLE */
            if (g_scare_phase == 2) {
                render_full();   /* 只渲染 dizzy, 不推 idle 帧 */
            } else {
                /* 正常 idle 帧推进 */
                if (g_idle_phase == 0) {
                    g_frame_idx++;
                    if (g_frame_idx >= IDLE_NUM_N) { g_frame_idx = 0; g_idle_phase = 1; }
                } else {
                    g_frame_idx = 0;
                    g_idle_phase = 0;
                    g_idle_lap = (g_idle_lap + 1) % IDLE_WORD_N;
                }
                render_full();
            }
            break;
        }
        case ST_WALK_AUTO: {
            int dx = g_walk_target_x - g_x;
            if (g_walk_phase == 0) {
                g_walk_dir_x = (dx > 0) ? 1 : -1;
                if (abs(dx) <= WALK_PX) {
                    g_walk_react_rid = (rand() & 1) ? RID_EXP_HAPPY : RID_EXP_LOVE;
                    g_walk_react_until = now + WALK_REACT_MS;
                    g_walk_phase = 1;
                } else {
                    g_x += g_walk_dir_x * WALK_PX;
                    g_frame_idx++;
                    clamp_position();
                }
            } else if (g_walk_phase == 1) {
                if (now >= g_walk_react_until) {
                    int odx = g_walk_origin_x - g_x;
                    g_walk_dir_x = (odx > 0) ? 1 : -1;
                    g_walk_phase = 2;
                }
            } else {
                int origin_dx = g_walk_origin_x - g_x;
                g_walk_dir_x = (origin_dx > 0) ? 1 : -1;
                if (abs(origin_dx) <= WALK_RETURN_PX) {
                    g_x = g_walk_origin_x;
                    enter_idle();
                    render_full();
                    return;
                }
                g_x += g_walk_dir_x * WALK_RETURN_PX;
                g_frame_idx++;
                clamp_position();
            }
            render_full();
            break;
        }
        case ST_WALK_TARGET: {
            int dx = g_walk_target_x - g_x;
            int dy = g_walk_target_y - g_y;
            if (abs(dx) <= WALK_TARGET_PX && abs(dy) <= WALK_TARGET_PX) {
                g_x = g_walk_target_x;
                g_y = g_walk_target_y;
                clamp_position();
                if (g_walk_target_x == g_screen_w / 2 - PET_W / 2 &&
                    g_walk_target_y == g_screen_h / 2 - PET_H / 2) {
                    start_sign();
                    render_full();
                    return;
                }
                enter_idle();
                g_last_input_tick = now;
                render_full();
                return;
            }
            if (abs(dx) > 0) {
                g_walk_dir_x = (dx > 0) ? 1 : -1;
                g_walk_dir_y = 0;
                g_x += g_walk_dir_x * WALK_TARGET_PX;
                if ((g_walk_dir_x > 0 && g_x > g_walk_target_x) ||
                    (g_walk_dir_x < 0 && g_x < g_walk_target_x)) g_x = g_walk_target_x;
            } else {
                g_x = g_walk_target_x;
                g_walk_dir_x = 0;
                g_walk_dir_y = (dy > 0) ? 1 : -1;
                g_y += g_walk_dir_y * WALK_TARGET_PX;
                if ((g_walk_dir_y > 0 && g_y > g_walk_target_y) ||
                    (g_walk_dir_y < 0 && g_y < g_walk_target_y)) g_y = g_walk_target_y;
            }
            g_frame_idx++;
            clamp_position();
            render_full();
            break;
        }
        case ST_RETURN_HOME: {
            int dx = g_walk_target_x - g_x;
            int dy = g_walk_target_y - g_y;
            if (abs(dx) <= WALK_RETURN_PX && abs(dy) <= WALK_RETURN_PX) {
                g_x = g_walk_target_x;
                g_y = g_walk_target_y;
                if (g_walk_target_x == g_home_x && g_walk_target_y == g_home_y) enter_sleep();
                else enter_idle();
                render_full();
                return;
            }
            if (abs(dx) > 0) {
                g_walk_dir_x = (dx > 0) ? 1 : -1;
                g_walk_dir_y = 0;
                g_x += g_walk_dir_x * WALK_RETURN_PX;
                if ((g_walk_dir_x > 0 && g_x > g_walk_target_x) ||
                    (g_walk_dir_x < 0 && g_x < g_walk_target_x)) g_x = g_walk_target_x;
            } else {
                g_x = g_walk_target_x;
                g_walk_dir_x = 0;
                g_walk_dir_y = (dy > 0) ? 1 : -1;
                g_y += g_walk_dir_y * WALK_RETURN_PX;
                if ((g_walk_dir_y > 0 && g_y > g_walk_target_y) ||
                    (g_walk_dir_y < 0 && g_y < g_walk_target_y)) g_y = g_walk_target_y;
            }
            g_frame_idx++;
            clamp_position();
            render_full();
            break;
        }
        case ST_SIGN: {
            g_frame_idx++;
            if (now >= g_sign_until) {
                start_return_to_origin();
                g_last_input_tick = now;
                render_full();
                return;
            }
            render_full();
            break;
        }
        case ST_SLEEP: {
            g_frame_idx++;
            if (g_frame_idx >= SLEEP_N * 100) g_frame_idx = 0;
            render_full();
            break;
        }
        case ST_DRAG: {
            POINT pt; GetCursorPos(&pt);
            BOOL cur_lb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            DBG_LOG("STDRAG", g_scare_phase, g_drag_variant);
            if (!cur_lb) {
                g_dragging = FALSE;
                enter_idle();
                g_last_input_tick = GetTickCount();
                g_idle_since_tick = g_last_input_tick;
                DBG_LOG("LBTNUP", 0, 0);
                render_full();
                return;
            }
            /* DEBUG: 每帧写状态 (会多, 但能验证 ST_DRAG 在跑) */
            {
                static DWORD s_last_dbg = 0;
                DWORD _n = GetTickCount();
                if (_n - s_last_dbg > 500) {
                    s_last_dbg = _n;
                    DBG_LOG("DRAG", g_scare_phase, g_drag_variant);
                }
            }
            g_x = pt.x - g_drag_off.x;
            g_y = pt.y - g_drag_off.y;
            clamp_position();
            {
                int vx = pt.x - g_drag_prev_pt.x;
                int vy = pt.y - g_drag_prev_pt.y;
                /* #2 惊吓: 只看方向反转 (不管距离) */
                /* 惊吓: 严格阈值 (两个速度 >= 8px 且 dot <= -30, 角度 ~120度) */
                if ((vx*vx + vy*vy) >= 64 && (g_drag_prev_vx*g_drag_prev_vx + g_drag_prev_vy*g_drag_prev_vy) >= 64) {
                    int dot = vx*g_drag_prev_vx + vy*g_drag_prev_vy;
                    if (dot <= -30) {
                        g_scare_phase = 1;
                        DBG_LOG("SHAKE!", vx, vy);
                    }
                    g_drag_prev_vx = vx;
                    g_drag_prev_vy = vy;
                } else if (vx != 0 || vy != 0) {
                    /* 速度小: 更新 prev 但不触发 */
                    g_drag_prev_vx = vx;
                    g_drag_prev_vy = vy;
                }
                int new_variant = pick_drag_frame_by_velocity(vx, vy);
                if (new_variant >= 0) g_drag_variant = new_variant;
                g_drag_prev_pt = pt;
            }
            render_now();
            break;
        }
        case ST_ENTERING: {
            int dx = g_enter_target_x - g_x;
            int dy = g_enter_target_y - g_y;
            int dist = abs(dx) + abs(dy);
            /* 到达当前阶段目标 (<=4px) -> snap + 切换阶段或完成 */
            if (dist <= 4) {
                g_x = g_enter_target_x;
                g_y = g_enter_target_y;
                if (g_enter_phase == 0) {
                    /* 切换: 中间 -> home, 但先 roar 2s */
                    g_enter_phase = 1;
                    g_enter_target_x = g_home_x;
                    g_enter_target_y = g_home_y;
                    g_enter_roar_until = GetTickCount() + 2000;
                    g_frame_idx = 0;
                } else {
                    /* 完成入场 (roar 结束) */
                    g_walk_dir_x = 0;
                    g_walk_dir_y = 0;
                    g_enter_roar_until = 0;
                    enter_idle();
                    g_last_input_tick = GetTickCount();
                    g_idle_since_tick = g_last_input_tick;
                    render_full();
                    return;
                }
            } else if (g_enter_phase == 1 && g_enter_roar_until > 0 &&
                       GetTickCount() < g_enter_roar_until) {
                /* roar 阶段: 显示 roar 帧, 不移动 (等 2s) */
                g_walk_dir_x = 0;
                g_walk_dir_y = 0;
                render_full();
                break;
            } else {
                /* 各轴独立步长 (避免单轴越过 target) */
                /* 先横后竖 */
                if (abs(dx) > 0) {
                    if (dx > 0) { g_x += (dx > 32 ? 32 : dx); g_walk_dir_x = 1; }
                    else        { g_x -= (-dx > 32 ? 32 : -dx); g_walk_dir_x = -1; }
                    g_walk_dir_y = 0;
                } else {
                    g_x = g_enter_target_x;
                    g_walk_dir_x = 0;
                    if (dy > 0) { g_y += (dy > 32 ? 32 : dy); g_walk_dir_y = 1; }
                    else        { g_y -= (-dy > 32 ? 32 : -dy); g_walk_dir_y = -1; }
                }
                clamp_position();
                g_frame_idx++;
            }
            render_full();
            break;
        }
        case ST_AWAIT_TARGET: {
            if (g_exp_active && now >= g_exp_until) g_exp_active = FALSE;
            if (now >= g_await_target_until) {
                g_exp_active = FALSE;
                enter_idle();
                g_last_input_tick = now;
                render_full();
                return;
            }
            BOOL cur_lb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            if (g_prev_lbutton_down && !cur_lb) {
                POINT pt; GetCursorPos(&pt);
                if (!hit_test_pet(pt.x, pt.y)) {
                    int tx = pt.x - PET_W / 2;
                    int ty = pt.y - PET_H / 2;
                    start_walk_to_target(tx, ty);
                    g_last_input_tick = now;
                    render_full();
                    return;
                }
            }
            g_prev_lbutton_down = cur_lb;
            render_full();
            break;
        }
    }
}

static void tick_clock(void) {
    DWORD now_t = GetTickCount();
    if (now_t - g_last_topmost_tick >= TOPMOST_INTERVAL_MS) {
        SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        g_last_topmost_tick = now_t;
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    if (st.wMinute == g_last_minute) return;
    g_last_minute = st.wMinute;
    g_last_hour = st.wHour;
    DWORD now = GetTickCount();
    if (st.wMinute == 0 && g_state == ST_IDLE) { start_walk_auto(); return; }
    /* 半点举牌已删: 久坐提醒自己记时间(g_active_ms), 不再需要整点半点硬触发 */
    if (g_state == ST_IDLE && (now - g_idle_since_tick) > IDLE_HOME_MIN_MS) start_return_home();
}

/* 判断一个 Run 项的值是否指向 pet.exe.
 * 原实现用 strstr(value,"pet.exe"), 会误删值里含该子串的无关启动项;
 * 这里改为"去掉首尾引号/空白后, 必须以 \pet.exe 结尾". */
static BOOL path_is_pet_exe(const char* v) {
    size_t n, i;
    if (!v) return FALSE;
    n = strlen(v);
    while (n > 0 && (v[n - 1] == ' ' || v[n - 1] == '\t' || v[n - 1] == '"' ||
                     v[n - 1] == '\r' || v[n - 1] == '\n')) n--;
    i = 0;
    while (i < n && (v[i] == ' ' || v[i] == '\t' || v[i] == '"')) i++;
    if (n - i < 8) return FALSE;
    return lstrcmpiA(v + n - 8, "\\pet.exe") == 0;
}

static void cleanup_pet_run_keys(void) {
    /* 删除 Run 键里指向 pet.exe 的旧值, 防止历史残留导致开机自启多个进程.
     * 原路径写成 "Software\\\\Microsoft\\\\..." —— C 里 \\\\ 是两个反斜杠,
     * 拼出的路径非法, RegOpenKeyExA 必失败 => 这段清理逻辑从未真正生效过. */
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_READ | KEY_WRITE, &hKey) != ERROR_SUCCESS) return;
    char to_delete[16][256];
    int n = 0;
    DWORD index = 0;
    char name[256];
    char value[1024];
    DWORD nameLen, valueLen, type;
    while (n < 16) {
        /* 老版本写 Run 值时刻意少了 '\0'(见 set_autostart_enabled), 所以读出来的
         * 字符串可能没有结尾; 必须先把缓冲区清零并把长度减 1, 否则 strlen 会越界. */
        memset(name, 0, sizeof(name));
        memset(value, 0, sizeof(value));
        nameLen = sizeof(name) - 1;
        valueLen = sizeof(value) - 1;
        LONG e2 = RegEnumValueA(hKey, index, name, &nameLen, NULL, &type, (LPBYTE)value, &valueLen);
        if (e2 != ERROR_SUCCESS) break;
        if (path_is_pet_exe(value)) {
            strncpy(to_delete[n], name, 256);
            to_delete[n][255] = 0;
            n++;
        }
        index++;
    }
    for (int i = 0; i < n; i++) {
        RegDeleteValueA(hKey, to_delete[i]);
    }
    RegCloseKey(hKey);
}

static void get_autostart_enabled(BOOL* enabled) {
    HKEY hKey = NULL;   /* 原源码漏了这句声明, 导致整个文件无法编译 */
    LONG r = RegOpenKeyExA(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_QUERY_VALUE, &hKey);
    if (r != ERROR_SUCCESS) { *enabled = FALSE; return; }
    char path[MAX_PATH] = {0};
    DWORD sz = sizeof(path), type = 0;
    r = RegQueryValueExA(hKey, "PetDinoPet", NULL, &type, (LPBYTE)path, &sz);
    RegCloseKey(hKey);
    *enabled = (r == ERROR_SUCCESS);
}

static void set_autostart_enabled(BOOL enabled) {
    /* 先清理指向 pet.exe 的旧值(防止历史残留导致开机自启多个进程) */
    cleanup_pet_run_keys();
    HKEY hKey = NULL;
    LONG r = RegOpenKeyExA(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey);
    if (r != ERROR_SUCCESS) return;
    if (!enabled) {
        /* 原来这里直接 return, 导致"取消勾选"是个空操作;
         * 而且 cleanup 只处理以 \pet.exe 结尾的值, 本程序自己那条不会被它清掉. */
        RegDeleteValueA(hKey, "PetDinoPet");
        RegCloseKey(hKey);
        return;
    }
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    /* 原来长度用的是 strlen(path), 少了结尾的 '\0', 写进去的不是合法 REG_SZ */
    RegSetValueExA(hKey, "PetDinoPet", 0, REG_SZ, (LPBYTE)path, (DWORD)(strlen(path) + 1));
    RegCloseKey(hKey);
}

static UINT  g_wmTaskbarCreated = 0;
static void add_tray_icon(void) {
    if (g_tray_added) return;
    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    /* 用自己的恐龙图标(资源 ID 1); 资源缺失时退回系统默认图标,
     * 原来一直用的是 IDI_APPLICATION, 托盘里是那个通用白框图标. */
    g_nid.hIcon = LoadIconA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(1));
    if (!g_nid.hIcon) g_nid.hIcon = LoadIconA(NULL, IDI_APPLICATION);
    strncpy(g_nid.szTip, "PetDinoPet", sizeof(g_nid.szTip) - 1);
    g_tray_added = Shell_NotifyIconA(NIM_ADD, &g_nid);
}

static void remove_tray_icon(void) {
    if (!g_tray_added) return;
    Shell_NotifyIconA(NIM_DELETE, &g_nid);
    g_tray_added = FALSE;
}

static void show_tray_menu(void) {
    HMENU hMenu = CreatePopupMenu();
    BOOL autostart = FALSE;
    get_autostart_enabled(&autostart);
    UINT autostart_flag = MF_STRING | (autostart ? MF_CHECKED : 0);
    char menu_autostart_gbk[64];
    char menu_exit_gbk[64];
    utf8_to_gbk("\xe5\xbc\x80\xe6\x9c\xba\xe8\x87\xaa\xe5\x90\xaf", menu_autostart_gbk, sizeof(menu_autostart_gbk));
    utf8_to_gbk("\xe9\x80\x80\xe5\x87\xba DinoPet", menu_exit_gbk, sizeof(menu_exit_gbk));
    char menu_sleep_gbk[64];
    utf8_to_gbk("\xe5\x9b\x9e\xe7\xaa\x9d\xe7\x9d\xa1\xe8\xa7\x89", menu_sleep_gbk, sizeof(menu_sleep_gbk));
    AppendMenuA(hMenu, autostart_flag, ID_TRAY_AUTOSTART, menu_autostart_gbk);
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING, ID_TRAY_SLEEP, menu_sleep_gbk);
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    {
        /* 久坐提醒: 子菜单 关闭/30/45/60 分钟, 勾选当前值 */
        HMENU hSub = CreatePopupMenu();
        char b_off[32], b_30[32], b_45[32], b_60[32], b_title[32];
        utf8_to_gbk("\xe5\x85\xb3\xe9\x97\xad", b_off, sizeof(b_off));                        /* 关闭 */
        utf8_to_gbk("30 \xe5\x88\x86\xe9\x92\x9f", b_30, sizeof(b_30));                       /* 30 分钟 */
        utf8_to_gbk("45 \xe5\x88\x86\xe9\x92\x9f", b_45, sizeof(b_45));                       /* 45 分钟 */
        utf8_to_gbk("60 \xe5\x88\x86\xe9\x92\x9f", b_60, sizeof(b_60));                       /* 60 分钟 */
        utf8_to_gbk("\xe4\xb9\x85\xe5\x9d\x90\xe6\x8f\x90\xe9\x86\x92", b_title, sizeof(b_title)); /* 久坐提醒 */
        AppendMenuA(hSub, MF_STRING | (g_remind_min == 0  ? MF_CHECKED : 0), ID_TRAY_REMIND_OFF, b_off);
        AppendMenuA(hSub, MF_STRING | (g_remind_min == 30 ? MF_CHECKED : 0), ID_TRAY_REMIND_30,  b_30);
        AppendMenuA(hSub, MF_STRING | (g_remind_min == 45 ? MF_CHECKED : 0), ID_TRAY_REMIND_45,  b_45);
        AppendMenuA(hSub, MF_STRING | (g_remind_min == 60 ? MF_CHECKED : 0), ID_TRAY_REMIND_60,  b_60);
        AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hSub, b_title);
    }
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING, ID_TRAY_EXIT, menu_exit_gbk);
    POINT pt; GetCursorPos(&pt);
    /* TrackPopupMenu 的经典坑: 属主窗口不是前台窗口时, 菜单可能立刻消失
     * 或者点外面不关闭. 先把自己设为前台, 菜单返回后再补一条 WM_NULL. */
    SetForegroundWindow(g_hwnd);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                             pt.x, pt.y, 0, g_hwnd, NULL);
    PostMessageA(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
    if (cmd == ID_TRAY_AUTOSTART) set_autostart_enabled(!autostart);
    else if (cmd == ID_TRAY_SLEEP) {
        /* 回窝睡觉: 已经在窝就直接睡, 否则走回去 */
        if (g_x == g_home_x && g_y == g_home_y && g_state == ST_IDLE) {
            enter_sleep();
            render_full();
        } else {
            start_return_home();
        }
    }
    else if (cmd == ID_TRAY_EXIT) DestroyWindow(g_hwnd);
    else if (cmd == ID_TRAY_REMIND_OFF) set_remind_min(0);
    else if (cmd == ID_TRAY_REMIND_30)  set_remind_min(30);
    else if (cmd == ID_TRAY_REMIND_45)  set_remind_min(45);
    else if (cmd == ID_TRAY_REMIND_60)  set_remind_min(60);
}

static void preload_startup(void) {
    /* 只预热启动阶段真正会用到的帧(待机 4 + 四向走 12), 其余按需加载.
     * 这样启动更快, 常驻内存也从 47 张降到最多 FRAME_CACHE_MAX 张. */
    static const int warm[] = {
        RID_IDLE_01, RID_IDLE_02, RID_IDLE_03, RID_IDLE_04,
        RID_WALK_RIGHT_01, RID_WALK_RIGHT_02, RID_WALK_RIGHT_03, RID_WALK_RIGHT_04,
        RID_WALK_LEFT_01,  RID_WALK_LEFT_02,  RID_WALK_LEFT_03,  RID_WALK_LEFT_04,
        RID_WALK_UP_01,    RID_WALK_UP_02,
        RID_WALK_DOWN_01,  RID_WALK_DOWN_02
    };
    DBG_LOG("PRELOAD_BEGIN", 0, 0);
    for (int i = 0; i < (int)(sizeof(warm) / sizeof(warm[0])); i++) {
        (void)get_bmp(warm[i]);
    }
    DBG_LOG("PRELOAD_END", (int)g_frame_count, 0);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    /* 资源管理器重启后会广播 TaskbarCreated. 原来只 NIM_ADD 一次, 所以 explorer
     * 重启后图标永久消失(进程还在, 但没有任何入口可以退出它). */
    if (g_wmTaskbarCreated && msg == g_wmTaskbarCreated) {
        g_tray_added = FALSE;
        add_tray_icon();
        return 0;
    }
    switch (msg) {
        case WM_CREATE: {
            /* 内存优化: 宠物不接受任何文本输入, 没必要让输入法挂进来.
             * 实测 QQ拼音/MSCTF 注入会给本进程加上 7 个线程、250+ 个句柄、
             * 约 6MB 私有内存(工作集 +24MB). 这里解除本窗口的输入法关联,
             * 不改变窗口的激活行为, 因此右键菜单等交互不受影响. */
            ImmAssociateContext(hwnd, NULL);
            g_idle_since_tick = GetTickCount();
            g_last_input_tick = g_idle_since_tick;
            reset_anim_timer();
            /* 原来是 600000ms(10 分钟). 而 tick_clock() 里做的是"分钟变化"判定:
             *   wMinute==0   -> 整点巡游
             *   idle > 5min  -> 回窝睡觉
             * 挂在 10 分钟定时器上, 等于每 10 分钟才判一次且必须恰好落在 :00,
             * 实际几乎永不触发. 改为 60s, 两个时间型功能才按设计工作.
             * (原来的 wMinute==30 半点举牌已删: 久坐提醒自己记时间, 不再需要它)
             * TOPMOST 刷新仍受内部 TOPMOST_INTERVAL_MS(10 分钟) 控制, 不受影响. */
            SetTimer(hwnd, TIMER_CLOCK, 60000, NULL);
            /* #6 键盘监听: 启动 200ms 检测 timer */
            SetTimer(hwnd, TIMER_KEYBOARD_CHECK, KEYBOARD_CHECK_MS, NULL);
            return 0;
        }
        case WM_TIMER: {
            if (wp == TIMER_ANIM)      tick_anim();
            else if (wp == TIMER_CLOCK) tick_clock();
            else if (wp == TIMER_CLICK_WAIT) {
                KillTimer(hwnd, TIMER_CLICK_WAIT);
                int n = g_await_click_count;
                g_await_click_count = 0;
                if (n == 1) {
                    click_to_expression();   /* 走/拖动中也能被打断: 不限制 ST_IDLE */
                } else if (n == 2) {
                    start_walk_auto();   /* 走/拖动中也能被打断: 不限制 ST_IDLE */
                } else if (n >= 3) {
                    start_walk_to_sign();   /* 走/拖动中也能被打断: 不限制 ST_IDLE */
                }
            } else if (wp == TIMER_KEYBOARD_CHECK) {
                check_keyboard_activity();  /* 内含 SetTimer 状态切换 */
                check_activity();           /* 久坐提醒累计 */
            } else if (wp == TIMER_DIZZY_TICK) {
                if (g_scare_phase == 2) {
                    DWORD _now = GetTickCount();
                    /* 自动结束: dizzy_until 到期 → 回窝睡觉 */
                    if (_now >= g_scare_until) {
                        /* ★ 顺序关键: 先 enter_sleep 让 g_state=ST_SLEEP, 再清 dizzy, 这样消息队列里残留的 WM_TIMER_ANIM 触发时会进 ST_SLEEP 分支, 不会渲 idle */
                        if (g_state == ST_IDLE) enter_sleep();
                        g_scare_phase = 0;
                        render_now();
                    } else {
                        /* 帧推进: 剩余 > 1.5s 推进, 否则固定最后一帧 */
                        if (_now - g_dizzy_last_tick > DIZZY_FRAME_MS) {
                            if ((int)(g_scare_until - _now) > DIZZY_HOLD_MS && g_dizzy_frame_idx < DIZZY_FRAMES_N - 1) {
                                g_dizzy_frame_idx++;
                            } else {
                                g_dizzy_frame_idx = DIZZY_FRAMES_N - 1;
                            }
                            g_dizzy_last_tick = _now;
                        }
                        /* dizzy 期间 200ms 渲染一次 (覆盖 IDLE_MS 1800) */
                        SetTimer(hwnd, TIMER_ANIM, 200, NULL);
                    }
                }
            } else if (wp == TIMER_PRESS_POLL) {
                if (g_press_pending && !g_dragging) {
                    POINT pt; GetCursorPos(&pt);
                    int dx = pt.x - g_press_pt.x;
                    int dy = pt.y - g_press_pt.y;
                    if (dx*dx + dy*dy > CLICK_DRAG_THRESH * CLICK_DRAG_THRESH) {
                        KillTimer(hwnd, TIMER_PRESS_POLL);
                        KillTimer(hwnd, TIMER_CLICK_WAIT);
                        g_press_pending = FALSE;
                        g_await_click_count = 0;
                        g_dragging = TRUE;
                        g_state = ST_DRAG;
                        g_frame_idx = 0;
                        g_drag_variant = 0;
                        g_drag_prev_pt = pt;
                        KillTimer(hwnd, TIMER_ANIM);
                        SetTimer(hwnd, TIMER_ANIM, DRAG_TICK_MS, NULL);
                        g_last_tick_ms = 0;
                        render_full();
                    }
                }
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            POINT pt; GetCursorPos(&pt);
            if (g_dragging) render_now();
            else if (hit_test_pet(pt.x, pt.y)) {
                if (g_state == ST_SLEEP) enter_idle();
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            POINT pt; GetCursorPos(&pt);
            if (g_dragging) return 0;
            if (!hit_test_pet(pt.x, pt.y)) return 0;
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            g_last_topmost_tick = GetTickCount();
            g_press_pt = pt;
            g_drag_prev_pt = pt;
            g_drag_prev_vx = 0;
            g_drag_prev_vy = 0;
            g_press_pending = TRUE;
            g_drag_off.x = pt.x - g_x;
            g_drag_off.y = pt.y - g_y;
            g_last_input_tick = GetTickCount();
            g_idle_since_tick = g_last_input_tick;
            if (g_state == ST_SLEEP) enter_idle();
            if (g_state == ST_AWAIT_TARGET) {
                KillTimer(hwnd, TIMER_PRESS_POLL);
                g_press_pending = FALSE;
                switch_to_next_expression();
                return 0;
            }
            KillTimer(hwnd, TIMER_PRESS_POLL);
            SetTimer(hwnd, TIMER_PRESS_POLL, PRESS_POLL_MS, NULL);
            {
                DWORD now = GetTickCount();
                if (now - g_idle_since_tick > 400) g_await_click_count = 0;
                g_await_click_count++;
                KillTimer(hwnd, TIMER_CLICK_WAIT);
                SetTimer(hwnd, TIMER_CLICK_WAIT, CLICK_WAIT_MS, NULL);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            KillTimer(hwnd, TIMER_PRESS_POLL);
            DBG_LOG("LBTNUP_W", 0, 0);
            if (g_dragging) {
                g_dragging = FALSE;
                /* #2 松开 -> 如果之前在 shake, 切 dizzy 2s */
                if (g_scare_phase == 1) {
                    g_scare_phase = 2;
                    g_scare_until = GetTickCount() + DIZZY_TOTAL_MS;
                    g_dizzy_frame_idx = 0;
                    g_dizzy_last_tick = GetTickCount();
                    SetTimer(hwnd, TIMER_DIZZY_TICK, DIZZY_TICK_MS, NULL);
                } else {
                    g_scare_phase = 0;
                    /* #5 记忆: 正常拖完松手 -> 更新 home + 保存到 ini */
                    g_home_x = g_x;
                    g_home_y = g_y;
                    save_position();
                }
                enter_idle();
                g_last_input_tick = GetTickCount();
                g_idle_since_tick = g_last_input_tick;
                render_full();
            } else if (g_press_pending) {
                g_press_pending = FALSE;
            }
            return 0;
        }
        case WM_MOUSEACTIVATE: {
            /* 点宠物不再把当前窗口的焦点抢走.
             * 用 MA_NOACTIVATE 而不是 WS_EX_NOACTIVATE: 前者只"拒绝激活",
             * 但仍然保留鼠标消息, 因此拖动/点击换表情都照旧;
             * 后者会让窗口永远不能激活, 右键菜单可能弹不出来或点了不响应. */
            return MA_NOACTIVATE;
        }
        case WM_RBUTTONDOWN: {
            show_tray_menu();
            return 0;
        }
        case WM_TRAYICON: {
            if (LOWORD(lp) == WM_RBUTTONUP) show_tray_menu();
            return 0;
        }
        case WM_DESTROY: {
            remove_tray_icon();
            KillTimer(hwnd, TIMER_ANIM);
            KillTimer(hwnd, TIMER_CLOCK);
            KillTimer(hwnd, TIMER_KEYBOARD_CHECK);
            release_render_surfaces();
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show) {
    (void)hPrev; (void)show;
    srand((unsigned)time(NULL));
    DBG_LOG("START", 0, 0);

    /* 单实例保护: 原来双击两次就有两只宠物, 抢同一个 pos.ini 互相覆盖位置,
     * 托盘里出现两个图标, 拖动时两只互相打架.
     *
     * 只用互斥体, 且只在"确认已存在"时退出:
     *  - 名字带版本后缀, 避免和旧版本残留的命名对象冲突(实测残留对象会让
     *    CreateMutex 返回 ACCESS_DENIED, 若据此退出, 程序就永远起不来);
     *  - 创建失败一律不阻止启动 —— 打不开比多开一只严重得多.
     * 曾试过用窗口类名兜底, 但被杀进程可能留下僵尸窗口, 同样会堵死启动. */
    /* 只用普通互斥体, 且只在"确认已存在"时退出:
     *  - 名字带版本后缀, 避免与旧版本遗留的命名对象冲突;
     *  - 打开失败(受限令牌/沙箱会返回 ACCESS_DENIED)一律不阻止启动 ——
     *    打不开远好过多开一只; 曾试过"打开失败就退出", 结果程序在受限环境里根本起不来.
     * 注: 受限环境里单实例可能失效, 这是环境限制, 不是代码能绕过的. */
    HANDLE hMutex = CreateMutexA(NULL, TRUE, "PetDinoPet_SingleInstance_31");
    if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return 0;
    }
    g_wmTaskbarCreated = RegisterWindowMessageA("TaskbarCreated");

    g_screen_w = GetSystemMetrics(SM_CXSCREEN);
    g_screen_h = GetSystemMetrics(SM_CYSCREEN);

    preload_startup();

    /* #5 记忆: 默认 home (右下角), 但从 ini 读上次位置覆盖 */
    g_home_x = g_screen_w - PET_W - 40;
    g_home_y = g_screen_h - PET_H - 60;
    load_position(g_home_x, g_home_y, &g_x, &g_y);
    g_remind_min = load_remind_min();   /* 久坐提醒间隔(分钟), 0=关闭 */
    /* 更新 home = 读取到的位置 */
    g_home_x = g_x;
    g_home_y = g_y;

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = "PetDinoClass";
    if (!RegisterClassA(&wc)) {
        MessageBoxA(NULL, "RegisterClass failed", "pet", MB_ICONERROR);
        return 1;
    }

    g_hwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        "PetDinoClass", "PetDeskPet",
        WS_POPUP,
        g_x, g_y, PET_W, PET_H,
        NULL, NULL, hInst, NULL);
    if (!g_hwnd) {
        MessageBoxA(NULL, "CreateWindowEx failed", "pet", MB_ICONERROR);
        return 1;
    }
    ShowWindow(g_hwnd, SW_SHOW);
    add_tray_icon();

    render_full();  /* 渲染 home 位 (可见) */

    /* 入场动画: 现在 g_hwnd 有效, timer 能注册 */
    start_entering();

    /* 测试开关改为互相独立(可组合), 方便 --remind-set=0 --test-remind 这样联调 */
    if (strstr(cmd, "--test-walk"))        { Sleep(1000); start_walk_auto(); }
    if (strstr(cmd, "--test-sign-now"))    { Sleep(500); start_sign(); }
    if (strstr(cmd, "--test-sleep-now"))   { Sleep(500); g_idle_since_tick = GetTickCount() - IDLE_HOME_MIN_MS - 1000; }
    if (strstr(cmd, "--remind-set"))   {
        /* 测试/脚本用: --remind-set=0|30|45|60, 等价于点托盘菜单里对应那一项,
         * 让宠物自己把 RemindMin 写进 pos.ini(pos.ini 只有本进程能写). */
        const char* eq = strchr(cmd, '=');
        int m = eq ? atoi(eq + 1) : 45;
        if (m < 0) m = 0;
        set_remind_min(m);
    }
    if (strstr(cmd, "--test-remind"))  {
        /* 测试开关: 把累计值顶到"当前配置的阈值前 2 秒", 走真正的触发路径 */
        int m = (g_remind_min > 0) ? g_remind_min : 45;
        g_active_ms = (DWORD)m * 60u * 1000u - 2000u;
        g_last_activity_tick = GetTickCount();
    }

    render_full();

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    remove_tray_icon();
    for (int i = 0; i < RID_LAST - RID_FIRST; i++) {
        if (g_bmp[i]) DeleteObject(g_bmp[i]);
    }
    return 0;
}
