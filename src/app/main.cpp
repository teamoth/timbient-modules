// Timbient Modules (native): the window (SDL3) and the sound device (miniaudio).
// Sound is produced on miniaudio's own real-time thread; the window, drawing and input run here,
// on the main thread, and can never make the audio wait.
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "../engine/engine.h"
#include "../ui/canvas.h"
#include "../ui/ui.h"
#include <cstdio>

using namespace tb;

static Engine* gEngine = nullptr;

// the sound card asks for more audio: render it (non-interleaved) and interleave into its buffer
static void audioCallback(ma_device* dev, void* output, const void*, ma_uint32 frames) {
  float* out = (float*)output; static float L[4096], R[4096];
  ma_uint32 done = 0;
  while (done < frames) {
    const ma_uint32 n = std::min<ma_uint32>(4096, frames - done);
    gEngine->render(L, R, (int)n);
    for (ma_uint32 i = 0; i < n; i++) { out[(done + i) * 2] = L[i]; out[(done + i) * 2 + 1] = R[i]; }
    done += n;
  }
}

int main(int argc, char** argv) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) { std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError()); return 1; }

  // ---- sound first, so the engine knows the sample rate before any module is built ----
  static Engine engine; gEngine = &engine;
  ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
  cfg.playback.format = ma_format_f32; cfg.playback.channels = 2; cfg.sampleRate = 0;   // the device's own rate
  cfg.dataCallback = audioCallback; cfg.periodSizeInFrames = 512; cfg.periods = 3;          // a comfortable buffer: steady on older machines
  cfg.performanceProfile = ma_performance_profile_conservative;
  ma_device device;
  if (ma_device_init(nullptr, &cfg, &device) != MA_SUCCESS) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Timbient Modules", "Could not open the sound device.", nullptr); return 1; }
  engine.setSampleRate((float)device.sampleRate);

  Ui ui(engine);
  ui.demoPatch();
  ma_device_start(&device);

  // ---- the window ----
  SDL_Window* win = SDL_CreateWindow("Timbient Modules", 1280, 800, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  SDL_Renderer* ren = SDL_CreateRenderer(win, nullptr);
  SDL_SetRenderVSync(ren, 1);
  Canvas canvas; SDL_Texture* tex = nullptr; int texW = 0, texH = 0, scale = 3;

  bool running = true; Uint64 last = SDL_GetTicksNS();
  while (running) {
    SDL_Event ev;
    // pixel size of the window and how big each canvas pixel is
    int pw = 0, ph = 0; SDL_GetCurrentRenderOutputSize(ren, &pw, &ph);
    float dens = SDL_GetWindowPixelDensity(win); if (dens <= 0) dens = 1;
    scale = std::max(2, (int)std::lround(std::min(pw, ph) / dens / 250.f * dens));
    const int W = std::max(64, pw / scale), H = std::max(48, ph / scale);
    while (SDL_PollEvent(&ev)) {
      switch (ev.type) {
        case SDL_EVENT_QUIT: running = false; break;
        case SDL_EVENT_KEY_DOWN:
          if (ev.key.key == SDLK_F11 || (ev.key.key == SDLK_F && (ev.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)))) ui.wantFullscreen = !ui.isFullscreen;
          if (ev.key.key == SDLK_ESCAPE && ui.isFullscreen) ui.wantFullscreen = false;
          break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: if (ev.button.button == SDL_BUTTON_LEFT) ui.pointerDown(ev.button.x * dens / scale, ev.button.y * dens / scale); break;
        case SDL_EVENT_MOUSE_MOTION: ui.pointerMove(ev.motion.x * dens / scale, ev.motion.y * dens / scale); break;
        case SDL_EVENT_MOUSE_BUTTON_UP: if (ev.button.button == SDL_BUTTON_LEFT) ui.pointerUp(ev.button.x * dens / scale, ev.button.y * dens / scale); break;
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN: ui.isFullscreen = true; ui.wantFullscreen = true; break;
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN: ui.isFullscreen = false; ui.wantFullscreen = false; break;
        default: break;
      }
    }
    if (ui.wantFullscreen != ui.isFullscreen) { SDL_SetWindowFullscreen(win, ui.wantFullscreen); ui.isFullscreen = ui.wantFullscreen; }
    engine.collectGarbage();

    const Uint64 now = SDL_GetTicksNS(); const float dt = std::min(0.1f, (now - last) / 1e9f); last = now;
    if (W != canvas.W || H != canvas.H) canvas.resize(W, H);
    ui.draw(canvas, dt);
    if (!tex || texW != W || texH != H) { if (tex) SDL_DestroyTexture(tex); tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, W, H); SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST); texW = W; texH = H; }
    SDL_UpdateTexture(tex, nullptr, canvas.rgba.data(), W * 4);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
    SDL_FRect dst{0, 0, (float)(W * scale), (float)(H * scale)}; SDL_RenderTexture(ren, tex, nullptr, &dst);
    SDL_RenderPresent(ren);
  }
  ma_device_uninit(&device);
  SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
  return 0;
}
