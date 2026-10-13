// =====================================================================
//  ШАУРМИЧНАЯ — симулятор ларька с шаурмой (3D)
//  C++ + SDL для ccdroid / C4droid. Один файл, внешних библиотек нет.
//  Если не собирается: ниже поставь SDL_MODE 1 (SDL 1.2) или 2 (SDL2).
// =====================================================================
#ifndef SDL_MODE
#define SDL_MODE 0            // 0 = авто, 1 = SDL 1.2, 2 = SDL2
#endif
#if SDL_MODE == 0
  #if defined(__has_include)
    #if __has_include(<SDL2/SDL.h>)
      #undef SDL_MODE
      #define SDL_MODE 2
    #endif
  #endif
#endif
#if SDL_MODE == 0
  #undef SDL_MODE
  #define SDL_MODE 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>
#if defined(__GNUC__) && !defined(__clang__) && !defined(__OPTIMIZE__)
  #pragma GCC optimize("O2")      // если среда собирает без оптимизации, включаем её для этого файла
#endif
#include <vector>
#include <map>
#if defined(__unix__) || defined(__linux__) || defined(__ANDROID__) || defined(__APPLE__)
  #include <unistd.h>
  #define HAVE_GETCWD 1
#endif
#if SDL_MODE == 2
  #include <SDL2/SDL.h>
#else
  #include <SDL/SDL.h>
#endif

// ---------- Утилиты ----------
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }
static inline float fminv(float a, float b) { return a < b ? a : b; }
static inline float fmaxv(float a, float b) { return a > b ? a : b; }
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline int clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }
static float frand(float a, float b) { return a + (b - a) * (rand() / (float)RAND_MAX); }

struct V3 { float x, y, z; };
static inline V3 v3(float x, float y, float z) { V3 r; r.x = x; r.y = y; r.z = z; return r; }
static inline V3 operator+(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline V3 operator-(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline V3 operator*(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline V3 vnorm(V3 a) { float l = sqrtf(dot(a, a)); if (l < 1e-8f) l = 1; return a * (1.0f / l); }

typedef uint32_t u32;
static inline u32 RGB(int r, int g, int b) {
  r = clampi(r, 0, 255); g = clampi(g, 0, 255); b = clampi(b, 0, 255);
  return ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}
static inline int CR(u32 c) { return (c >> 16) & 255; }
static inline int CG(u32 c) { return (c >> 8) & 255; }
static inline int CB(u32 c) { return c & 255; }
static u32 mixc(u32 a, u32 b, float t) {
  if (t <= 0) return a;
  if (t >= 1) return b;
  return RGB((int)(CR(a) + (CR(b) - CR(a)) * t), (int)(CG(a) + (CG(b) - CG(a)) * t),
             (int)(CB(a) + (CB(b) - CB(a)) * t));
}
static u32 shadec(u32 c, float f) { return RGB((int)(CR(c) * f), (int)(CG(c) * f), (int)(CB(c) * f)); }

// ---------- Экран и буферы ----------
static int SW = 800, SH = 480, IW = 400, IH = 240, fs = 2;
static int LW = 800, LH = 480;      // логический (горизонтальный) размер экрана
static bool rotMode = false;        // экран вертикальный -> картинку поворачиваем
static int rotDir = 1;              // 1 = по часовой, -1 = против
static int quality = 1;              // 0 низкая, 1 средняя, 2 высокая (разрешение картинки)
static bool aaOn = true;             // сглаживание краёв (можно выключить в меню, если телефон тормозит)
static int wheelN = 10;              // число граней у колёс и фар (растёт с уровнем графики)
static float gProf3D = 0, gProfPr = 0;   // мс за кадр: 3D-превью и вывод на экран (для счётчика FPS)
static bool soundOn = true;           // звук мотора
static bool fpsOn = true;            // счётчик FPS в углу экрана
static bool gDry = false;            // пробный проход: считаем, где фары относительно поверхности, ничего не рисуя
static bool autoPerf = true;         // автоматически упрощать картинку, если телефон не успевает
static int gPerf = 0;                // 0 - полная картинка, 1 - без сглаживания и суперсэмплинга, 2 - ещё и проще колёса, 3 - ещё и картинка на ступень мельче (только пока не хватает скорости)
static int gResDrop = 0;              // на сколько ступеней качества временно уменьшено разрешение (авто-режим, уровень 3)
static bool gApplyQ = false;
static int insL = 0, insR = 0;      // поля экрана: сколько физ. пикселей не видно с начала/конца оси X картинки
static u32 *fb = NULL;
static float *zb = NULL;
static int clipX0, clipY0, clipX1, clipY1;
static float focal = 300, vcx = 0, vcy = 0;
static u32 fogCol = 0;
static float fogA = 1e9f, fogB = 2e9f;
static float gTime = 0;

// ---------- Ввод ----------
struct Ev { int type, x, y; };   // 0 выход, 1 нажатие, 2 отпускание, 3 движение, 4 назад
struct Ptr {
  bool down, pressed, clicked;
  int x, y, sx, sy, cx, cy, dx, dy;
};
static Ptr P;

// ---------- Бэкенд SDL ----------
#if SDL_MODE == 2
static SDL_Window *gWin = NULL;
static SDL_Renderer *gRen = NULL;
static SDL_Texture *gTex = NULL;
static bool gfxInit() {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) return false;
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
  gWin = SDL_CreateWindow("Shaurma", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0,
                          SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN);
  if (!gWin) return false;
  SDL_GetWindowSize(gWin, &SW, &SH);
  gRen = SDL_CreateRenderer(gWin, -1, 0);
  if (!gRen) return false;
  return true;
}
static void gfxTex() {
  if (gTex) SDL_DestroyTexture(gTex);
  gTex = SDL_CreateTexture(gRen, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, IW, IH);
}
static void gfxResize() {}
static void gfxPresent() {
  SDL_UpdateTexture(gTex, NULL, fb, IW * 4);
  SDL_RenderClear(gRen);
  if (rotMode) {
    SDL_Rect d;
    int avail = SH - insL - insR;
    int cy = rotDir > 0 ? insL + avail / 2 : SH - insL - avail / 2;
    d.w = avail; d.h = SW; d.x = SW / 2 - d.w / 2; d.y = cy - d.h / 2;
    SDL_RenderCopyEx(gRen, gTex, NULL, &d, rotDir > 0 ? 90.0 : 270.0, NULL, SDL_FLIP_NONE);
  } else {
    SDL_Rect d;
    d.x = insL; d.y = 0; d.w = SW - insL - insR; d.h = SH;
    SDL_RenderCopy(gRen, gTex, NULL, &d);
  }
  SDL_RenderPresent(gRen);
}
static void gfxQuit() { SDL_Quit(); }
#else
static SDL_Surface *gScr = NULL;
static bool gfxInit() {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) return false;
  gScr = SDL_SetVideoMode(0, 0, 32, SDL_SWSURFACE | SDL_FULLSCREEN);
  if (!gScr) gScr = SDL_SetVideoMode(800, 480, 32, SDL_SWSURFACE);
  if (!gScr) return false;
  SW = gScr->w;
  SH = gScr->h;
  return true;
}
static void gfxTex() {}
static u32 *convBuf = NULL, *rotBuf = NULL;
static int *axMap = NULL;
static void gfxResize() { free(convBuf); convBuf = NULL; free(rotBuf); rotBuf = NULL; }
static void gfxPresent() {
  if (SDL_MUSTLOCK(gScr)) SDL_LockSurface(gScr);
  SDL_PixelFormat *f = gScr->format;
  int bpp = f->BytesPerPixel;
  int n = IW * IH;
  if (!axMap) axMap = (int *)malloc(sizeof(int) * SW);
  const u32 *src = fb;
  bool native = bpp == 4 && f->Rshift == 16 && f->Gshift == 8 && f->Bshift == 0 && f->Rloss == 0 && f->Gloss == 0 && f->Bloss == 0;
  if (!native) {       // перевод в формат экрана (только если он не 0x00RRGGBB)
    if (!convBuf) convBuf = (u32 *)malloc(sizeof(u32) * n);
    for (int i = 0; i < n; i++) {
      u32 c = fb[i];
      convBuf[i] = (((c >> 16) & 255) >> f->Rloss << f->Rshift) | (((c >> 8) & 255) >> f->Gloss << f->Gshift) |
                   ((c & 255) >> f->Bloss << f->Bshift);
    }
    src = convBuf;
  }
  int PW = rotMode ? SH : SW, avail = imax(1, PW - insL - insR);
  if (rotMode) {       // транспонируем блоками: строки rotBuf = столбцы картинки (читать потом подряд)
    if (!rotBuf) rotBuf = (u32 *)malloc(sizeof(u32) * n);
    const int B = 16;
    for (int vb = 0; vb < IH; vb += B)
      for (int ub = 0; ub < IW; ub += B) {
        int ve = imin(vb + B, IH), ue = imin(ub + B, IW);
        for (int v = vb; v < ve; v++) {
          const u32 *sr = src + v * IW;
          for (int u = ub; u < ue; u++) rotBuf[u * IH + v] = sr[u];
        }
      }
    for (int x = 0; x < SW; x++) axMap[x] = imin((rotDir > 0 ? SW - 1 - x : x) * IH / SW, IH - 1);
  } else {
    for (int x = 0; x < SW; x++) axMap[x] = clampi((x - insL) * IW / avail, 0, IW - 1);
  }
  unsigned char *base = (unsigned char *)gScr->pixels;
  int prevBy = -1;
  unsigned char *prevRow = NULL;
  for (int y = 0; y < SH; y++) {
    unsigned char *row = base + y * gScr->pitch;
    int by;
    const u32 *sr;
    if (!rotMode) {
      by = imin(y * IH / SH, IH - 1);
      sr = src + by * IW;
    } else {
      int pu = rotDir > 0 ? y : SH - 1 - y;
      if (pu < insL || pu >= SH - insR) { memset(row, 0, SW * bpp); prevBy = -1; continue; }   // поле экрана
      by = clampi((pu - insL) * IW / avail, 0, IW - 1);
      sr = rotBuf + by * IH;
    }
    if (by == prevBy) { memcpy(row, prevRow, SW * bpp); continue; }
    if (bpp == 4) { u32 *d = (u32 *)row; for (int x = 0; x < SW; x++) d[x] = sr[axMap[x]]; }
    else if (bpp == 2) { uint16_t *d = (uint16_t *)row; for (int x = 0; x < SW; x++) d[x] = (uint16_t)sr[axMap[x]]; }
    else for (int x = 0; x < SW; x++) { u32 v = sr[axMap[x]]; row[x * 3] = v & 255; row[x * 3 + 1] = (v >> 8) & 255; row[x * 3 + 2] = (v >> 16) & 255; }
    if (!rotMode) {   // поля слева и справа
      if (bpp == 4) { u32 *d = (u32 *)row; for (int x = 0; x < imin(insL, SW); x++) d[x] = 0; for (int x = imax(0, SW - insR); x < SW; x++) d[x] = 0; }
      else if (bpp == 2) { uint16_t *d = (uint16_t *)row; for (int x = 0; x < imin(insL, SW); x++) d[x] = 0; for (int x = imax(0, SW - insR); x < SW; x++) d[x] = 0; }
    }
    prevBy = by;
    prevRow = row;
  }
  if (SDL_MUSTLOCK(gScr)) SDL_UnlockSurface(gScr);
  SDL_Flip(gScr);
}
static void gfxQuit() { SDL_Quit(); }
#endif

static bool pollEv(Ev &e) {
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    switch (ev.type) {
      case SDL_QUIT: e.type = 0; e.x = e.y = 0; return true;
      case SDL_MOUSEBUTTONDOWN: e.type = 1; e.x = ev.button.x; e.y = ev.button.y; return true;
      case SDL_MOUSEBUTTONUP: e.type = 2; e.x = ev.button.x; e.y = ev.button.y; return true;
      case SDL_MOUSEMOTION: e.type = 3; e.x = ev.motion.x; e.y = ev.motion.y; return true;
      case SDL_KEYDOWN:
        if (ev.key.keysym.sym == SDLK_ESCAPE) { e.type = 4; e.x = e.y = 0; return true; }
        break;
#if SDL_MODE == 2
      case SDL_APP_WILLENTERBACKGROUND:
      case SDL_APP_TERMINATING:
      case SDL_APP_LOWMEMORY:
        e.type = 5; e.x = e.y = 0; return true;
#else
      case SDL_ACTIVEEVENT:
        if (ev.active.gain == 0) { e.type = 5; e.x = e.y = 0; return true; }
        break;
#endif
      default: break;
    }
  }
  return false;
}

// окно на Android часто сначала меньше экрана (пока прячутся системные панели), а потом вырастает:
// если не пересчитать размер, справа остаётся чёрная полоса
static bool gfxCheckResize() {
#if SDL_MODE == 2
  int w = 0, h = 0; SDL_GetWindowSize(gWin, &w, &h);
  if (w > 0 && h > 0 && (w != SW || h != SH)) { SW = w; SH = h; return true; }
#endif
  return false;
}

static void mapPt(int x, int y, int &mx, int &my) {
  int pu, pv;   // pu - физ. координата вдоль оси X картинки, pv - вдоль оси Y
  if (!rotMode) { pu = x; pv = y; }
  else if (rotDir > 0) { pu = y; pv = SW - 1 - x; }
  else { pu = SH - 1 - y; pv = x; }
  int PW = rotMode ? SH : SW, PH = rotMode ? SW : SH;
  int avail = imax(1, PW - insL - insR);
  mx = clampi((pu - insL) * IW / avail, 0, IW - 1);
  my = clampi(pv * IH / imax(1, PH), 0, IH - 1);
}
static void computeSize() {
  static const int T[3] = {360, 480, 600};
  int md = imax(1, imin(SW, SH));
  int qe = imax(0, quality - gResDrop);
  IW = imax(200, LW * T[qe] / md);
  IH = imax(120, LH * T[qe] / md);
  fs = imax(1, imin(IW, IH) / 170);
}
static bool allocBuffers() {
  free(fb); free(zb);
  fb = (u32 *)malloc(sizeof(u32) * IW * IH);
  zb = (float *)malloc(sizeof(float) * IW * IH);
  if (!fb || !zb) return false;
  memset(fb, 0, sizeof(u32) * IW * IH);
  gfxResize();
  gfxTex();
  return true;
}
// Настройки экрана (поля) хранятся отдельно от игры
// Папка для файлов игры. На Android рабочая папка недоступна для записи, поэтому пишем во внутреннюю память приложения
// (иначе деньги и прогресс не сохранялись). На других системах - рядом с игрой, как раньше.
static char gBase[400] = "";
static char gPathBuf[4][520];
static int gPathI = 0;
static const char *pth(const char *name) {
  gPathI = (gPathI + 1) & 3;
  snprintf(gPathBuf[gPathI], sizeof gPathBuf[0], "%s%s", gBase, name);
  return gPathBuf[gPathI];
}
static const char *CFGF = "shaurma.cfg";
static int autoInset() { return rotMode ? SH * 19 / 1000 : 0; }
static void saveCfg() {
  FILE *f = fopen(pth(CFGF), "w");
  if (!f) return;
  fprintf(f, "%d %d %d %d %d %d %d %d 2\n", insL, insR, rotDir, quality, aaOn ? 1 : 0, fpsOn ? 1 : 0, autoPerf ? 1 : 0, soundOn ? 1 : 0);
  fclose(f);
}
static void loadCfg() {
  FILE *f = fopen(pth(CFGF), "r");
  if (!f) return;
  int a = 0, b = 0, d = 1, q = 1, aa = 1, fp = 1, ap = 1, so = 1, ver = 0;
  int got = fscanf(f, "%d %d %d %d %d %d %d %d %d", &a, &b, &d, &q, &aa, &fp, &ap, &so, &ver);
  if (got < 9 || ver < 2) { a = 0; b = 0; }          // старые поля экрана не берём: приложение теперь рисует на весь экран
  if (got >= 8) soundOn = so != 0;
  if (got >= 3) { insL = clampi(a, 0, 260); insR = clampi(b, 0, 260); rotDir = d < 0 ? -1 : 1; }
  if (got >= 4) quality = clampi(q, 0, 2);
  if (got >= 5) aaOn = aa != 0;
  if (got >= 6) fpsOn = fp != 0;
  if (got >= 7) autoPerf = ap != 0;
  fclose(f);
}

