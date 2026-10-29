// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WideOpen contributors. https://github.com/Lyrixxx/WideOpen-TWAU-Remastered
// TWAU Widescreen: ultrawide / any-aspect fix for The Wolf Among Us Remastered (Telltale Tool, 2026).
//
// Loaded by the game as dinput8.dll (it imports DirectInput8Create); the real one is loaded from the
// system folder and every call is passed through.
//
// What the game does (TheWolfAmongUsRemastered.exe, Oct 2026 build):
//  - Camera FOV is stored as a 4:3-style angle; the vertical FOV is 0.75 * that angle and the horizontal
//    one is vertical * aspect. So the engine is already Hor+ - as long as the camera's aspect is the
//    screen's.
//  - Camera aspect: Camera::SetAspectRatio(cam, a) stores `a` in the camera; a value > 0 overrides the
//    screen aspect (0 = use the screen). Scene cameras get it from their "Aspect Ratio" property (16:9),
//    so on a 32:9 screen the picture is a 16:9 image stretched to fill.
//  - Lua RenderForce_16_by_9_AspectRatio() turns on a 16:9 viewport (pillarboxing).
//
// The fix: Camera::SetAspectRatio calls that come from the property system (scene data) store 0 (use
// the screen aspect) or the AspectRatio from the ini; direct engine calls (cube maps, special cameras)
// pass through unchanged. The forced 16:9 viewport is blocked, and the options menu's built-in list of
// allowed resolutions (16:9 / 16:10 only) is removed so the monitor's native 21:9 / 32:9 mode can be picked.
//
// Functions are found by meaning, not by address: the camera property registration that names
// "Aspect Ratio" and the Lua registration of "RenderForce_16_by_9_AspectRatio", so small game updates
// should not break it. Built without a C runtime (see build.sh).

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef int i32;
typedef long long i64;
typedef void *HANDLE;
typedef int BOOL;
typedef unsigned short wchar;
#define WINAPI __stdcall
#define NULL ((void *)0)

__declspec(dllimport) HANDLE WINAPI GetModuleHandleW(const wchar *);
__declspec(dllimport) u32 WINAPI GetModuleFileNameW(HANDLE, wchar *, u32);
__declspec(dllimport) BOOL WINAPI VirtualProtect(void *, u64, u32, u32 *);
__declspec(dllimport) BOOL WINAPI FlushInstructionCache(HANDLE, const void *, u64);
__declspec(dllimport) HANDLE WINAPI GetCurrentProcess(void);
__declspec(dllimport) HANDLE WINAPI LoadLibraryW(const wchar *);
__declspec(dllimport) void *WINAPI GetProcAddress(HANDLE, const char *);
__declspec(dllimport) u32 WINAPI GetSystemDirectoryW(wchar *, u32);
__declspec(dllimport) HANDLE WINAPI CreateFileW(const wchar *, u32, u32, void *, u32, u32, HANDLE);
__declspec(dllimport) BOOL WINAPI WriteFile(HANDLE, const void *, u32, u32 *, void *);
__declspec(dllimport) BOOL WINAPI FlushFileBuffers(HANDLE);
__declspec(dllimport) u32 WINAPI GetPrivateProfileStringW(const wchar *, const wchar *, const wchar *, wchar *, u32,
                                                          const wchar *);
__declspec(dllimport) BOOL WINAPI DisableThreadLibraryCalls(HANDLE);
__declspec(dllimport) HANDLE WINAPI CreateThread(void *, u64, u32(WINAPI *)(void *), void *, u32, u32 *);
__declspec(dllimport) void WINAPI Sleep(u32);
__declspec(dllimport) u64 WINAPI GetTickCount64(void);
typedef struct {
    u16 year, month, dow, day, hour, minute, second, ms;
} SYSTEMTIME_;
__declspec(dllimport) void WINAPI GetLocalTime(SYSTEMTIME_ *);
__declspec(dllimport) BOOL WINAPI CreateDirectoryW(const wchar *, void *);
__declspec(dllimport) BOOL WINAPI ReadFile(HANDLE, void *, u32, u32 *, void *);
__declspec(dllimport) BOOL WINAPI CloseHandle(HANDLE);
__declspec(dllimport) BOOL WINAPI DeleteFileW(const wchar *);
typedef struct { // WIN32_FIND_DATAW: FILETIMEs are two DWORDs (4-byte aligned), so the name sits at 44
    u32 attrs;
    u32 created[2], accessed[2], written[2]; // low, high
    u32 size_hi, size_lo, res0, res1;
    wchar name[260];
    wchar alt[14];
} FINDDATA_;
_Static_assert(__builtin_offsetof(FINDDATA_, name) == 44, "WIN32_FIND_DATAW layout");
__declspec(dllimport) HANDLE WINAPI FindFirstFileW(const wchar *, FINDDATA_ *);
__declspec(dllimport) BOOL WINAPI FindNextFileW(HANDLE, FINDDATA_ *);
__declspec(dllimport) BOOL WINAPI FindClose(HANDLE);
__declspec(dllimport) u32 WINAPI GetPrivateProfileSectionW(const wchar *, wchar *, u32, const wchar *);

int _fltused = 0;

void *memset(void *d, int c, u64 n) {
    volatile u8 *p = d;
    while (n--) *p++ = (u8)c;
    return d;
}
void *memcpy(void *d, const void *s, u64 n) {
    volatile u8 *p = d;
    const u8 *q = s;
    while (n--) *p++ = *q++;
    return d;
}

#define VERSION "1.0.0"

// build.sh passes WO_DEV=1 for -dev versions: test conveniences on (speed / screenshot hotkeys, detailed
// log). Release builds ship the settings players want, with nothing extra switched on.
#ifndef WO_DEV
#define WO_DEV 0
#endif

// ---------------------------------------------------------------- config

static struct {
    int enabled;      // master switch
    float aspect;     // 0 = follow the screen (auto); otherwise the aspect scene cameras get
    int block16x9;    // ignore RenderForce_16_by_9_AspectRatio (pillarboxing)
    int mode169;      // [Display] Mode=16:9: the game's own 16:9 picture with black bars, at any resolution
    int all_res;      // list every display mode, not only the built-in 16:9/16:10 sizes
    int log;          // 0 off, 1 normal (what the mod did, once), 2 detailed (everything, for bug hunting)
    int aa;           // EnumRenderAntialiasType to start with, -1 = leave the game's default
    int jitter;       // EnumRenderTAAJitterType to start with, -1 = leave the game's default
    int mipbias;      // "Render - TAA Mip Bias" (texture sharpening under TAA), -1 = game default (2)
    int skip_to_menu; // skip the legal screen and "press any key" title
    int hud_edges;    // HUD elements the scripts pin to 16:9 positions go to the real screen edges
    int fix_overlay;  // loading-screen overlay sprites keep their shape
    float fov_mul;    // FOV multiplier for game cameras without their own [Camera] value (1 = game)
    int menu_bars;    // menu edge frames that black out past 16:9: 0 game, 1 stretch to the screen, 2 hide
    int dev_menus;    // the scripts' developer menus (IsShipping() = false)
    int fx_169;       // mirror / effects cameras keep a 16:9 aspect
    char hide_lua[2048]; // "TWAUWS_HIDE = {'a','b'}" from [UI] HideAgents
} cfg = {1, 0.0f, 1, 0, 1, 1, 9, 4, 0, 1, 1, 1, 1.0f, 1, 0, 1, {0}};

// camera FOV per type (see camera_type) and [CameraOverrides] name prefixes
enum { CT_OTHER, CT_EXPLORE, CT_CUTSCENE, CT_DIALOGUE, CT_MENU, CT_UI, CT_FX, CT_COUNT };
#define CT_FIRST_FIXED CT_MENU // types from here on default to 1.0 instead of FOVMultiplier
static const char *const k_ctype[CT_COUNT] = {"other", "explore", "cutscene", "dialogue", "menu", "ui", "effects"};
static float g_fov_type[CT_COUNT] = {1, 1, 1, 1, 1, 1, 1};
#define MAX_CAMOVR 32
static struct {
    char prefix[64];
    float mul;
} g_camovr[MAX_CAMOVR];
static int g_ncamovr;

static volatile int g_vanilla; // comparison screenshot: the game's own AA settings for the "original" frame

static wchar g_dir[520]; // game folder, with trailing backslash

static int wlen(const wchar *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}
static void wcat(wchar *d, const wchar *s) {
    d += wlen(d);
    while ((*d++ = *s++)) {
    }
}

// "auto", "2.3889", "21:9", "5120x1440"
static float parse_aspect(const wchar *s) {
    float a = 0, b = 0, *cur = &a, frac = 0;
    int sep = 0;
    for (; *s; s++) {
        wchar c = *s;
        if (c >= '0' && c <= '9') {
            if (frac > 0) {
                *cur += (c - '0') * frac;
                frac *= 0.1f;
            } else
                *cur = *cur * 10 + (c - '0');
        } else if (c == '.' && frac == 0)
            frac = 0.1f;
        else if ((c == ':' || c == 'x' || c == 'X' || c == '/') && !sep) {
            sep = 1;
            cur = &b;
            frac = 0;
        } else if (c == ' ' || c == '\t')
            continue;
        else
            return 0; // "auto" or junk
    }
    if (sep) return b > 0 ? a / b : 0;
    return a;
}

// strict: [+-]digits[.digits] | true/false/on/off/yes/no. Returns 0 (and leaves *out) when it isn't one.
static int parse_value(const wchar *s, float *out) {
    while (*s == ' ' || *s == '\t') s++;
    wchar c0 = (wchar)(*s | 32);
    if (c0 == 't' || c0 == 'y' || (c0 == 'o' && (s[1] | 32) == 'n')) return *out = 1, 1;
    if (c0 == 'f' || c0 == 'n' || (c0 == 'o' && (s[1] | 32) == 'f')) return *out = 0, 1;
    int neg = *s == '-';
    if (*s == '-' || *s == '+') s++;
    float v = 0, frac = 0;
    int digits = 0;
    for (; *s; s++) {
        if (*s >= '0' && *s <= '9') {
            if (frac > 0) v += (*s - '0') * frac, frac *= 0.1f;
            else v = v * 10 + (*s - '0');
            digits++;
        } else if (*s == '.' && frac == 0)
            frac = 0.1f;
        else if (*s == ' ' || *s == '\t')
            continue;
        else
            return 0;
    }
    if (!digits) return 0;
    *out = neg ? -v : v;
    return 1;
}

static wchar g_ini[600];

// read [sec] key, falling back to the pre-0.8 name ([osec] okey) and then to def; cuts "; comments"
static void ini_get(const wchar *sec, const wchar *key, const wchar *osec, const wchar *okey, const wchar *def,
                    wchar *buf, u32 n) {
    GetPrivateProfileStringW(sec, key, L"\x01", buf, n, g_ini);
    if (buf[0] == 1 && osec) GetPrivateProfileStringW(osec, okey ? okey : key, L"\x01", buf, n, g_ini);
    if (buf[0] == 1) {
        u32 i = 0;
        for (; def[i] && i + 1 < n; i++) buf[i] = def[i];
        buf[i] = 0;
    }
    for (wchar *c = buf; *c; c++)
        if (*c == ';' || *c == '#') {
            *c = 0;
            break;
        }
    for (int l = wlen(buf); l && (buf[l - 1] == ' ' || buf[l - 1] == '\t'); l--) buf[l - 1] = 0;
}

