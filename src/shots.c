// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WideOpen contributors. https://github.com/Lyrixxx/WideOpen-TWAU-Remastered
// Game speed hotkey and before/after comparison screenshots. Included by twauws.c (single translation unit).
//
// Speed: Lua SetTimeScale(x) (native 0x1407e6d90) clamps x to [0, 10] and stores it in a global float plus a
// "changed" byte. The hotkey writes those two directly, only while the game runs at 1.0 (so pauses, which
// set 0, are left alone).
//
// Comparison: IDXGISwapChain::Present / Present1 are hooked by patching the swap-chain vtable (taken from a
// throw-away device; DXVK and native dxgi share one vtable per class). With the fix the game is Hor+ (the
// vertical view is kept, the sides are added), so the original 16:9 picture is exactly the centre 16:9 of
// the widescreen frame - no camera switching needed. On the hotkey the world is frozen (time scale 0), the
// frame is grabbed with the mod's settings, then again with the game's own anti-aliasing settings. One PNG
// holds three panels: 1 the original as it looked (centre 16:9 stretched to the screen), 2 the original at
// its correct 16:9 shape (the extra widescreen area dimmed around it), 3 the fix. Own zlib/LZ77 encoder.
// With a FOV zoom the "original" panels show the zoomed view.

__declspec(dllimport) void *WINAPI VirtualAlloc(void *, u64, u32, u32);
__declspec(dllimport) BOOL WINAPI VirtualFree(void *, u64, u32);
__declspec(dllimport) u32 WINAPI GetCurrentProcessId(void);

static void *big_alloc(u64 n) { return VirtualAlloc(NULL, n, 0x3000, 0x04); } // commit|reserve, RW
static void big_free(void *p) {
    if (p) VirtualFree(p, 0, 0x8000);
}

// ---------------------------------------------------------------- hotkeys (polled, game window only)

typedef short(WINAPI *getasynckeystate_t)(int);
typedef HANDLE(WINAPI *getforegroundwindow_t)(void);
typedef u32(WINAPI *getwindowthreadprocessid_t)(HANDLE, u32 *);
static getasynckeystate_t pGetAsyncKeyState;
static getforegroundwindow_t pGetForegroundWindow;
static getwindowthreadprocessid_t pGetWindowThreadProcessId;

// "F7", "F12", "A".."Z", "0".."9", "0x75" (virtual-key code); "" / "none" = off
static int parse_key(const wchar *s) {
    while (*s == ' ') s++;
    if (!*s || (s[0] | 32) == 'n') return 0;
    if (s[0] == '0' && (s[1] | 32) == 'x') {
        int v = 0;
        for (s += 2; *s; s++) {
            wchar c = (wchar)(*s | 32);
            if (c >= '0' && c <= '9') v = v * 16 + (c - '0');
            else if (c >= 'a' && c <= 'f') v = v * 16 + (c - 'a' + 10);
            else break;
        }
        return v > 0 && v < 256 ? v : 0;
    }
    if ((s[0] | 32) == 'f' && s[1] >= '1' && s[1] <= '9') {
        int n = s[1] - '0';
        if (s[2] >= '0' && s[2] <= '9') n = n * 10 + (s[2] - '0');
        return n >= 1 && n <= 24 ? 0x6F + n : 0;
    }
    if (s[1] == 0 || s[1] == ' ') {
        wchar c = s[0];
        if (c >= 'a' && c <= 'z') c -= 32;
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
    }
    return 0;
}

