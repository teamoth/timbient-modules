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
  float holdL = 0, holdR = 0, holdLT = 0, holdRT = 0;   // OUTPUT's meters: the loudest moment, held
  Module* m = nullptr; int x = 0, y = 0; bool fold = false;
  float hist[48] = {0}; int hi = 0; float ht = 0;   // recent values, for drawing the LFO's line
  float lit[64] = {0};                               // Note Grid pads that were just pressed
  float scroll = 0, hold = 0; int seqVer = 0, entered = 0;   // the Tracker: first row on show, a pause in FOLLOW after an edit
  // Sample / Grain Looper: the sounds held (Grain Looper uses only the first), each kept as a file named by id
  struct Snd { std::string id, name; float root = 60; std::vector<float> peaks; bool missing = false; };
  static constexpr int SLOTS = 8; Snd snd[SLOTS]; int shown = 0;
  std::string dev;                                                       // MIDI OUT: the device chosen (empty: the first one there)
  std::vector<std::pair<float, float>> dots; int hopSeen = 0;            // Random Hops: recent hops (age, height); Tombola uses dots too
};
struct UiCable { int from, fromPort, to, toPort, toParam; PType type; unsigned seen = 0; std::vector<float> pulses; };   // pulses: triggers on their way along it (0 at the output, 1 at the input)

// a list that drops down from a button: pick one item (an optional title row can't be picked)
struct Dropdown { std::vector<std::string> items; int x = 0, y = 0, w = 0, cur = -1, title = -1, danger = -1; std::function<void(int)> pick; std::vector<int> sw; int rows = 1, colW = 60; };   // sw: a palette colour shown beside each item

class Ui {
public:
  explicit Ui(Engine& e) : eng(e) {}
  Engine& eng;
  std::vector<std::unique_ptr<UiMod>> mods; std::vector<UiCable> cables;
  int viewX = 0, viewY = 0, nextId = 1;
  std::string statusMsg; float statusT = 0;
  bool wantFullscreen = false, isFullscreen = false;
  std::string midiState = "MIDI OFF";                 // shown on MIDI modules
  std::function<void()> onChange;                     // called after anything that should be saved
  std::function<void(int)> onZoom;                    // the zoom buttons: -1 out, +1 in
  std::function<bool(UiMod*, const std::string&)> onAction;   // the app's own handling of some buttons (LOAD FILE, RECORD...); true if it took it

  // building a patch
  UiMod* add(const std::string& type, int x, int y, int id = 0);   // id: put back with the number it had (undo)
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
  void openDropdown(Dropdown d) { dd = std::make_unique<Dropdown>(std::move(d)); menuOpen = false; tp.open = false; }
  // the shared tempo: a small panel with a slider, - and + buttons and tap tempo; it stays open until you tap somewhere else
  void openTempo(int x, int y) { tp.open = true; tp.x = x; tp.y = y; dd.reset(); menuOpen = false; }
  // the sound browser: a folder's sound files; tap one to hear it, LOAD puts it in the module. Stays open until you tap away
  struct Browser { bool open = false; std::string dir; struct Entry { std::string name; bool folder; }; std::vector<Entry> items;
    int target = 0, slot = 0, sel = -1, playing = -1; float scroll = 0; std::string err; } br;
  void openBrowser(int targetId, int slot, const std::string& dir);   // slot -1: the next empty place (KIT / MULTI)
  void browseTo(const std::string& dir);
  void browserScroll(float rows);
  void browserKey(int dir);               // up / down arrows: the next sound, heard straight away
  void closeBrowser();
  void browserPick(int i);             // a tap on a row: open a folder, or hear a sound (a second tap stops it)
  std::function<void(const std::string&)> onPreview;                     // play a file to the speakers (empty: stop)
  std::function<void(int, int, const std::string&)> onBrowseLoad;        // put a file in a module's place
  std::function<void()> onBrowseFolder;                                  // ask the system for a folder
  std::function<void(int, int)> onBrowseFile;
  std::function<bool(const std::string&, std::vector<Browser::Entry>&)> onList;   // a folder's contents (the app does this with SDL)                            // the system's own file picker instead
  // a small box for typing a name
  struct Ask { std::string title, text; std::function<void(const std::string&)> done; bool dice = false; };
  std::unique_ptr<Ask> ask;
  void askText(const std::string& title, const std::string& init, std::function<void(const std::string&)> done) { ask = std::make_unique<Ask>(Ask{title, init, std::move(done)}); dd.reset(); menuOpen = false; }
  void askName(const std::string& title, std::function<void(const std::string&)> done) { askText(title, randomName(), std::move(done)); ask->dice = true; }   // with a dice for another name
  static std::string randomName();
  void typed(const std::string& t);      // text typed while the box is open
  void keyEnter(); void keyBackspace(); void keyEscape();