static void load_config(void) {
    wchar *ini = g_ini, buf[64];
    ini[0] = 0;
    wcat(ini, g_dir);
    wcat(ini, L"TWAUWidescreen.ini");
#define BOOLKEY(sec, key, osec, okey, field)                                                              \
    ini_get(sec, key, osec, okey, L"", buf, 64);                                                           \
    {                                                                                                      \
        float b_;                                                                                          \
        if (buf[0] && parse_value(buf, &b_)) cfg.field = b_ != 0; /* else: keep the default */             \
    }
    BOOLKEY(L"General", L"Enabled", L"Fix", NULL, enabled);
    ini_get(L"General", L"Log", L"Debug", NULL, WO_DEV ? L"2" : L"1", buf, 64);
    cfg.log = (buf[0] == '0' || buf[0] == 'o' || buf[0] == 'O' || buf[0] == 'n' || buf[0] == 'N') ? 0
              : (buf[0] == '2' || buf[0] == 'd' || buf[0] == 'D')                                ? 2
                                                                                                 : 1;
    // Mode decides black bars; Block16x9 is the pre-1.0 key (no longer in the ini): Block16x9=0 = 16:9 mode
    BOOLKEY(L"Display", L"Block16x9", L"Fix", NULL, block16x9);
    ini_get(L"Display", L"Mode", NULL, NULL, L"", buf, 64);
    if (buf[0]) cfg.mode169 = buf[0] == '1' && buf[1] == '6'; // "16:9" / "16x9"; anything else = widescreen
    else cfg.mode169 = !cfg.block16x9;
    cfg.block16x9 = !cfg.mode169;
    BOOLKEY(L"Display", L"AllResolutions", L"Fix", NULL, all_res);
    ini_get(L"Display", L"AspectRatio", L"Fix", NULL, L"auto", buf, 64);
    cfg.aspect = parse_aspect(buf);
    // EnumRenderAntialiasType: 0 none, 1 FXAA, 2 SMAA, 3-5 MSAA 2/4/8x, 6-8 TAA+MSAA 2/4/8x, 9 TAA.
    // MSAA is not offered: the PC render path turns MSAA into FXAA and TAA+MSAA into plain TAA.
    ini_get(L"AntiAliasing", L"Method", L"AntiAliasing", L"Mode", L"auto", buf, 64);
    if (buf[0] == 'g' || buf[0] == 'G') cfg.aa = -1;
    else if (buf[0] == 'n' || buf[0] == 'N' || buf[0] == '0') cfg.aa = 0;
    else if (buf[0] == 'f' || buf[0] == 'F') cfg.aa = 1;
    else if (buf[0] == 's' || buf[0] == 'S') cfg.aa = 2;
    else cfg.aa = 9; // auto / taa
    // EnumRenderTAAJitterType: 1 none, 2 uniform 2x, 3 Hammersley 4x, 4 Hammersley 8x (game default: none)
    ini_get(L"AntiAliasing", L"TAAJitter", NULL, NULL, L"auto", buf, 64);
    if (buf[0] == 'g' || buf[0] == 'G') cfg.jitter = -1;
    else if (buf[0] == 'n' || buf[0] == 'N' || buf[0] == '0') cfg.jitter = 1;
    else if (buf[0] == '2') cfg.jitter = 2;
    else if (buf[0] == '4') cfg.jitter = 3;
    else cfg.jitter = 4; // auto / 8x
    // texture LOD bias used with TAA: game default 2 (sharper, can shimmer/oversharpen); auto = 0
    ini_get(L"AntiAliasing", L"Sharpening", L"AntiAliasing", L"TAAMipBias", L"auto", buf, 64);
    if (buf[0] == 'g' || buf[0] == 'G') cfg.mipbias = -1;
    else if (buf[0] >= '0' && buf[0] <= '9') cfg.mipbias = (int)parse_aspect(buf);
    else cfg.mipbias = 0;
    BOOLKEY(L"Startup", L"SkipToMenu", NULL, NULL, skip_to_menu);
    BOOLKEY(L"UI", L"InventoryAtScreenEdge", L"UI", L"HUDAtScreenEdges", hud_edges);
    BOOLKEY(L"UI", L"FixLoadingIcon", NULL, NULL, fix_overlay);
    {
        // [UI] HideAgents = comma-separated agent names -> Lua table (names: letters, digits, _ - . only)
        static wchar list[1024];
        ini_get(L"UI", L"HideElements", L"UI", L"HideAgents", L"", list, 1024);
        int o = 0;
        const char *head = "TWAUWS_HIDE = {";
        while (*head) cfg.hide_lua[o++] = *head++;
        int open = 0;
        for (wchar *c = list; *c && o < 2000; c++) {
            wchar ch = *c;
            int ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' ||
                     ch == '-' || ch == '.';
            if (ok) {
                if (!open) cfg.hide_lua[o++] = '\'', open = 1;
                cfg.hide_lua[o++] = (char)ch;
            } else if (ch == ',' || ch == ';') {
                if (open) cfg.hide_lua[o++] = '\'', cfg.hide_lua[o++] = ',', open = 0;
                if (ch == ';') break;
            }
        }
        if (open) cfg.hide_lua[o++] = '\'';
        cfg.hide_lua[o++] = '}';
        cfg.hide_lua[o] = 0;
    }
    ini_get(L"UI", L"MenuEdgeFrames", NULL, NULL, L"stretch", buf, 64);
    cfg.menu_bars = (buf[0] == 'h' || buf[0] == 'H') ? 2 : (buf[0] == 'g' || buf[0] == 'G' || buf[0] == '0') ? 0 : 1;
    BOOLKEY(L"Dev", L"DebugMenus", NULL, NULL, dev_menus);
    BOOLKEY(L"Camera", L"FixMirrors", L"Camera", L"MirrorsAt16x9", fx_169);
    ini_get(L"Camera", L"FOV", L"Camera", L"FOVMultiplier", L"1.0", buf, 64);
    cfg.fov_mul = parse_aspect(buf);
    if (cfg.fov_mul < 0.5f || cfg.fov_mul > 2.0f) cfg.fov_mul = 1.0f;
    {
        // per type: blank = FOV for game cameras, 1.0 for menu/UI/effects cameras
        static const wchar *const keys[CT_COUNT] = {L"FOVOther", L"FOVExplore", L"FOVCutscene", L"FOVDialogue",
                                                    L"FOVMenuBackground", L"FOVUI", L"FOVEffects"};
        for (int t = 0; t < CT_COUNT; t++) {
            ini_get(L"Camera", keys[t], t == CT_MENU ? L"Camera" : NULL, t == CT_MENU ? L"FOVMenu" : NULL, L"", buf, 64);
            float v = buf[0] ? parse_aspect(buf) : (t >= CT_FIRST_FIXED ? 1.0f : cfg.fov_mul);
            g_fov_type[t] = (v >= 0.5f && v <= 2.0f) ? v : 1.0f;
        }
        // [CameraOverrides] camera-name prefix = multiplier (first match wins; names are in the log)
        static wchar sec[4096];
        u32 sn = GetPrivateProfileSectionW(L"CameraOverrides", sec, 4096, ini);
        for (wchar *e = sec; e < sec + sn && *e && g_ncamovr < MAX_CAMOVR; e += wlen(e) + 1) {
            if (*e == ';' || *e == '#') continue;
            int k = 0;
            wchar *v = e;
            while (*v && *v != '=' && k < 63) g_camovr[g_ncamovr].prefix[k++] = (char)*v++;
            while (k && g_camovr[g_ncamovr].prefix[k - 1] == ' ') k--;
            g_camovr[g_ncamovr].prefix[k] = 0;
            if (*v != '=' || !k) continue;
            float m = parse_aspect(v + 1);
            if (m >= 0.5f && m <= 2.0f) g_camovr[g_ncamovr++].mul = m;
        }
    }
}

// ---------------------------------------------------------------- log

static HANDLE g_log;
static volatile long g_log_lock;
static char g_line[65536];
static int g_pos;

static void put(const char *s) {
    while (*s && g_pos < (int)sizeof g_line - 2) g_line[g_pos++] = *s++;
}
static void put_hex(u64 v) {
    char b[19];
    int i = 18;
    b[i] = 0;
    do {
        b[--i] = "0123456789abcdef"[v & 15];
        v >>= 4;
    } while (v);
    b[--i] = 'x';
    b[--i] = '0';
    put(b + i);
}
static void put_int(i64 v) {
    char b[24];
    int i = 23;
    int neg = v < 0;
    u64 u = neg ? (u64)-v : (u64)v;
    b[i] = 0;
    do {
        b[--i] = '0' + (char)(u % 10);
        u /= 10;
    } while (u);
    if (neg) b[--i] = '-';
    put(b + i);
}
static void put_f(float f) {
    if (f != f) {
        put("nan");
        return;
    }
    if (f < 0) {
        put("-");
        f = -f;
    }
    if (f > 1e9f) {
        put("big");
        return;
    }
    u64 w = (u64)f;
    put_int((i64)w);
    put(".");
    u64 fr = (u64)((f - (float)w) * 10000.0f + 0.5f);
    if (fr >= 10000) fr = 9999;
    char b[5] = {(char)('0' + fr / 1000), (char)('0' + fr / 100 % 10), (char)('0' + fr / 10 % 10),
                 (char)('0' + fr % 10), 0};
    put(b);
}
static u64 g_t0;
static void line_begin(void) {
    while (__sync_lock_test_and_set(&g_log_lock, 1)) {
    }
    g_pos = 0;
    // [  12.3s] since the DLL loaded
    u64 ms = GetTickCount64() - g_t0;
    char b[16];
    int i = 15;
    b[i] = 0;
    b[--i] = ' ';
    b[--i] = ']';
    b[--i] = 's';
    b[--i] = (char)('0' + ms / 100 % 10);
    b[--i] = '.';
    u64 sec = ms / 1000;
    do {
        b[--i] = (char)('0' + sec % 10);
        sec /= 10;
    } while (sec && i > 2);
    while (i > 9) b[--i] = ' ';
    b[--i] = '[';
    put(b + i);
}
#define DETAIL (cfg.log >= 2)
static void line_end(void) {
    g_line[g_pos++] = '\r';
    g_line[g_pos++] = '\n';
    if (g_log) {
        u32 n;
        WriteFile(g_log, g_line, (u32)g_pos, &n, NULL);
        FlushFileBuffers(g_log);
    }
    __sync_lock_release(&g_log_lock);
}
static void log_s(const char *s) {
    if (!g_log) return;
    line_begin();
    put(s);
    line_end();
}

static wchar g_log_path[720];
static char g_gamever[32] = "unknown", g_gamebuild[24] = "unknown";

// exe VS_FIXEDFILEINFO (signature 0xFEEF04BD in .rsrc): FileVersion, e.g. 2026.6.12.2153
static void read_game_version(u8 *exe) {
    u8 *nt = exe + *(u32 *)(exe + 0x3c);
    u16 nsec = *(u16 *)(nt + 6), optsz = *(u16 *)(nt + 20);
    u8 *sec = nt + 24 + optsz;
    for (int i = 0; i < nsec; i++, sec += 40) {
        if (sec[0] != '.' || sec[1] != 'r' || sec[2] != 's' || sec[3] != 'r') continue;
        u8 *r = exe + *(u32 *)(sec + 12);
        u32 n = *(u32 *)(sec + 8);
        for (u32 k = 0; k + 16 < n; k += 4)
            if (*(u32 *)(r + k) == 0xFEEF04BD) {
                u32 ms = *(u32 *)(r + k + 8), ls = *(u32 *)(r + k + 12);
                u32 v[4] = {ms >> 16, ms & 0xffff, ls >> 16, ls & 0xffff};
                int o = 0;
                for (int j = 0; j < 4; j++) {
                    char t[8];
                    int q = 0;
                    u32 x = v[j];
                    do t[q++] = (char)('0' + x % 10), x /= 10;
                    while (x);
                    while (q) g_gamever[o++] = t[--q];
                    if (j < 3) g_gamever[o++] = '.';
                }
                g_gamever[o] = 0;
                return;
            }
    }
}