static int game_has_focus(void) {
    if (!pGetForegroundWindow || !pGetWindowThreadProcessId) return 1;
    u32 pid = 0;
    HANDLE w = pGetForegroundWindow();
    if (!w) return 0;
    pGetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

static int key_down(int vk) { return vk && pGetAsyncKeyState && (pGetAsyncKeyState(vk) & 0x8000); }

// ---------------------------------------------------------------- time scale

static float *g_ts_value;
static u8 *g_ts_flag;
static float g_ts_cap = 10.0f;

// SetTimeScale native: movss xmm2,[rip+cap] ... movss [rip+value],xmm2; mov byte [rip+flag],1
static int find_timescale(void) {
    u8 *nat = find_lua_native("SetTimeScale");
    if (!nat) return 0;
    for (u32 i = 0; i < 0x80; i++) {
        u8 *p = nat + i;
        if (eq(p, "\xF3\x0F\x10\x15", 4) && !g_ts_value) g_ts_cap = *(float *)rip_target(p, 4, 8);
        if (eq(p, "\xF3\x0F\x11\x15", 4) && eq(p + 8, "\xC6\x05", 2) && p[14] == 1) {
            g_ts_value = (float *)rip_target(p, 4, 8);
            g_ts_flag = rip_target(p + 8, 2, 7);
            if (!(g_ts_cap > 0.5f && g_ts_cap < 1000.0f)) g_ts_cap = 10.0f;
            return 1;
        }
    }
    return 0;
}

static void set_timescale(float v) {
    if (!g_ts_value) return;
    *g_ts_value = v;
    *g_ts_flag = 1;
}

// ---------------------------------------------------------------- "original game" simulation

// the game's own value for a setting: what it tried to set (seen by the watcher), else its default
static void apply_prefs_mode(int vanilla) {
    for (int i = 0; i < g_nprefs; i++) {
        pref_t *p = &g_prefs[i];
        if (vanilla && !p->has_game) continue;
        float v = vanilla ? p->game : p->want;
        if (p->type == PT_FLOAT) *(float *)p->value = v;
        if (p->type == PT_INT) *(i32 *)p->value = (i32)v;
        if (p->type == PT_BOOL) *p->value = v != 0;
    }
}

// ---------------------------------------------------------------- frame grab

typedef struct {
    u32 w, h;
    u8 *rgb; // w*h*3
} image_t;

#define VCALL(obj, idx, type) ((type)((*(void ***)(obj))[idx]))
typedef u32(WINAPI *release_t)(void *);
typedef long(WINAPI *getbuffer_t)(void *, u32, const void *, void **);
typedef void(WINAPI *getdesc_t)(void *, void *);
typedef void(WINAPI *getdevice_t)(void *, void **);
typedef void(WINAPI *getctx_t)(void *, void **);
typedef long(WINAPI *createtex_t)(void *, const void *, const void *, void **);
typedef void(WINAPI *copyres_t)(void *, void *, void *);
typedef long(WINAPI *map_t)(void *, void *, u32, u32, u32, void *);
typedef void(WINAPI *unmap_t)(void *, void *, u32);

static const u8 IID_ID3D11Texture2D[16] = {0xf2, 0xaa, 0x15, 0x6f, 0x08, 0xd2, 0x89, 0x4e,
                                           0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c};

typedef struct {
    u32 w, h, mips, array, format, samples, quality, usage, bind, cpu, misc;
} tex2d_desc_t;

static float half_to_float(u16 h) {
    u32 s = (u32)(h >> 15) << 31, e = (h >> 10) & 31, m = h & 1023, f;
    if (e == 0) f = s; // zero / denormal -> 0
    else if (e == 31) f = s | 0x7F800000;
    else f = s | ((e + 112) << 23) | (m << 13);
    return *(float *)&f;
}

static u8 lin_to_srgb8(float x) { // scRGB (HDR) back buffers: linear -> ~sRGB, clipped
    if (!(x > 0)) return 0;
    if (x >= 1) return 255;
    float r = x > 0.25f ? x : 0.25f;
    for (int i = 0; i < 5; i++) r = 0.5f * (r + x / r); // sqrt(x)
    float q = r > 0.25f ? r : 0.25f;
    for (int i = 0; i < 5; i++) q = 0.5f * (q + r / q); // x^0.25
    float y = 0.585f * r + 0.415f * q;                  // ~x^(1/2.2)
    int v = (int)(y * 255.0f + 0.5f);
    return (u8)(v < 0 ? 0 : v > 255 ? 255 : v);
}

static int grab_frame(void *swapchain, image_t *img) {
    void *tex = NULL, *dev = NULL, *ctx = NULL, *stage = NULL;
    int ok = 0;
    if (VCALL(swapchain, 9, getbuffer_t)(swapchain, 0, IID_ID3D11Texture2D, &tex) < 0 || !tex) return 0;
    tex2d_desc_t d;
    VCALL(tex, 10, getdesc_t)(tex, &d);
    VCALL(tex, 3, getdevice_t)(tex, &dev);
    if (!dev) goto out;
    VCALL(dev, 40, getctx_t)(dev, &ctx);
    if (!ctx || d.samples > 1) goto out;
    int fmt = d.format;
    int kind = (fmt == 28 || fmt == 29) ? 1 : (fmt == 87 || fmt == 91) ? 2 : fmt == 24 ? 3 : fmt == 10 ? 4 : 0;
    if (!kind) {
        line_begin();
        put("screenshot: unsupported back-buffer format ");
        put_int(fmt);
        line_end();
        goto out;
    }
    tex2d_desc_t sd = d;
    sd.mips = 1, sd.array = 1, sd.usage = 3 /*STAGING*/, sd.bind = 0, sd.cpu = 0x20000 /*READ*/, sd.misc = 0;
    if (VCALL(dev, 5, createtex_t)(dev, &sd, NULL, &stage) < 0 || !stage) goto out;
    VCALL(ctx, 47, copyres_t)(ctx, stage, tex);
    struct {
        u8 *data;
        u32 pitch, depth;
    } m;
    if (VCALL(ctx, 14, map_t)(ctx, stage, 0, 1 /*READ*/, 0, &m) < 0) goto out;
    img->w = d.w, img->h = d.h;
    img->rgb = big_alloc((u64)d.w * d.h * 3);
    if (img->rgb) {
        for (u32 y = 0; y < d.h; y++) {
            u8 *src = m.data + (u64)y * m.pitch, *dst = img->rgb + (u64)y * d.w * 3;
            for (u32 x = 0; x < d.w; x++, dst += 3) {
                if (kind == 1) dst[0] = src[x * 4], dst[1] = src[x * 4 + 1], dst[2] = src[x * 4 + 2];
                else if (kind == 2) dst[0] = src[x * 4 + 2], dst[1] = src[x * 4 + 1], dst[2] = src[x * 4];
                else if (kind == 3) {
                    u32 v = *(u32 *)(src + x * 4);
                    dst[0] = (u8)((v & 1023) >> 2), dst[1] = (u8)(((v >> 10) & 1023) >> 2),
                    dst[2] = (u8)(((v >> 20) & 1023) >> 2);
                } else {
                    u16 *h = (u16 *)(src + x * 8);
                    dst[0] = lin_to_srgb8(half_to_float(h[0])), dst[1] = lin_to_srgb8(half_to_float(h[1])),
                    dst[2] = lin_to_srgb8(half_to_float(h[2]));
                }
            }
        }
        ok = 1;
    }
    VCALL(ctx, 15, unmap_t)(ctx, stage, 0);
out:
    if (stage) VCALL(stage, 2, release_t)(stage);
    if (ctx) VCALL(ctx, 2, release_t)(ctx);
    if (dev) VCALL(dev, 2, release_t)(dev);
    if (tex) VCALL(tex, 2, release_t)(tex);
    return ok;
}

// ---------------------------------------------------------------- 5x7 font (columns, bit 0 = top)

static const u8 k_font[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x5F, 0x00, 0x00}, {0x00, 0x07, 0x00, 0x07, 0x00},
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, {0x24, 0x2A, 0x7F, 0x2A, 0x12}, {0x23, 0x13, 0x08, 0x64, 0x62},
    {0x36, 0x49, 0x56, 0x20, 0x50}, {0x00, 0x05, 0x03, 0x00, 0x00}, {0x00, 0x1C, 0x22, 0x41, 0x00},
    {0x00, 0x41, 0x22, 0x1C, 0x00}, {0x2A, 0x1C, 0x7F, 0x1C, 0x2A}, {0x08, 0x08, 0x3E, 0x08, 0x08},
    {0x00, 0x50, 0x30, 0x00, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08}, {0x00, 0x60, 0x60, 0x00, 0x00},
    {0x20, 0x10, 0x08, 0x04, 0x02}, {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x72, 0x49, 0x49, 0x49, 0x46}, {0x21, 0x41, 0x49, 0x4D, 0x33}, {0x18, 0x14, 0x12, 0x7F, 0x10},
    {0x27, 0x45, 0x45, 0x45, 0x39}, {0x3C, 0x4A, 0x49, 0x49, 0x31}, {0x41, 0x21, 0x11, 0x09, 0x07},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x46, 0x49, 0x49, 0x29, 0x1E}, {0x00, 0x36, 0x36, 0x00, 0x00},
    {0x00, 0x56, 0x36, 0x00, 0x00}, {0x08, 0x14, 0x22, 0x41, 0x00}, {0x14, 0x14, 0x14, 0x14, 0x14},
    {0x00, 0x41, 0x22, 0x14, 0x08}, {0x02, 0x01, 0x59, 0x09, 0x06}, {0x3E, 0x41, 0x5D, 0x59, 0x4E},
    {0x7C, 0x12, 0x11, 0x12, 0x7C}, {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x7F, 0x41, 0x41, 0x41, 0x3E}, {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01},
    {0x3E, 0x41, 0x41, 0x51, 0x73}, {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40},
    {0x7F, 0x02, 0x1C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46},
    {0x26, 0x49, 0x49, 0x49, 0x32}, {0x03, 0x01, 0x7F, 0x01, 0x03}, {0x3F, 0x40, 0x40, 0x40, 0x3F},
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x3F, 0x40, 0x38, 0x40, 0x3F}, {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x03, 0x04, 0x78, 0x04, 0x03}, {0x61, 0x59, 0x49, 0x4D, 0x43}, // ' ' .. 'Z'
};

