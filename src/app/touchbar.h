#pragma once
// The Touch Bar, on Macs that have one: while a blend is going, a wide BLEND slider (a crossfader between the two sessions)
// with PAUSE / GO, NOW and CANCEL; otherwise BLEND TO... and the OUTPUT's LEVEL. Elsewhere these do nothing.
#include <functional>

struct SDL_Window;

namespace tb {

struct TouchBarHooks {
  std::function<void(float)> blendSet, levelSet;   // 0..1
  std::function<void()> pause, now, cancel, blendTo;
};
struct TouchBarState { bool blending = false, paused = false; float at = 0, level = 0; };

#ifdef __APPLE__
void touchBarInstall(SDL_Window* window, const TouchBarHooks& hooks);
void touchBarUpdate(const TouchBarState& state);   // each frame (it only changes what's different)
#else
inline void touchBarInstall(SDL_Window*, const TouchBarHooks&) {}
inline void touchBarUpdate(const TouchBarState&) {}
#endif

}  // namespace tb