static int read_small_file(const wchar *path, char *buf, u32 cap) {
    HANDLE h = CreateFileW(path, 0x80000000, 1, NULL, 3, 0x80, NULL); // GENERIC_READ, OPEN_EXISTING
    if (h == (HANDLE)(i64)-1) return 0;
    u32 n = 0;
    ReadFile(h, buf, cap - 1, &n, NULL);
    CloseHandle(h);
    buf[n] = 0;
    return (int)n;
}

// "key" "value" in a Steam .acf / "key": "value" in a GOG .info (first match); returns length or 0
static int kv_get(const char *buf, const char *key, char *out, int cap) {
    int kl = 0;
    while (key[kl]) kl++;
    for (const char *c = buf; *c; c++) {
        if (*c != '"') continue;
        int i = 0;
        while (i < kl && (c[1 + i] | 32) == (key[i] | 32)) i++;
        if (i < kl || c[1 + kl] != '"') continue;
        c += kl + 2;
        while (*c && *c != '"' && *c != '\n') c++; // to the value's opening quote
        if (*c != '"') return 0;
        c++;
        int o = 0;
        while (*c && *c != '"' && o < cap - 1) out[o++] = *c++;
        out[o] = 0;
        return o;
    }
    return 0;
}

// name of the n-th folder from the end of g_dir (0 = the game folder itself), lower-cased into out
static void dir_part(int n, char *out, int cap) {
    int end = wlen(g_dir) - 1; // g_dir ends with a backslash
    for (int k = 0; k < n && end > 0; k++) {
        end--;
        while (end > 0 && g_dir[end] != '\\' && g_dir[end] != '/') end--;
    }
    int st = end - 1;
    while (st >= 0 && g_dir[st] != '\\' && g_dir[st] != '/') st--;
    int o = 0;
    for (int i = st + 1; i < end && o < cap - 1; i++) {
        wchar ch = g_dir[i];
        out[o++] = (char)((ch >= 'A' && ch <= 'Z') ? ch + 32 : (ch < 128 ? ch : '?'));
    }
    out[o] = 0;
}

static int streq_i(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if ((*a | 32) != (*b | 32)) return 0;
    return *a == *b;
}

// Which store this copy came from, and its build id - only from files that provably describe THIS folder:
//   Steam: game folder is <library>\steamapps\common\<installdir>\, the manifest beside "common" names
//          that installdir; StateFlags without bit 4 (fully installed) = files may not match the build.
//   GOG:   goggame-<id>.info in the game folder ("buildId").   Epic: .egstore folder in the game folder.
static char g_store[16] = "unknown";
static char g_store_why[400]; // why Steam didn't match (logged when the store stays unknown)
static void why(const char *a, const char *b, const char *c, const char *d) {
    int o = 0;
    for (const char *const *t = (const char *const[]){a, b, c, d, NULL}; *t; t++)
        for (const char *q = *t; *q && o < (int)sizeof g_store_why - 1; q++) g_store_why[o++] = *q;
    g_store_why[o] = 0;
}
static void why_tail(void) {
    int o = 0;
    while (g_store_why[o]) o++;
    if (o < (int)sizeof g_store_why - 2) g_store_why[o++] = ')', g_store_why[o] = 0;
}
static void read_store_build(void) {
    static char buf[16384];
    static wchar p[720];
    char id[16], v[64], folder[128], parent[32], grand[32];
    int k = 0;
    dir_part(0, folder, sizeof folder);
    dir_part(1, parent, sizeof parent);
    dir_part(2, grand, sizeof grand);
    p[0] = 0;
    wcat(p, g_dir);
    wcat(p, L"steam_appid.txt");
    if (read_small_file(p, buf, 64))
        for (char *c = buf; *c >= '0' && *c <= '9' && k < 15; c++) id[k++] = *c;
    id[k] = 0;
    if (!k) why("no steam_appid.txt in the game folder", "", "", "");
    else if (!streq_i(parent, "common") || !streq_i(grand, "steamapps"))
        why("game folder is not <library>\\steamapps\\common\\<folder> (saw ...\\", grand, "\\", parent), why_tail();
    if (streq_i(parent, "common") && streq_i(grand, "steamapps")) {
        // try the manifest named by steam_appid.txt first (it can hold a placeholder id such as 480), then
        // every appmanifest_*.acf in this steamapps folder: the one whose installdir is this folder
        // <library>\steamapps\ = g_dir minus "common\<folder>\"
        static wchar sa[720];
        int cut = wlen(g_dir) - 1, seps = 0;
        while (cut > 0 && seps < 2) {
            cut--;
            if (g_dir[cut] == '\\' || g_dir[cut] == '/') seps++;
        }
        for (int i = 0; i <= cut; i++) sa[i] = g_dir[i];
        sa[cut + 1] = 0;
        int nn = 0, nread = 0, nwolf = 0;
        char seen[160];
        seen[0] = 0;
        for (int pass = 0; pass < 2; pass++) { // pass 0: appmanifest_<steam_appid.txt>.acf, pass 1: all of them
            FINDDATA_ fd0;
            p[0] = 0;
            wcat(p, sa);
            wcat(p, L"appmanifest_");
            if (pass == 0) {
                if (!k) continue;
                int o = wlen(p);
                for (int i = 0; i < k; i++) p[o++] = (wchar)id[i];
                p[o] = 0;
                wcat(p, L".acf");
            } else
                wcat(p, L"*.acf");
            HANDLE fh = FindFirstFileW(p, &fd0);
            if (fh == (HANDLE)(i64)-1) continue;
            int found = 0;
            do {
                nn++;
                p[0] = 0;
                wcat(p, sa);
                wcat(p, fd0.name);
                if (!read_small_file(p, buf, sizeof buf)) continue;
                nread++;
                if (!kv_get(buf, "installdir", v, sizeof v)) continue;
                int wolf = 0;
                for (char *c = v; c[0] && c[1] && c[2] && c[3]; c++)
                    if ((c[0] | 32) == 'w' && (c[1] | 32) == 'o' && (c[2] | 32) == 'l' && (c[3] | 32) == 'f') wolf = 1;
                if (wolf && nwolf++ < 3) {
                    int o = 0;
                    while (seen[o]) o++;
                    for (const char *q = v; *q && o < 150; q++) seen[o++] = *q;
                    if (o < 152) seen[o++] = ';', seen[o] = 0;
                }
                if (!streq_i(v, folder) || !kv_get(buf, "buildid", g_gamebuild, sizeof g_gamebuild)) continue;
                int flags = 0;
                if (kv_get(buf, "StateFlags", v, sizeof v))
                    for (char *c = v; *c >= '0' && *c <= '9'; c++) flags = flags * 10 + (*c - '0');
                if (!(flags & 4)) { // update pending / partially applied: say so in {gamebuild}
                    int o2 = 0;
                    while (g_gamebuild[o2]) o2++;
                    const char *t = "-pending";
                    while (*t && o2 < (int)sizeof g_gamebuild - 1) g_gamebuild[o2++] = *t++;
                    g_gamebuild[o2] = 0;
                }
                found = 1;
            } while (!found && FindNextFileW(fh, &fd0));
            FindClose(fh);
            if (found) {
                const char *t = "steam";
                for (int i = 0; (g_store[i] = t[i]); i++) {
                }
                return;
            }
        }
        if (!nn) why("no Steam manifests beside the common folder", "", "", "");
        if (nn) {
            char cnt[48];
            int o = 0;
            u32 vals[2] = {(u32)nn, (u32)nread};
            for (int q = 0; q < 2; q++) {
                char t[12];
                int z = 0;
                u32 x = vals[q];
                do t[z++] = (char)('0' + x % 10), x /= 10;
                while (x);
                while (z) cnt[o++] = t[--z];
                if (!q) cnt[o++] = '/';
            }
            cnt[o] = 0;
            why("no manifest lists this folder (manifests found/read ", cnt, "; similar installdirs: ", seen);
        }
    }
    // GOG
    FINDDATA_ fd;
    p[0] = 0;
    wcat(p, g_dir);
    wcat(p, L"goggame-*.info");
    HANDLE h = FindFirstFileW(p, &fd);
    if (h != (HANDLE)(i64)-1) {
        FindClose(h);
        p[0] = 0;
        wcat(p, g_dir);
        wcat(p, fd.name);
        if (read_small_file(p, buf, sizeof buf)) kv_get(buf, "buildId", g_gamebuild, sizeof g_gamebuild);
        const char *t = "gog";
        for (int i = 0; (g_store[i] = t[i]); i++) {
        }
        return;
    }
    // Epic
    p[0] = 0;
    wcat(p, g_dir);
    wcat(p, L".egstore");
    h = FindFirstFileW(p, &fd);
    if (h != (HANDLE)(i64)-1) {
        FindClose(h);
        const char *t = "epic";
        for (int i = 0; (g_store[i] = t[i]); i++) {
        }
    }
}

static void wput_num(wchar *d, int *o, u32 v, int digits) {
    for (int i = digits - 1; i >= 0; i--) {
        d[*o + i] = (wchar)('0' + v % 10);
        v /= 10;
    }
    *o += digits;
}

static int wtoken(const wchar *s, const char *tok) { // case-insensitive "{tok}" at s
    if (*s != '{') return 0;
    int i = 0;
    for (; tok[i]; i++)
        if ((s[1 + i] | 32) != tok[i]) return 0;
    return s[1 + i] == '}' ? i + 2 : 0;
}