static void fill_rect(image_t *im, int x0, int y0, int w, int h, u32 rgb) {
    for (int y = y0; y < y0 + h; y++)
        if (y >= 0 && y < (int)im->h)
            for (int x = x0; x < x0 + w; x++)
                if (x >= 0 && x < (int)im->w) {
                    u8 *p = im->rgb + ((u64)y * im->w + x) * 3;
                    p[0] = (u8)(rgb >> 16), p[1] = (u8)(rgb >> 8), p[2] = (u8)rgb;
                }
}

static int text_width(const char *s, int scale) {
    int n = 0;
    while (s[n]) n++;
    return n * 6 * scale - scale;
}

static void draw_text(image_t *im, int x, int y, const char *s, int scale, u32 rgb) {
    for (; *s; s++, x += 6 * scale) {
        int c = (u8)*s;
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c == '|') {
            fill_rect(im, x + 2 * scale, y, scale, 7 * scale, rgb);
            continue;
        }
        if (c < 32 || c > 'Z') c = '?';
        const u8 *g = k_font[c - 32];
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 7; row++)
                if (g[col] >> row & 1) fill_rect(im, x + col * scale, y + row * scale, scale, scale, rgb);
    }
}

// ---------------------------------------------------------------- PNG (zlib: LZ77 + fixed Huffman)

typedef struct {
    u8 *p;
    u64 n, cap;
    u32 bits, nbits;
} bitbuf_t;

