// Timbient native: the screen side. It draws the patch and turns mouse and touch into commands for the engine.
// It never touches the running patch directly, only the values the engine publishes.
#pragma once
#include "../engine/engine.h"
#include "canvas.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tb {

struct UiMod {
  Module* m = nullptr; int x = 0, y = 0; bool fold = false;
  float hist[48] = {0}; int hi = 0; float ht = 0;   // recent values, for drawing the LFO's line
  float lit[64] = {0};                               // Note Grid pads that were just pressed
  float scroll = 0, hold = 0; int seqVer = 0, entered = 0;   // the Tracker: first row on show, a pause in FOLLOW after an edit
  std::string audioId; std::vector<float> peaks; bool missing = false;   // Sample / Grain Looper: the sound it holds, and its outline
};
struct UiCable { int from, fromPort, to, toPort, toParam; PType type; };

// a list that drops down from a button: pick one item (an optional title row can't be picked)
struct Dropdown { std::vector<std::string> items; int x = 0, y = 0, w = 0, cur = -1, title = -1, danger = -1; std::function<void(int)> pick; };

class Ui {
public:
  explicit Ui(Engine& e) : eng(e) {}
  Engine& eng;
  std::vector<std::unique_ptr<UiMod>> mods; std::vector<UiCable> cables;
  int viewX = 0, viewY = 0, nextId = 1;
  std::string statusMsg; float statusT = 0;
  bool wantFullscreen = false, isFullscreen = false;
  std::function<void()> onChange;                     // called after anything that should be saved
  std::function<void(int)> onZoom;                    // the zoom buttons: -1 out, +1 in
  std::function<bool(UiMod*, const std::string&)> onAction;   // the app's own handling of some buttons (LOAD FILE, RECORD...); true if it took it

  // building a patch
  UiMod* add(const std::string& type, int x, int y);
  void remove(UiMod* u);
  bool connect(UiMod* a, int outPort, UiMod* b, int inPort);
  bool connect(UiMod* a, const std::string& outPort, UiMod* b, const std::string& inPort);
  bool modulate(UiMod* src, UiMod* dst, int paramIndex);
  void unplug(const UiCable& c);
  void set(UiMod* m, const std::string& k, float v);
  void clearPatch();
  void demoPatch();
  UiMod* byId(int id) { for (auto& u : mods) if (u->m->id == id) return u.get(); return nullptr; }

  // each frame
  void draw(Canvas& c, float dt);
  // input, in canvas pixels
  void pointerDown(float x, float y);
  void pointerMove(float x, float y);
  void pointerUp(float x, float y);
  void status(const std::string& s, float t = 2) { statusMsg = s; statusT = t; }
  void openDropdown(Dropdown d) { dd = std::make_unique<Dropdown>(std::move(d)); menuOpen = false; }
  // a small box for typing a name
  struct Ask { std::string title, text; std::function<void(const std::string&)> done; };
  std::unique_ptr<Ask> ask;
  void askText(const std::string& title, const std::string& init, std::function<void(const std::string&)> done) { ask = std::make_unique<Ask>(Ask{title, init, std::move(done)}); dd.reset(); menuOpen = false; }
  void typed(const std::string& t);      // text typed while the box is open
  void keyEnter(); void keyBackspace(); void keyEscape();

  struct Geom { int jr, vy, py, h, w; };
  Geom geom(const UiMod& u) const;
  // the top bar: extra buttons the app adds (sessions, settings...)
  struct BarBtn { std::string label; std::function<void(int x)> fn; int col = 9; };
  std::vector<BarBtn> bar;

private:
  UiMod* hit(int wx, int wy, int& lx, int& ly);
  struct JackHit { UiMod* u = nullptr; bool out = false; int port = -1; PType type = PType::Audio; int x = 0, y = 0; };
  JackHit jackAt(int wx, int wy);
  int paramAt(UiMod* u, int ly);
  void drawModule(Canvas& c, UiMod& u, float dt);
  void drawDisplay(Canvas& c, UiMod& u, int x, int y, int w, float dt);
  void drawCable(Canvas& c, int ax, int ay, int bx, int by, int col);
  void drawJack(Canvas& c, int x, int y, PType t, bool filled, bool lit);
  int jackY(int k) const { return 8 + k * 7 + 3; }
  void keysPress(UiMod& u, int x, int y);
  void drawTracker(Canvas& c, UiMod& u, int x, int y, int w, float dt);
  void trackerWatch(UiMod& u);
  void changed() { if (onChange) onChange(); }
  struct Drag { int kind = 0; UiMod* m = nullptr; int ox = 0, oy = 0, k = -1; float sx = 0, sy = 0, n0 = 0; int vx0 = 0, vy0 = 0;
    JackHit from; bool rev = false; int wx = 0, wy = 0; } drag;
  enum { D_NONE, D_PAN, D_MOVE, D_PARAM, D_CABLE, D_XFADE, D_MENU, D_SCROLL };
  std::unique_ptr<Dropdown> dd; bool menuOpen = false; int lastW = 400; float blink = 0;
  struct MenuItem { std::string type; int x, y, w; };
  std::vector<MenuItem> menuItems;
  struct BarHit { int x0, x1; std::function<void()> fn; };
  std::vector<BarHit> barHits;
};

}  // namespace tb