// ---------- Шрифт 5x7 (заглавные, латиница + кириллица) ----------
static const unsigned char FLAT[26][7] = {
  {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
  {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
  {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
  {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},{0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
  {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x19,0x15,0x13,0x11,0x11,0x11},{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
  {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
  {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},{0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
  {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},{0x11,0x11,0x11,0x15,0x15,0x1B,0x11},{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
  {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}};
static const unsigned char FDIG[10][7] = {
  {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
  {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
  {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},{0x1F,0x01,0x02,0x04,0x08,0x08,0x08},{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
  {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}};
// А Б В Г Д Е Ж З И Й К Л М Н О П Р С Т У Ф Х Ц Ч Ш Щ Ъ Ы Ь Э Ю Я
static const unsigned char FCYR[32][7] = {
  {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x1F,0x10,0x10,0x1E,0x11,0x11,0x1E},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
  {0x1F,0x10,0x10,0x10,0x10,0x10,0x10},{0x06,0x0A,0x0A,0x0A,0x0A,0x1F,0x11},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
  {0x15,0x15,0x15,0x0E,0x15,0x15,0x15},{0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},{0x11,0x11,0x13,0x15,0x19,0x11,0x11},
  {0x0A,0x11,0x13,0x15,0x19,0x11,0x11},{0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x07,0x09,0x09,0x09,0x09,0x09,0x11},
  {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
  {0x1F,0x11,0x11,0x11,0x11,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
  {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},{0x11,0x11,0x11,0x0F,0x01,0x11,0x0E},{0x04,0x0E,0x15,0x15,0x15,0x0E,0x04},
  {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},{0x12,0x12,0x12,0x12,0x12,0x1F,0x01},{0x11,0x11,0x11,0x0F,0x01,0x01,0x01},
  {0x15,0x15,0x15,0x15,0x15,0x15,0x1F},{0x15,0x15,0x15,0x15,0x15,0x1F,0x01},{0x18,0x08,0x08,0x0E,0x09,0x09,0x0E},
  {0x11,0x11,0x11,0x19,0x15,0x15,0x19},{0x10,0x10,0x10,0x1E,0x11,0x11,0x1E},{0x0E,0x11,0x01,0x0F,0x01,0x11,0x0E},
  {0x12,0x15,0x15,0x1D,0x15,0x15,0x12},{0x0F,0x11,0x11,0x0F,0x05,0x09,0x11}};
static const unsigned char FYO[7] = {0x0A,0x1F,0x10,0x1E,0x10,0x10,0x1F};
struct PG { char c; unsigned char r[7]; };
static const PG FPUN[] = {
  {'.',{0,0,0,0,0,0x0C,0x0C}},{',',{0,0,0,0,0x0C,0x04,0x08}},{':',{0,0x0C,0x0C,0,0x0C,0x0C,0}},
  {'!',{0x04,0x04,0x04,0x04,0x04,0,0x04}},{'?',{0x0E,0x11,0x01,0x02,0x04,0,0x04}},{'-',{0,0,0,0x1F,0,0,0}},
  {'+',{0,0x04,0x04,0x1F,0x04,0x04,0}},{'/',{0x01,0x01,0x02,0x04,0x08,0x10,0x10}},
  {'%',{0x18,0x19,0x02,0x04,0x08,0x13,0x03}},{'$',{0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}},
  {'(',{0x02,0x04,0x08,0x08,0x08,0x04,0x02}},{')',{0x08,0x04,0x02,0x02,0x02,0x04,0x08}},
  {'<',{0x02,0x04,0x08,0x10,0x08,0x04,0x02}},{'>',{0x08,0x04,0x02,0x01,0x02,0x04,0x08}},
  {'=',{0,0,0x1F,0,0x1F,0,0}},{'|',{0x04,0x04,0x04,0x04,0x04,0x04,0x04}},{'_',{0,0,0,0,0,0,0x1F}},
  {'\'',{0x04,0x04,0x08,0,0,0,0}},{'*',{0,0x15,0x0E,0x1F,0x0E,0x15,0}},{'#',{0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0}},
  {'~',{0,0,0x08,0x15,0x02,0,0}},{'"',{0x0A,0x0A,0x0A,0,0,0,0}}};
static const int NPUN = sizeof(FPUN) / sizeof(FPUN[0]);

static const unsigned char *glyph(int cp) {
  static const unsigned char blank[7] = {0, 0, 0, 0, 0, 0, 0};
  if (cp >= 0x430 && cp <= 0x44F) cp -= 0x20;
  if (cp == 0x451) cp = 0x401;
  if (cp >= 'a' && cp <= 'z') cp -= 32;
  if (cp >= 'A' && cp <= 'Z') return FLAT[cp - 'A'];
  if (cp >= '0' && cp <= '9') return FDIG[cp - '0'];
  if (cp >= 0x410 && cp <= 0x42F) return FCYR[cp - 0x410];
  if (cp == 0x401) return FYO;
  if (cp < 128)
    for (int i = 0; i < NPUN; i++)
      if (FPUN[i].c == (char)cp) return FPUN[i].r;
  return blank;
}
static int u8next(const char *&p) {
  unsigned char c = (unsigned char)*p;
  if (c < 0x80) { p++; return c; }
  if ((c & 0xE0) == 0xC0 && p[1] && (p[1] & 0xC0) == 0x80) { int r = ((c & 0x1F) << 6) | (p[1] & 0x3F); p += 2; return r; }
  if ((c & 0xF0) == 0xE0 && p[1] && p[2] && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) { int r = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3; return r; }
  p++;
  return '?';
}
static int textW(const char *s, int sc) {
  int n = 0;
  while (*s) { u8next(s); n++; }
  return n * 6 * sc;
}
static inline void putpx(int x, int y, u32 c) {
  if ((unsigned)x < (unsigned)IW && (unsigned)y < (unsigned)IH) fb[y * IW + x] = c;
}
static void fillRect(int x, int y, int w, int h, u32 c) {
  int x0 = imax(x, 0), y0 = imax(y, 0), x1 = imin(x + w, IW), y1 = imin(y + h, IH);
  for (int j = y0; j < y1; j++) {
    u32 *r = fb + j * IW;
    for (int i = x0; i < x1; i++) r[i] = c;
  }
}
static void fillRectA(int x, int y, int w, int h, u32 c, int a) {   // a: 0..255
  int x0 = imax(x, 0), y0 = imax(y, 0), x1 = imin(x + w, IW), y1 = imin(y + h, IH);
  int ia = 255 - a;
  for (int j = y0; j < y1; j++) {
    u32 *r = fb + j * IW;
    for (int i = x0; i < x1; i++) {
      u32 d = r[i];
      r[i] = RGB((CR(d) * ia + CR(c) * a) / 255, (CG(d) * ia + CG(c) * a) / 255, (CB(d) * ia + CB(c) * a) / 255);
    }
  }
}
static void fillCircle(int cx, int cy, int r, u32 c, int a) {
  int ia = 255 - a;
  for (int j = -r; j <= r; j++) {
    int y = cy + j;
    if ((unsigned)y >= (unsigned)IH) continue;
    int w = (int)sqrtf((float)(r * r - j * j));
    for (int i = -w; i <= w; i++) {
      int x = cx + i;
      if ((unsigned)x >= (unsigned)IW) continue;
      u32 d = fb[y * IW + x];
      fb[y * IW + x] = RGB((CR(d) * ia + CR(c) * a) / 255, (CG(d) * ia + CG(c) * a) / 255, (CB(d) * ia + CB(c) * a) / 255);
    }
  }
}
// align: 0 слева, 1 по центру, 2 справа
static void drawText(int x, int y, const char *s, int sc, u32 col, int align) {
  int w = textW(s, sc);
  if (align == 1) x -= w / 2;
  else if (align == 2) x -= w;
  while (*s) {
    int cp = u8next(s);
    const unsigned char *g = glyph(cp);
    for (int r = 0; r < 7; r++)
      for (int c = 0; c < 5; c++)
        if (g[r] & (0x10 >> c)) fillRect(x + c * sc, y + r * sc, sc, sc, col);
    x += 6 * sc;
  }
}
static void drawTextSh(int x, int y, const char *s, int sc, u32 col, int align) {
  drawText(x + sc, y + sc, s, sc, RGB(0, 0, 0), align);
  drawText(x, y, s, sc, col, align);
}
// ---------- Камера и растеризатор ----------
static V3 camEye, camF, camR, camU;
static void setCamera(V3 eye, V3 target) {
  camEye = eye;
  camF = vnorm(target - eye);
  camR = vnorm(cross(v3(0, 1, 0), camF));
  camU = cross(camF, camR);
}
static inline V3 toView(V3 p) {
  V3 d = p - camEye;
  return v3(dot(d, camR), dot(d, camU), dot(d, camF));
}
static bool projectPt(V3 p, int &sx, int &sy, float &z) {
  V3 v = toView(p);
  z = v.z;
  if (v.z < 0.5f) return false;
  sx = (int)(vcx + v.x * focal / v.z);
  sy = (int)(vcy - v.y * focal / v.z);
  return true;
}

static void rasterTri(float x0, float y0, float w0, float x1, float y1, float w1, float x2, float y2, float w2, u32 col) {
  // сортируем вершины по Y: a <= b <= c
  float ax = x0, ay = y0, aw = w0, bx = x1, by = y1, bw = w1, cx = x2, cy = y2, cw = w2, t;
  if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; t = aw; aw = bw; bw = t; }
  if (by > cy) { t = bx; bx = cx; cx = t; t = by; by = cy; cy = t; t = bw; bw = cw; cw = t; }
  if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; t = aw; aw = bw; bw = t; }
  if (cy - ay < 1e-4f) return;
  int yS = imax(clipY0, (int)ceilf(ay - 0.502f)), yE = imin(clipY1 - 1, (int)floorf(cy - 0.498f));
  if (yS > yE) return;
  float iac = 1.0f / (cy - ay);
  float dxac = (cx - ax) * iac, dwac = (cw - aw) * iac;
  float dxab = 0, dwab = 0, dxbc = 0, dwbc = 0;
  if (by - ay > 1e-6f) { float i = 1.0f / (by - ay); dxab = (bx - ax) * i; dwab = (bw - aw) * i; }
  if (cy - by > 1e-6f) { float i = 1.0f / (cy - by); dxbc = (cx - bx) * i; dwbc = (cw - bw) * i; }
  for (int y = yS; y <= yE; y++) {
    float py = y + 0.5f;
    float xl = ax + dxac * (py - ay), wl = aw + dwac * (py - ay), xr, wr;
    if (py < by) { xr = ax + dxab * (py - ay); wr = aw + dwab * (py - ay); }
    else { xr = bx + dxbc * (py - by); wr = bw + dwbc * (py - by); }
    if (xl > xr) { t = xl; xl = xr; xr = t; t = wl; wl = wr; wr = t; }
    int xs = imax(clipX0, (int)ceilf(xl - 0.502f)), xe = imin(clipX1 - 1, (int)floorf(xr - 0.498f));
    if (xs > xe) continue;
    float span = xr - xl, dw = span > 1e-6f ? (wr - wl) / span : 0.0f;
    float w = wl + dw * (xs + 0.5f - xl);
    float *zp = zb + y * IW + xs;
    u32 *fp = fb + y * IW + xs;
    int cnt = xe - xs + 1;
    for (; cnt >= 4; cnt -= 4, zp += 4, fp += 4, w += dw * 4.0f) {      // по 4 пикселя за проход
      float w1 = w + dw, w2 = w1 + dw, w3 = w2 + dw;
      if (w > zp[0]) { zp[0] = w; fp[0] = col; }
      if (w1 > zp[1]) { zp[1] = w1; fp[1] = col; }
      if (w2 > zp[2]) { zp[2] = w2; fp[2] = col; }
      if (w3 > zp[3]) { zp[3] = w3; fp[3] = col; }
    }
    for (; cnt > 0; cnt--, zp++, fp++, w += dw) {
      if (w > *zp) { *zp = w; *fp = col; }
    }
  }
}

// Мягкая тень: умножает цвет пикселей под многоугольником (k/256), глубину не пишет
static void rasterTriShadow(float x0, float y0, float w0, float x1, float y1, float w1, float x2, float y2, float w2, int k) {
  float ax = x0, ay = y0, aw = w0, bx = x1, by = y1, bw = w1, cx = x2, cy = y2, cw = w2, t;
  if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; t = aw; aw = bw; bw = t; }
  if (by > cy) { t = bx; bx = cx; cx = t; t = by; by = cy; cy = t; t = bw; bw = cw; cw = t; }
  if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; t = aw; aw = bw; bw = t; }
  if (cy - ay < 1e-4f) return;
  int yS = imax(clipY0, (int)ceilf(ay - 0.502f)), yE = imin(clipY1 - 1, (int)floorf(cy - 0.498f));
  if (yS > yE) return;
  float iac = 1.0f / (cy - ay), dxac = (cx - ax) * iac, dwac = (cw - aw) * iac, dxab = 0, dwab = 0, dxbc = 0, dwbc = 0;
  if (by - ay > 1e-6f) { float i = 1.0f / (by - ay); dxab = (bx - ax) * i; dwab = (bw - aw) * i; }
  if (cy - by > 1e-6f) { float i = 1.0f / (cy - by); dxbc = (cx - bx) * i; dwbc = (cw - bw) * i; }
  for (int y = yS; y <= yE; y++) {
    float py = y + 0.5f, xl = ax + dxac * (py - ay), wl = aw + dwac * (py - ay), xr, wr;
    if (py < by) { xr = ax + dxab * (py - ay); wr = aw + dwab * (py - ay); }
    else { xr = bx + dxbc * (py - by); wr = bw + dwbc * (py - by); }
    if (xl > xr) { t = xl; xl = xr; xr = t; t = wl; wl = wr; wr = t; }
    int xs = imax(clipX0, (int)ceilf(xl - 0.502f)), xe = imin(clipX1 - 1, (int)floorf(xr - 0.498f));
    if (xs > xe) continue;
    float span = xr - xl, dw = span > 1e-6f ? (wr - wl) / span : 0.0f, w = wl + dw * (xs + 0.5f - xl);
    int idx = y * IW + xs;
    for (int x = xs; x <= xe; x++, idx++, w += dw) {
      float zv = zb[idx];
      if (zv > 0.0f && w > zv * 0.9985f) {
        u32 c = fb[idx];
        fb[idx] = ((((c >> 16) & 255u) * (u32)k >> 8) << 16) | ((((c >> 8) & 255u) * (u32)k >> 8) << 8) | (((c & 255u) * (u32)k) >> 8);
      }
    }
  }
}
static void drawPolyView(const V3 *in, int n, u32 col, int shadowK = 0) {
  if (gDry) return;
  V3 v[32];
  int m = 0;
  const float NZ = 0.3f;
  for (int i = 0; i < n; i++) {
    V3 a = in[i], b = in[(i + 1) % n];
    bool ia = a.z >= NZ, ib = b.z >= NZ;
    if (ia) v[m++] = a;
    if (ia != ib) {
      float t = (NZ - a.z) / (b.z - a.z);
      v[m++] = v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, NZ);
    }
  }
  if (m < 3) return;
  float sx[32], sy[32], sw[32], zs = 0;
  for (int i = 0; i < m; i++) {
    float iz = 1.0f / v[i].z;
    sx[i] = vcx + v[i].x * focal * iz;
    sy[i] = vcy - v[i].y * focal * iz;
    sw[i] = iz;
    zs += v[i].z;
  }
  zs /= m;
  if (shadowK) {
    for (int i = 1; i + 1 < m; i++) rasterTriShadow(sx[0], sy[0], sw[0], sx[i], sy[i], sw[i], sx[i + 1], sy[i + 1], sw[i + 1], shadowK);
    return;
  }
  if (zs > fogA) col = mixc(col, fogCol, (zs - fogA) / (fogB - fogA));
  for (int i = 1; i + 1 < m; i++) rasterTri(sx[0], sy[0], sw[0], sx[i], sy[i], sw[i], sx[i + 1], sy[i + 1], sw[i + 1], col);
}

static V3 LDIR;
static float gLight = 1.0f, gNight = 0.0f;        // освещённость мира (1 - день) и «ночность» (0 день ... 1 ночь)
static float lightOf(V3 n) {
  float diff = fmaxv(0.0f, dot(n, LDIR));
  float amb = n.y < -0.4f ? 0.30f : 0.38f + 0.14f * n.y;
  return (amb + 0.62f * diff) * gLight;
}
// Глянец кодируется в старшем байте цвета (0 - матовый, 255 - зеркальный)
#define SHINE(c, sh) (((c) & 0xFFFFFFu) | ((u32)(sh) << 24))

// Грань с известной нормалью: отсечение обратных, свет, отрисовка
// Растеризатор с плавным цветом по треугольнику (освещение интерполируется между вершинами)
// вершина: [0]=x, [1]=y, [2]=1/z, [3]=R, [4]=G, [5]=B
static void rasterTriG(float *a, float *b, float *c) {
  float *t;
  if (a[1] > b[1]) { t = a; a = b; b = t; }
  if (b[1] > c[1]) { t = b; b = c; c = t; }
  if (a[1] > b[1]) { t = a; a = b; b = t; }
  if (c[1] - a[1] < 1e-4f) return;
  int yS = imax(clipY0, (int)ceilf(a[1] - 0.502f)), yE = imin(clipY1 - 1, (int)floorf(c[1] - 0.498f));
  if (yS > yE) return;
  static const int I[5] = {0, 2, 3, 4, 5};
  float iac = 1.0f / (c[1] - a[1]), dac[5], dab[5] = {0, 0, 0, 0, 0}, dbc[5] = {0, 0, 0, 0, 0};
  bool hasAB = b[1] - a[1] > 1e-6f, hasBC = c[1] - b[1] > 1e-6f;
  float iab = hasAB ? 1.0f / (b[1] - a[1]) : 0.0f, ibc = hasBC ? 1.0f / (c[1] - b[1]) : 0.0f;
  for (int m = 0; m < 5; m++) {
    dac[m] = (c[I[m]] - a[I[m]]) * iac;
    if (hasAB) dab[m] = (b[I[m]] - a[I[m]]) * iab;
    if (hasBC) dbc[m] = (c[I[m]] - b[I[m]]) * ibc;
  }
  for (int y = yS; y <= yE; y++) {
    float py = y + 0.5f, L[5], R[5];
    for (int m = 0; m < 5; m++) {
      L[m] = a[I[m]] + dac[m] * (py - a[1]);
      R[m] = py < b[1] ? a[I[m]] + dab[m] * (py - a[1]) : b[I[m]] + dbc[m] * (py - b[1]);
    }
    if (L[0] > R[0]) for (int m = 0; m < 5; m++) { float q = L[m]; L[m] = R[m]; R[m] = q; }
    int xs = imax(clipX0, (int)ceilf(L[0] - 0.502f)), xe = imin(clipX1 - 1, (int)floorf(R[0] - 0.498f));
    if (xs > xe) continue;
    float span = R[0] - L[0], inv = span > 1e-6f ? 1.0f / span : 0.0f, off = xs + 0.5f - L[0];
    float dw = (R[1] - L[1]) * inv, dr = (R[2] - L[2]) * inv, dg = (R[3] - L[3]) * inv, db = (R[4] - L[4]) * inv;
    float w = L[1] + dw * off, rr = L[2] + dr * off, gg = L[3] + dg * off, bb = L[4] + db * off;
    int idx = y * IW + xs;
    {
      float n1 = (float)(xe - xs), r1 = rr + dr * n1, g1 = gg + dg * n1, b1 = bb + db * n1;
      if (rr >= 0.0f && rr <= 255.0f && r1 >= 0.0f && r1 <= 255.0f && gg >= 0.0f && gg <= 255.0f && g1 >= 0.0f && g1 <= 255.0f &&
          bb >= 0.0f && bb <= 255.0f && b1 >= 0.0f && b1 <= 255.0f) {      // быстрый путь: цвет не выходит за 0..255
        for (int x = xs; x <= xe; x++, idx++, w += dw, rr += dr, gg += dg, bb += db) {
          if (w > zb[idx]) { zb[idx] = w; fb[idx] = (u32)(((int)rr << 16) | ((int)gg << 8) | (int)bb); }
        }
        continue;
      }
    }
    for (int x = xs; x <= xe; x++, idx++, w += dw, rr += dr, gg += dg, bb += db) {
      if (w > zb[idx]) {
        zb[idx] = w;
        int ir = (int)rr, ig = (int)gg, ib = (int)bb;
        ir = ir < 0 ? 0 : (ir > 255 ? 255 : ir);
        ig = ig < 0 ? 0 : (ig > 255 ? 255 : ig);
        ib = ib < 0 ? 0 : (ib > 255 ? 255 : ib);
        fb[idx] = (u32)((ir << 16) | (ig << 8) | ib);
      }
    }
  }
}
// Многоугольник с цветами в вершинах: отсечение по ближней плоскости, туман, веер треугольников
// Треугольник с текстурой (ливрея): атрибуты [0]=x, [1]=y, [2]=1/z, [3]=u, [4]=v, [5]=освещённость; tc - цвета уровня текстуры, rx0..ry1 - рамка проекции на этом уровне
static void rasterTriT(const float *a0, const float *b0, const float *c0, const u32 *tc, int stride, int rx0, int ry0, int rx1, int ry1, float invs, float fogT) {
  const float *a = a0, *b = b0, *c = c0, *t;
  if (a[1] > b[1]) { t = a; a = b; b = t; }
  if (b[1] > c[1]) { t = b; b = c; c = t; }
  if (a[1] > b[1]) { t = a; a = b; b = t; }
  if (c[1] - a[1] < 1e-4f) return;
  int yS = imax(clipY0, (int)ceilf(a[1] - 0.502f)), yE = imin(clipY1 - 1, (int)floorf(c[1] - 0.498f));
  if (yS > yE) return;
  static const int I[5] = {0, 2, 3, 4, 5};
  float iac = 1.0f / (c[1] - a[1]), dac[5], dab[5] = {0, 0, 0, 0, 0}, dbc[5] = {0, 0, 0, 0, 0};
  bool hasAB = b[1] - a[1] > 1e-6f, hasBC = c[1] - b[1] > 1e-6f;
  float iab = hasAB ? 1.0f / (b[1] - a[1]) : 0.0f, ibc = hasBC ? 1.0f / (c[1] - b[1]) : 0.0f;
  for (int m = 0; m < 5; m++) {
    dac[m] = (c[I[m]] - a[I[m]]) * iac;
    if (hasAB) dab[m] = (b[I[m]] - a[I[m]]) * iab;
    if (hasBC) dbc[m] = (c[I[m]] - b[I[m]]) * ibc;
  }
  float fr = (float)((fogCol >> 16) & 255), fg = (float)((fogCol >> 8) & 255), fbl = (float)(fogCol & 255);
  for (int y = yS; y <= yE; y++) {
    float py = y + 0.5f, L[5], R[5];
    for (int m = 0; m < 5; m++) {
      L[m] = a[I[m]] + dac[m] * (py - a[1]);
      R[m] = py < b[1] ? a[I[m]] + dab[m] * (py - a[1]) : b[I[m]] + dbc[m] * (py - b[1]);
    }
    if (L[0] > R[0]) for (int m = 0; m < 5; m++) { float q = L[m]; L[m] = R[m]; R[m] = q; }
    int xs = imax(clipX0, (int)ceilf(L[0] - 0.502f)), xe = imin(clipX1 - 1, (int)floorf(R[0] - 0.498f));
    if (xs > xe) continue;
    float span = R[0] - L[0], inv = span > 1e-6f ? 1.0f / span : 0.0f, off = xs + 0.5f - L[0];
    float dw = (R[1] - L[1]) * inv, du = (R[2] - L[2]) * inv, dv = (R[3] - L[3]) * inv, dl = (R[4] - L[4]) * inv;
    float w = L[1] + dw * off, u = L[2] + du * off, v = L[3] + dv * off, l = L[4] + dl * off;
    int idx = y * IW + xs;
    for (int x = xs; x <= xe; x++, idx++, w += dw, u += du, v += dv, l += dl) {
      if (w > zb[idx]) {
        int tx = (int)(u * invs), ty = (int)(v * invs);
        tx = tx < rx0 ? rx0 : (tx > rx1 ? rx1 : tx);
        ty = ty < ry0 ? ry0 : (ty > ry1 ? ry1 : ty);
        u32 pc = tc[ty * stride + tx];
        float lk = l < 0.0f ? 0.0f : (l > 1.3f ? 1.3f : l);
        float cr = ((pc >> 16) & 255) * lk, cg = ((pc >> 8) & 255) * lk, cb = (pc & 255) * lk;
        if (fogT > 0.0f) { cr += (fr - cr) * fogT; cg += (fg - cg) * fogT; cb += (fbl - cb) * fogT; }
        int ir = cr > 255.0f ? 255 : (int)cr, ig = cg > 255.0f ? 255 : (int)cg, ib = cb > 255.0f ? 255 : (int)cb;
        zb[idx] = w; fb[idx] = (u32)((ir << 16) | (ig << 8) | ib);
      }
    }
  }
}
static void drawPolyViewG(const V3 *in, const float (*cin)[3], int n) {
  if (gDry) return;
  if (n == 3 && in[0].z >= 0.3f && in[1].z >= 0.3f && in[2].z >= 0.3f) {      // целый треугольник перед камерой: без отсечения
    float vt3[3][6];
    float zs3 = (in[0].z + in[1].z + in[2].z) * (1.0f / 3.0f);
    for (int i = 0; i < 3; i++) {
      float iz = 1.0f / in[i].z;
      vt3[i][0] = vcx + in[i].x * focal * iz; vt3[i][1] = vcy - in[i].y * focal * iz; vt3[i][2] = iz;
      vt3[i][3] = cin[i][0]; vt3[i][4] = cin[i][1]; vt3[i][5] = cin[i][2];
    }
    if (zs3 > fogA) {
      float t = clampf((zs3 - fogA) / (fogB - fogA), 0.0f, 1.0f);
      float fr = (float)((fogCol >> 16) & 255), fg = (float)((fogCol >> 8) & 255), fbb = (float)(fogCol & 255);
      for (int i = 0; i < 3; i++) { vt3[i][3] += (fr - vt3[i][3]) * t; vt3[i][4] += (fg - vt3[i][4]) * t; vt3[i][5] += (fbb - vt3[i][5]) * t; }
    }
    {   // мелкий треугольник (до ~20 пикселей): плавный цвет не виден, красим одним средним цветом - в разы быстрее
      float ar2 = fabsf((vt3[1][0] - vt3[0][0]) * (vt3[2][1] - vt3[0][1]) - (vt3[2][0] - vt3[0][0]) * (vt3[1][1] - vt3[0][1]));
      if (ar2 < 40.0f) {
        int ir = (int)((vt3[0][3] + vt3[1][3] + vt3[2][3]) * (1.0f / 3.0f)), ig = (int)((vt3[0][4] + vt3[1][4] + vt3[2][4]) * (1.0f / 3.0f)),
            ib = (int)((vt3[0][5] + vt3[1][5] + vt3[2][5]) * (1.0f / 3.0f));
        ir = ir < 0 ? 0 : (ir > 255 ? 255 : ir); ig = ig < 0 ? 0 : (ig > 255 ? 255 : ig); ib = ib < 0 ? 0 : (ib > 255 ? 255 : ib);
        rasterTri(vt3[0][0], vt3[0][1], vt3[0][2], vt3[1][0], vt3[1][1], vt3[1][2], vt3[2][0], vt3[2][1], vt3[2][2], (u32)((ir << 16) | (ig << 8) | ib));
        return;
      }
    }
    rasterTriG(vt3[0], vt3[1], vt3[2]);
    return;
  }
  V3 v[32];
  float cv[32][3];
  int m = 0;
  const float NZ = 0.3f;
  for (int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    V3 a = in[i], b = in[j];
    bool ia = a.z >= NZ, ib = b.z >= NZ;
    if (ia) { v[m] = a; cv[m][0] = cin[i][0]; cv[m][1] = cin[i][1]; cv[m][2] = cin[i][2]; m++; }
    if (ia != ib) {
      float t = (NZ - a.z) / (b.z - a.z);
      v[m] = v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, NZ);
      for (int k = 0; k < 3; k++) cv[m][k] = cin[i][k] + (cin[j][k] - cin[i][k]) * t;
      m++;
    }
  }
  if (m < 3) return;
  float vt[32][6], zs = 0;
  for (int i = 0; i < m; i++) {
    float iz = 1.0f / v[i].z;
    vt[i][0] = vcx + v[i].x * focal * iz;
    vt[i][1] = vcy - v[i].y * focal * iz;
    vt[i][2] = iz;
    vt[i][3] = cv[i][0]; vt[i][4] = cv[i][1]; vt[i][5] = cv[i][2];
    zs += v[i].z;
  }
  zs /= m;
  if (zs > fogA) {
    float t = clampf((zs - fogA) / (fogB - fogA), 0.0f, 1.0f);
    float fr = (float)((fogCol >> 16) & 255), fg = (float)((fogCol >> 8) & 255), fbb = (float)(fogCol & 255);
    for (int i = 0; i < m; i++) {
      vt[i][3] += (fr - vt[i][3]) * t; vt[i][4] += (fg - vt[i][4]) * t; vt[i][5] += (fbb - vt[i][5]) * t;
    }
  }
  for (int i = 1; i + 1 < m; i++) rasterTriG(vt[0], vt[i], vt[i + 1]);
}
// цвет в вершине: освещение по нормали вершины плюс глянец (те же формулы, что у плоской грани)
// Цвет «окружения» в направлении R: светлый горизонт и голубое небо сверху, тёмная дорога снизу
static void envColor(V3 R, float *o) {
  if (R.y >= 0) {
    float t = sqrtf(fminv(1.0f, R.y));
    o[0] = 238.0f + (40.0f - 238.0f) * t; o[1] = 244.0f + (96.0f - 244.0f) * t; o[2] = 250.0f + (196.0f - 250.0f) * t;
  } else {
    float t = fminv(1.0f, -R.y * 2.2f);
    o[0] = 120.0f + (22.0f - 120.0f) * t; o[1] = 124.0f + (24.0f - 124.0f) * t; o[2] = 130.0f + (28.0f - 130.0f) * t;
  }
}
// Глянец: отражение окружения (сильнее под скользящим углом, как у лака и стекла) и яркий блик солнца. c = r, g, b (0..255)
static void glossMix(V3 p, V3 nrm, int shi, float *c) {
  V3 vd = vnorm(camEye - p), hv = vnorm(vd + LDIR);
  float nd = dot(nrm, vd), fb = 1.0f - fmaxv(0.0f, nd), fr = fb * fb * fb, g = shi * (1.0f / 255.0f);
  V3 R = nrm * (2.0f * nd) - vd;
  float e[3];
  envColor(R, e);
  float kr = clampf(g * (0.30f + 0.70f * fr), 0.0f, 0.85f);
  c[0] += (e[0] - c[0]) * kr; c[1] += (e[1] - c[1]) * kr; c[2] += (e[2] - c[2]) * kr;
  float sb = fmaxv(0.0f, dot(nrm, hv)), s2 = sb * sb, s4 = s2 * s2, s8 = s4 * s4, s16 = s8 * s8;
  float k2 = clampf(s16 * s8 * s4 * g, 0.0f, 0.9f);
  c[0] += (255.0f - c[0]) * k2; c[1] += (255.0f - c[1]) * k2; c[2] += (255.0f - c[2]) * k2;
}
// цвет в вершине: освещение по нормали, затемнение у земли и глянец
static void shadeVertex(V3 p, V3 nrm, u32 col, int shi, float *o) {
  float l = lightOf(nrm);
  float c[3] = {((col >> 16) & 255) * l, ((col >> 8) & 255) * l, (col & 255) * l};
  if (shi) {
    float ao = 0.52f + 0.48f * clampf(p.y * (1.0f / 1.0f), 0.0f, 1.0f);     // низ кузова темнее: машина «стоит» на земле
    c[0] *= ao; c[1] *= ao; c[2] *= ao;
    glossMix(p, nrm, shi, c);
  }
  o[0] = c[0]; o[1] = c[1]; o[2] = c[2];
}
// то же для плоской грани: возвращает готовый цвет
static u32 shadeFlat(V3 fc, V3 nrm, u32 col, int shi, bool lit) {
  col &= 0xFFFFFFu;
  if (!shi) return lit ? shadec(col, lightOf(nrm)) : col;
  float l = lit ? lightOf(nrm) : 1.0f;
  float ao = lit ? 0.52f + 0.48f * clampf(fc.y * (1.0f / 1.0f), 0.0f, 1.0f) : 1.0f;
  float c[3] = {((col >> 16) & 255) * l * ao, ((col >> 8) & 255) * l * ao, (col & 255) * l * ao};
  glossMix(fc, nrm, shi, c);
  return RGB((int)clampf(c[0], 0.0f, 255.0f), (int)clampf(c[1], 0.0f, 255.0f), (int)clampf(c[2], 0.0f, 255.0f));
}
static void faceSmooth(const V3 *p, const V3 *nv, int n, V3 fnrm, u32 col) {
  V3 fc = p[0];
  for (int i = 1; i < n; i++) fc = fc + p[i];
  fc = fc * (1.0f / n);
  if (dot(fnrm, fc - camEye) >= 0) return;
  int shi = (int)((col >> 24) & 255u);
  col &= 0xFFFFFFu;
  V3 vv[8];
  float cc[8][3];
  for (int i = 0; i < n && i < 8; i++) { vv[i] = toView(p[i]); shadeVertex(p[i], nv[i], col, shi, cc[i]); }
  drawPolyViewG(vv, cc, n);
}
static void faceN(const V3 *p, int n, V3 nrm, u32 col, bool lit) {
  if (gDry) return;
  V3 fc = p[0];
  for (int i = 1; i < n; i++) fc = fc + p[i];
  fc = fc * (1.0f / n);
  if (dot(nrm, fc - camEye) >= 0) return;
  int shi = (int)((col >> 24) & 255u);
  col &= 0xFFFFFFu;
  V3 vv[24];
  for (int i = 0; i < n; i++) vv[i] = toView(p[i]);
  u32 c2 = shadeFlat(fc, nrm, col, shi, lit);
  drawPolyView(vv, n, c2);
}
static void quadFace(V3 c, V3 u, V3 v, V3 n, u32 col, bool lit) {
  V3 p[4];
  p[0] = c - u - v; p[1] = c + u - v; p[2] = c + u + v; p[3] = c - u + v;
  faceN(p, 4, n, col, lit);
}
// Параллелепипед: центр, полуразмеры, поворот вокруг Y
static void drawBox(V3 c, V3 h, float yaw, u32 col, bool lit) {
  if (gDry) return;
  float cs = cosf(yaw), sn = sinf(yaw);
  V3 ax = v3(cs, 0, -sn), ay = v3(0, 1, 0), az = v3(sn, 0, cs);
  V3 ex = ax * h.x, ey = ay * h.y, ez = az * h.z;
  quadFace(c + ey, ex, ez, ay, col, lit);
  quadFace(c + ex, ey, ez, ax, col, lit);
  quadFace(c - ex, ey, ez, ax * -1.0f, col, lit);
  quadFace(c + ez, ex, ey, az, col, lit);
  quadFace(c - ez, ex, ey, az * -1.0f, col, lit);
}
// Призма (колесо) вокруг оси X кузова
static void drawWheel(V3 c, V3 ax, V3 ay, V3 az, float r, float hw, u32 col) {
  // мелкие детали (фары, колпаки) рисуются с меньшим числом граней - на картинке не видно, а полигонов втрое меньше
  int N = r < 0.13f ? imin(wheelN, 8) : (r < 0.25f ? imin(wheelN, 12) : wheelN);
  static float CT[25][24], ST[25][24];
  static bool tabOk[25];
  if (N < 6) N = 6;
  if (!tabOk[N]) {
    for (int i = 0; i < N; i++) { float t = 6.2831853f * i / N; CT[N][i] = cosf(t); ST[N][i] = sinf(t); }
    tabOk[N] = true;
  }
  V3 ring[24];
  for (int i = 0; i < N; i++) ring[i] = ay * (r * CT[N][i]) + az * (r * ST[N][i]);
  for (int i = 0; i < N; i++) {
    int j = (i + 1) % N;
    V3 p[4];
    p[0] = c + ring[i] - ax * hw; p[1] = c + ring[j] - ax * hw;
    p[2] = c + ring[j] + ax * hw; p[3] = c + ring[i] + ax * hw;
    V3 mid = vnorm(ring[i] + ring[j]);
    faceN(p, 4, mid, col, true);
  }
  V3 cap[24];
  for (int s = -1; s <= 1; s += 2) {
    for (int i = 0; i < N; i++) cap[i] = c + ring[i] + ax * (hw * s);
    faceN(cap, N, ax * (float)s, col, true);
  }
}

static void setClipRect(int x0, int y0, int x1, int y1) { clipX0 = x0; clipY0 = y0; clipX1 = x1; clipY1 = y1; }
static void clearDepth() {
  if (clipX0 == 0 && clipX1 == IW) { memset(zb + clipY0 * IW, 0, sizeof(float) * (size_t)(clipY1 - clipY0) * IW); return; }
  for (int y = clipY0; y < clipY1; y++) { float *r = zb + y * IW; for (int x = clipX0; x < clipX1; x++) r[x] = 0; }
}
static void fillSky() {
  float horizon = vcy + focal * camF.y / sqrtf(fmaxv(0.05f, 1 - camF.y * camF.y));
  u32 top = mixc(RGB(60, 70, 110), RGB(6, 8, 26), gNight);
  for (int y = clipY0; y < clipY1; y++) {
    u32 c = y < horizon ? mixc(top, fogCol, clampf((y - clipY0) / fmaxv(1.0f, horizon - clipY0), 0, 1)) : fogCol;
    u32 *r = fb + y * IW; for (int x = clipX0; x < clipX1; x++) r[x] = c;
  }
}
static void groundQuad(float x0, float z0, float x1, float z1, float y, u32 col) {
  if (gLight < 0.999f) col = RGB((int)(CR(col) * gLight), (int)(CG(col) * gLight), (int)(CB(col) * gLight));
  V3 p[4] = {v3(x0, y, z0), v3(x1, y, z0), v3(x1, y, z1), v3(x0, y, z1)};
  V3 vv[4]; for (int i = 0; i < 4; i++) vv[i] = toView(p[i]);
  drawPolyView(vv, 4, col);
}
static void discOct(float cx, float cz, float r, float y, u32 col) {
  V3 vv[8];
  for (int i = 0; i < 8; i++) { float t = 6.2831853f * i / 8 + 0.3927f; vv[i] = toView(v3(cx + cosf(t) * r, y, cz + sinf(t) * r)); }
  drawPolyView(vv, 8, col);
}
static bool hitR(int x, int y, int w, int h) {
  if (!P.clicked) return false;
  if (P.cx >= x && P.cx < x + w && P.cy >= y && P.cy < y + h && P.sx >= x && P.sx < x + w && P.sy >= y && P.sy < y + h) { P.clicked = false; return true; }
  return false;
}
static bool button(int x, int y, int w, int h, const char *label, bool en, u32 col) {
  u32 c = en ? col : RGB(58, 60, 70);
  fillRect(x, y, w, h, shadec(c, 0.55f));
  fillRect(x + 1, y + 1, w - 2, h - 2, c);
  fillRect(x + 1, y + 1, w - 2, imax(1, h / 8), shadec(c, 1.25f));
  char lines[3][64]; int n = 0; const char *p = label;
  while (n < 3) {
    const char *e = strchr(p, '\n'); int len = e ? (int)(e - p) : (int)strlen(p); if (len > 63) len = 63;
    memcpy(lines[n], p, len); lines[n][len] = 0; n++; if (!e) break; p = e + 1;
  }
  int sc = fs; while (sc > 1 && textW(lines[0], sc) > w - 4) sc--;
  int lh = 9 * sc, y0 = y + (h - n * lh + 2 * sc) / 2;
  for (int i = 0; i < n; i++) drawText(x + w / 2, y0 + i * lh, lines[i], sc, en ? RGB(255, 255, 255) : RGB(140, 142, 152), 1);
  return en && hitR(x, y, w, h);
}
static void drawLine(int x0, int y0, int x1, int y1, u32 c, int th) {
  int dx = abs(x1 - x0), dy = abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy, n = 0;
  while (n++ < 4000) {
    fillRect(x0 - th / 2, y0 - th / 2, th, th, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err; if (e2 > -dy) { err -= dy; x0 += sx; } if (e2 < dx) { err += dx; y0 += sy; }
  }
}


// =====================================================================
//  ШАУРМИЧНАЯ: игра
// =====================================================================
#include <algorithm>
#include <string>
enum { B_LAVASH, B_PITA, NBASE };
enum { I_CHICKEN, I_BEEF, I_TOMATO, I_CUCUMBER, I_CABBAGE, I_ONION, I_FRIES, I_CHEESE, I_JALAP, S_GARLIC, S_HOT, S_CHEESESAUCE, NING };
enum { D_COLA, D_AYRAN, NDRINK };
struct Ing { const char *name; const char *shortn; u32 col; int unlock; int price; };   // unlock: номер улучшения (-1 - есть сразу)
static const Ing ING[NING] = {
  {"Курица", "КУРИЦА", RGB(220, 170, 110), -1, 60}, {"Говядина", "ГОВЯДИНА", RGB(150, 80, 50), 0, 90},
  {"Помидор", "ПОМИДОР", RGB(225, 50, 40), -1, 15}, {"Огурец", "ОГУРЕЦ", RGB(80, 170, 70), -1, 15},
  {"Капуста", "КАПУСТА", RGB(200, 230, 170), -1, 10}, {"Лук", "ЛУК", RGB(220, 200, 230), -1, 10},
  {"Картошка", "КАРТОШКА", RGB(250, 210, 90), -1, 25}, {"Сыр", "СЫР", RGB(255, 200, 40), 2, 30},
  {"Халапеньо", "ХАЛАПЕНЬО", RGB(40, 120, 40), 3, 25},
  {"Чесночный", "ЧЕСНОЧН.", RGB(250, 250, 235), -1, 15}, {"Острый", "ОСТРЫЙ", RGB(220, 40, 30), 4, 15}, {"Сырный", "СЫРНЫЙ", RGB(250, 190, 60), 5, 20},
};
static const char *BASEN[NBASE] = {"ЛАВАШ", "ПИТА"};
static const char *DRINKN[NDRINK] = {"КОЛА", "АЙРАН"};
static const u32 DRINKC[NDRINK] = {RGB(120, 40, 30), RGB(240, 240, 240)};
// улучшения
struct Upg { const char *name; const char *desc; int cost; };
enum { U_BEEF, U_PITA, U_CHEESE, U_JALAP, U_HOT, U_CHEESESAUCE, U_DRINKS, U_GRILL, U_TV, U_NEON, U_AUTOCUT, NUPG };
static const Upg UPG[NUPG] = {
  {"Говядина", "второй вертел и новое мясо, дороже заказы", 3000}, {"Пита", "заказы в пите", 1500},
  {"Сыр", "новая начинка", 1000}, {"Халапеньо", "острая начинка", 1200}, {"Острый соус", "новый соус", 800},
  {"Сырный соус", "новый соус", 1200}, {"Напитки", "кола и айран к заказу", 2500}, {"Быстрый гриль", "жарит вдвое быстрее", 2000},
  {"Телевизор", "клиенты ждут дольше", 3500}, {"Неоновая вывеска", "клиентов больше", 4000}, {"Автонарезка", "мясо срезается само", 5000},
};
// заказы, клиенты, сборка
struct Order { int base; std::vector<int> fill; bool grill; int drink; int price; };
struct Cust { Order o; float pat, patMax, x, z, tx, tz, walk; int state; u32 shirt, skin; float mood; };   // state: 0 идёт в очередь, 1 ждёт, 2 уходит
struct Wrap { int base; std::vector<int> fill; bool wrapped, grilled; float grillT; int drink; };
static std::vector<Cust> custs;
static Wrap wr;
static int money = 2000, day = 1, lavash = 20, pita = 0, chickenLoads = 1, beefLoads = 0;
static unsigned upg = 0;
static float rating = 3.5f; static float ratHist[20]; static int ratN = 0;
static float spitMeat[2] = {20, 0}, spitCook[2] = {1, 0}, portions[2] = {0, 0}, cutT[2] = {0, 0};
static float dayT = 0, spawnT = 3, msgT = 0; static char msg[96] = "";
static int dayServed = 0, dayLost = 0, dayRev = 0, dayTips = 0, dayWrong = 0;
enum { GS_TITLE, GS_DAY, GS_SUMMARY, GS_SHOP };
static int gs = GS_TITLE; static bool paused = false, hasSave = false;
static const float DAY_LEN = 270.0f;                     // день 08:00-23:00 (15 часов) = 4.5 минуты
enum { SH_CLOSED, SH_OPEN, SH_BREAK, SH_CLOSING };       // ларёк: закрыт (подготовка), открыт, перерыв, закрывается (дообслуживаем очередь)
static int shopSt = SH_CLOSED; static float shutterK = 0, breakT = 0;
static float energy = 100, clean = 100; static bool tiredSaid = false;
static int weather = 0, dayGoal = 10, goalDone = 0, dayRent = 0, dayBonus = 0, dayCleanBad = 0; static float waitSum = 0;
static const char *WDAY[7] = {"ПН", "ВТ", "СР", "ЧТ", "ПТ", "СБ", "ВС"};
static float hourNow() { return 8.0f + dayT * 15.0f; }
static float rushMul(float h) { return h < 9 ? 0.5f : h < 12 ? 1.0f : h < 14 ? 1.8f : h < 18 ? 1.0f : h < 20 ? 1.5f : 0.8f; }   // обед и вечер - наплыв
static bool has(int u) { return (upg >> u) & 1; }
static bool ingOpen(int i) { return ING[i].unlock < 0 || has(ING[i].unlock); }
static void say(const char *s) { snprintf(msg, sizeof msg, "%s", s); msgT = 2.5f; }
static void serve();
// ---------- повар: подходишь к холодильнику или вертелу, там выбираешь продукты, потом несёшь на доску ----------
enum { A_GOTO, A_CUT, A_DELIVER, A_WRAP, A_GRILL, A_SERVE, A_CLEAN };
enum { ST_NONE = -1, ST_FRIDGE, ST_SPIT0, ST_SPIT1, ST_DRINKS };
struct Act { int type, p; };
struct Cook { float x, z, face, walk, animT; int phase, carry; };
static Cook cook = {0, -0.35f, 0, 0, 0, 0, -1};
static std::vector<Act> acts;
static std::vector<int> hands;                  // что повар держит: 100+основа, начинка, 200+напиток
static int session = ST_NONE;                   // у какого места сейчас стоит и выбирает
static float grabT = 0;                         // анимация «взял с полки»
static Wrap pw; static float pp[2] = {0, 0}; static int pCut[2] = {0, 0};
static int wrapLoc = 0;
static float fridgeOpen[2] = {0, 0}; static bool firstPerson = false; static float camYawS = 0, camPitchS = -0.35f;
static int stock[NING]; static int drinks[NDRINK];   // запас в холодильниках (порции)
struct Delivery { int item, amount; float t; };       // заказанные продукты едут
static std::vector<Delivery> deliveries;
static const float STAND_Z = -0.35f, COOK_SPEED = 4.3f, BOARD_X = -0.6f;
static float spitX(int s) { return s ? -1.75f : -2.6f; }
static float stationX(int st) { return st == ST_FRIDGE ? -2.55f : st == ST_DRINKS ? 2.55f : spitX(st == ST_SPIT1); }
static float stationZ(int st) { return st == ST_FRIDGE || st == ST_DRINKS ? -1.3f : STAND_Z; }
struct Plan { float sx, sz, st; bool dst; float dx, dz, dt; int carry; };
static Plan planOf(const Act &a) {
  Plan p = {BOARD_X, STAND_Z, 0.3f, false, BOARD_X, STAND_Z, 0.25f, -1};
  switch (a.type) {
    case A_GOTO: p.sx = stationX(a.p); p.sz = stationZ(a.p); p.st = 0.35f; break;
    case A_CUT: p.sx = spitX(a.p); p.st = 0.8f; break;
    case A_DELIVER: p.st = 0.25f + 0.2f * hands.size(); break;
    case A_WRAP: p.st = 1.3f; break;
    case A_GRILL: p.st = 0.25f; p.dst = true; p.dx = 2.5f; p.dt = 0.3f; p.carry = 300; break;
    case A_SERVE: p.sx = wrapLoc == 1 ? 2.5f : BOARD_X; p.st = 0.25f; p.dst = true; p.dx = 0; p.dz = -0.05f; p.dt = 0.45f; p.carry = 300; break;
    case A_CLEAN: p.sx = 0.6f; p.st = 2.5f; break;                         // протереть прилавок
  }
  return p;
}
static void resetPlan() { pw = wr; for (size_t i = 0; i < hands.size(); i++) { int h = hands[i]; if (h >= 200) pw.drink = h - 200; else if (h >= 100) pw.base = h - 100; else pw.fill.push_back(h); } pp[0] = portions[0]; pp[1] = portions[1]; pCut[0] = pCut[1] = 0; }
static void cancelActs(const char *why) { acts.clear(); cook.phase = 0; session = ST_NONE; if (wrapLoc == 2) wrapLoc = 0; resetPlan(); if (why) say(why); }
static bool queueFull() { if (acts.size() >= 8) { say("ПОВАР ЗАНЯТ, ПОДОЖДИ"); return true; } return false; }
static void push(int t, int p) { Act a = {t, p}; acts.push_back(a); }
static void goStation(int st) { if (queueFull()) return; if (session == st && acts.empty()) return; session = ST_NONE; push(A_GOTO, st); }
static void qDeliverIfNeeded() { if (!hands.empty()) { bool q = false; for (size_t i = 0; i < acts.size(); i++) if (acts[i].type == A_DELIVER) q = true; if (!q) push(A_DELIVER, 0); } }
static bool canTakeFill(int i) {
  if (pw.wrapped) { say("ШАУРМА УЖЕ ЗАВЁРНУТА"); return false; }
  if (pw.base < 0) { say("СНАЧАЛА ВОЗЬМИ ЛАВАШ ИЛИ ПИТУ"); return false; }
  for (size_t q = 0; q < pw.fill.size(); q++) if (pw.fill[q] == i) { say("ЭТО УЖЕ ВЗЯЛ"); return false; }
  return true;
}
// взять продукт, стоя у холодильника/вертела (вызывается из панели продуктов)
static void takeItem(int code) {
  if (grabT > 0 || hands.size() >= 9) return;
  if (code >= 200) { int d = code - 200; if (pw.drink >= 0) { say("НАПИТОК УЖЕ ВЗЯЛ"); return; } if (drinks[d] <= 0) { say("ЭТОТ НАПИТОК ЗАКОНЧИЛСЯ, ЗАКАЖИ"); return; } drinks[d]--; pw.drink = d; }
  else if (code >= 100) { int b = code - 100; if (pw.base >= 0) { say("ОСНОВА УЖЕ ЕСТЬ"); return; } if (b == B_LAVASH ? lavash <= 0 : pita <= 0) { say("ЗАКОНЧИЛОСЬ, ЗАКАЖИ ПРОДУКТЫ"); return; } if (b == B_LAVASH) lavash--; else pita--; pw.base = b; }
  else if (code == I_CHICKEN || code == I_BEEF) { int s = code == I_BEEF; if (!canTakeFill(code)) return; if (portions[s] < 1) { say("СНАЧАЛА СРЕЖЬ МЯСО"); return; } portions[s] -= 1; pp[s] = portions[s]; pw.fill.push_back(code); }
  else { if (!canTakeFill(code)) return; if (stock[code] <= 0) { say("ЗАКОНЧИЛОСЬ, ЗАКАЖИ ПРОДУКТЫ"); return; } stock[code]--; pw.fill.push_back(code); }
  hands.push_back(code); grabT = 0.3f;
}
static void qCut(int s) {
  if (queueFull()) return;
  if (spitMeat[s] - pCut[s] < 1) { int loads = s ? beefLoads : chickenLoads; if (loads <= 0 && spitMeat[s] < 1) { say("МЯСО ЗАКОНЧИЛОСЬ! ЗАКАЖИ"); return; } if (spitMeat[s] >= 1) { say("НА ВЕРТЕЛЕ БОЛЬШЕ НЕТ МЯСА"); return; } }
  else if (spitCook[s] < 1) { say("МЯСО ЕЩЁ ЖАРИТСЯ"); return; }
  if (spitMeat[s] >= 1) { pCut[s]++; pp[s] += 1; }
  push(A_CUT, s);
}
static void qWrap() { if (queueFull()) return; if (pw.base < 0 || pw.wrapped) return; session = ST_NONE; qDeliverIfNeeded(); pw.wrapped = true; push(A_WRAP, 0); }
static void qGrill() { if (queueFull()) return; if (!pw.wrapped || pw.grilled) return; session = ST_NONE; pw.grilled = true; push(A_GRILL, 0); }
static void qServe() { if (queueFull()) return; if (!pw.wrapped) { say("СНАЧАЛА ЗАВЕРНИ ШАУРМУ"); return; } for (size_t i = 0; i < acts.size(); i++) if (acts[i].type == A_SERVE) return; session = ST_NONE; qDeliverIfNeeded(); push(A_SERVE, 0); }
static void qToBoard() { session = ST_NONE; qDeliverIfNeeded(); }
static void trashWrap() { clean = fmaxv(0.0f, clean - 3); wr = Wrap(); wr.base = -1; wr.drink = -1; wrapLoc = 0; hands.clear(); cancelActs("ВЫБРОШЕНО"); }
static void applyAct(const Act &a) {
  switch (a.type) {
    case A_GOTO: session = a.p; break;
    case A_CUT: { int s = a.p;
      if (spitMeat[s] < 1) { int &loads = s ? beefLoads : chickenLoads; if (loads > 0) { loads--; spitMeat[s] = 20; spitCook[s] = 0; say("НОВОЕ МЯСО НА ВЕРТЕЛЕ, ЖАРИТСЯ..."); } }
      else { portions[s] += 1; spitMeat[s] -= 1; pCut[s] = imax(0, pCut[s] - 1); }
    } break;
    case A_DELIVER:                                       // кладём на доску: сначала основу, потом всё остальное
      for (size_t i = 0; i < hands.size(); i++) if (hands[i] >= 100 && hands[i] < 200) wr.base = hands[i] - 100;
      for (size_t i = 0; i < hands.size(); i++) { int h = hands[i]; if (h >= 200) wr.drink = h - 200; else if (h < 100) wr.fill.push_back(h); }
      hands.clear(); break;
    case A_WRAP: wr.wrapped = true; break;
    case A_GRILL: wrapLoc = 1; wr.grillT = has(U_GRILL) ? 1.5f : 3.0f; break;
    case A_CLEAN: clean = 100; energy = fmaxv(0.0f, energy - 3); say("ЧИСТО!"); break;
  }
}
static float tiredK() { return 0.55f + 0.45f * clampf(energy / 60.0f, 0, 1); }   // уставший повар всё делает медленнее
static void cookUpdate(float dt) {
  if (grabT > 0) grabT -= dt;
  if (energy < 25 && !tiredSaid) { tiredSaid = true; say("ПОВАР УСТАЛ! СДЕЛАЙ ПЕРЕРЫВ"); }
  if (energy > 40) tiredSaid = false;
  dt *= tiredK();
  if (acts.empty()) {                                      // свободен: стоит, где был (у холодильника/вертела - выбирает)
    if (session == ST_NONE) {
      float dx = BOARD_X - cook.x, dz = STAND_Z - cook.z, d = sqrtf(dx * dx + dz * dz);
      if (d > 0.05f && hands.empty()) { float st = fminv(COOK_SPEED * dt, d); cook.x += dx / d * st; cook.z += dz / d * st; cook.walk += dt * 10; cook.face = atan2f(dx, dz); }
      else if (d <= 0.05f) cook.face = atan2f(0 - cook.x, 0.5f - cook.z);
    }
    cook.phase = 0; cook.animT = 0; return;
  }
  Act a = acts[0]; Plan p = planOf(a);
  float tx = cook.phase < 2 ? p.sx : p.dx, tz = cook.phase < 2 ? p.sz : p.dz;
  if (cook.phase == 0 || cook.phase == 2) {
    float dx = tx - cook.x, dz = tz - cook.z, d = sqrtf(dx * dx + dz * dz);
    if (d > 0.05f) { float st = fminv(COOK_SPEED * dt, d); cook.x += dx / d * st; cook.z += dz / d * st; cook.walk += dt * 10; cook.face = atan2f(dx, dz); energy = fmaxv(0.0f, energy - st * 0.3f); }
    else { cook.phase++; cook.animT = 0; float lx = fabsf(tx - BOARD_X) < 0.01f ? 0.0f : tx; cook.face = (tz < -1.0f) ? (tx < 0 ? -1.5708f : 1.5708f) : atan2f(lx - cook.x, 0.5f - cook.z); }
    return;
  }
  if (a.type == A_SERVE && cook.phase == 1 && wrapLoc == 1 && wr.grillT > 0) return;
  cook.animT += dt;
  if (cook.animT < (cook.phase == 1 ? p.st : p.dt)) return;
  if (cook.phase == 1) {
    if (!p.dst) { applyAct(a); energy = fmaxv(0.0f, energy - 1.6f); if (!acts.empty()) acts.erase(acts.begin()); cook.phase = 0; return; }
    if (p.carry == 300) wrapLoc = 2;
    cook.phase = 2; return;
  }
  if (a.type == A_SERVE) { int before = dayServed; serve(); wrapLoc = 0; if (dayServed != before) resetPlan(); }
  else applyAct(a);
  energy = fmaxv(0.0f, energy - 1.6f);
  if (!acts.empty()) acts.erase(acts.begin());
  cook.phase = 0;
}
static void fridgeUpdate(float dt) {
  for (int f = 0; f < 2; f++) {
    int st = f ? ST_DRINKS : ST_FRIDGE; float sx = stationX(st), d = sqrtf((cook.x - sx) * (cook.x - sx) + (cook.z + 1.3f) * (cook.z + 1.3f));
    bool want = d < 0.8f && (session == st || (!acts.empty() && acts[0].type == A_GOTO && acts[0].p == st));
    fridgeOpen[f] += ((want ? 1.0f : 0.0f) - fridgeOpen[f]) * fminv(1.0f, dt * 7.0f);
  }
  for (size_t i = 0; i < deliveries.size();) {             // привезли продукты
    deliveries[i].t -= dt;
    if (deliveries[i].t <= 0) { Delivery d = deliveries[i]; deliveries.erase(deliveries.begin() + i);
      if (d.item >= 300) { if (d.item == 300) chickenLoads += d.amount; else beefLoads += d.amount; }
      else if (d.item >= 200) drinks[d.item - 200] += d.amount; else if (d.item >= 100) { if (d.item == 100) lavash += d.amount; else pita += d.amount; } else stock[d.item] += d.amount;
      say("КУРЬЕР ПРИВЁЗ ПРОДУКТЫ!"); }
    else i++;
  }
}
static const char *actName(int t) { static const char *N[] = {"ИДЁТ", "РЕЖЕТ МЯСО", "НЕСЁТ НА ДОСКУ", "ЗАВОРАЧИВАЕТ", "НЕСЁТ НА ГРИЛЬ", "ОТДАЁТ ЗАКАЗ", "ДЕЛАЕТ УБОРКУ"}; return N[t]; }
static void qClean() { if (queueFull()) return; for (size_t i = 0; i < acts.size(); i++) if (acts[i].type == A_CLEAN) return; session = ST_NONE; qDeliverIfNeeded(); push(A_CLEAN, 0); }
// ---------- закупка: что можно заказать ----------
struct Pack { int item; const char *name; int amount, cost; };
static const Pack PACKS[] = {
  {100, "ЛАВАШ", 20, 600}, {101, "ПИТА", 20, 700}, {300, "КУРИЦА (ВЕРТЕЛ)", 1, 1200}, {301, "ГОВЯДИНА (ВЕРТЕЛ)", 1, 1600},
  {I_TOMATO, "ПОМИДОРЫ", 15, 180}, {I_CUCUMBER, "ОГУРЦЫ", 15, 180}, {I_CABBAGE, "КАПУСТА", 15, 120}, {I_ONION, "ЛУК", 15, 120},
  {I_FRIES, "КАРТОШКА ФРИ", 15, 250}, {I_CHEESE, "СЫР", 15, 350}, {I_JALAP, "ХАЛАПЕНЬО", 15, 300},
  {S_GARLIC, "ЧЕСНОЧНЫЙ СОУС", 25, 200}, {S_HOT, "ОСТРЫЙ СОУС", 25, 200}, {S_CHEESESAUCE, "СЫРНЫЙ СОУС", 25, 260},
  {200, "КОЛА", 12, 480}, {201, "АЙРАН", 12, 420},
};
static const int NPACK = sizeof(PACKS) / sizeof(PACKS[0]);
static bool packOpen(const Pack &p) {
  if (p.item == 101) return has(U_PITA); if (p.item == 301) return has(U_BEEF); if (p.item >= 200 && p.item < 300) return has(U_DRINKS);
  if (p.item < 100) return ingOpen(p.item); return true;
}
static int haveOf(int item) {
  if (item == 100) return lavash; if (item == 101) return pita; if (item == 300) return chickenLoads; if (item == 301) return beefLoads;
  if (item >= 200) return drinks[item - 200]; return stock[item];
}
static void addStock(int item, int amount) {
  if (item == 100) lavash += amount; else if (item == 101) pita += amount; else if (item == 300) chickenLoads += amount; else if (item == 301) beefLoads += amount;
  else if (item >= 200) drinks[item - 200] += amount; else stock[item] += amount;
}
static bool shopOpen = false;                    // окно заказа продуктов посреди дня

static void saveGame() {
  FILE *f = fopen(pth("shaurma.sav"), "w"); if (!f) return;
  fprintf(f, "%d %d %d %d %d %d %u %.3f %.2f %.2f %.2f %.2f\n", money, day, lavash, pita, chickenLoads, beefLoads, upg, rating, spitMeat[0], spitMeat[1], portions[0], portions[1]);
  for (int i = 0; i < NING; i++) fprintf(f, "%d ", stock[i]); fprintf(f, "%d %d %.1f\n", drinks[0], drinks[1], clean);
  fclose(f); hasSave = true;
}
static bool loadGame() {
  FILE *f = fopen(pth("shaurma.sav"), "r"); if (!f) return false;
  int n = fscanf(f, "%d %d %d %d %d %d %u %f %f %f %f %f", &money, &day, &lavash, &pita, &chickenLoads, &beefLoads, &upg, &rating, &spitMeat[0], &spitMeat[1], &portions[0], &portions[1]);
  bool ok = n == 12;
  for (int i = 0; i < NING; i++) stock[i] = 15; drinks[0] = drinks[1] = 12;                         // старое сохранение: даём стартовый запас
  int m = 0; for (int i = 0; i < NING; i++) m += fscanf(f, "%d", &stock[i]); m += fscanf(f, "%d %d", &drinks[0], &drinks[1]); clean = 100; m += fscanf(f, "%f", &clean); (void)m;
  fclose(f); spitCook[0] = spitCook[1] = 1;
  return ok;
}
static void newGame() {
  money = 2000; day = 1; lavash = 20; pita = 0; chickenLoads = 1; beefLoads = 0; upg = 0; rating = 3.5f; ratN = 0;
  spitMeat[0] = 20; spitMeat[1] = 0; spitCook[0] = 1; spitCook[1] = 0; portions[0] = portions[1] = 0;
  for (int i = 0; i < NING; i++) stock[i] = (i >= S_GARLIC) ? 25 : 15; drinks[0] = drinks[1] = 12; deliveries.clear();
}
static void startDay() {
  custs.clear(); wr = Wrap(); wr.base = -1; wr.drink = -1; wrapLoc = 0; acts.clear(); hands.clear(); session = ST_NONE; shopOpen = false; cook.x = BOARD_X; cook.z = STAND_Z; cook.carry = -1; cook.phase = 0;
  for (size_t i = 0; i < deliveries.size(); i++) addStock(deliveries[i].item, deliveries[i].amount); deliveries.clear();   // за ночь всё привезли
  resetPlan(); dayT = 0; spawnT = 2; dayServed = dayLost = dayRev = dayTips = dayWrong = 0; gs = GS_DAY; paused = false;
  shopSt = SH_CLOSED; shutterK = 0; breakT = 0; energy = 100; tiredSaid = false; goalDone = 0; waitSum = 0; dayCleanBad = 0; dayBonus = 0;
  weather = ((day * 7919 + 13) % 100) < 25 ? 1 : 0;                       // примерно каждый четвёртый день дождь
  dayGoal = imin(25, 8 + day / 2); dayRent = imin(1500, 600 + day * 50);
  char b[96]; snprintf(b, sizeof b, "ДЕНЬ %d (%s)%s. ПОДГОТОВЬСЯ И ОТКРОЙ ЛАРЁК", day, WDAY[(day - 1) % 7], weather ? ", ДОЖДЬ" : ""); say(b);
}
static void addRating(float r) { ratHist[ratN % 20] = r; ratN++; int n = imin(ratN, 20); float s = 0; for (int i = 0; i < n; i++) s += ratHist[i]; rating = clampf(s / n * 0.7f + rating * 0.3f, 1, 5); }
static Order makeOrder() {
  Order o; o.base = (has(U_PITA) && pita > 0 && rand() % 3 == 0) ? B_PITA : B_LAVASH;
  bool beefOk = has(U_BEEF) && (spitMeat[1] >= 1 || beefLoads > 0 || portions[1] >= 1);
  int meat = (beefOk && rand() % 2) ? I_BEEF : I_CHICKEN; o.fill.push_back(meat);
  int tops[] = {I_TOMATO, I_CUCUMBER, I_CABBAGE, I_ONION, I_FRIES, I_CHEESE, I_JALAP}; int nt = 1 + rand() % imin(4, 1 + day / 2);
  for (int k = 0; k < 20 && nt > 0; k++) { int t = tops[rand() % 7]; if (!ingOpen(t) || stock[t] <= 0) continue; bool dup = false; for (size_t q = 0; q < o.fill.size(); q++) if (o.fill[q] == t) dup = true; if (dup) continue; o.fill.push_back(t); nt--; }
  int sauces[] = {S_GARLIC, S_HOT, S_CHEESESAUCE}; for (int k = 0; k < 10; k++) { int s = sauces[rand() % 3]; if (ingOpen(s) && stock[s] > 0) { o.fill.push_back(s); break; } }
  std::sort(o.fill.begin(), o.fill.end());
  o.grill = rand() % 2 == 0;
  o.drink = (has(U_DRINKS) && rand() % 3 == 0) ? rand() % 2 : -1; if (o.drink >= 0 && drinks[o.drink] <= 0) o.drink = -1;
  o.price = o.base == B_PITA ? 160 : 140; for (size_t i = 0; i < o.fill.size(); i++) o.price += ING[o.fill[i]].price; if (o.grill) o.price += 20; if (o.drink >= 0) o.price += 70;
  return o;
}
static void queueSpot(int k, float &x, float &z) { if (k == 0) { x = 0; z = 2.7f; } else { x = 1.3f + k * 1.15f; z = 3.4f; } }
static int queueIndex(size_t ci) { int k = 0; for (size_t i = 0; i < ci; i++) if (custs[i].state < 2) k++; return k; }
static void spawnCust() {
  Cust c; c.o = makeOrder(); c.patMax = c.pat = (85.0f + rand() % 25) * (has(U_TV) ? 1.35f : 1.0f); c.x = 9.0f; c.z = 3.4f; c.walk = 0; c.state = 0; c.mood = 0;
  static const u32 SH[] = {RGB(60, 90, 160), RGB(160, 60, 60), RGB(60, 140, 80), RGB(200, 160, 50), RGB(120, 70, 150), RGB(50, 50, 55), RGB(230, 230, 230)};
  static const u32 SK[] = {RGB(240, 200, 170), RGB(220, 170, 130), RGB(170, 120, 80), RGB(250, 215, 190)};
  c.shirt = SH[rand() % 7]; c.skin = SK[rand() % 4];
  custs.push_back(c);
}
static bool wrapMatches(const Order &o) {
  if (wr.base != o.base || !wr.wrapped || wr.grilled != o.grill || wr.drink != o.drink) return false;
  std::vector<int> a = wr.fill; std::sort(a.begin(), a.end());
  return a == o.fill;
}
static void serve() {
  int ci = -1; for (size_t i = 0; i < custs.size(); i++) if (custs[i].state == 1 && queueIndex(i) == 0) { ci = (int)i; break; }
  if (ci < 0) { say("У ОКНА НИКОГО НЕТ"); return; }
  if (shopSt == SH_BREAK) { say("СЕЙЧАС ПЕРЕРЫВ"); return; }
  if (wr.base < 0 || !wr.wrapped) { say("СНАЧАЛА ЗАВЕРНИ ШАУРМУ"); return; }
  Cust &c = custs[ci];
  clean = fmaxv(0.0f, clean - 4);
  bool dirty = clean < 50; if (dirty) dayCleanBad++;
  waitSum += c.pat / c.patMax;
  if (wrapMatches(c.o)) {
    float k = c.pat / c.patMax; int tip = (int)(c.o.price * 0.3f * k * (weather ? 1.3f : 1.0f) * (dirty ? 0.4f : 1.0f));
    goalDone++;
    money += c.o.price + tip; dayRev += c.o.price + tip; dayTips += tip; dayServed++;
    addRating((k > 0.3f ? 5.0f : 4.0f) - (dirty ? 1.5f : 0.0f)); c.mood = 1;
    if (dirty && rand() % 2) { say("КЛИЕНТ: ГРЯЗНОВАТО У ВАС..."); custs[ci].state = 2; wr = Wrap(); wr.base = -1; wr.drink = -1; return; }
    char b[64]; snprintf(b, sizeof b, tip > 0 ? "СПАСИБО! +%d Р (ЧАЕВЫЕ %d)" : "СПАСИБО! +%d Р", c.o.price + tip, tip); say(b);
  } else {
    int pay = c.o.price / 2; money += pay; dayRev += pay; dayServed++; dayWrong++; addRating(2.0f); c.mood = -1;
    char b[64]; snprintf(b, sizeof b, "НЕ ТО! ЗАПЛАТИЛ ПОЛОВИНУ: +%d Р", pay); say(b);
  }
  c.state = 2; wr = Wrap(); wr.base = -1; wr.drink = -1;
}
static void stepDay(float dt) {
  if (gs != GS_DAY || paused || shopOpen) return;
  dayT += dt / DAY_LEN;
  float hr = hourNow();
  // ларёк: рольставня, перерыв, закрытие
  float shT = shopSt == SH_OPEN || shopSt == SH_CLOSING ? 1.0f : (shopSt == SH_BREAK ? 0.35f : 0.0f);
  shutterK += (shT - shutterK) * fminv(1.0f, dt * 2.5f);
  if (shopSt == SH_BREAK) { breakT -= dt; energy = fminv(100.0f, energy + dt * 7.0f); if (breakT <= 0) { shopSt = SH_OPEN; say("ПЕРЕРЫВ ОКОНЧЕН, ОТКРЫВАЕМ ОКНО!"); } }
  else if (acts.empty()) energy = fminv(100.0f, energy + dt * 0.25f);
  if (shopSt == SH_OPEN && hr >= 22.0f) { shopSt = SH_CLOSING; say("22:00 - НОВЫХ КЛИЕНТОВ НЕ БУДЕТ, ДООБСЛУЖИ ОЧЕРЕДЬ"); }
  // вертела: мясо жарится после загрузки, автонарезка
  for (int s = 0; s < 2; s++) {
    if (spitMeat[s] > 0 && spitCook[s] < 1) spitCook[s] = fminv(1.0f, spitCook[s] + dt / 10.0f);
    if (cutT[s] > 0) { cutT[s] -= dt; if (cutT[s] <= 0) { portions[s] += 1; spitMeat[s] -= 1; } }
    if (has(U_AUTOCUT) && cutT[s] <= 0 && portions[s] < 2 && spitMeat[s] >= 1 && spitCook[s] >= 1) cutT[s] = 1.2f;
  }
  if (wr.grillT > 0) { wr.grillT -= dt; if (wr.grillT <= 0) { wr.grilled = true; say("ПОДЖАРЕНО!"); } }
  cookUpdate(dt); fridgeUpdate(dt);
  // новые клиенты
  if (shopSt == SH_OPEN && hr < 22.0f) {                     // клиенты идут, только когда открыто
    spawnT -= dt;
    int inq = 0; for (size_t i = 0; i < custs.size(); i++) if (custs[i].state < 2) inq++;
    float mul = rushMul(hr) * ((day - 1) % 7 >= 5 ? 1.3f : 1.0f) * (weather ? 0.7f : 1.0f) * (has(U_NEON) ? 1.3f : 1.0f);
    if (spawnT <= 0 && inq < 4) { spawnCust(); spawnT = (7.0f + rand() % 6) * 3.5f / fmaxv(1.5f, rating) / mul; }
  }
  // движение и терпение
  for (size_t i = 0; i < custs.size(); i++) {
    Cust &c = custs[i];
    if (c.state < 2) { int k = queueIndex(i); queueSpot(k, c.tx, c.tz); }
    else { c.tx = -10.0f; c.tz = 3.6f; }
    float dx = c.tx - c.x, dz = c.tz - c.z, d = sqrtf(dx * dx + dz * dz);
    if (d > 0.05f) { float sp = 2.2f * dt; c.x += dx / d * fminv(sp, d); c.z += dz / d * fminv(sp, d); c.walk += dt * 8; }
    else if (c.state == 0) c.state = 1;
    if (c.state < 2) {
      bool atWin = queueIndex(i) == 0 && c.state == 1;
      c.pat -= dt * (atWin && shopSt != SH_BREAK ? 1.0f : 0.35f);
      if (hr >= 23.0f && c.pat > 0) c.pat = 0;                    // ночь: все расходятся
      if (c.pat <= 0) { c.state = 2; c.mood = -1; dayLost++; addRating(1.0f); say("КЛИЕНТ УШЁЛ, НЕ ДОЖДАВШИСЬ!"); }
    }
  }
  for (size_t i = 0; i < custs.size();) { if (custs[i].state == 2 && custs[i].x < -9.5f) custs.erase(custs.begin() + i); else i++; }
  bool queueEmpty = true; for (size_t i = 0; i < custs.size(); i++) if (custs[i].state < 2) queueEmpty = false;
  bool over = hr >= 23.0f || (shopSt == SH_CLOSING && queueEmpty);
  if (over && custs.empty()) {                              // день закончен: аренда, премия за цель
    shopSt = SH_CLOSED; dayBonus = goalDone >= dayGoal ? 300 + 80 * day : 0;
    money += dayBonus - dayRent; gs = GS_SUMMARY; day++; saveGame();
  }
}
// ---------- действия игрока ----------
static void addFill(int i) {
  if (wr.base < 0) { say("СНАЧАЛА ЛАВАШ ИЛИ ПИТА"); return; }
  if (wr.wrapped) { say("УЖЕ ЗАВЁРНУТО"); return; }
  for (size_t q = 0; q < wr.fill.size(); q++) if (wr.fill[q] == i) { say("ЭТО УЖЕ ПОЛОЖИЛИ"); return; }
  if (i == I_CHICKEN || i == I_BEEF) { int s = i == I_CHICKEN ? 0 : 1; if (portions[s] < 1) { say("СНАЧАЛА СРЕЖЬ МЯСО С ВЕРТЕЛА"); return; } portions[s] -= 1; }
  wr.fill.push_back(i);
}
static void setBase(int b) {
  if (wr.base >= 0) { say("ОСНОВА УЖЕ ЕСТЬ"); return; }
  if (b == B_LAVASH) { if (lavash <= 0) { say("ЛАВАШ ЗАКОНЧИЛСЯ! КУПИ В КОНЦЕ ДНЯ"); return; } lavash--; }
  else { if (pita <= 0) { say("ПИТА ЗАКОНЧИЛАСЬ"); return; } pita--; }
  wr.base = b;
}
static void cutMeat(int s) {
  if (cutT[s] > 0) return;
  if (spitMeat[s] < 1) { int &loads = s ? beefLoads : chickenLoads; if (loads <= 0) { say("МЯСО ЗАКОНЧИЛОСЬ! КУПИ В КОНЦЕ ДНЯ"); return; } loads--; spitMeat[s] = 20; spitCook[s] = 0; say("НОВОЕ МЯСО НА ВЕРТЕЛЕ, ЖАРИТСЯ..."); return; }
  if (spitCook[s] < 1) { say("МЯСО ЕЩЁ ЖАРИТСЯ"); return; }
  cutT[s] = 0.7f;
}

// ---------- 3D ----------
static void person(float x, float z, float face, u32 shirt, u32 skin, float walk, float mood) {
  V3 p = v3(x, 0, z); float h = face, cs = cosf(h), sn = sinf(h); V3 ax = v3(cs, 0, -sn), az = v3(sn, 0, cs);
  float w = sinf(walk);
  discOct(x, z, 0.35f, 0.01f, RGB(60, 60, 64));
  drawBox(p + ax * 0.12f + az * (w * 0.1f) + v3(0, 0.4f, 0), v3(0.08f, 0.4f, 0.09f), h, RGB(40, 50, 80), true);
  drawBox(p - ax * 0.12f - az * (w * 0.1f) + v3(0, 0.4f, 0), v3(0.08f, 0.4f, 0.09f), h, RGB(40, 50, 80), true);
  drawBox(p + v3(0, 1.1f, 0), v3(0.26f, 0.32f, 0.15f), h, shirt, true);
  for (int k = -1; k <= 1; k += 2) drawBox(p + ax * (0.33f * k) + v3(0, 1.08f, 0) - az * (w * 0.05f * k), v3(0.07f, 0.3f, 0.07f), h, shirt, true);
  u32 face_ = mood < -0.5f ? mixc(skin, RGB(230, 60, 50), 0.5f) : skin;
  drawBox(p + v3(0, 1.62f, 0), v3(0.15f, 0.17f, 0.15f), h, face_, true);
  drawBox(p + v3(0, 1.8f, 0), v3(0.16f, 0.04f, 0.16f), h, RGB(60, 40, 30), true);
}
static float itemSc = 1.0f;
static void itemBox(int kind, V3 p, float yaw) {          // предмет в руках повара
  if (kind >= 300) { drawBox(p, v3(0.12f, 0.08f, 0.28f) * itemSc, yaw, wr.grilled ? RGB(200, 150, 80) : RGB(240, 220, 170), true); return; }
  if (kind >= 200) { drawBox(p, v3(0.07f, 0.17f, 0.07f) * itemSc, yaw, DRINKC[kind - 200], true); return; }
  if (kind >= 100) { drawBox(p, v3(0.3f, 0.01f, 0.22f) * itemSc, yaw, kind == 100 + B_PITA ? RGB(230, 200, 150) : RGB(240, 220, 170), true); return; }
  if (kind >= S_GARLIC) { drawBox(p, v3(0.06f, 0.16f, 0.06f) * itemSc, yaw, ING[kind].col, true); return; }
  drawBox(p, v3(0.12f, 0.04f, 0.1f) * itemSc, yaw, ING[kind].col, true);
}
static void drawCook() {
  float h = cook.face, cs = cosf(h), sn = sinf(h); V3 ax = v3(cs, 0, -sn), az = v3(sn, 0, cs), p = v3(cook.x, 0, cook.z);
  bool moving = acts.empty() ? (fabsf(cook.x - BOARD_X) > 0.06f || fabsf(cook.z - STAND_Z) > 0.06f) : (cook.phase == 0 || cook.phase == 2);
  float w = moving ? sinf(cook.walk) : 0;
  int at = acts.empty() ? -1 : acts[0].type; bool anim = !acts.empty() && (cook.phase == 1 || cook.phase == 3);
  discOct(p.x, p.z, 0.35f, 0.01f, RGB(110, 100, 90));
  drawBox(p + ax * 0.12f + az * (w * 0.12f) + v3(0, 0.4f, 0), v3(0.08f, 0.4f, 0.09f), h, RGB(40, 40, 50), true);
  drawBox(p - ax * 0.12f - az * (w * 0.12f) + v3(0, 0.4f, 0), v3(0.08f, 0.4f, 0.09f), h, RGB(40, 40, 50), true);
  drawBox(p + v3(0, 1.1f, 0), v3(0.27f, 0.32f, 0.16f), h, RGB(245, 245, 245), true);                // белая куртка
  drawBox(p + v3(0, 0.95f, 0) + az * 0.165f, v3(0.22f, 0.3f, 0.01f), h, RGB(200, 60, 50), true);     // фартук
  drawBox(p + v3(0, 1.62f, 0), v3(0.15f, 0.17f, 0.15f), h, RGB(235, 195, 160), true);
  drawBox(p + v3(0, 1.88f, 0), v3(0.17f, 0.1f, 0.17f), h, RGB(255, 255, 255), true);                 // поварской колпак
  // руки: тянутся вперёд к предмету, режут, крутят шаурму или несут
  float t = cook.animT;
  for (int k = -1; k <= 1; k += 2) {
    V3 sh = p + ax * (0.34f * k) + v3(0, 1.3f, 0), hand;
    if (anim && at == A_CUT && k == 1) hand = sh + az * 0.45f + v3(0, -0.15f + 0.18f * sinf(t * 22), 0);
    else if (anim && at == A_WRAP) hand = sh + az * (0.45f + 0.08f * sinf(t * 9 + k)) + v3(0, -0.3f + 0.06f * cosf(t * 9 + k), 0) - ax * (0.12f * k);
    else if (anim) hand = sh + az * (0.55f) + v3(0, -0.25f, 0) - ax * (0.08f * k);
    else if (cook.carry >= 0) hand = sh + az * 0.38f + v3(0, -0.28f, 0) - ax * (0.12f * k);
    else hand = sh + v3(0, -0.45f, 0) - az * (w * 0.08f * k);
    V3 mid = (sh + hand) * 0.5f, dv = hand - sh; float len = sqrtf(dot(dv, dv));
    float yaw = atan2f(dv.x, dv.z), pitchL = len;
    drawBox(mid, v3(0.065f, 0.065f, pitchL * 0.5f + 0.02f), yaw, RGB(245, 245, 245), true);
    drawBox(hand, v3(0.06f, 0.06f, 0.06f), yaw, RGB(235, 195, 160), true);
    if (anim && at == A_CUT && k == 1) drawBox(hand + az * 0.12f, v3(0.015f, 0.04f, 0.14f), h, RGB(220, 220, 230), false);   // нож
  }
  if (cook.carry >= 0) itemBox(cook.carry, p + az * 0.45f + v3(0, 1.05f, 0), h);
}
static void drawHands() {
  V3 E = camEye, F = camF, R = camR, U = camU; float yaw = atan2f(F.x, F.z), t = cook.animT;
  int at = acts.empty() ? -1 : acts[0].type; bool anim = !acts.empty() && (cook.phase == 1 || cook.phase == 3);
  if (grabT > 0) { anim = true; at = -2; t = 0.3f - grabT; }
  bool holding = !hands.empty() || wrapLoc == 2;
  bool moving = !acts.empty() && (cook.phase == 0 || cook.phase == 2);
  float bob = moving ? sinf(cook.walk) * 0.015f : 0;
  for (int k = -1; k <= 1; k += 2) {
    V3 h = E + F * 0.42f + R * (0.2f * k) + U * (-0.12f + bob * k);       // руки в нижней части видимой картинки (над кнопками)
    if (anim && at == A_CUT && k == 1) h = h + F * 0.12f + U * (0.06f * sinf(t * 22));
    else if (anim && at == A_WRAP) h = h + F * (0.1f + 0.04f * sinf(t * 9 + k)) + U * (0.04f * cosf(t * 9 + k)) - R * (0.07f * k);
    else if (anim && at == A_CLEAN) h = h + F * (0.12f + 0.06f * cosf(t * 7 + k)) + R * (0.08f * sinf(t * 7 + k)) - U * 0.04f;
    else if (anim) h = h + F * (0.16f * sinf(fminv(1.0f, t / 0.3f) * 3.14159f)) + U * 0.03f;
    else if (holding) h = h - R * (0.08f * k) + U * 0.02f;
    drawBox(h - F * 0.15f - U * 0.03f, v3(0.032f, 0.032f, 0.12f), yaw, RGB(245, 245, 245), true);      // рукав
    drawBox(h, v3(0.034f, 0.026f, 0.042f), yaw, RGB(235, 195, 160), true);                             // кисть
    if (anim && at == A_CUT && k == 1) drawBox(h + F * 0.1f, v3(0.012f, 0.03f, 0.1f), yaw, RGB(220, 220, 230), false);   // нож
    if (anim && at == A_CLEAN && k == 1) drawBox(h + F * 0.02f - U * 0.03f, v3(0.05f, 0.012f, 0.06f), yaw, RGB(90, 160, 220), false);   // тряпка
  }
  itemSc = 0.4f;                                           // в руках вещи мелкие, чтобы не закрывали обзор
  if (wrapLoc == 2) itemBox(300, E + F * 0.5f + U * (-0.13f + bob), yaw);
  else for (size_t i = 0; i < hands.size() && i < 6; i++) itemBox(hands[i], E + F * 0.5f + U * (-0.14f + bob + 0.018f * i) + R * (0.025f * ((int)i % 2 ? 1 : -1)), yaw + i * 0.4f);
  itemSc = 1.0f;
}
static void drawKiosk() {
  // улица
  groundQuad(-30, 1.8f, 30, 40, 0, RGB(120, 120, 126));
  groundQuad(-30, 5.0f, 30, 7.5f, 0.01f, RGB(70, 70, 76));
  for (int i = -6; i < 7; i++) groundQuad(i * 3.0f, 6.2f, i * 3.0f + 1.4f, 6.3f, 0.02f, RGB(230, 230, 230));
  for (int i = 0; i < 6; i++) drawBox(v3(-14 + i * 6.0f, 2.5f, 12.0f + (i % 2) * 2), v3(2.4f, 2.5f + (i % 3), 1.5f), 0, i % 2 ? RGB(150, 120, 100) : RGB(120, 130, 150), true);
  // пол киоска, стены, окно
  groundQuad(-4, -3, 4, 1.8f, 0.0f, RGB(170, 160, 140));
  drawBox(v3(-3.6f, 1.6f, 1.75f), v3(1.6f, 1.6f, 0.08f), 0, RGB(230, 120, 40), true);
  drawBox(v3(3.6f, 1.6f, 1.75f), v3(1.6f, 1.6f, 0.08f), 0, RGB(230, 120, 40), true);
  drawBox(v3(0, 2.9f, 1.75f), v3(2.0f, 0.3f, 0.08f), 0, RGB(230, 120, 40), true);
  drawBox(v3(0, 3.25f, 1.6f), v3(4.2f, 0.12f, 0.6f), 0, RGB(200, 60, 40), true);                     // козырёк
  drawBox(v3(0, 3.0f, 1.9f), v3(1.6f, 0.18f, 0.03f), 0, has(U_NEON) && ((int)(gTime * 2) & 1) ? RGB(255, 80, 200) : RGB(250, 220, 90), false);   // вывеска
  groundQuad(-4, -3, 4, 0, 0.003f, RGB(190, 180, 160));
  // холодильники: корпус, полки с продуктами, дверца открывается, когда к ним подходишь
  for (int f = 0; f < 2; f++) {
    if (f == 1 && !has(U_DRINKS)) continue;
    float sg = f ? 1.0f : -1.0f, fx = 3.05f * sg, bx = 3.83f * sg;
    u32 body = f ? RGB(200, 40, 40) : RGB(235, 235, 240), in = RGB(225, 235, 245);
    drawBox(v3(bx, 1.0f, -1.3f), v3(0.03f, 1.0f, 0.5f), 0, body, true);                                    // задняя стенка
    drawBox(v3((fx + bx) * 0.5f, 1.0f, -1.82f), v3(0.4f, 1.0f, 0.03f), 0, body, true);                     // бока
    drawBox(v3((fx + bx) * 0.5f, 1.0f, -0.78f), v3(0.4f, 1.0f, 0.03f), 0, body, true);
    drawBox(v3((fx + bx) * 0.5f, 1.98f, -1.3f), v3(0.4f, 0.03f, 0.5f), 0, body, true);                     // верх и низ
    drawBox(v3((fx + bx) * 0.5f, 0.05f, -1.3f), v3(0.4f, 0.05f, 0.5f), 0, body, true);
    drawBox(v3(bx - 0.04f * sg, 1.0f, -1.3f), v3(0.01f, 0.95f, 0.46f), 0, in, false);                       // светлая стенка внутри
    for (int j = 0; j < 3; j++) drawBox(v3((fx + bx) * 0.5f, 0.55f + j * 0.48f, -1.3f), v3(0.38f, 0.015f, 0.47f), 0, RGB(200, 210, 220), true);   // полки
    if (f == 0) {
      for (int j = 0; j < imin(8, (lavash + 4) / 5); j++) drawBox(v3(-3.45f, 1.53f + j * 0.025f, -1.5f), v3(0.18f, 0.01f, 0.2f), 0, RGB(240, 220, 170), true);   // лаваш
      if (has(U_PITA)) for (int j = 0; j < imin(6, (pita + 4) / 5); j++) drawBox(v3(-3.45f, 1.53f + j * 0.03f, -1.05f), v3(0.15f, 0.012f, 0.15f), 0, RGB(230, 200, 150), true);
      int k = 0;
      for (int i = I_TOMATO; i < S_GARLIC; i++) { if (!ingOpen(i)) continue; float z = -1.66f + (k % 4) * 0.24f, y = 1.08f - (k / 4) * 0.48f; k++;    // контейнеры с овощами: чем больше запас, тем полнее
        drawBox(v3(-3.42f, y, z), v3(0.2f, 0.06f, 0.1f), 0, RGB(235, 235, 240), true); if (stock[i] > 0) drawBox(v3(-3.42f, y + 0.04f + 0.03f * fminv(1.0f, stock[i] / 15.0f), z), v3(0.17f, 0.01f + 0.02f * fminv(1.0f, stock[i] / 15.0f), 0.08f), 0, ING[i].col, false); }
      int q = 0; for (int i = S_GARLIC; i < NING; i++) { if (!ingOpen(i)) continue; if (stock[i] > 0) drawBox(v3(-3.35f, 0.75f, -1.6f + q * 0.22f), v3(0.06f, 0.16f, 0.06f), 0, ING[i].col, true); q++; }   // соусы внизу
    } else for (int j = 0; j < 3; j++) for (int q2 = 0; q2 < 4; q2++) { int d = (j + q2) % 2; if (drinks[d] > (j * 4 + q2) / 2) drawBox(v3(3.4f, 0.73f + j * 0.48f, -1.62f + q2 * 0.21f), v3(0.05f, 0.15f, 0.05f), 0, DRINKC[d], true); }
    // дверца на петле у ближнего края
    float th = fridgeOpen[f] * 1.75f, hz = -0.8f;
    V3 dc = v3(fx + sg * 0.02f - sg * 0.48f * sinf(th), 1.0f, hz - 0.48f * cosf(th));
    drawBox(dc, v3(0.025f, 0.98f, 0.48f), -sg * th, f ? RGB(220, 60, 60) : RGB(245, 245, 250), true);
    drawBox(dc + v3(-sg * 0.03f * cosf(th), 0.1f, 0) + v3(0, 0, 0.36f * cosf(th)) + v3(-sg * 0.36f * sinf(th), 0, 0), v3(0.02f, 0.25f, 0.02f), -sg * th, RGB(150, 150, 160), true);   // ручка
  }
  // рольставня окна: опускается, когда закрыто или перерыв
  { float h = (1.0f - shutterK) * 1.55f;
    if (h > 0.02f) { drawBox(v3(0, 2.62f - h * 0.5f, 1.68f), v3(2.0f, h * 0.5f, 0.03f), 0, RGB(150, 155, 162), true);
      for (float y = 2.6f - 0.12f; y > 2.62f - h; y -= 0.15f) drawBox(v3(0, y, 1.645f), v3(2.0f, 0.008f, 0.005f), 0, RGB(110, 115, 122), false); } }
  // прилавок
  drawBox(v3(0, 0.5f, 0.8f), v3(3.4f, 0.5f, 0.8f), 0, RGB(150, 150, 160), true);
  drawBox(v3(0, 1.02f, 0.8f), v3(3.45f, 0.02f, 0.82f), 0, RGB(205, 205, 212), true);
  // грязь: пятна на прилавке и на полу, чем ниже чистота - тем больше
  { int spots = (int)((100.0f - clean) / 9.0f);
    for (int i = 0; i < spots; i++) { float x = -2.4f + fmodf(i * 1.37f, 4.8f), z = 0.25f + fmodf(i * 0.71f, 1.2f);
      if (i % 2) drawBox(v3(x, 1.045f, z), v3(0.09f + 0.03f * (i % 3), 0.003f, 0.07f), i * 0.7f, RGB(120, 90, 50), false);
      else drawBox(v3(x * 0.8f, 0.012f, -0.6f - fmodf(i * 0.53f, 1.3f)), v3(0.12f, 0.004f, 0.09f), i * 0.9f, RGB(90, 70, 45), false); } }
  // вертела
  for (int s = 0; s < 2; s++) {
    if (s == 1 && !has(U_BEEF)) continue;
    float x = s == 0 ? -2.6f : -1.75f, z = 0.55f;
    drawBox(v3(x, 1.55f, z + 0.42f), v3(0.3f, 0.48f, 0.04f), 0, RGB(255, 120 + (int)(40 * sinf(gTime * 6)), 40), false);   // нагреватель за мясом (со стороны окна)
    drawBox(v3(x, 1.6f, z), v3(0.02f, 0.62f, 0.02f), 0, RGB(180, 180, 190), true);
    float amt = spitMeat[s] / 20.0f; u32 mc = mixc(RGB(230, 200, 180), s ? RGB(140, 70, 40) : RGB(205, 140, 70), spitCook[s]);
    for (int j = 0; j < 7; j++) { float r = (0.12f + 0.2f * amt) * (1.0f - j * 0.07f); if (amt <= 0) break; drawBox(v3(x, 1.08f + j * 0.15f, z), v3(r, 0.075f, r), gTime * 1.5f + j * 0.3f, mixc(mc, RGB(90, 50, 30), (j % 2) * 0.15f), true); }
    if (portions[s] > 0) drawBox(v3(x, 1.08f, z + 0.5f), v3(0.18f, 0.03f + 0.01f * fminv(5, portions[s]), 0.14f), 0, mc, true);
    if (cutT[s] > 0) drawBox(v3(x + 0.3f, 1.4f + 0.2f * sinf(gTime * 30), z), v3(0.02f, 0.18f, 0.06f), 0, RGB(220, 220, 230), false);   // нож
  }
  // гриль-пресс
  bool grilling = wr.grillT > 0;
  drawBox(v3(2.5f, 1.15f, 0.6f), v3(0.45f, 0.12f, 0.35f), 0, RGB(60, 60, 64), true);
  drawBox(v3(2.5f, grilling ? 1.33f : 1.5f, 0.6f), v3(0.45f, 0.05f, 0.35f), grilling ? 0 : 0.0f, RGB(80, 80, 86), true);
  if (grilling) drawBox(v3(2.5f, 1.29f, 0.6f), v3(0.42f, 0.02f, 0.32f), 0, RGB(255, 120, 40), false);
  if (wrapLoc == 1 && !grilling) drawBox(v3(2.5f, 1.35f, 0.6f), v3(0.12f, 0.08f, 0.28f), 0.3f, RGB(200, 150, 80), true);   // дожарилась, лежит на гриле
  // доска и шаурма
  drawBox(v3(0, 1.07f, 0.5f), v3(0.6f, 0.03f, 0.4f), 0, RGB(180, 130, 80), true);
  bool wrapping = !acts.empty() && acts[0].type == A_WRAP && cook.phase == 1;
  if (wr.base >= 0 && wrapping) {                          // закрутка: лаваш сворачивается в рулет
    float pr = clampf(cook.animT / 1.3f, 0, 1), rollX = -0.42f + 0.42f * pr, r = 0.02f + 0.07f * pr;
    if (pr < 0.98f) drawBox(v3((rollX + 0.42f) * 0.5f, 1.11f, 0.5f), v3((0.42f - rollX) * 0.5f, 0.01f, 0.3f), 0, wr.base == B_PITA ? RGB(230, 200, 150) : RGB(240, 220, 170), true);
    if (pr < 0.6f) for (size_t i = 0; i < wr.fill.size(); i++) { float a = i * 1.9f, ix = sinf(a) * 0.18f; if (ix > rollX + 0.05f) drawBox(v3(ix, 1.14f + i * 0.012f, 0.5f + cosf(a) * 0.12f), v3(0.08f, 0.02f, 0.05f), a, ING[wr.fill[i]].col, true); }
    drawBox(v3(rollX, 1.1f + r, 0.5f), v3(r, r, 0.3f), pr * 6.0f, RGB(235, 212, 160), true);
  } else if (wr.base >= 0 && !grilling && wrapLoc == 0) {
    if (!wr.wrapped) {
      drawBox(v3(0, 1.11f, 0.5f), v3(0.42f, 0.01f, 0.3f), 0, wr.base == B_PITA ? RGB(230, 200, 150) : RGB(240, 220, 170), true);
      for (size_t i = 0; i < wr.fill.size(); i++) { float a = i * 1.9f; drawBox(v3(sinf(a) * 0.18f, 1.14f + i * 0.012f, 0.5f + cosf(a) * 0.12f), v3(0.1f, 0.02f, 0.06f), a, ING[wr.fill[i]].col, true); }
    } else {
      u32 c = wr.grilled ? RGB(200, 150, 80) : RGB(240, 220, 170);
      drawBox(v3(0, 1.18f, 0.5f), v3(0.12f, 0.08f, 0.3f), 0.3f, c, true);
      if (wr.grilled) for (int j = -1; j <= 1; j++) drawBox(v3(j * 0.03f, 1.265f, 0.5f + j * 0.1f), v3(0.11f, 0.005f, 0.015f), 0.3f, RGB(110, 70, 40), false);
    }
  }
  if (wr.drink >= 0) drawBox(v3(0.75f, 1.25f, 0.5f), v3(0.07f, 0.17f, 0.07f), 0, DRINKC[wr.drink], true);
  if (!firstPerson) drawCook();
  // клиенты
  for (size_t i = 0; i < custs.size(); i++) { Cust &c = custs[i]; float face = c.state == 2 ? -1.5708f : (fabsf(c.tx - c.x) > 0.1f ? (c.tx < c.x ? -1.5708f : 1.5708f) : 3.14159f); person(c.x, c.z, face, c.shirt, c.skin, c.walk, c.state == 2 ? c.mood : (c.pat / c.patMax < 0.3f ? -1 : 0)); }
}
static bool scr(V3 p, int &x, int &y) { V3 v = toView(p); if (v.z < 0.2f) return false; x = (int)(vcx + v.x * focal / v.z); y = (int)(vcy - v.y * focal / v.z); return true; }

// ---------- интерфейс ----------
static void bubble(const Cust &c) {
  int sx = 0, sy = 0;
  const Order &o = c.o; char lines[16][24]; int n = 0;
  snprintf(lines[n++], 24, "%s", BASEN[o.base]);
  for (size_t i = 0; i < o.fill.size(); i++) snprintf(lines[n++], 24, "%s", ING[o.fill[i]].shortn);
  if (o.grill) snprintf(lines[n++], 24, "ПОДЖАРИТЬ");
  if (o.drink >= 0) snprintf(lines[n++], 24, "+ %s", DRINKN[o.drink]);
  int sc = imax(1, fs), lh = 9 * sc, w = 0; for (int i = 0; i < n; i++) w = imax(w, textW(lines[i], sc)); w += sc * 14;
  int h = n * lh + sc * 8, bx = IW - w - fs * 2, by = fs * 24; (void)sx; (void)sy;
  fillRect(bx, by, w, h, RGB(255, 255, 255)); fillRect(bx, by + h - fs * 3, (int)(w * clampf(c.pat / c.patMax, 0, 1)), fs * 3, c.pat / c.patMax > 0.3f ? RGB(80, 200, 90) : RGB(230, 60, 50));
  for (int i = 0; i < n; i++) {
    bool done = false;                                     // галочка у того, что уже положено
    if (i == 0) done = wr.base == o.base;
    else if (i <= (int)o.fill.size()) { int it = o.fill[i - 1]; for (size_t q = 0; q < wr.fill.size(); q++) if (wr.fill[q] == it) done = true; }
    else if (o.grill && i == (int)o.fill.size() + 1) done = wr.grilled;
    else done = wr.drink == o.drink;
    drawText(bx + sc * 10, by + sc * 4 + i * lh, lines[i], sc, done ? RGB(40, 150, 60) : RGB(40, 40, 50), 0);
    if (done) fillRect(bx + sc * 3, by + sc * 6 + i * lh, sc * 4, sc * 3, RGB(40, 150, 60));
  }
}
static void drawHUD() {
  char b[96];
  fillRectA(0, 0, IW, fs * 13, RGB(20, 16, 14), 200);
  float hrf = fminv(23.0f, hourNow()); int hour = (int)hrf, mn = (int)((hrf - hour) * 60);
  snprintf(b, sizeof b, "%s %02d:%02d", WDAY[(day - 1) % 7], hour, mn); drawTextSh(fs * 3, fs * 3, b, fs + 1, RGB(255, 230, 160), 0);
  int hx = fs * 3 + textW(b, fs + 1) + fs * 10;
  snprintf(b, sizeof b, "%d Р", money); drawTextSh(hx, fs * 3, b, fs + 1, RGB(130, 230, 130), 0); hx += textW(b, fs + 1) + fs * 10;
  snprintf(b, sizeof b, "РЕЙТИНГ %.1f", rating); drawTextSh(hx, fs * 3, b, fs + 1, rating >= 3.5f ? RGB(255, 220, 80) : RGB(255, 120, 90), 0); hx += textW(b, fs + 1) + fs * 10;
  {   // запасы: лаваш, мясо на вертеле + запасные загрузки
    char s2[96]; int n = snprintf(b, sizeof b, "ЛАВАШ %d  КУР %d+%d", lavash, (int)spitMeat[0], chickenLoads);
    if (has(U_BEEF)) { snprintf(s2, sizeof s2, "  ГОВ %d+%d", (int)spitMeat[1], beefLoads); n += snprintf(b + n, sizeof b - n, "%s", s2); }
    if (has(U_PITA)) snprintf(b + n, sizeof b - n, "  ПИТА %d", pita);
    int sc = fs; while (sc > 1 && hx + textW(b, sc) > IW - fs * 32) sc--;
    drawTextSh(hx, fs * 4, b, sc, RGB(220, 220, 230), 0);
  }
  if (button(IW - fs * 30, fs, fs * 28, fs * 11, paused ? "ДАЛЬШЕ" : "ПАУЗА", true, RGB(90, 80, 70))) paused = !paused;
  // вторая строка: силы, чистота, цель дня, погода
  { int y2 = fs * 15, bwid = fs * 30, x2 = fs * 3;
    drawTextSh(x2, y2, "СИЛЫ", fs, RGB(230, 230, 230), 0); x2 += textW("СИЛЫ", fs) + fs * 3;
    fillRect(x2, y2, bwid, fs * 5, RGB(40, 40, 44)); fillRect(x2, y2, (int)(bwid * energy / 100), fs * 5, energy > 40 ? RGB(90, 200, 100) : RGB(230, 90, 60)); x2 += bwid + fs * 8;
    drawTextSh(x2, y2, "ЧИСТОТА", fs, RGB(230, 230, 230), 0); x2 += textW("ЧИСТОТА", fs) + fs * 3;
    fillRect(x2, y2, bwid, fs * 5, RGB(40, 40, 44)); fillRect(x2, y2, (int)(bwid * clean / 100), fs * 5, clean > 50 ? RGB(90, 170, 230) : RGB(230, 140, 50)); x2 += bwid + fs * 8;
    snprintf(b, sizeof b, "ЦЕЛЬ: %d/%d БЕЗ ОШИБОК%s", goalDone, dayGoal, weather ? "   ДОЖДЬ" : ""); drawTextSh(x2, y2, b, fs, goalDone >= dayGoal ? RGB(120, 240, 120) : RGB(255, 230, 160), 0); }
  // управление ларьком
  { int bx = IW - fs * 30 - fs * 46 - fs * 46, bw2 = fs * 44, by2 = fs;
    if (shopSt == SH_CLOSED) { if (button(bx + fs * 46, by2, bw2, fs * 11, "ОТКРЫТЬСЯ", hourNow() < 22.0f, RGB(60, 160, 70))) { shopSt = SH_OPEN; spawnT = 2; say("ЛАРЁК ОТКРЫТ!"); } }
    else if (shopSt == SH_OPEN) {
      if (button(bx, by2, bw2, fs * 11, "ПЕРЕРЫВ", true, RGB(170, 130, 50))) { shopSt = SH_BREAK; breakT = DAY_LEN / 15.0f * 0.5f; say("ПЕРЕРЫВ 30 МИНУТ: ПОВАР ОТДЫХАЕТ"); }
      if (button(bx + fs * 46, by2, bw2, fs * 11, "ЗАКРЫТЬ", true, RGB(160, 60, 50))) { shopSt = SH_CLOSING; say("ЗАКРЫВАЕМСЯ: ДООБСЛУЖИ ОЧЕРЕДЬ"); }
    }
    else if (shopSt == SH_BREAK) { if (button(bx + fs * 46, by2, bw2, fs * 11, "ВЕРНУТЬСЯ", true, RGB(60, 160, 70))) { shopSt = SH_OPEN; say("ОКНО СНОВА ОТКРЫТО"); } }
    else button(bx + fs * 46, by2, bw2, fs * 11, "ЗАКРЫВАЕМСЯ", false, RGB(90, 90, 90)); }
  // пузыри заказов
  for (size_t i = 0; i < custs.size(); i++) if (custs[i].state == 1 && queueIndex(i) == 0) bubble(custs[i]);
  for (size_t i = 0; i < custs.size(); i++) if (custs[i].state < 2 && queueIndex(i) > 0) {      // терпение у очереди
    int sx, sy; if (!scr(v3(custs[i].x, 2.1f, custs[i].z), sx, sy)) continue; int bw = fs * 14;
    fillRect(sx - bw / 2, sy, bw, fs * 2, RGB(40, 20, 20)); fillRect(sx - bw / 2, sy, (int)(bw * clampf(custs[i].pat / custs[i].patMax, 0, 1)), fs * 2, RGB(230, 180, 60));
  }
  if (shopSt == SH_BREAK) {                                // перерыв: сколько осталось
    int mins = (int)ceilf(breakT / (DAY_LEN / 15.0f) * 60.0f); snprintf(b, sizeof b, "ПЕРЕРЫВ: ЕЩЁ %d МИН (ПОВАР ОТДЫХАЕТ)", mins);
    int w = textW(b, fs + 1) + fs * 10; fillRectA(IW / 2 - w / 2, IH / 3, w, fs * 14, RGB(60, 40, 10), 200); drawText(IW / 2, IH / 3 + fs * 4, b, fs + 1, RGB(255, 220, 120), 1);
  } else if (shopSt == SH_CLOSED && hourNow() < 22.0f) {
    const char *t = "ЛАРЁК ЗАКРЫТ: НАРЕЖЬ МЯСО, ПРОВЕРЬ ХОЛОДИЛЬНИК И ЖМИ «ОТКРЫТЬСЯ»"; int sc = fs; while (sc > 1 && textW(t, sc) > IW - fs * 10) sc--;
    int w = textW(t, sc) + fs * 8; fillRectA(IW / 2 - w / 2, IH / 3 + fs * 18, w, fs * 11, RGB(0, 0, 0), 160); drawText(IW / 2, IH / 3 + fs * 21, t, sc, RGB(255, 255, 255), 1);
  }
  // ---- места: холодильник, вертела, напитки; на доске - завернуть, гриль, отдать ----
  int bh = imax(fs * 17, IH / 8), gap = fs, yb = IH - bh - gap;
  std::vector<int> bt; bt.push_back(0); bt.push_back(1); if (has(U_BEEF)) bt.push_back(2); if (has(U_DRINKS)) bt.push_back(3); bt.push_back(4); bt.push_back(5); bt.push_back(7); bt.push_back(6);
  int aw = (IW - fs * 2 - gap * ((int)bt.size() - 1)) / (int)bt.size();
  bool serveQ = false; for (size_t i = 0; i < acts.size(); i++) if (acts[i].type == A_SERVE) serveQ = true;
  for (size_t k = 0; k < bt.size(); k++) {
    int x = fs + (int)k * (aw + gap), id = bt[k];
    if (id == 0) { if (button(x, yb, aw, bh, "ХОЛОДИЛЬНИК", true, session == ST_FRIDGE ? RGB(90, 150, 200) : RGB(70, 110, 150))) goStation(ST_FRIDGE); }
    else if (id == 1) { if (button(x, yb, aw, bh, "ВЕРТЕЛ:\nКУРИЦА", true, session == ST_SPIT0 ? RGB(190, 120, 60) : RGB(140, 90, 50))) goStation(ST_SPIT0); }
    else if (id == 2) { if (button(x, yb, aw, bh, "ВЕРТЕЛ:\nГОВЯДИНА", true, session == ST_SPIT1 ? RGB(170, 90, 60) : RGB(120, 60, 40))) goStation(ST_SPIT1); }
    else if (id == 3) { if (button(x, yb, aw, bh, "НАПИТКИ", true, session == ST_DRINKS ? RGB(140, 90, 170) : RGB(90, 60, 120))) goStation(ST_DRINKS); }
    else if (id == 4) {
      if (!pw.wrapped) { if (button(x, yb, aw, bh, "ЗАВЕРНУТЬ", pw.base >= 0, RGB(90, 110, 150))) qWrap(); }
      else if (button(x, yb, aw, bh, wr.grillT > 0 ? "ЖАРИТСЯ..." : (pw.grilled ? "ПОДЖАРЕНО" : "НА ГРИЛЬ"), !pw.grilled && !serveQ, RGB(90, 110, 150))) qGrill();
    }
    else if (id == 5) { if (button(x, yb, aw, bh, "ОТДАТЬ!", pw.wrapped && !serveQ, RGB(60, 150, 70))) qServe(); }
    else if (id == 7) { if (button(x, yb, aw, bh, "УБОРКА", clean < 99, clean < 50 ? RGB(200, 120, 40) : RGB(60, 110, 140))) qClean(); }
    else if (id == 6) { if (button(x, yb, aw, bh, "ВЫБРОСИТЬ", pw.base >= 0 || pw.drink >= 0 || !acts.empty() || !hands.empty(), RGB(120, 60, 60))) trashWrap(); }
  }
  // ---- панель продуктов: появляется, когда открыл холодильник или стоишь у вертела ----
  if (session != ST_NONE && (acts.empty() || acts[0].type == A_CUT)) {
    std::vector<int> items; char lbl[48]; const char *title = "";
    if (session == ST_FRIDGE) { title = "ХОЛОДИЛЬНИК: НАЖМИ, ЧТО ВЗЯТЬ"; items.push_back(100 + B_LAVASH); if (has(U_PITA)) items.push_back(100 + B_PITA); for (int i = I_TOMATO; i < NING; i++) if (ingOpen(i)) items.push_back(i); }
    else if (session == ST_DRINKS) { title = "НАПИТКИ"; items.push_back(200); items.push_back(201); }
    else title = session == ST_SPIT0 ? "ВЕРТЕЛ С КУРИЦЕЙ" : "ВЕРТЕЛ С ГОВЯДИНОЙ";
    int cols = session == ST_FRIDGE ? 4 : 2, rows = session == ST_FRIDGE ? ((int)items.size() + cols - 1) / cols : 1;
    int pw2 = imin(IW - fs * 8, fs * 260), cw = (pw2 - fs * 4 - gap * (cols - 1)) / cols, ch = imax(fs * 12, imin(fs * 16, (yb - fs * 48) / (rows + 1) - gap));
    int ph = fs * 12 + rows * (ch + gap) + ch + fs * 4, px = IW / 2 - pw2 / 2, py = yb - ph - fs * 2;
    fillRectA(px, py, pw2, ph, RGB(20, 26, 34), 215);
    drawText(px + pw2 / 2, py + fs * 3, title, fs, RGB(255, 240, 200), 1);
    int y0 = py + fs * 12;
    if (session == ST_SPIT0 || session == ST_SPIT1) {
      int sp = session == ST_SPIT1;
      snprintf(lbl, sizeof lbl, spitMeat[sp] >= 1 ? (spitCook[sp] < 1 ? "ЖАРИТСЯ..." : "СРЕЗАТЬ") : "НОВОЕ МЯСО (%d)", sp ? beefLoads : chickenLoads);
      if (button(px + fs * 2, y0, cw, ch, lbl, true, RGB(150, 90, 50))) qCut(sp);
      snprintf(lbl, sizeof lbl, "ВЗЯТЬ ПОРЦИЮ (%d)", (int)portions[sp]);
      if (button(px + fs * 2 + cw + gap, y0, cw, ch, lbl, portions[sp] >= 1, RGB(170, 110, 60))) takeItem(sp ? I_BEEF : I_CHICKEN);
    } else for (size_t k = 0; k < items.size(); k++) {
      int c = items[k], cx = px + fs * 2 + ((int)k % cols) * (cw + gap), cy = y0 + ((int)k / cols) * (ch + gap), have; const char *nm; u32 col;
      if (c >= 200) { nm = DRINKN[c - 200]; have = drinks[c - 200]; col = RGB(90, 60, 120); }
      else if (c >= 100) { nm = BASEN[c - 100]; have = c == 100 ? lavash : pita; col = RGB(170, 130, 70); }
      else { nm = ING[c].shortn; have = stock[c]; col = c >= S_GARLIC ? RGB(150, 110, 60) : RGB(70, 120, 70); }
      snprintf(lbl, sizeof lbl, "%s (%d)", nm, have);
      if (button(cx, cy, cw, ch, lbl, have > 0, col)) takeItem(c);
    }
    int ty2 = py + ph - ch - fs * 2;
    snprintf(lbl, sizeof lbl, hands.empty() ? "ЗАКРЫТЬ" : "НЕСТИ НА ДОСКУ (%d)", (int)hands.size());
    if (button(px + pw2 / 2 - pw2 / 4, ty2, pw2 / 2, ch, lbl, true, RGB(60, 140, 80))) qToBoard();
  }
  if (!hands.empty() && session == ST_NONE && acts.empty()) qToBoard();
  if (!acts.empty()) { char qb[64]; snprintf(qb, sizeof qb, "ПОВАР: %s", actName(acts[0].type)); if (acts.size() > 1) snprintf(qb + strlen(qb), sizeof qb - strlen(qb), " (ЕЩЁ %d)", (int)acts.size() - 1); drawTextSh(fs * 3, fs * 24, qb, fs, RGB(255, 240, 200), 0); }
  if (!deliveries.empty()) { char db[64]; snprintf(db, sizeof db, "ДОСТАВКА ЧЕРЕЗ %d С", (int)ceilf(deliveries[0].t)); drawTextSh(fs * 3, fs * 33, db, fs, RGB(140, 220, 255), 0); }
  // ---- заказ продуктов посреди дня (с доставкой) ----
  if (button(IW - fs * 30, fs * 13, fs * 28, fs * 9, "ПРОДУКТЫ", true, RGB(60, 110, 150))) shopOpen = !shopOpen;
  if (shopOpen) {
    fillRectA(0, 0, IW, IH, RGB(0, 0, 0), 150);
    int pw3 = imin(IW - fs * 6, fs * 300), px3 = IW / 2 - pw3 / 2, py3 = fs * 16, n = 0; for (int i = 0; i < NPACK; i++) if (packOpen(PACKS[i])) n++;
    int rows3 = (n + 2) / 3, ch3 = imax(fs * 13, (IH - py3 - fs * 40) / rows3 - gap), cw3 = (pw3 - gap * 2) / 3, k = 0;
    fillRectA(px3 - fs * 2, py3 - fs * 4, pw3 + fs * 4, rows3 * (ch3 + gap) + fs * 34, RGB(20, 26, 34), 235);
    char hb[96]; snprintf(hb, sizeof hb, "ЗАКАЗ ПРОДУКТОВ  (%d Р, ДОСТАВКА 20 С)", money); drawText(IW / 2, py3, hb, fs, RGB(255, 230, 160), 1);
    for (int i = 0; i < NPACK; i++) { const Pack &pk = PACKS[i]; if (!packOpen(pk)) continue;
      char lb[64]; snprintf(lb, sizeof lb, "%s +%d (%d)\n%d Р", pk.name, pk.amount, haveOf(pk.item), pk.cost);
      if (button(px3 + (k % 3) * (cw3 + gap), py3 + fs * 10 + (k / 3) * (ch3 + gap), cw3, ch3, lb, money >= pk.cost, haveOf(pk.item) < pk.amount / 3 ? RGB(150, 80, 60) : RGB(80, 100, 120))) {
        money -= pk.cost; Delivery d = {pk.item, pk.amount, 20.0f}; deliveries.push_back(d); say("ЗАКАЗАНО! КУРЬЕР ПРИЕДЕТ ЧЕРЕЗ 20 С"); }
      k++; }
    if (button(IW / 2 - fs * 40, py3 + fs * 12 + rows3 * (ch3 + gap), fs * 80, fs * 14, "ВЕРНУТЬСЯ К РАБОТЕ", true, RGB(60, 140, 80))) shopOpen = false;
  }
  if (msgT > 0) { int w = textW(msg, fs + 1) + fs * 8; fillRectA(IW / 2 - w / 2, fs * 16, w, fs * 13, RGB(0, 0, 0), 170); drawText(IW / 2, fs * 19, msg, fs + 1, RGB(255, 255, 255), 1); }
}
static void frameDay(float dt) {
  stepDay(dt); if (msgT > 0) msgT -= dt;
  setClipRect(0, 0, IW, IH);
  // от первого лица: глаза повара, поворот головы плавный
  firstPerson = true;
  bool serving = !acts.empty() && acts[0].type == A_SERVE && cook.phase >= 2;
  float ty = cook.face, tp = cook.z < -0.9f ? -0.15f : (serving ? -0.08f : (acts.empty() ? -0.2f : -0.42f));
  if (!acts.empty() && acts[0].type == A_CLEAN && cook.phase == 1) tp = -0.65f;   // у окна и без дела смотрим на клиентов
  float dy_ = ty - camYawS; while (dy_ > 3.14159f) dy_ -= 6.28318f; while (dy_ < -3.14159f) dy_ += 6.28318f;
  camYawS += dy_ * fminv(1.0f, dt * 7.0f); camPitchS += (tp - camPitchS) * fminv(1.0f, dt * 5.0f);
  bool mv = !acts.empty() && (cook.phase == 0 || cook.phase == 2);
  V3 eye = v3(cook.x - sinf(camYawS) * 0.12f, 1.68f + (mv ? fabsf(sinf(cook.walk)) * 0.035f : 0), cook.z - cosf(camYawS) * 0.12f);
  setCamera(eye, eye + v3(sinf(camYawS) * cosf(camPitchS), sinf(camPitchS), cosf(camYawS) * cosf(camPitchS)));
  focal = IW * 0.5f; vcx = IW * 0.5f; vcy = IH * 0.31f;
  float hr = hourNow(), light = hr < 19 ? 1.0f : 1.0f - (hr - 19) * 0.15f;
  if (weather) light *= 0.8f;
  fogCol = mixc(weather ? RGB(130, 140, 155) : RGB(160, 190, 220), RGB(40, 40, 70), 1 - clampf(light, 0.4f, 1)); fogA = 25; fogB = weather ? 45 : 60; gLight = clampf(light, 0.55f, 1.0f); gNight = 0;
  LDIR = vnorm(v3(0.3f, 0.9f, -0.4f));
  fillSky(); clearDepth(); drawKiosk(); drawHands();
  if (weather) for (int i = 0; i < 90; i++) {                // дождь за окном
    int x = (int)((i * 97 + (int)(gTime * 900) * (1 + i % 3)) % IW), y = (int)((i * 53 + gTime * (500 + i % 5 * 60)) ) % imax(1, IH * 2 / 3);
    fillRectA(x, y, imax(1, fs / 2), fs * 6, RGB(200, 210, 230), 90); }
  // табличка на рольставне
  if (shutterK < 0.9f) { int sx, sy; float sgY = 2.62f - (1.0f - shutterK) * 1.55f + 0.28f; if (scr(v3(0, sgY, 1.62f), sx, sy) && sy > fs * 30) { const char *t = shopSt == SH_BREAK ? "ПЕРЕРЫВ" : "ЗАКРЫТО"; int w = textW(t, fs + 1) + fs * 8; fillRect(sx - w / 2, sy - fs * 6, w, fs * 13, RGB(250, 245, 230)); drawText(sx, sy - fs * 3, t, fs + 1, RGB(200, 40, 40), 1); } }
  drawHUD();
}
static void frameSummary() {
  fillRect(0, 0, IW, IH, RGB(40, 28, 22));
  char b[96]; int y = IH / 10;
  snprintf(b, sizeof b, "ДЕНЬ %d ЗАКОНЧЕН", day - 1); drawTextSh(IW / 2, y, b, fs * 2, RGB(255, 210, 120), 1); y += fs * 24;
  snprintf(b, sizeof b, "ОБСЛУЖЕНО: %d (НЕ ТО: %d)   УШЛИ: %d", dayServed, dayWrong, dayLost); drawText(IW / 2, y, b, fs + 1, RGB(230, 230, 230), 1); y += fs * 14;
  snprintf(b, sizeof b, "ВЫРУЧКА: %d Р (ЧАЕВЫЕ %d Р)", dayRev, dayTips); drawText(IW / 2, y, b, fs + 1, RGB(130, 230, 130), 1); y += fs * 14;
  snprintf(b, sizeof b, "АРЕНДА: -%d Р   %s", dayRent, dayBonus ? "" : ""); drawText(IW / 2, y, b, fs + 1, RGB(255, 140, 120), 1); y += fs * 12;
  if (dayBonus) snprintf(b, sizeof b, "ЦЕЛЬ ДНЯ ВЫПОЛНЕНА (%d/%d): ПРЕМИЯ +%d Р", goalDone, dayGoal, dayBonus); else snprintf(b, sizeof b, "ЦЕЛЬ ДНЯ НЕ ВЫПОЛНЕНА (%d/%d)", goalDone, dayGoal);
  drawText(IW / 2, y, b, fs + 1, dayBonus ? RGB(130, 240, 130) : RGB(200, 200, 200), 1); y += fs * 12;
  snprintf(b, sizeof b, "РЕЙТИНГ: %.1f   ДЕНЬГИ: %d Р", rating, money); drawText(IW / 2, y, b, fs + 1, RGB(255, 220, 80), 1); y += fs * 14;
  {   // отзывы покупателей
    const char *rv[4]; int nr = 0;
    if (dayServed > 0 && waitSum / dayServed > 0.6f) rv[nr++] = "«ОЧЕНЬ БЫСТРО, СПАСИБО!»";
    if (dayBonus) rv[nr++] = "«ЛУЧШАЯ ШАУРМА В РАЙОНЕ!»";
    if (dayLost > 2) rv[nr++] = "«ДОЛГО ЖДАЛ И УШЁЛ ГОЛОДНЫМ»";
    if (dayWrong > 0 && nr < 4) rv[nr++] = "«ПЕРЕПУТАЛИ МОЙ ЗАКАЗ»";
    if (dayCleanBad > 0 && nr < 4) rv[nr++] = "«НА ПРИЛАВКЕ ГРЯЗНО»";
    if (!nr && dayServed > 0) rv[nr++] = "«НОРМАЛЬНО, ПРИДУ ЕЩЁ»";
    for (int i = 0; i < nr; i++) { drawText(IW / 2, y, rv[i], fs, RGB(220, 220, 255), 1); y += fs * 10; }
  }
  { int lowN = 0; std::string low; for (int i = 0; i < NPACK; i++) { const Pack &pk = PACKS[i]; if (!packOpen(pk) || pk.item >= 300) continue; if (haveOf(pk.item) < pk.amount / 2) { if (lowN < 4) { if (lowN) low += ", "; low += pk.name; } lowN++; } }
    if (lavash < 10) { low = "ЛАВАШ, " + low; lowN++; }
    if (lowN) { snprintf(b, sizeof b, "ЗАКОНЧИВАЕТСЯ: %s%s", low.c_str(), lowN > 4 ? "..." : ""); drawText(IW / 2, y, b, fs, RGB(255, 110, 90), 1); } }
  y += fs * 12;
  if (button(IW / 2 - IW / 5, y, IW * 2 / 5, IH / 7, "ЗАКУПКА И УЛУЧШЕНИЯ", true, RGB(70, 120, 160))) gs = GS_SHOP;
}
static void buyBonus(int i) {     // к новой покупке - стартовый запас, чтобы сразу можно было работать
  if (i == U_BEEF) { spitMeat[1] = 20; spitCook[1] = 1; }
  if (i == U_PITA) pita += 20;
}
static void frameShop() {
  fillRect(0, 0, IW, IH, RGB(30, 34, 40));
  char b[96]; drawTextSh(IW / 2, fs * 3, "ЗАКУПКА И УЛУЧШЕНИЯ", fs + 1, RGB(255, 220, 120), 1);
  snprintf(b, sizeof b, "ДЕНЬГИ: %d Р", money); drawText(IW / 2, fs * 15, b, fs + 1, RGB(130, 230, 130), 1);
  int colW = IW / 2 - fs * 4, x0 = fs * 3, y = fs * 26, bh = imax(fs * 14, IH / 11);
  drawText(x0, y, "ПРОДУКТЫ (СРАЗУ В ХОЛОДИЛЬНИК)", fs, RGB(200, 200, 210), 0); y += fs * 10;
  { int n = 0; for (int i = 0; i < NPACK; i++) if (packOpen(PACKS[i])) n++;
    int ph = imax(fs * 11, (IH - y - IH / 6) / ((n + 1) / 2) - fs), pw2 = (colW - fs) / 2, k = 0;
    for (int i = 0; i < NPACK; i++) { const Pack &pk = PACKS[i]; if (!packOpen(pk)) continue;
      snprintf(b, sizeof b, "%s +%d (%d)\n%d Р", pk.name, pk.amount, haveOf(pk.item), pk.cost);
      if (button(x0 + (k % 2) * (pw2 + fs), y + (k / 2) * (ph + fs), pw2, ph, b, money >= pk.cost, haveOf(pk.item) < pk.amount / 2 ? RGB(150, 80, 60) : RGB(90, 110, 80))) { money -= pk.cost; addStock(pk.item, pk.amount); }
      k++; } }
  int x1 = IW / 2 + fs, yy = fs * 26, uh = imax(fs * 12, (IH - yy - IH / 6) / 12 - fs);
  drawText(x1, yy, "УЛУЧШЕНИЯ", fs, RGB(200, 200, 210), 0); yy += fs * 10;
  for (int i = 0; i < NUPG; i++) {
    if (has(i)) snprintf(b, sizeof b, "%s - ЕСТЬ", UPG[i].name); else snprintf(b, sizeof b, "%s - %d Р", UPG[i].name, UPG[i].cost);
    if (button(x1, yy, colW, uh, b, !has(i) && money >= UPG[i].cost, has(i) ? RGB(60, 90, 60) : RGB(80, 90, 120))) { money -= UPG[i].cost; upg |= 1u << i; buyBonus(i); char m2[96]; snprintf(m2, sizeof m2, "КУПЛЕНО: %s - %s", UPG[i].name, UPG[i].desc); say(m2); }
    yy += uh + fs / 2;
  }
  if (button(IW / 2 - IW / 6, IH - IH / 7 - fs * 2, IW / 3, IH / 7, "ОТКРЫТЬ ЛАРЁК", true, RGB(60, 150, 70))) { saveGame(); startDay(); }
  if (msgT > 0) { msgT -= 0.016f; int w = textW(msg, fs) + fs * 8; fillRectA(IW / 2 - w / 2, fs * 20, w, fs * 11, RGB(0, 0, 0), 190); drawText(IW / 2, fs * 22, msg, fs, RGB(255, 255, 255), 1); }
}
static void frameTitle() {
  firstPerson = false;
  // фон: ларёк в 3D
  setClipRect(0, 0, IW, IH); setCamera(v3(sinf(gTime * 0.2f) * 2.0f, 2.6f, -2.5f), v3(0, 1.3f, 2.0f));
  focal = IW * 0.6f; vcx = IW * 0.5f; vcy = IH * 0.45f; fogCol = RGB(160, 190, 220); fogA = 25; fogB = 60; gLight = 1; gNight = 0; LDIR = vnorm(v3(0.3f, 0.9f, -0.4f));
  fillSky(); clearDepth(); drawKiosk();
  fillRectA(0, IH / 12, IW, IH / 4, RGB(0, 0, 0), 140);
  drawTextSh(IW / 2, IH / 10, "ШАУРМИЧНАЯ", fs * 3, RGB(255, 200, 90), 1);
  drawText(IW / 2, IH / 10 + fs * 26, "СОБИРАЙ ЗАКАЗЫ, ЖАРЬ МЯСО, РАЗВИВАЙ ЛАРЁК", fs, RGB(240, 240, 240), 1);
  int bw = IW / 3, bh = IH / 7, y = IH / 2;
  if (hasSave) { char b[64]; snprintf(b, sizeof b, "ПРОДОЛЖИТЬ (ДЕНЬ %d)", day); if (button(IW / 2 - bw / 2, y, bw, bh, b, true, RGB(60, 150, 70))) startDay(); y += bh + fs * 3; }
  if (button(IW / 2 - bw / 2, y, bw, bh, hasSave ? "НОВАЯ ИГРА" : "НАЧАТЬ", true, hasSave ? RGB(120, 80, 60) : RGB(60, 150, 70))) { newGame(); saveGame(); startDay(); }
}

int main(int argc, char **argv) {
  (void)argc; (void)argv;
  if (!gfxInit()) return 1;
#if defined(__ANDROID__) && defined(SDL_MAJOR_VERSION) && SDL_MAJOR_VERSION >= 2
  { const char *ip = SDL_AndroidGetInternalStoragePath(); if (ip && *ip) snprintf(gBase, sizeof gBase, "%s/", ip); }
#endif
  rotMode = SH > SW;
  LW = rotMode ? SH : SW; LH = rotMode ? SW : SH;
  computeSize();
  if (!allocBuffers()) return 1;
  srand((unsigned)time(NULL));
  hasSave = loadGame();
  memset(&P, 0, sizeof P);
  bool running = true;
  Uint32 last = SDL_GetTicks();
  while (running) {
    Uint32 now = SDL_GetTicks();
    float dt = clampf((now - last) / 1000.0f, 0.001f, 0.05f); last = now; gTime += dt;
    if (gfxCheckResize()) { rotMode = SH > SW; LW = rotMode ? SH : SW; LH = rotMode ? SW : SH; computeSize(); if (!allocBuffers()) break; }
    P.pressed = false; P.clicked = false; P.dx = P.dy = 0;
    Ev e;
    while (pollEv(e)) {
      if (e.type == 0) running = false;
      else if (e.type == 5) { if (gs != GS_TITLE) saveGame(); }
      else if (e.type == 4) {   // кнопка «назад»
        if (gs == GS_TITLE) running = false;
        else if (gs == GS_DAY && !paused) paused = true;
        else { if (gs != GS_DAY) saveGame(); gs = GS_TITLE; }
      } else {
        int mx, my; mapPt(e.x, e.y, mx, my);
        if (e.type == 1) { P.down = true; P.pressed = true; P.x = P.sx = mx; P.y = P.sy = my; }
        else if (e.type == 3) { P.x = mx; P.y = my; }
        else if (e.type == 2) { P.x = mx; P.y = my; P.cx = mx; P.cy = my; P.down = false; P.clicked = true; }
      }
    }
    if (gs == GS_TITLE) frameTitle();
    else if (gs == GS_DAY) frameDay(dt);
    else if (gs == GS_SUMMARY) frameSummary();
    else frameShop();
    gfxPresent();
    Uint32 spent = SDL_GetTicks() - now;
    if (spent < 16) SDL_Delay(16 - spent);
  }
  if (gs != GS_TITLE) saveGame();
  gfxQuit();
  return 0;
}