static void bb_byte(bitbuf_t *b, u8 v) {
    if (b->n < b->cap) b->p[b->n] = v;
    b->n++;
}
static void bb_bits(bitbuf_t *b, u32 v, u32 n) { // LSB first
    b->bits |= v << b->nbits;
    b->nbits += n;
    while (b->nbits >= 8) bb_byte(b, (u8)b->bits), b->bits >>= 8, b->nbits -= 8;
}
static void bb_code(bitbuf_t *b, u32 code, u32 n) { // Huffman codes go MSB first
    u32 r = 0;
    for (u32 i = 0; i < n; i++) r |= ((code >> i) & 1) << (n - 1 - i);
    bb_bits(b, r, n);
}
static void put_lit(bitbuf_t *b, u32 v) {
    if (v < 144) bb_code(b, 0x30 + v, 8);
    else if (v < 256) bb_code(b, 0x190 + v - 144, 9);
    else if (v < 280) bb_code(b, v - 256, 7);
    else bb_code(b, 0xC0 + v - 280, 8);
}
static const u16 k_lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const u8 k_lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const u16 k_dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const u8 k_dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static void deflate_fixed(bitbuf_t *b, const u8 *d, u64 n) {
    static u32 head[1 << 16];
    for (u32 i = 0; i < (1 << 16); i++) head[i] = 0xFFFFFFFF;
    bb_bits(b, 1, 1); // final block
    bb_bits(b, 1, 2); // fixed Huffman
    u64 i = 0;
    while (i < n) {
        u32 best = 0, dist = 0;
        if (i + 3 <= n) {
            u32 h = ((u32)d[i] << 16 | (u32)d[i + 1] << 8 | d[i + 2]) * 2654435761u >> 16;
            u32 cand = head[h];
            head[h] = (u32)i;
            if (cand != 0xFFFFFFFF && i - cand <= 32768 && i - cand > 0) {
                u32 l = 0, max = n - i < 258 ? (u32)(n - i) : 258;
                while (l < max && d[cand + l] == d[i + l]) l++;
                if (l >= 3) best = l, dist = (u32)(i - cand);
            }
        }
        if (best) {
            int c = 28;
            while (k_lbase[c] > best) c--;
            put_lit(b, 257 + c);
            if (k_lext[c]) bb_bits(b, best - k_lbase[c], k_lext[c]);
            int dc = 29;
            while (k_dbase[dc] > dist) dc--;
            bb_code(b, dc, 5);
            if (k_dext[dc]) bb_bits(b, dist - k_dbase[dc], k_dext[dc]);
            i += best;
        } else
            put_lit(b, d[i++]);
    }
    put_lit(b, 256);
    if (b->nbits) bb_byte(b, (u8)b->bits), b->bits = 0, b->nbits = 0;
}

static u32 crc32_buf(u32 c, const u8 *p, u64 n) {
    static u32 t[256];
    if (!t[1])
        for (u32 i = 0; i < 256; i++) {
            u32 v = i;
            for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320 ^ (v >> 1) : v >> 1;
            t[i] = v;
        }
    c = ~c;
    for (u64 i = 0; i < n; i++) c = t[(c ^ p[i]) & 255] ^ (c >> 8);
    return ~c;
}

static void be32(u8 *p, u32 v) { p[0] = (u8)(v >> 24), p[1] = (u8)(v >> 16), p[2] = (u8)(v >> 8), p[3] = (u8)v; }

static int write_png(const wchar *path, image_t *im) {
    u64 raw_n = (u64)(im->w * 3 + 1) * im->h;
    u8 *raw = big_alloc(raw_n);
    if (!raw) return 0;
    for (u32 y = 0; y < im->h; y++) { // filter 1 (Sub) on every row
        u8 *r = raw + (u64)y * (im->w * 3 + 1), *s = im->rgb + (u64)y * im->w * 3;
        r[0] = 1;
        for (u32 x = 0; x < im->w * 3; x++) r[1 + x] = (u8)(s[x] - (x >= 3 ? s[x - 3] : 0));
    }
    bitbuf_t b = {0};
    b.cap = raw_n + raw_n / 4 + 1024;
    b.p = big_alloc(b.cap + 64);
    if (!b.p) return big_free(raw), 0;
    b.n = 8; // room for "length + IDAT" in front
    bb_byte(&b, 0x78), bb_byte(&b, 0x01);
    deflate_fixed(&b, raw, raw_n);
    u32 a = 1, s2 = 0; // adler32
    for (u64 i = 0; i < raw_n; i++) a = (a + raw[i]) % 65521, s2 = (s2 + a) % 65521;
    big_free(raw);
    u32 adler = s2 << 16 | a;
    bb_byte(&b, (u8)(adler >> 24)), bb_byte(&b, (u8)(adler >> 16)), bb_byte(&b, (u8)(adler >> 8)), bb_byte(&b, (u8)adler);
    if (b.n > b.cap) return big_free(b.p), 0;
    u32 idat_len = (u32)(b.n - 8);
    be32(b.p, idat_len);
    b.p[4] = 'I', b.p[5] = 'D', b.p[6] = 'A', b.p[7] = 'T';
    u32 crc = crc32_buf(0, b.p + 4, idat_len + 4);
    u8 head[33] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10, 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
    be32(head + 16, im->w), be32(head + 20, im->h);
    head[24] = 8, head[25] = 2, head[26] = 0, head[27] = 0, head[28] = 0; // 8-bit RGB
    be32(head + 29, crc32_buf(0, head + 12, 17));
    u8 tail[16] = {0}, crcb[4];
    be32(crcb, crc);
    be32(tail, 0), tail[4] = 'I', tail[5] = 'E', tail[6] = 'N', tail[7] = 'D';
    be32(tail + 8, crc32_buf(0, tail + 4, 4));
    HANDLE f = CreateFileW(path, 0x40000000, 0, NULL, 2, 0x80, NULL);
    if (f == (HANDLE)(i64)-1) return big_free(b.p), 0;
    u32 w;
    WriteFile(f, head, 33, &w, NULL);
    WriteFile(f, b.p, (u32)b.n, &w, NULL);
    WriteFile(f, crcb, 4, &w, NULL);
    WriteFile(f, tail, 12, &w, NULL);
    CloseHandle(f);
    big_free(b.p);
    return 1;
}