  struct Geom { int jr, vy, py, h, w, vh; };   // vh: the display's height (the Tracker's grows with its rows)
  Geom geom(const UiMod& u) const;
  // the top bar: extra buttons the app adds (sessions, settings...)
  struct BarBtn { std::string label; std::function<void(int x)> fn; int col = 9; };
  std::vector<BarBtn> bar;

private:
  UiMod* hit(int wx, int wy, int& lx, int& ly);
  struct JackHit { UiMod* u = nullptr; bool out = false; int port = -1; PType type = PType::Audio; int x = 0, y = 0; int param = -1; };   // param: a setting's mod socket
  JackHit jackAt(int wx, int wy, int tol = 3);
  int paramAt(UiMod* u, int ly);
  void nudge(UiMod* u, int k, int dir);
  void drawModule(Canvas& c, UiMod& u, float dt);
  void drawDisplay(Canvas& c, UiMod& u, int x, int y, int w, float dt);
  void drawCable(Canvas& c, int ax, int ay, int bx, int by, int col, int flowCol = -1, float flow = 0);
  void drawJack(Canvas& c, int x, int y, PType t, bool filled, bool lit);
  int jackY(int k) const { return 8 + k * 7 + 3; }
public:
  bool unplugSelected();   // Delete / Backspace, or the UNPLUG button
  static constexpr bool jacksIn = true;
  bool cableMotion = true; float flowT = 0;   // SETTINGS: pulses travel along trigger cables, audio cables flow (off: the old header flash)
  int inJackX(const UiMod& u) const { return u.x + (jacksIn ? 4 : 0); }
  int outJackX(const UiMod& u) const { return u.x + u.m->def->width - 1 - (jacksIn ? 4 : 0); }
  int sockX(const UiMod& u) const { return u.x + (jacksIn ? 4 : -2); }
  int minusX(const UiMod& u, int j) const;   // where a slider row's "-" sits, from the module's left
private:
  void keysPress(UiMod& u, int x, int y);
  void drawTracker(Canvas& c, UiMod& u, int x, int y, int w, float dt);
  void trackerWatch(UiMod& u);
  void changed() { if (onChange) onChange(); }
  struct Drag { int kind = 0; UiMod* m = nullptr; int ox = 0, oy = 0, k = -1; float sx = 0, sy = 0, n0 = 0; int vx0 = 0, vy0 = 0;
    JackHit from; bool rev = false; int wx = 0, wy = 0; bool moved = false, held = false; float holdT = 0; int dir = 0; UiCable grab{}; int splice = -1; bool hasOrig = false; UiCable orig{}; } drag;
  enum { D_NONE, D_PAN, D_MOVE, D_PARAM, D_CABLE, D_XFADE, D_MENU, D_SCROLL, D_GRAB, D_TEMPO, D_STEP, D_BROWSE, D_PULL };
  // cables as curves you can touch
  bool cableEnds(const UiCable& c, float& ax, float& ay, float& bx, float& by);
  int cableAt(int wx, int wy, float maxD, bool audioOnly = false);
  std::string cableName(const UiCable& c);
  UiCable sel{}; float selT = 0;      // a cable tapped: shown brighter for a moment
  bool selOn = false; int ubx = 0, uby = 0;   // ...with an UNPLUG button beside it: the only way to take a cable out
  void selectCable(const UiCable& c, float sx, float sy);
  struct TempoPanel { bool open = false; int x = 0, y = 0; } tp; std::vector<float> taps;
  static constexpr int TPW = 126, TPH = 31;
  void setBpm(float v);
  bool tempoDown(float sx, float sy);
  bool browserDown(float sx, float sy);
  void drawBrowser(Canvas& c);
  int brX = 0, brY = 0, brW = 0, brH = 0, brRows = 0;
  std::unique_ptr<Dropdown> dd; bool menuOpen = false; int lastW = 400; float blink = 0;
  struct MenuItem { std::string type; int x, y, w; };
  std::vector<MenuItem> menuItems;
  struct BarHit { int x0, x1; std::function<void()> fn; };
  std::vector<BarHit> barHits;
};

}  // namespace tb