// [General] LogFile template -> full path. Relative = inside the game folder; "/" or "\" separate folders
// (created as needed); "X:\..." or "\\..." is absolute. Tokens: {version} {date} {time} {datetime} {level}
// {gameversion} {gamebuild}.
static void build_log_path(const wchar *tpl) {
    SYSTEMTIME_ st;
    GetLocalTime(&st);
    wchar *d = g_log_path;
    int o = 0;
    int abs = (tpl[0] && tpl[1] == ':') || tpl[0] == '\\' || tpl[0] == '/';
    if (!abs)
        for (const wchar *g = g_dir; *g && o < 520; g++) d[o++] = *g;
    for (const wchar *c = tpl; *c && o < 660;) { // longest token is < 40 chars: stays inside 720
        int n;
        if ((n = wtoken(c, "version"))) {
            for (const char *v = VERSION; *v; v++) d[o++] = (wchar)*v;
        } else if ((n = wtoken(c, "datetime"))) {
            wput_num(d, &o, st.year, 4), d[o++] = '-', wput_num(d, &o, st.month, 2), d[o++] = '-';
            wput_num(d, &o, st.day, 2), d[o++] = '_';
            wput_num(d, &o, st.hour, 2), d[o++] = '-', wput_num(d, &o, st.minute, 2), d[o++] = '-';
            wput_num(d, &o, st.second, 2);
        } else if ((n = wtoken(c, "date"))) {
            wput_num(d, &o, st.year, 4), d[o++] = '-', wput_num(d, &o, st.month, 2), d[o++] = '-';
            wput_num(d, &o, st.day, 2);
        } else if ((n = wtoken(c, "time"))) {
            wput_num(d, &o, st.hour, 2), d[o++] = '-', wput_num(d, &o, st.minute, 2), d[o++] = '-';
            wput_num(d, &o, st.second, 2);
        } else if ((n = wtoken(c, "gameversion"))) {
            for (const char *v = g_gamever; *v; v++) d[o++] = (wchar)*v;
        } else if ((n = wtoken(c, "gamebuild"))) {
            for (const char *v = g_gamebuild; *v; v++) d[o++] = (wchar)*v;
        } else if ((n = wtoken(c, "store"))) {
            for (const char *v = g_store; *v; v++) d[o++] = (wchar)*v;
        } else if ((n = wtoken(c, "level"))) {
            for (const char *v = cfg.log >= 2 ? "detailed" : "normal"; *v; v++) d[o++] = (wchar)*v;
        } else {
            wchar ch = *c;
            if (ch == '/') ch = '\\';
            if (ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|') ch = '_';
            d[o++] = ch;
            n = 1;
        }
        c += n;
    }
    d[o] = 0;
}

#define DEFAULT_LOGFILE L"TWAUWidescreen/logs/{datetime}_v{version}.log"
static int g_log_fallback;

// LogKeep: when the file name has tokens (a new file per launch), keep the newest N logs in its folder
// whose names start with the template's fixed prefix and end in .log.
static void prune_logs(const wchar *tpl, int keep) {
    if (keep <= 0) return;
    const wchar *fn = tpl;
    for (const wchar *c = tpl; *c; c++)
        if (*c == '/' || *c == '\\') fn = c + 1;
    int brace = -1;
    for (int i = 0; fn[i]; i++)
        if (fn[i] == '{') {
            brace = i;
            break;
        }
    if (brace < 0) return; // fixed name: overwritten each launch, nothing to prune
    static wchar pat[700];
    int dl = 0, o;
    for (int i = 0; g_log_path[i]; i++)
        if (g_log_path[i] == '\\') dl = i + 1;
    for (o = 0; o < dl; o++) pat[o] = g_log_path[o];
    for (int i = 0; i < brace; i++) pat[o++] = fn[i];
    pat[o] = 0;
    wcat(pat, L"*.log");
    static struct {
        u64 t;
        wchar name[260];
    } f[200];
    int n = 0;
    FINDDATA_ fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == (HANDLE)(i64)-1) return;
    do {
        if (!(fd.attrs & 0x10) && n < 200) {
            f[n].t = (u64)fd.written[1] << 32 | fd.written[0];
            int i = 0;
            for (; fd.name[i] && i < 259; i++) f[n].name[i] = fd.name[i];
            f[n].name[i] = 0;
            n++;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    while (n > keep) { // delete the oldest, never the file just opened
        int old = 0;
        for (int i = 1; i < n; i++)
            if (f[i].t < f[old].t) old = i;
        static wchar p[700];
        for (o = 0; o < dl; o++) p[o] = g_log_path[o];
        p[o] = 0;
        wcat(p, f[old].name);
        int cur = 1;
        for (int i = 0; p[i] || g_log_path[i]; i++)
            if ((p[i] | 32) != (g_log_path[i] | 32)) {
                cur = 0;
                break;
            }
        if (!cur) DeleteFileW(p);
        f[old] = f[--n];
    }
}

static void open_log(void) {
    static wchar tpl[300];
    wchar buf[16];
    ini_get(L"General", L"LogFile", NULL, NULL, DEFAULT_LOGFILE, tpl, 300);
    if (!tpl[0]) wcat(tpl, DEFAULT_LOGFILE);
    build_log_path(tpl);
    // create the folders on the way (skip the drive / share prefix)
    for (wchar *c = g_log_path + 3; *c; c++)
        if (*c == '\\') {
            *c = 0;
            CreateDirectoryW(g_log_path, NULL);
            *c = '\\';
        }
    // GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL
    g_log = CreateFileW(g_log_path, 0x40000000, 1, NULL, 2, 0x80, NULL);
    if (g_log == (HANDLE)(i64)-1) g_log = NULL;
    if (!g_log) { // bad name / folder not writable: fall back to the game folder, else no log
        g_log_fallback = 1;
        build_log_path(L"TWAUWidescreen.log");
        g_log = CreateFileW(g_log_path, 0x40000000, 1, NULL, 2, 0x80, NULL);
        if (g_log == (HANDLE)(i64)-1) g_log = NULL;
        return;
    }
    float keep = WO_DEV ? 30 : 10;
    ini_get(L"General", L"LogKeep", NULL, NULL, WO_DEV ? L"30" : L"10", buf, 16);
    if (!parse_value(buf, &keep) || keep < 0) keep = WO_DEV ? 30 : 10;
    prune_logs(tpl, (int)keep);
}

// ---------------------------------------------------------------- PE / scanning

static u8 *g_text, *g_rdata;
static u32 g_text_n, g_rdata_n;

static int eq(const u8 *a, const char *b, u32 n) {
    for (u32 i = 0; i < n; i++)
        if (a[i] != (u8)b[i]) return 0;
    return 1;
}

static int find_sections(u8 *base) {
    u8 *nt = base + *(u32 *)(base + 0x3c);
    u16 nsec = *(u16 *)(nt + 6), optsz = *(u16 *)(nt + 20);
    u8 *sec = nt + 24 + optsz;
    for (int i = 0; i < nsec; i++, sec += 40) {
        u32 va = *(u32 *)(sec + 12), vs = *(u32 *)(sec + 8);
        if (eq(sec, ".text\0", 6)) g_text = base + va, g_text_n = vs;
        if (eq(sec, ".rdata\0", 7)) g_rdata = base + va, g_rdata_n = vs;
    }
    return g_text && g_rdata;
}

// NUL-delimited string in .rdata
static u8 *find_string(const char *s) {
    u32 n = 0;
    while (s[n]) n++;
    for (u32 i = 1; i + n + 1 < g_rdata_n; i++)
        if (g_rdata[i - 1] == 0 && g_rdata[i + n] == 0 && eq(g_rdata + i, s, n)) return g_rdata + i;
    return NULL;
}

static u8 *rip_target(u8 *ins, u32 disp_at, u32 len) { return ins + len + *(i32 *)(ins + disp_at); }

// follow incremental-link `jmp rel32` thunks
static u8 *follow(u8 *p) {
    for (int i = 0; i < 4 && p[0] == 0xE9; i++) p = p + 5 + *(i32 *)(p + 1);
    return p;
}

// next 7-byte rip-relative instruction with the 3-byte prefix `op` that points at `target`, from text+*pos
static u8 *next_rip_ref(const char *op, u8 *target, u32 *pos) {
    for (u32 i = *pos; i + 7 < g_text_n; i++) {
        u8 *p = g_text + i;
        if (p[0] == (u8)op[0] && p[1] == (u8)op[1] && p[2] == (u8)op[2] && rip_target(p, 3, 7) == target) {
            *pos = i + 1;
            return p;
        }
    }
    *pos = g_text_n;
    return NULL;
}

static int write_jump(u8 *at, void *to) {
    u32 old;
    if (!VirtualProtect(at, 12, 0x40, &old)) return 0;
    at[0] = 0x48; // mov rax, imm64
    at[1] = 0xB8;
    *(u64 *)(at + 2) = (u64)to;
    at[10] = 0xFF; // jmp rax
    at[11] = 0xE0;
    VirtualProtect(at, 12, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, 12);
    return 1;
}

// ---------------------------------------------------------------- Camera::SetAspectRatio

static u8 *g_set_aspect;
static u32 g_off_aspect, g_off_dirty1, g_off_dirty2;

// ucomiss xmm1,[rcx+A]; je; movss [rcx+A],xmm1; mov byte [rcx+D1],1; mov byte [rcx+D2],1; ret
static int match_set_aspect(u8 *f) {
    if (!eq(f, "\x0F\x2E\x89", 3) || f[7] != 0x74 || !eq(f + 9, "\xF3\x0F\x11\x89", 4)) return 0;
    if (*(u32 *)(f + 3) != *(u32 *)(f + 13) || !eq(f + 17, "\xC6\x81", 2) || f[23] != 1 ||
        !eq(f + 24, "\xC6\x81", 2) || f[30] != 1 || f[31] != 0xC3)
        return 0;
    g_off_aspect = *(u32 *)(f + 3);
    g_off_dirty1 = *(u32 *)(f + 19);
    g_off_dirty2 = *(u32 *)(f + 26);
    return 1;
}

static u8 *find_set_aspect(void) {
    u8 *s = find_string("Aspect Ratio");
    if (!s) return NULL;
    u32 pos = 0;
    u8 *ref;
    // camera property registration: lea rax,[setter]; ...; lea rdx,["Aspect Ratio"]
    while ((ref = next_rip_ref("\x48\x8D\x15", s, &pos))) {
        for (u32 back = 7; back < 0x30; back++) {
            u8 *p = ref - back;
            if (eq(p, "\x48\x8D\x05", 3)) {
                u8 *f = follow(rip_target(p, 3, 7));
                if (match_set_aspect(f)) return f;
            }
        }
    }
    return NULL;
}

// a plain `call rel32` to the setter (or its thunk) = an engine call with a deliberate value
static int is_direct_call(u8 *ra) {
    if (ra[-5] != 0xE8) return 0;
    return follow(ra + *(i32 *)(ra - 4)) == g_set_aspect;
}

#define MAX_SEEN 64
static struct {
    u8 *ra;
    u32 bits;
} g_seen[MAX_SEEN];
static volatile long g_seen_n;

static void log_aspect_call(u8 *ra, u8 *cam, float v, float out, int direct) {
    if (!g_log || !DETAIL) return;
    u32 bits = *(u32 *)&v;
    long n = g_seen_n;
    for (long i = 0; i < n && i < MAX_SEEN; i++)
        if (g_seen[i].ra == ra && g_seen[i].bits == bits) return;
    if (n >= MAX_SEEN) return;
    line_begin();
    // re-check under the lock
    for (long i = 0; i < g_seen_n; i++)
        if (g_seen[i].ra == ra && g_seen[i].bits == bits) {
            __sync_lock_release(&g_log_lock);
            return;
        }
    g_seen[g_seen_n].ra = ra;
    g_seen[g_seen_n].bits = bits;
    g_seen_n++;
    put("SetAspectRatio from exe+");
    put_hex((u64)(ra - (u8 *)GetModuleHandleW(NULL)));
    put(direct ? " (engine call)" : " (scene property)");
    put(" cam=");
    put_hex((u64)cam);
    put(" asked=");
    put_f(v);
    put(" stored=");
    put_f(out);
    put(out == 0 ? " (screen aspect)" : "");
    line_end();
}

static int is_fx_camera(u8 *cam); // mirror / render-to-texture cameras (see camera_type)

static void hook_set_aspect(u8 *cam, float v) {
    u8 *ra = __builtin_return_address(0);
    int direct = is_direct_call(ra);
    float out = v;
    if (cfg.enabled && !direct && v > 0.0f) out = cfg.aspect;
    if (cfg.enabled && !direct && cfg.fx_169 && is_fx_camera(cam)) out = 1.7777778f;
    log_aspect_call(ra, cam, v, out, direct);
    float *f = (float *)(cam + g_off_aspect);
    if (*f != out) {
        *f = out;
        cam[g_off_dirty1] = 1;
        cam[g_off_dirty2] = 1;
    }
}

// ---------------------------------------------------------------- camera FOV (zoom)
//
// Camera::SetHFOV(cam, fov) is registered for the "Field of View" camera property next to "Aspect Ratio".
// Scene/cutscene cameras get it from their props (cam_nav 54.4, cam_cutscene 49.1, cam_closeUp 35);
// UI cameras (cam_ui_*, by agent name) use 10-20 and are skipped. Property calls above 15 degrees get the ini multiplier; direct engine
// calls (cube maps, special cameras) and UI cameras are left alone. Scripts never call CameraSetFOV and
// only read CameraGetFOV to detect cuts, so scaled values don't compound.

// Camera+0x50 = mpAgent; Agent+0x58 = name (String: SSO buffer or pointer when capacity at +0x70 >= 16,
// length at +0x68) - see the AgentGetName native. UI cameras are cam_ui_* (main menu, HUD, dialog box).
static const char *camera_name(u8 *cam, u64 *len) {
    u8 *agent = *(u8 **)(cam + 0x50);
    *len = 0;
    if (!agent || (u64)agent < 0x10000) return "";
    u64 n = *(u64 *)(agent + 0x68), cap = *(u64 *)(agent + 0x70);
    if (n > 200 || cap < n) return "";
    *len = n;
    return cap >= 16 ? *(const char **)(agent + 0x58) : (const char *)(agent + 0x58);
}

static int starts_with(const char *s, u64 n, const char *p) {
    u64 i = 0;
    for (; p[i]; i++)
        if (i >= n || (s[i] | 32) != (p[i] | 32)) return 0;
    return 1;
}

// Camera types by agent name (episode scenes: cam_nav_*, cam_cutscene*, cam_Bigby_CU / _MED / _OTS,
// cam_twoShot, cam_pov*; UI: cam_ui_*, camera_ui_menu_title, cam_settings, cam_inventory; menu 3D: keyart_camera;
// effects: cam_fx_mirror*, obj_lookAt*Head).
// (types, multipliers and name overrides are declared with the config)

static int contains(const char *s, u64 n, const char *p) {
    for (u64 i = 0; i < n; i++)
        if (starts_with(s + i, n - i, p)) return 1;
    return 0;
}

static int camera_type(const char *s, u64 n) {
    if (starts_with(s, n, "ui_") || contains(s, n, "_ui") || contains(s, n, "settings") ||
        contains(s, n, "inventory") || contains(s, n, "hud"))
        return CT_UI;
    // walk-around cameras first: some are named after the room's mirror (cam_nav_mirror*, Crane's office)
    if (contains(s, n, "cam_nav") || contains(s, n, "_nav")) return CT_EXPLORE;
    // render-to-texture / helper cameras (mirror reflections, look-at targets): zoom or the screen's aspect
    // would misalign them. Only these names - a plain "mirror" in the name can be a normal shot.
    if (contains(s, n, "cam_fx") || contains(s, n, "reflection") || contains(s, n, "lookat")) return CT_FX;
    if (contains(s, n, "keyart") || contains(s, n, "menu")) return CT_MENU; // main menu 3D background
    if (contains(s, n, "cutscene")) return CT_CUTSCENE;
    if (contains(s, n, "_cu") || contains(s, n, "cu_") || contains(s, n, "med") || contains(s, n, "ots") ||
        contains(s, n, "twoshot") || contains(s, n, "pov") || contains(s, n, "_ws") || contains(s, n, "_ecu"))
        return CT_DIALOGUE;
    return CT_OTHER;
}

#define MAX_SEENCAM 96
static char g_seencam[MAX_SEENCAM][48];
static int g_nseencam;

static int first_sighting(const char *s, u64 n) {
    if (n > 47) n = 47;
    for (int i = 0; i < g_nseencam; i++) {
        u64 k = 0;
        while (k < n && g_seencam[i][k] == s[k]) k++;
        if (k == n && g_seencam[i][n] == 0) return 0;
    }
    if (g_nseencam >= MAX_SEENCAM) return 0;
    for (u64 k = 0; k < n; k++) g_seencam[g_nseencam][k] = s[k];
    g_seencam[g_nseencam++][n] = 0;
    return 1;
}

static int is_fx_camera(u8 *cam) {
    u64 n;
    const char *name = camera_name(cam, &n);
    return camera_type(name, n) == CT_FX;
}

// Mirror reflections are render-to-texture cameras with no aspect of their own: they'd take the screen's
// (32:9) and the reflection no longer matches the mirror. They keep the 16:9 they had on a 16:9 screen.
static void fix_fx_aspect(u8 *cam) {
    if (!g_set_aspect || !cfg.enabled || !cfg.fx_169) return;
    float *f = (float *)(cam + g_off_aspect);
    if (*f != 1.7777778f) {
        *f = 1.7777778f;
        cam[g_off_dirty1] = 1;
        cam[g_off_dirty2] = 1;
    }
}

typedef void (*sethfov_t)(u8 *cam, float fov);
static sethfov_t g_real_sethfov;
static u8 *g_hfov_thunk;

static void hook_set_hfov(u8 *cam, float fov) {
    u8 *ra = __builtin_return_address(0);
    u8 *t = ra[-5] == 0xE8 ? ra + *(i32 *)(ra - 4) : NULL;
    int direct = t && (t == g_hfov_thunk || t == (u8 *)g_real_sethfov);
    float out = fov;
    u64 n;
    const char *name = camera_name(cam, &n);
    int type = camera_type(name, n), ovr = -1;
    if (type == CT_FX && !direct) fix_fx_aspect(cam);
    float mul = g_fov_type[type];
    for (int i = 0; i < g_ncamovr; i++)
        if (starts_with(name, n, g_camovr[i].prefix)) {
            mul = g_camovr[i].mul, ovr = i;
            break;
        }
    if (cfg.enabled && !direct && fov > 1.0f && mul != 1.0f) {
        out = fov * mul;
        if (out > 170.0f) out = 170.0f;
    }
    if (!direct && DETAIL && first_sighting(name, n)) {
        line_begin();
        put("camera '");
        for (u64 i = 0; i < n && i < 60; i++) {
            char c[2] = {name[i], 0};
            put(c);
        }
        put("' type ");
        put(k_ctype[type]);
        if (ovr >= 0) put(" (override)");
        if (type == CT_FX && cfg.fx_169) put(" (aspect 16:9)");
        put(", FOV ");
        put_f(fov);
        put(out != fov ? " -> " : " (unchanged)");
        if (out != fov) put_f(out);
        line_end();
    }
    g_real_sethfov(cam, out);
}

// the thunk the camera property registration stores for "Field of View"
static u8 *find_prop_setter_thunk(const char *prop) {
    u8 *s = find_string(prop);
    if (!s) return NULL;
    u32 pos = 0;
    u8 *ref;
    while ((ref = next_rip_ref("\x48\x8D\x15", s, &pos)))
        for (u32 back = 7; back < 0x30; back++)
            if (eq(ref - back, "\x48\x8D\x05", 3)) {
                u8 *th = rip_target(ref - back, 3, 7);
                // push rbx; sub rsp,20h; ucomiss xmm1,[rcx+fov]
                if (th[0] == 0xE9 && eq(follow(th), "\x40\x53\x48\x83\xEC\x20\x0F\x2E\x89", 9)) return th;
                break;
            }
    return NULL;
}

// ---------------------------------------------------------------- forced 16:9 viewport

static u8 *g_force_flag, *g_force_dirty;
static int g_force_logged;

// C function behind a Lua global, from its registration: lea r9,[name]; lea r8,[native]
static u8 *find_lua_native(const char *name) {
    u8 *s = find_string(name);
    if (!s) return NULL;
    u32 pos = 0;
    u8 *ref;
    while ((ref = next_rip_ref("\x4C\x8D\x0D", s, &pos)))
        if (eq(ref + 7, "\x4C\x8D\x05", 3)) return follow(rip_target(ref + 7, 3, 7));
    return NULL;
}

// cmp cl,[rip+F]; je; mov [rip+F],cl; mov byte [rip+D],1; ret
static u8 *find_force_setter(void) {
    u8 *native = find_lua_native("RenderForce_16_by_9_AspectRatio");
    if (native) {
        for (u32 i = 0; i < 0x60; i++) {
            u8 *p = native + i;
            if (!eq(p, "\xB1\x01\xE8", 3)) continue; // mov cl,1; call setter
            u8 *f = follow(p + 7 + *(i32 *)(p + 3));
            if (eq(f, "\x3A\x0D", 2) && f[6] == 0x74 && eq(f + 8, "\x88\x0D", 2) && eq(f + 14, "\xC6\x05", 2) &&
                f[20] == 1 && f[21] == 0xC3 && rip_target(f, 2, 6) == rip_target(f + 8, 2, 6)) {
                g_force_flag = rip_target(f, 2, 6);
                g_force_dirty = rip_target(f + 14, 2, 7);
                return f;
            }
        }
    }
    return NULL;
}

// ---------------------------------------------------------------- resolution list
//
// RenderGetDisplayResolutions() only returns display modes that are within 0.001 px of a built-in list of
// 13 sizes (16:9 / 16:10, max 3840x2160), so 21:9 and 32:9 modes never reach the options menu:
//   comiss d2,eps; jb add; inc ecx; add rax,8; cmp ecx,13; jb next; jmp skip
// NOPing the final `jmp skip` makes every mode fall through to `add`.
static u8 *find_res_filter(void) {
    u8 *native = find_lua_native("RenderGetDisplayResolutions");
    if (!native) return NULL;
    for (u32 i = 0; i < 0x300; i++) {
        u8 *p = native + i;
        if (eq(p, "\x0F\x2F", 2) && p[3] == 0x72 && eq(p + 5, "\xFF\xC1\x48\x83\xC0\x08\x83\xF9", 8) &&
            p[14] == 0x72 && p[16] == 0xEB && p + 5 + p[4] == p + 18)
            return p + 16;
    }
    return NULL;
}

static int nop2(u8 *at) {
    u32 old;
    if (!VirtualProtect(at, 2, 0x40, &old)) return 0;
    at[0] = 0x90;
    at[1] = 0x90;
    VirtualProtect(at, 2, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, 2);
    return 1;
}

// RenderSetAspectRatio native: ...; movaps xmm0,xmm6; call setter. Setter: ucomiss xmm0,[rip+last];
// movss [rip+desired],xmm0; je; mov byte [rip+dirty],1; ret. "desired" is the aspect the forced viewport uses.
static float *g_desired_aspect;
static u8 *find_desired_aspect(void) {
    u8 *nat = find_lua_native("RenderSetAspectRatio");
    if (!nat) return NULL;
    for (u32 i = 0; i < 0x60; i++)
        if (eq(nat + i, "\x0F\x28\xC6\xE8", 4)) {
            u8 *f = follow(nat + i + 8 + *(i32 *)(nat + i + 4));
            if (eq(f, "\x0F\x2E\x05", 3) && eq(f + 7, "\xF3\x0F\x11\x05", 4) && f[15] == 0x74 && eq(f + 17, "\xC6\x05", 2)) {
                g_desired_aspect = (float *)rip_target(f + 7, 4, 8);
                return f;
            }
        }
    return NULL;
}

// Mode=16:9: if the game hasn't asked for its 16:9 viewport yet, switch it on the way it does (flag,
// desired aspect 16/9, render settings dirty). Called from SceneAdd on the game thread.
static int g_mode169_done;
static void ensure_mode169(void) {
    if (g_mode169_done || !cfg.enabled || !cfg.mode169 || !g_force_flag) return;
    g_mode169_done = 1;
    if (*g_force_flag && (!g_desired_aspect || *g_desired_aspect == 1.7777778f)) return; // already on
    if (g_desired_aspect) *g_desired_aspect = 1.7777778f;
    *g_force_flag = 1;
    *g_force_dirty = 1;
    log_s("16:9 mode: black bars switched on by the mod (the game had not asked yet)");
}

static void hook_force_16x9(u8 on) {
    if (!g_force_logged) {
        g_force_logged = 1;
        line_begin();
        put("game asked for a forced 16:9 viewport (");
        put_int(on);
        put(!cfg.enabled ? "): allowed" : cfg.mode169 ? "): on (Mode=16:9)" : cfg.block16x9 ? "): blocked" : "): allowed");
        line_end();
    }
    u8 want = !cfg.enabled ? on : cfg.mode169 ? 1 : cfg.block16x9 ? 0 : on;
    if (*g_force_flag != want) {
        *g_force_flag = want;
        *g_force_dirty = 1;
    }
}


// ---------------------------------------------------------------- render prefs
//
// Render prefs are GlobalPrefsObjects built by static initialisers (they run after our DllMain):
//   movabs rax, crc64("<pref name>"); ...; call GlobalPrefsObject::ctor; ...; mov dword [rip+value], default
// Patching that immediate changes the default the game starts with. Names are Telltale Symbols: CRC-64
// (ECMA-182, MSB first) of the lower-cased name.

static u64 crc64(const char *s) {
    u64 c = 0;
    for (; *s; s++) {
        u8 b = (u8)*s;
        if (b >= 'A' && b <= 'Z') b += 32;
        c ^= (u64)b << 56;
        for (int i = 0; i < 8; i++) c = (c & (1ull << 63)) ? (c << 1) ^ 0x42F0E1EBA9EA3693ull : c << 1;
    }
    return c;
}

enum { PT_INT = 1, PT_BOOL = 2, PT_FLOAT = 3 };

typedef struct {
    char name[96];
    int type;     // PT_*
    u8 *value;    // the live value in the game's memory (object + 0x50)
    u8 *imm;      // int/bool: the default's immediate in the initialiser (patched before it runs)
    float want;   // value asked for in the ini
    float game;   // the game's own value (its default, or what it later tried to set)
    int has_game;
} pref_t;


static const char *type_name(int t) { return t == PT_INT ? "int" : t == PT_BOOL ? "bool" : "float"; }

// movabs rax, crc; ... lea rcx,[object]; call ctor; ... store default into the object:
//   int:   mov dword [rip+v], imm32   (C7 05)     bool: mov byte [rip+v], imm8 (C6 05)
//   float: movss xmm0,[rip+const]; movss [rip+v], xmm0 (F3 0F 11 05) - no immediate, set live instead
//   0:     mov [rip+v], <zeroed register> (89/88) - no immediate, set live instead
static int find_pref(pref_t *p) {
    u64 h = crc64(p->name);
    for (u32 i = 0; i + 10 < g_text_n; i++) {
        u8 *q = g_text + i;
        if (q[0] != 0x48 || q[1] != 0xB8 || *(u64 *)(q + 2) != h) continue;
        u8 *obj = NULL;
        for (u32 j = 10; j < 0xA0; j++) {
            u8 *m = q + j, *t;
            if (!obj && eq(m, "\x48\x8D\x0D", 3)) obj = rip_target(m, 3, 7);
            if (!obj || m[-1] == 0x48) continue; // skip the REX.W qword stores next to the value
            if (m[0] == 0xC7 && m[1] == 0x05 && (t = rip_target(m, 2, 10)) >= obj && t < obj + 0x100) {
                p->type = PT_INT, p->value = t, p->imm = m + 6;
                return 1;
            }
            if (m[0] == 0xC6 && m[1] == 0x05 && (t = rip_target(m, 2, 7)) >= obj && t < obj + 0x100) {
                p->type = PT_BOOL, p->value = t, p->imm = m + 6;
                return 1;
            }
            if (eq(m, "\xF3\x0F\x11\x05", 4) && (t = rip_target(m, 4, 8)) >= obj && t < obj + 0x100) {
                p->type = PT_FLOAT, p->value = t, p->imm = NULL;
                return 1;
            }
            // default 0 stored from a zeroed register: mov dword [rip+v], e?x / mov byte [rip+v], al|cl
            if ((m[0] == 0x89 || m[0] == 0x88) && (m[1] & 0xC7) == 0x05 && (t = rip_target(m, 2, 6)) >= obj &&
                t < obj + 0x100) {
                p->type = m[0] == 0x89 ? PT_INT : PT_BOOL, p->value = t, p->imm = NULL;
                return 1;
            }
        }
    }
    return 0;
}

#define MAX_PREFS 64
static pref_t g_prefs[MAX_PREFS];
static int g_nprefs;
static pref_t *g_pref_aa, *g_pref_jitter;

static void put_val(int type, float v) {
    if (type == PT_FLOAT) put_f(v);
    else put_int((i64)v);
}

// find a pref and make `want` its starting value
static pref_t *apply_pref(const char *name, float want) {
    pref_t *p = NULL;
    for (int i = 0; i < g_nprefs; i++) {
        const char *a = g_prefs[i].name, *b = name;
        while (*a && *a == *b) a++, b++;
        if (!*a && !*b) p = &g_prefs[i];
    }
    if (!p) {
        if (g_nprefs >= MAX_PREFS) return NULL;
        p = &g_prefs[g_nprefs];
        int k = 0;
        while (name[k] && k < 95) p->name[k] = name[k], k++;
        p->name[k] = 0;
        if (!find_pref(p)) {
            line_begin();
            put("WARNING: setting '");
            put(name);
            put("' not found (misspelt, or a game update renamed it)");
            line_end();
            return NULL;
        }
        g_nprefs++;
    }
    p->want = want;
    if (p->imm) p->game = p->type == PT_INT ? (float)*(i32 *)p->imm : (float)*p->imm, p->has_game = 1;
    else if (p->type != PT_FLOAT) p->game = 0, p->has_game = 1; // zero default
    line_begin();
    put("setting '");
    put(p->name);
    put("' (");
    put(type_name(p->type));
    put(")");
    if (p->imm) {
        i32 def = p->type == PT_INT ? *(i32 *)p->imm : *p->imm;
        put(": game default ");
        put_int(def);
        i32 w = (i32)want;
        u32 old;
        if (w != def && VirtualProtect(p->imm, 4, 0x40, &old)) {
            if (p->type == PT_INT) *(i32 *)p->imm = w;
            else *p->imm = (u8)(w != 0);
            VirtualProtect(p->imm, 4, old, &old);
            FlushInstructionCache(GetCurrentProcess(), p->imm, 4);
        }
        put(", now ");
        put_val(p->type, want);
    } else {
        put(": held at ");
        put_val(p->type, want);
    }
    line_end();
    return p;
}

// Every setting from the ini is held at its value (the game re-applies its own AA/jitter values ~2 s after
// start; floats and zero defaults have no immediate at all). Also logs the AA prefs when they change.
static u64 g_overridden; // settings the game tried to change (logged once each)
static u32 WINAPI watch_prefs(void *unused) {
    (void)unused;
    i32 last_aa = -100, last_j = -100;
    for (int t = 0;; t++) {
        for (int i = 0; i < g_nprefs; i++) {
            pref_t *p = &g_prefs[i]; // the game re-applies some (AA, jitter) ~2 s after start: hold all
            float now = p->type == PT_FLOAT ? *(float *)p->value
                        : p->type == PT_INT ? (float)*(i32 *)p->value
                                            : (float)*p->value;
            float want = p->type == PT_BOOL ? (float)(p->want != 0) : p->type == PT_INT ? (float)(i32)p->want : p->want;
            if (g_vanilla) continue; // screenshot is showing the game's own values
            if (now == want) continue;
            if (t > 0) p->game = now, p->has_game = 1;
            if (t > 0 && !(g_overridden & (1ull << i))) {
                g_overridden |= 1ull << i;
                line_begin();
                put("the game changed '");
                put(p->name);
                put("' to ");
                put_val(p->type, now);
                put(" - held at ");
                put_val(p->type, want);
                line_end();
            }
            if (p->type == PT_FLOAT) *(float *)p->value = p->want;
            if (p->type == PT_INT) *(i32 *)p->value = (i32)p->want;
            if (p->type == PT_BOOL) *p->value = p->want != 0;
        }
        if (g_log && DETAIL && t < 1200) {
            i32 aa = g_pref_aa ? *(i32 *)g_pref_aa->value : -1, j = g_pref_jitter ? *(i32 *)g_pref_jitter->value : -1;
            if (aa != last_aa || j != last_j) {
                line_begin();
                put("live: Antialiasing=");
                put_int(aa);
                put(" TAA Jitter=");
                put_int(j);
                put(" (after ");
                put_int(t / 2);
                put(" s)");
                line_end();
                last_aa = aa;
                last_j = j;
            }
        }
        Sleep(500);
    }
    return 0;
}

// [Prefs] section: "Exact Setting Name=value" for any of the game's render settings
static void load_pref_section(void) {
    static wchar sec[8192];
    wchar ini[600] = {0};
    wcat(ini, g_dir);
    wcat(ini, L"TWAUWidescreen.ini");
    u32 n = GetPrivateProfileSectionW(L"Prefs", sec, 8192, ini);
    for (wchar *e = sec; e < sec + n && *e; e += wlen(e) + 1) {
        if (*e == ';' || *e == '#') continue;
        char name[96];
        int k = 0;
        wchar *v = e;
        while (*v && *v != '=' && k < 95) name[k++] = (char)*v++;
        while (k && name[k - 1] == ' ') k--;
        name[k] = 0;
        if (*v != '=' || !k) continue;
        v++;
        while (*v == ' ') v++;
        for (wchar *c = v; *c; c++) // cut an inline "; comment" and trailing blanks
            if (*c == ';' || *c == '#') {
                *c = 0;
                break;
            }
        for (int l = wlen(v); l && (v[l - 1] == ' ' || v[l - 1] == '\t'); l--) v[l - 1] = 0;
        float f;
        if (!parse_value(v, &f)) {
            line_begin();
            put("WARNING: [Prefs] '");
            put(name);
            put("' has a value that isn't a number or true/false - skipped");
            line_end();
            continue;
        }
        apply_pref(name, f);
    }
}

// ---------------------------------------------------------------- Lua injection
//
// The game's Lua DoString(code) native builds a String and hands it to the engine's executor:
//   mov rdx,rax; lea rcx,[rsp+X]; call String::String(const char*)
//   xor r8d,r8d; lea rdx,[rsp+X]; lea rcx,[rsp+Y]; call ExecuteString(DCArray<String>* ret, String*, bool)
// We call the same two functions with our own code, from inside a Lua native the boot script calls.

typedef void (*str_ctor_t)(void *str, const char *s);
typedef void *(*exec_t)(void *ret, void *str, int flag);
static str_ctor_t g_str_ctor;
static exec_t g_exec;

static int find_dostring(void) {
    u8 *nat = find_lua_native("DoString");
    if (!nat) return 0;
    for (u32 i = 0; i < 0x100; i++) {
        u8 *p = nat + i;
        if (!g_str_ctor && eq(p, "\x48\x8B\xD0\x48\x8D\x4C\x24", 7) && p[8] == 0xE8)
            g_str_ctor = (str_ctor_t)(p + 13 + *(i32 *)(p + 9));
        if (!g_exec && eq(p, "\x45\x33\xC0\x48\x8D\x54\x24", 7) && eq(p + 8, "\x48\x8D\x4C\x24", 4) && p[13] == 0xE8)
            g_exec = (exec_t)(p + 18 + *(i32 *)(p + 14));
    }
    return g_str_ctor && g_exec;
}

static void run_lua(const char *code, const char *what) {
    static u8 str[0x40], ret[0x40]; // String / DCArray<String>; leaked on purpose (a few bytes, once)
    if (!g_str_ctor || !g_exec) return;
    memset(str, 0, sizeof str);
    memset(ret, 0, sizeof ret);
    g_str_ctor(str, code);
    g_exec(ret, str, 0);
    if (what) {
        line_begin();
        put("Lua injected: ");
        put(what);
        line_end();
    }
    // DCArray<String> ret: +0x18 count, +0x20 elements of 0x28 (String at +8: buffer/pointer, +0x10 size,
    // +0x18 capacity). A chunk can `return "text"` to report to this log.
    i32 cnt = *(i32 *)(ret + 0x18);
    u8 *el = *(u8 **)(ret + 0x20);
    if (cnt > 0 && el) {
        u64 len = *(u64 *)(el + 0x18), cap = *(u64 *)(el + 0x20);
        const char *t = cap > 15 ? *(const char **)(el + 8) : (const char *)(el + 8);
        if (len && len < 60000 && !(len == 3 && t[0] == 'n' && t[1] == 'i' && t[2] == 'l')) {
            line_begin();
            put("Lua: ");
            for (u64 i = 0; i < len; i += 400) {
                char chunk[401];
                u64 k = 0;
                for (; k < 400 && i + k < len; k++) chunk[k] = t[i + k];
                chunk[k] = 0;
                put(chunk);
            }
            line_end();
        }
    }
}

// The thunk for a Lua native (its registration: lea r9,[name]; lea r8,[thunk]) is redirected through a
// 14-byte `jmp [rip]` in int3 padding (thunks are 5 bytes and our DLL is out of rel32 range).
static u8 *find_lua_thunk(const char *name) {
    u8 *s = find_string(name);
    if (!s) return NULL;
    u32 pos = 0;
    u8 *ref;
    while ((ref = next_rip_ref("\x4C\x8D\x0D", s, &pos)))
        if (eq(ref + 7, "\x4C\x8D\x05", 3)) return rip_target(ref + 7, 3, 7);
    return NULL;
}

static u8 *find_cave(void) {
    for (u32 i = 0; i + 32 < g_text_n; i++) {
        u32 k = 0;
        while (k < 32 && g_text[i + k] == 0xCC) k++;
        if (k == 32) return g_text + i + 8; // keep a few int3 before it
        i += k;
    }
    return NULL;
}

static int redirect_thunk(u8 *thunk, void *to) {
    u8 *cave = find_cave();
    u32 old;
    if (!thunk || thunk[0] != 0xE9 || !cave) return 0;
    if (!VirtualProtect(cave, 14, 0x40, &old)) return 0;
    cave[0] = 0xFF, cave[1] = 0x25; // jmp [rip+0]
    *(u32 *)(cave + 2) = 0;
    *(u64 *)(cave + 6) = (u64)to;
    VirtualProtect(cave, 14, old, &old);
    if (!VirtualProtect(thunk, 5, 0x40, &old)) return 0;
    *(i32 *)(thunk + 1) = (i32)(cave - (thunk + 5));
    VirtualProtect(thunk, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), cave, 14);
    FlushInstructionCache(GetCurrentProcess(), thunk, 5);
    return 1;
}

// bootTitle.lua Startup(): ... RenderSetAspectRatio(16/9); ...; if GetResetCode() == 2 then
// UserManager_LogIn(); SubProject_Switch("Menu", "Menu_Main.lua") (the path used after an engine reset:
// no legal/logo screen, no "press any key") else <legal screen, title, wait for a key> end.
static const char k_skip_to_menu[] =
    "local grc = GetResetCode\n"
    "GetResetCode = function(...) GetResetCode = grc return 2 end\n";

typedef int (*lua_cfunc)(void *L);
static lua_cfunc g_real_render_set_aspect;
static int g_boot_injected;

// UI scripts that place HUD anchors at fixed 16:9 positions. Agent.lua redefines AgentSetPosX in Lua
// whenever a project's scripts load, so this re-wraps it from SceneAdd whenever it isn't ours.
//   Inventory.lua Inventory_Show(): if IsPlatformPC() then AgentSetPosX("ui_inventory_pin_left", -9.6)
//   -9.6 = left edge of a 16:9 UI (half height 5.4 * 16/9); the real edge is -5.4 * screen aspect.
static const char k_ui_fix[] =
    "if AgentSetPosX ~= TWAUWS_SETX then\n"
    "  local setx = AgentSetPosX\n"
    "  TWAUWS_SETX = function(agent, x, ...)\n"
    "    if agent == 'ui_inventory_pin_left' and type(x) == 'number' then\n"
    "      local r = RenderGetAspectRatio() / 1.7777778\n"
    "      if r > 1 then x = x * r end\n"
    "    end\n"
    "    return setx(agent, x, ...)\n"
    "  end\n"
    "  AgentSetPosX = TWAUWS_SETX\n"
    "end\n";

// Menu side bars: ui_menu_foreground.scene (Menu project copy) has black masks
// ui_menu_foreground_widescreen_left/right centred at x = +-19.2 (UI cams: z = 41, vfov 15 deg, so 16:9
// spans +-9.6 and the masks cover exactly what lies outside it). The menu scenes load asynchronously, so
// the agents don't exist yet when SceneAdd returns (their names are listed, AgentExistsInScene is false):
// a script thread keeps hiding them (and fitting the distress edge frames / [UI] HideAgents) 4x a second.
// What it did is reported to the log at the next SceneAdd.
static const char k_menu_bars[] =
    "TWAUWS_ORIG = TWAUWS_ORIG or {}\n"
    "local MASKS = {'ui_menu_foreground_widescreen_left', 'ui_menu_foreground_widescreen_right'}\n"
    "local FRAMES = {'ui_menu_foreground_distress_left', 'ui_menu_foreground_distress_right'}\n"
    "local function find(n)\n"
    "  local ok, a = pcall(AgentFindInScene, n, 'ui_menu_foreground.scene')\n"
    "  if ok and a then return a end\n"
    "  ok, a = pcall(AgentExists, n)\n"
    "  if ok and a then ok, a = pcall(AgentFind, n) if ok and a then return a end end\n"
    "end\n"
    "local function note(s)\n"
    "  TWAUWS_NOTED = TWAUWS_NOTED or {}\n"
    "  if not TWAUWS_NOTED[s] then TWAUWS_NOTED[s] = true TWAUWS_LOG = (TWAUWS_LOG or '') .. s .. '; ' end\n"
    "end\n"
    "TWAUWS_FIXMENU = function()\n"
    "  local r = RenderGetAspectRatio() / 1.7777778\n"
    "  if r < 1 then r = 1 end\n"
    "  for _, n in ipairs(MASKS) do\n"
    "    local a = find(n)\n"
    "    if a and not AgentIsHidden(a) then AgentHide(a, true) note('hid ' .. n) end\n"
    "  end\n"
    "  if TWAUWS_EDGEMODE ~= 'game' then\n"
    "    for _, n in ipairs(FRAMES) do\n"
    "      local a = find(n)\n"
    "      if a then\n"
    "        if TWAUWS_EDGEMODE == 'hide' then\n"
    "          if not AgentIsHidden(a) then AgentHide(a, true) note('hid ' .. n) end\n"
    "        else\n"
    "          local o = TWAUWS_ORIG[n]\n"
    "          if not o then\n"
    "            local p = AgentGetPos(a)\n"
    "            local sv = AgentGetProperty(a, 'Render Axis Scale')\n"
    "            o = {x = p.x, y = p.y, z = p.z, sx = sv and sv.x or 1, sy = sv and sv.y or 1, sz = sv and sv.z or 1}\n"
    "            TWAUWS_ORIG[n] = o\n"
    "          end\n"
    "          AgentSetPos(a, Vector(o.x * r, o.y, o.z))\n"
    "          AgentSetProperty(a, 'Render Axis Scale', Vector(o.sx * r, o.sy, o.sz))\n"
    "          note(n .. string.format(' stretched (x %.2f -> %.2f, axis scale %.2f -> %.2f)', o.x, o.x * r, o.sx, o.sx * r))\n"
    "        end\n"
    "      end\n"
    "    end\n"
    "  end\n"
    "  for _, n in ipairs(TWAUWS_HIDE or {}) do\n"
    "    local a = find(n)\n"
    "    if a and not AgentIsHidden(a) then AgentHide(a, true) note('hid ' .. n) end\n"
    "  end\n"
    "end\n"
    "-- the menu scenes load asynchronously: keep checking from a script thread (the newest one wins)\n"
    "TWAUWS_TOKEN = (TWAUWS_TOKEN or 0) + 1\n"
    "local me = TWAUWS_TOKEN\n"
    "ThreadStart(function()\n"
    "  local n = 0\n"
    "  while TWAUWS_TOKEN == me do\n"
    "    pcall(TWAUWS_FIXMENU)\n"
    "    Sleep(0.25)\n"
    "  end\n"
    "end)\n"
    "pcall(TWAUWS_FIXMENU)\n"
    "local l = TWAUWS_LOG\n"
    "TWAUWS_LOG = nil\n"
    "return l\n";

// Developer features are gated only by IsShipping() in the scripts: Settings > Debug Options (debug
// overlay, Fablespedia unlocks, TTStats, vibration), Pause > Unlock Chapters, F1 / Back = DebugMenu
// (project switcher, load scripts/scenes). Opt-in: IsShipping() returns false.
static const char k_dev[] = "IsShipping = function() return false end\n";

// Log=detailed: which scenes are active (when it changes) and every new scene's agent names (once).
static const char k_scene_report[] =
    "TWAUWS_REPORTED = TWAUWS_REPORTED or {}\n"
    "local ok, names = pcall(SceneGetActiveSceneNames)\n"
    "if not ok or type(names) ~= 'table' then return nil end\n"
    "local out, sl = {}, {}\n"
    "for _, sn in pairs(names) do\n"
    "  sn = tostring(sn)\n"
    "  sl[#sl + 1] = sn\n"
    "  if not TWAUWS_REPORTED[sn] then\n"
    "    local ok2, an = pcall(SceneGetAgentNames, sn)\n"
    "    if ok2 and type(an) == 'table' and #an > 0 then\n"
    "      TWAUWS_REPORTED[sn] = true\n"
    "      local d = {}\n"
    "      for _, n in ipairs(an) do d[#d + 1] = tostring(n) end\n"
    "      out[#out + 1] = 'scene ' .. sn .. ' (' .. #d .. ' agents): ' .. table.concat(d, ', ')\n"
    "    end\n"
    "  end\n"
    "end\n"
    "local act = table.concat(sl, ', ')\n"
    "if act ~= TWAUWS_LASTACTIVE then TWAUWS_LASTACTIVE = act out[#out + 1] = 'active scenes: ' .. act end\n"
    "if #out == 0 then return nil end\n"
    "return table.concat(out, ' | ')\n";

static lua_cfunc g_real_scene_add;
static int g_ui_logged, g_bars_logged, g_dev_logged;

static int hook_scene_add(void *L) {
    ensure_mode169();
    if (cfg.enabled && cfg.hud_edges) {
        run_lua(k_ui_fix, g_ui_logged ? NULL : "HUD anchors at the screen edges (inventory)");
        g_ui_logged = 1;
    }
    if (cfg.enabled && cfg.dev_menus) {
        run_lua(k_dev, g_dev_logged ? NULL : "developer menus on (IsShipping = false)");
        g_dev_logged = 1;
    }
    if (cfg.enabled) {
        run_lua(cfg.menu_bars == 2   ? "TWAUWS_EDGEMODE = 'hide'"
                : cfg.menu_bars == 1 ? "TWAUWS_EDGEMODE = 'stretch'"
                                     : "TWAUWS_EDGEMODE = 'game'",
                NULL);
        run_lua(cfg.hide_lua, NULL);
        run_lua(k_menu_bars, g_bars_logged || !DETAIL ? NULL : "menu fix thread started");
        g_bars_logged = 1;
        if (DETAIL) run_lua(k_scene_report, NULL);
    }
    return g_real_scene_add(L);
}

// ---------------------------------------------------------------- loading-screen overlay sprites
//
// Overlays (ui_loadingScreen.overlay: the bottom-right spinner) size their sprites in screen fractions
// (0.04 x 0.0711 = square at 16:9 only). Their per-frame update (0x140882710 in the Oct 2026 build) walks
// the instance's objects (inst+0x18, next at +0x10; +0x18 type, 2 = text; +0x30 width animation;
// +0x88/+0x8c width/height). We narrow sprite widths by (16/9)/aspect before it runs.

typedef void (*ovl_update_t)(u8 *inst, void *p2, float *p3);
typedef float (*getaspect_t)(void);
static ovl_update_t g_real_ovl;
static getaspect_t g_get_aspect;
#define MAX_OVL 32
static struct {
    u8 *obj;
    float written;
} g_ovl[MAX_OVL];
static int g_novl, g_ovl_next, g_ovl_logged;

static void hook_ovl_update(u8 *inst, void *p2, float *p3) {
    if (cfg.enabled && cfg.fix_overlay && g_get_aspect && inst) {
        float k = 1.7777778f / g_get_aspect();
        if (k < 0.999f && k > 0.1f)
            for (u8 *o = *(u8 **)(inst + 0x18); o; o = *(u8 **)(o + 0x10)) {
                if (*(i32 *)(o + 0x18) == 2 || *(void **)(o + 0x30)) continue;
                float *w = (float *)(o + 0x88);
                int i = 0;
                while (i < g_novl && g_ovl[i].obj != o) i++;
                if (i < g_novl && *w == g_ovl[i].written) continue; // already narrowed
                if (i == g_novl) {
                    if (g_novl < MAX_OVL) g_novl++;
                    else i = g_ovl_next, g_ovl_next = (g_ovl_next + 1) % MAX_OVL;
                    g_ovl[i].obj = o;
                }
                if (g_ovl_logged < (DETAIL ? 10 : 1)) {
                    g_ovl_logged++;
                    line_begin();
                    put("overlay sprite narrowed: width ");
                    put_f(*w);
                    put(" -> ");
                    put_f(*w * k);
                    put(" (height ");
                    put_f(*(float *)(o + 0x8c));
                    put(")");
                    line_end();
                }
                *w *= k;
                g_ovl[i].written = *w;
            }
    }
    g_real_ovl(inst, p2, p3);
}

static u8 *find_ovl_update(void) {
    for (u32 i = 0; i + 32 < g_text_n; i++) {
        u8 *p = g_text + i;
        if (eq(p, "\x40\x55\x56\x57\x41\x56\x48\x8D\x6C\x24\xF8\x48\x81\xEC", 14) &&
            eq(p + 16, "\x00\x00\xF3\x41\x0F\x10\x10\x0F\x57\xC0\xF3\x0F\x10\x49\x4C", 15))
            return p;
    }
    return NULL;
}

static u8 *find_thunk_to(u8 *f) {
    for (u32 i = 0; i + 5 < g_text_n; i++)
        if (g_text[i] == 0xE9 && g_text + i + 5 + *(i32 *)(g_text + i + 1) == f) return g_text + i;
    return NULL;
}

// RenderGetAspectRatio's native: ...; call lua_settop; call <get aspect>; movaps xmm1,xmm0
static getaspect_t find_get_aspect(void) {
    u8 *nat = find_lua_native("RenderGetAspectRatio");
    if (!nat) return NULL;
    for (u32 i = 0; i < 0x40; i++)
        if (nat[i] == 0xE8 && eq(nat + i + 5, "\x0F\x28\xC8", 3)) return (getaspect_t)follow(nat + i + 5 + *(i32 *)(nat + i + 1));
    return NULL;
}

static int hook_render_set_aspect(void *L) {
    if (!g_boot_injected) {
        g_boot_injected = 1;
        if (cfg.enabled && cfg.skip_to_menu) run_lua(k_skip_to_menu, "skip legal screen + title (straight to the main menu)");
    }
    return g_real_render_set_aspect(L);
}

#include "shots.c"

// ---------------------------------------------------------------- init

static void init(void) {
    u8 *exe = (u8 *)GetModuleHandleW(NULL);
    u32 n = GetModuleFileNameW(NULL, g_dir, 512);
    while (n && g_dir[n - 1] != '\\' && g_dir[n - 1] != '/') n--;
    g_dir[n] = 0;
    g_t0 = GetTickCount64();
    load_config();
    read_game_version(exe);
    read_store_build();
    if (cfg.log) open_log();

    line_begin();
    put("TWAU Widescreen " VERSION " | game ");
    put(g_gamever);
    put(" (");
    put(g_store);
    put(" build ");
    put(g_gamebuild);
    put(")");
    put(" | exe timestamp ");
    {
        u8 *nt = exe + *(u32 *)(exe + 0x3c);
        put_hex(*(u32 *)(nt + 8));
    }
    put(cfg.enabled ? ", fix on" : ", fix OFF (Enabled=0)");
    put(", aspect ");
    if (cfg.aspect > 0)
        put_f(cfg.aspect);
    else
        put("auto (screen)");
    put(cfg.block16x9 ? ", forced 16:9 blocked" : ", forced 16:9 allowed");
    put(WO_DEV ? ", DEV build" : ", release build");
    put(DETAIL ? ", log detailed" : ", log normal (Log=2 for detailed)");
    line_end();
    if (g_log_fallback) log_s("WARNING: LogFile path could not be created - logging to TWAUWidescreen.log instead");
    if (cfg.enabled && cfg.mode169) {
        // 16:9 mode = the original framing: settings that change framing or the widescreen layout step aside
        int fov_changed = g_ncamovr > 0;
        for (int t = 0; t < CT_COUNT; t++) {
            if (t == CT_FX) continue;
            fov_changed |= g_fov_type[t] != 1.0f;
            g_fov_type[t] = 1.0f;
        }
        g_ncamovr = 0;
        cfg.hud_edges = 0;
        cfg.menu_bars = 0;
        line_begin();
        put("16:9 mode: original framing - FOV 1.0");
        put(fov_changed ? " (your FOV settings are ignored)" : "");
        put(", inventory and menu frames as in the game, comparison screenshots off");
        line_end();
    }
    if (g_gamever[0] == 'u') log_s("note: game version not found in the exe (shown as unknown)");
    if (g_gamebuild[0] == 'u') {
        line_begin();
        put("note: store build id not found for this folder - shown as unknown");
        if (g_store_why[0]) {
            put(" (Steam: ");
            put(g_store_why);
            put(")");
        }
        put(" | game folder: ");
        for (const wchar *c = g_dir; *c; c++) {
            char ch[2] = {(char)(*c < 128 ? *c : '?'), 0};
            put(ch);
        }
        line_end();
    }

    if (!find_sections(exe)) {
        log_s("ERROR: exe sections not found");
        return;
    }
    g_set_aspect = find_set_aspect();
    if (g_set_aspect) {
        line_begin();
        put("Camera::SetAspectRatio at exe+");
        put_hex((u64)(g_set_aspect - exe));
        put(" (aspect field +");
        put_hex(g_off_aspect);
        put(")");
        line_end();
        write_jump(g_set_aspect, (void *)hook_set_aspect);
    } else
        log_s("ERROR: Camera::SetAspectRatio not found (game update?) - picture will stay stretched");

    if (cfg.enabled && cfg.all_res) {
        u8 *rf = find_res_filter();
        if (rf && nop2(rf)) {
            line_begin();
            put("resolution whitelist removed at exe+");
            put_hex((u64)(rf - exe));
            put(": the options menu now lists every display mode");
            line_end();
        } else
            log_s("ERROR: resolution whitelist not found (game update?) - only 16:9/16:10 sizes will be listed");
    }

    int fov_on = g_ncamovr > 0 || cfg.fx_169; // the FX-camera aspect fix also lives in this hook
    for (int t = 0; t < CT_COUNT; t++) fov_on |= g_fov_type[t] != 1.0f;
    if (cfg.enabled && fov_on) {
        g_hfov_thunk = find_prop_setter_thunk("Field of View");
        if (g_hfov_thunk) {
            g_real_sethfov = (sethfov_t)follow(g_hfov_thunk);
            if (redirect_thunk(g_hfov_thunk, (void *)hook_set_hfov)) {
                line_begin();
                put("camera FOV armed:");
                for (int t = 0; t < CT_COUNT; t++) {
                    put(" ");
                    put(k_ctype[t]);
                    put("=");
                    put_f(g_fov_type[t]);
                }
                put(", ");
                put_int(g_ncamovr);
                put(" name overrides (Camera::SetHFOV at exe+");
                put_hex((u64)((u8 *)g_real_sethfov - exe));
                put(")");
                line_end();
            }
        } else
            log_s("ERROR: camera FOV: SetHFOV not found (game update?)");
    }

    if (find_desired_aspect()) {
        if (DETAIL) {
            line_begin();
            put("desired-aspect setter found (16:9 mode)");
            line_end();
        }
    } else if (cfg.mode169)
        log_s("WARNING: 16:9 mode: desired-aspect setter not found - relying on the game's own 16:9 request");
    if (cfg.mode169) log_s("display mode: 16:9 with black bars (Mode=16:9)");

    u8 *force = find_force_setter();
    if (force) {
        line_begin();
        put("forced-16:9 setter at exe+");
        put_hex((u64)(force - exe));
        line_end();
        write_jump(force, (void *)hook_force_16x9);
    } else
        log_s("WARNING: forced-16:9 setter not found (game update?)");

    if (cfg.enabled) {
        // [AntiAliasing] first, then [Prefs] (which wins when both name the same setting)
        if (cfg.aa >= 0) g_pref_aa = apply_pref("Render - Antialiasing", (float)cfg.aa);
        if (cfg.jitter >= 0) g_pref_jitter = apply_pref("Render - TAA Jitter", (float)cfg.jitter);
        if (cfg.mipbias >= 0) apply_pref("Render - TAA Mip Bias", (float)cfg.mipbias);
        load_pref_section();
    }
    CreateThread(NULL, 0, watch_prefs, NULL, 0, NULL);

    int have_dostring = find_dostring();
    if (!have_dostring) log_s("ERROR: DoString internals not found (game update?) - startup skip / HUD fix off");
    if (cfg.enabled && have_dostring) {
        u8 *thunk = find_lua_thunk("SceneAdd");
        if (thunk && thunk[0] == 0xE9) {
            g_real_scene_add = (lua_cfunc)follow(thunk);
            if (redirect_thunk(thunk, (void *)hook_scene_add)) log_s("HUD edge fix armed");
        } else
            log_s("ERROR: HUD edge fix: SceneAdd not found (game update?)");
    }
    if (cfg.enabled && cfg.fix_overlay) {
        u8 *f = find_ovl_update(), *thunk = f ? find_thunk_to(f) : NULL;
        g_get_aspect = find_get_aspect();
        if (f && thunk && g_get_aspect && redirect_thunk(thunk, (void *)hook_ovl_update)) {
            g_real_ovl = (ovl_update_t)f;
            line_begin();
            put("loading-icon fix armed (overlay update at exe+");
            put_hex((u64)(f - exe));
            put(")");
            line_end();
        } else
            log_s("ERROR: loading-icon fix: overlay update not found (game update?)");
    }

    if (cfg.enabled && cfg.skip_to_menu) {
        u8 *thunk = find_lua_thunk("RenderSetAspectRatio");
        if (have_dostring && thunk && thunk[0] == 0xE9) {
            g_real_render_set_aspect = (lua_cfunc)follow(thunk);
            if (redirect_thunk(thunk, (void *)hook_render_set_aspect))
                log_s("startup skip armed (runs when the boot script starts)");
            else
                log_s("ERROR: startup skip: could not hook the boot script");
        } else
            log_s("ERROR: startup skip: DoString / RenderSetAspectRatio not found (game update?)");
    }
    if (cfg.enabled) {
        if (!g_get_aspect) g_get_aspect = find_get_aspect();
        init_hotkeys();
    }
}

// ---------------------------------------------------------------- dinput8 proxy

static HANDLE g_real;

static void *real_proc(const char *name) {
    if (!g_real) {
        wchar p[600];
        u32 n = GetSystemDirectoryW(p, 520);
        p[n] = 0;
        wcat(p, L"\\dinput8.dll");
        g_real = LoadLibraryW(p);
        if (!g_real) return NULL;
    }
    return GetProcAddress(g_real, name);
}

typedef long(WINAPI *di8create_t)(void *, u32, const void *, void **, void *);
__declspec(dllexport) long WINAPI DirectInput8Create(void *inst, u32 ver, const void *riid, void **out, void *outer) {
    di8create_t f = (di8create_t)real_proc("DirectInput8Create");
    return f ? f(inst, ver, riid, out, outer) : (long)0x80004005;
}
typedef long(WINAPI *getclass_t)(const void *, const void *, void **);
__declspec(dllexport) long WINAPI DllGetClassObject(const void *clsid, const void *riid, void **out) {
    getclass_t f = (getclass_t)real_proc("DllGetClassObject");
    return f ? f(clsid, riid, out) : (long)0x80040111;
}
__declspec(dllexport) long WINAPI DllCanUnloadNow(void) { return 1; }

BOOL WINAPI DllMain(HANDLE inst, u32 reason, void *reserved) {
    (void)reserved;
    if (reason == 1) {
        DisableThreadLibraryCalls(inst);
        init();
    }
    return 1;
}