// ---------------------------------------------------------------- compose + save

static struct {
    int key, width, singles;
    wchar folder[300];
} g_shotcfg = {0x77 /*F8*/, 5120, 1, {0}};
static image_t g_shot_after, g_shot_before;

static void downscale_into(image_t *dst, int dx, int dy, image_t *src, int f) {
    u32 w = src->w / f, h = src->h / f;
    for (u32 y = 0; y < h; y++)
        for (u32 x = 0; x < w; x++) {
            u32 acc[3] = {0, 0, 0};
            for (int j = 0; j < f; j++)
                for (int i = 0; i < f; i++) {
                    u8 *p = src->rgb + (((u64)(y * f + j)) * src->w + x * f + i) * 3;
                    acc[0] += p[0], acc[1] += p[1], acc[2] += p[2];
                }
            u8 *q = dst->rgb + (((u64)(dy + y)) * dst->w + dx + x) * 3;
            q[0] = (u8)(acc[0] / (f * f)), q[1] = (u8)(acc[1] / (f * f)), q[2] = (u8)(acc[2] / (f * f));
        }
}

static void shot_path(wchar *out, const wchar *suffix) {
    // <folder>\<datetime>_<suffix>.png (folder relative to the game folder unless absolute)
    static wchar tpl[400];
    tpl[0] = 0;
    wcat(tpl, g_shotcfg.folder);
    int n = wlen(tpl);
    if (n && tpl[n - 1] != '/' && tpl[n - 1] != '\\') wcat(tpl, L"/");
    wcat(tpl, L"{datetime}_");
    wcat(tpl, suffix);
    wcat(tpl, L".png");
    wchar keep[720];
    for (int i = 0; (keep[i] = g_log_path[i]); i++) {
    }
    build_log_path(tpl); // same token/folder rules as LogFile
    for (int i = 0; (out[i] = g_log_path[i]); i++) {
    }
    for (int i = 0; (g_log_path[i] = keep[i]); i++) {
    }
    for (wchar *c = out + 3; *c; c++)
        if (*c == '\\') {
            *c = 0;
            CreateDirectoryW(out, NULL);
            *c = '\\';
        }
}

static void put_path(const wchar *p) {
    for (; *p; p++) {
        char ch[2] = {(char)(*p < 128 ? *p : '?'), 0};
        put(ch);
    }
}

// centre 16:9 of src: stretched across the full width (mode 0), or at its true shape with the rest of
// `wide` dimmed around it (mode 1). dst is src-sized.
static void make_original(image_t *dst, image_t *src, image_t *wide, int mode) {
    u32 W = src->w, H = src->h;
    u32 cw = (u32)((float)H * 16.0f / 9.0f + 0.5f);
    if (cw > W) cw = W;
    u32 cx = (W - cw) / 2;
    for (u32 y = 0; y < H; y++) {
        u8 *s = src->rgb + (u64)y * W * 3, *wd = wide->rgb + (u64)y * W * 3, *d = dst->rgb + (u64)y * W * 3;
        for (u32 x = 0; x < W; x++, d += 3) {
            if (mode == 0) { // linear resample of the centre strip to W
                float fx = cx + ((float)x + 0.5f) * (float)cw / (float)W - 0.5f;
                if (fx < cx) fx = (float)cx;
                u32 x0 = (u32)fx, x1 = x0 + 1 < cx + cw ? x0 + 1 : x0;
                float t = fx - (float)x0;
                for (int c = 0; c < 3; c++) d[c] = (u8)(s[x0 * 3 + c] * (1 - t) + s[x1 * 3 + c] * t + 0.5f);
            } else if (x >= cx && x < cx + cw) {
                d[0] = s[x * 3], d[1] = s[x * 3 + 1], d[2] = s[x * 3 + 2];
            } else { // outside the original's view: the widescreen frame, dimmed and greyed
                u32 g = (wd[x * 3] * 77 + wd[x * 3 + 1] * 150 + wd[x * 3 + 2] * 29) >> 8;
                for (int c = 0; c < 3; c++) d[c] = (u8)((g * 3 + wd[x * 3 + c]) / 4 / 4);
            }
        }
    }
    if (mode == 1) { // thin lines at the edges of the 16:9 view
        int lw = (int)(H / 360) > 1 ? (int)(H / 360) : 1;
        fill_rect(dst, (int)cx - lw, 0, lw, H, 0xE0B040);
        fill_rect(dst, (int)(cx + cw), 0, lw, H, 0xE0B040);
    }
}

static void label_bar(image_t *out, int y, int bar, int sc, u32 accent, const char *title, const char *sub) {
    fill_rect(out, 0, y, out->w, bar, 0x18181E);
    fill_rect(out, 0, y + bar - sc / 2 - 1, out->w, sc / 2 + 1, accent);
    draw_text(out, 3 * sc, y + 3 * sc, title, sc, 0xF2F2F2);
    draw_text(out, 3 * sc + text_width(title, sc) + 4 * sc, y + 3 * sc, sub, sc, 0x9A9AA6);
}

static u32 WINAPI save_comparison(void *unused) {
    (void)unused;
    image_t *A = &g_shot_after, *B = &g_shot_before; // A: mod settings, B: the game's AA settings
    static wchar path[720];
    image_t tmp = {A->w, A->h, big_alloc((u64)A->w * A->h * 3)};
    image_t out = {0, 0, NULL};
    if (!tmp.rgb) goto done;
    if (g_shotcfg.singles) {
        shot_path(path, L"3_widescreen");
        write_png(path, A);
        make_original(&tmp, B, A, 0);
        shot_path(path, L"1_original_stretched");
        write_png(path, &tmp);
        { // the original's true 16:9 picture on its own
            u32 cw = (u32)((float)B->h * 16.0f / 9.0f + 0.5f);
            if (cw > B->w) cw = B->w;
            image_t c = {cw, B->h, tmp.rgb};
            u32 cx = (B->w - cw) / 2;
            for (u32 y = 0; y < B->h; y++)
                for (u32 i = 0; i < cw * 3; i++) c.rgb[(u64)y * cw * 3 + i] = B->rgb[((u64)y * B->w + cx) * 3 + i];
            shot_path(path, L"2_original_16x9");
            write_png(path, &c);
        }
    }
    int f = 1;
    while (A->w / f > (u32)g_shotcfg.width && f < 8) f++;
    u32 pw = A->w / f, ph = A->h / f;
    int sc = pw >= 2400 ? 4 : pw >= 1600 ? 3 : 2;
    int bar = 13 * sc, gap = 2 * sc, foot = 7 * (sc > 2 ? sc - 1 : 1) + 4 * sc;
    out.w = pw, out.h = (u32)(3 * (bar + ph) + 2 * gap + foot);
    out.rgb = big_alloc((u64)out.w * out.h * 3);
    if (!out.rgb) goto done;
    fill_rect(&out, 0, 0, out.w, out.h, 0x0E0E12);
    char res[32];
    {
        int o = 0;
        u32 vals[2] = {A->w, A->h};
        for (int k = 0; k < 2; k++) {
            char t[12];
            int q = 0;
            u32 v = vals[k];
            do t[q++] = (char)('0' + v % 10), v /= 10;
            while (v);
            while (q) res[o++] = t[--q];
            if (!k) res[o++] = 'X';
        }
        res[o] = 0;
    }
    int y = 0;
    label_bar(&out, y, bar, sc, 0xC0563A, "1  ORIGINAL GAME", "AS IT LOOKED: 16:9 STRETCHED TO FILL THE SCREEN");
    y += bar;
    make_original(&tmp, B, A, 0);
    downscale_into(&out, 0, y, &tmp, f);
    y += ph + gap;
    label_bar(&out, y, bar, sc, 0xE0B040, "2  ORIGINAL GAME", "CORRECT 16:9 SHAPE (DIMMED = NOT SHOWN)");
    y += bar;
    make_original(&tmp, B, A, 1);
    downscale_into(&out, 0, y, &tmp, f);
    y += ph + gap;
    label_bar(&out, y, bar, sc, 0x3AB0A0, "3  TWAU WIDESCREEN " VERSION, res);
    y += bar;
    downscale_into(&out, 0, y, A, f);
    y += ph;
    {
        int fs = sc > 2 ? sc - 1 : 1;
        char ft[128] = "THE WOLF AMONG US REMASTERED  |  GAME ";
        int o = 0;
        while (ft[o]) o++;
        for (const char *v = g_gamever; *v && o < 100; v++) ft[o++] = *v;
        const char *t = "  |  PANELS 1-2 USE THE GAME'S OWN ANTI-ALIASING";
        while (*t && o < 126) ft[o++] = *t++;
        ft[o] = 0;
        draw_text(&out, 3 * sc, y + 2 * sc, ft, fs, 0x6C6C78);
    }
    shot_path(path, L"comparison");
    if (write_png(path, &out)) {
        line_begin();
        put("comparison screenshot saved: ");
        put_path(path);
        line_end();
    } else
        log_s("ERROR: comparison screenshot could not be written");
done:
    big_free(out.rgb), big_free(tmp.rgb);
    big_free(A->rgb), big_free(B->rgb);
    A->rgb = B->rgb = NULL;
    return 0;
}

// ---------------------------------------------------------------- Present hook + sequence

typedef long(WINAPI *present_t)(void *, u32, u32);
typedef long(WINAPI *present1_t)(void *, u32, u32, const void *);
static present_t g_real_present;
static present1_t g_real_present1;
static volatile int g_shot_req, g_shot_state, g_shot_busy;
static int g_shot_frames;
static float g_shot_saved_ts = 1.0f;

// Sequence (one step per presented frame): freeze (time scale 0), 8 frames, grab with the mod's settings;
// game AA settings, 40 frames for TAA to settle, grab; settings back, unfreeze.
static void shot_frame(void *sc) {
    if (g_shot_state == 0) {
        if (!g_shot_req) return;
        g_shot_req = 0;
        if (g_shot_busy) return;
        g_shot_busy = 1;
        g_shot_saved_ts = g_ts_value ? *g_ts_value : 1.0f;
        set_timescale(0);
        g_shot_state = 1, g_shot_frames = 0;
        return;
    }
    g_shot_frames++;
    if (g_shot_state == 1 && g_shot_frames >= 8) {
        if (!grab_frame(sc, &g_shot_after)) goto fail;
        g_vanilla = 1;
        apply_prefs_mode(1);
        g_shot_state = 2, g_shot_frames = 0;
    } else if (g_shot_state == 2 && g_shot_frames >= 40) {
        int ok = grab_frame(sc, &g_shot_before);
        g_vanilla = 0;
        apply_prefs_mode(0);
        set_timescale(g_shot_saved_ts);
        g_shot_state = 0;
        if (!ok) goto fail2;
        CreateThread(NULL, 0, save_comparison, NULL, 0, NULL);
    }
    return;
fail:
    set_timescale(g_shot_saved_ts);
fail2:
    g_vanilla = 0;
    apply_prefs_mode(0);
    g_shot_state = 0;
    g_shot_busy = 0;
    big_free(g_shot_after.rgb), big_free(g_shot_before.rgb);
    g_shot_after.rgb = g_shot_before.rgb = NULL;
    log_s("ERROR: comparison screenshot: could not read the frame");
}

static long WINAPI hook_present(void *sc, u32 sync, u32 flags) {
    if (!(flags & 1 /*DXGI_PRESENT_TEST*/)) shot_frame(sc);
    return g_real_present(sc, sync, flags);
}
static long WINAPI hook_present1(void *sc, u32 sync, u32 flags, const void *params) {
    if (!(flags & 1)) shot_frame(sc);
    return g_real_present1(sc, sync, flags, params);
}

// a throw-away device + swap chain gives the swap chain vtable the game's swap chain shares
typedef long(WINAPI *d3d11createdevsc_t)(void *, int, void *, u32, const void *, u32, u32, const void *, void **,
                                          void **, void *, void **);
typedef HANDLE(WINAPI *createwindowex_t)(u32, const wchar *, const wchar *, u32, int, int, int, int, HANDLE, HANDLE,
                                         HANDLE, void *);
typedef BOOL(WINAPI *destroywindow_t)(HANDLE);
typedef long(WINAPI *qi_t)(void *, const void *, void **);
static const u8 IID_IDXGISwapChain1[16] = {0xf7, 0x45, 0x0a, 0x79, 0x42, 0x0d, 0x76, 0x48,
                                           0x98, 0x3a, 0x0a, 0x55, 0xcf, 0xe6, 0xf4, 0xaa};

static int patch_slot(void **slot, void *to, void **orig) {
    u32 old;
    if (!VirtualProtect(slot, 8, 0x04, &old)) return 0;
    *orig = *slot;
    *slot = to;
    VirtualProtect(slot, 8, old, &old);
    return 1;
}

static int hook_swapchain(void) {
    HANDLE u32dll = LoadLibraryW(L"user32.dll"), d3d = LoadLibraryW(L"d3d11.dll");
    if (!u32dll || !d3d) return 0;
    createwindowex_t cw = (createwindowex_t)GetProcAddress(u32dll, "CreateWindowExW");
    destroywindow_t dw = (destroywindow_t)GetProcAddress(u32dll, "DestroyWindow");
    d3d11createdevsc_t mk = (d3d11createdevsc_t)GetProcAddress(d3d, "D3D11CreateDeviceAndSwapChain");
    if (!cw || !dw || !mk) return 0;
    HANDLE wnd = cw(0, L"STATIC", L"twauws", 0x00CF0000 /*WS_OVERLAPPEDWINDOW*/, 0, 0, 64, 64, NULL, NULL, NULL, NULL);
    if (!wnd) return 0;
    struct {
        u32 w, h, rnum, rden, format, scan, scale;
        u32 samples, quality, usage, count;
        u32 pad;
        HANDLE wnd;
        u32 windowed, effect, flags, pad2;
    } desc = {64, 64, 60, 1, 28, 0, 0, 1, 0, 0x20 /*RENDER_TARGET_OUTPUT*/, 1, 0, wnd, 1, 0, 0, 0};
    void *sc = NULL, *dev = NULL, *ctx = NULL;
    u32 fl;
    int ok = 0;
    if (mk(NULL, 1 /*HARDWARE*/, NULL, 0, NULL, 0, 7, &desc, &sc, &dev, &fl, &ctx) >= 0 && sc) {
        void **vt = *(void ***)sc;
        ok = patch_slot(&vt[8], (void *)hook_present, (void **)&g_real_present);
        void *sc1 = NULL;
        if (VCALL(sc, 0, qi_t)(sc, IID_IDXGISwapChain1, &sc1) >= 0 && sc1) {
            void **vt1 = *(void ***)sc1;
            if (vt1[22] != (void *)hook_present1) patch_slot(&vt1[22], (void *)hook_present1, (void **)&g_real_present1);
            if (vt1 != vt && vt1[8] != (void *)hook_present) {
                present_t dummy;
                patch_slot(&vt1[8], (void *)hook_present, (void **)&dummy);
            }
            VCALL(sc1, 2, release_t)(sc1);
        }
    }
    if (ctx) VCALL(ctx, 2, release_t)(ctx);
    if (dev) VCALL(dev, 2, release_t)(dev);
    if (sc) VCALL(sc, 2, release_t)(sc);
    dw(wnd);
    return ok;
}

// ---------------------------------------------------------------- input thread

static struct {
    int key, hold;
    float factor;
} g_speedcfg = {0x76 /*F7*/, 0, 3.0f};
static int g_speed_on;

static void speed_tick(void) {
    if (!g_ts_value || g_shot_state) return;
    float cur = *g_ts_value;
    if (g_speed_on && cur == 1.0f) set_timescale(g_speedcfg.factor); // not while paused (0) or scripted
    if (!g_speed_on && cur == g_speedcfg.factor) set_timescale(1.0f);
}

static u32 WINAPI input_thread(void *unused) {
    (void)unused;
    Sleep(3000); // let the game create its device first
    if (g_shotcfg.key) {
        if (hook_swapchain()) log_s("comparison screenshots ready");
        else log_s("ERROR: comparison screenshots: could not hook the swap chain");
    }
    int was_speed = 0, was_shot = 0;
    for (;;) {
        int focus = game_has_focus();
        int sp = focus && key_down(g_speedcfg.key), sh = focus && key_down(g_shotcfg.key);
        if (g_speedcfg.key) {
            int want = g_speedcfg.hold ? sp : (sp && !was_speed ? !g_speed_on : g_speed_on);
            if (want != g_speed_on) {
                g_speed_on = want;
                if (!g_speedcfg.hold || DETAIL) {
                    line_begin();
                    put(want ? "game speed x" : "game speed back to normal");
                    if (want) put_f(g_speedcfg.factor);
                    line_end();
                }
            }
            speed_tick();
        }
        if (sh && !was_shot && g_real_present && !g_shot_busy) g_shot_req = 1;
        if (g_shot_busy && !g_shot_state && !g_shot_after.rgb && !g_shot_before.rgb) g_shot_busy = 0; // saved
        was_speed = sp, was_shot = sh;
        Sleep(15);
    }
    return 0;
}

static void load_hotkey_config(void) {
    wchar buf[300];
    float f;
    ini_get(L"Speed", L"Key", NULL, NULL, L"F7", buf, 64);
    g_speedcfg.key = parse_key(buf);
    ini_get(L"Speed", L"Enabled", NULL, NULL, WO_DEV ? L"1" : L"0", buf, 64); // release: off unless asked for
    if (!parse_value(buf, &f)) f = WO_DEV; // unreadable: the build's default
    if (f == 0) g_speedcfg.key = 0;
    ini_get(L"Speed", L"Mode", NULL, NULL, L"toggle", buf, 64);
    g_speedcfg.hold = (buf[0] | 32) == 'h';
    ini_get(L"Speed", L"Factor", NULL, NULL, L"3.0", buf, 64);
    g_speedcfg.factor = parse_value(buf, &f) && f >= 0.1f && f <= 10.0f ? f : 3.0f;
    ini_get(L"Screenshots", L"Key", NULL, NULL, L"F8", buf, 64);
    g_shotcfg.key = parse_key(buf);
    ini_get(L"Screenshots", L"Enabled", NULL, NULL, WO_DEV ? L"1" : L"0", buf, 64);
    if (!parse_value(buf, &f)) f = WO_DEV;
    if (f == 0) g_shotcfg.key = 0;
    ini_get(L"Screenshots", L"Width", NULL, NULL, L"5120", buf, 64);
    g_shotcfg.width = parse_value(buf, &f) && f >= 320 && f <= 16384 ? (int)f : 5120;
    ini_get(L"Screenshots", L"SaveSingles", NULL, NULL, L"1", buf, 64);
    g_shotcfg.singles = parse_value(buf, &f) ? f != 0 : 1;
    ini_get(L"Screenshots", L"Folder", NULL, NULL, L"TWAUWidescreen/shots", buf, 300);
    if (!buf[0]) wcat(buf, L"TWAUWidescreen/shots");
    for (int i = 0; (g_shotcfg.folder[i] = buf[i]); i++) {
    }
}

static void init_hotkeys(void) {
    load_hotkey_config();
    HANDLE u = LoadLibraryW(L"user32.dll");
    if (u) {
        pGetAsyncKeyState = (getasynckeystate_t)GetProcAddress(u, "GetAsyncKeyState");
        pGetForegroundWindow = (getforegroundwindow_t)GetProcAddress(u, "GetForegroundWindow");
        pGetWindowThreadProcessId = (getwindowthreadprocessid_t)GetProcAddress(u, "GetWindowThreadProcessId");
    }
    if (!pGetAsyncKeyState) {
        log_s("ERROR: hotkeys: user32 not available - speed / screenshots off");
        return;
    }
    if (!find_timescale()) {
        log_s("WARNING: game speed / screenshot freeze: SetTimeScale not found (game update?)");
        g_speedcfg.key = 0;
    }
    if (g_speedcfg.key || g_shotcfg.key) {
        line_begin();
        put("hotkeys:");
        if (g_speedcfg.key) {
            put(" speed x");
            put_f(g_speedcfg.factor);
            put(g_speedcfg.hold ? " (hold)" : " (toggle)");
        }
        if (g_shotcfg.key) put(" | comparison screenshot");
        line_end();
        CreateThread(NULL, 0, input_thread, NULL, 0, NULL);
    }
}
