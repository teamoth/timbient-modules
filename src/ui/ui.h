// Timbient native: the screen side. It draws the patch and turns mouse and touch into commands for the engine.
// It never touches the running patch directly, only the values the engine publishes.
#pragma once
#include "../engine/engine.h"
#include "../engine/analyse.h"
#include "canvas.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tb {

struct UiMod;
void keysCell(const UiMod& u, int& cw, int& ch);   // NOTE GRID: how big its buttons are
struct UiMod {
  int colScroll = 0;   // TRACKER: the first of the columns on show
  int tlSel = -1;      // TIMELINE: the event picked (its place in the list), or -1
  float holdL = 0, holdR = 0, holdLT = 0, holdRT = 0;   // OUTPUT's meters: the loudest moment, held
  Module* m = nullptr; int x = 0, y = 0; bool fold = false;
  float hist[48] = {0}; int hi = 0; float ht = 0;   // recent values, for drawing the LFO's line
  float lit[64] = {0};                               // Note Grid pads that were just pressed
  float scroll = 0, hold = 0; int seqVer = 0, entered = 0;   // the Tracker: first row on show, a pause in FOLLOW after an edit
  // Sample / Grain Looper: the sounds held (Grain Looper uses only the first), each kept as a file named by id
  struct Snd { std::string id, name; float root = 60; std::vector<float> peaks; bool missing = false; double secs = 0; };   // secs: how long it is
  static constexpr int SLOTS = 8; Snd snd[SLOTS]; int shown = 0;
  std::string dev;                                                       // MIDI OUT: the device chosen (empty: the first one there)
  std::vector<std::pair<float, float>> dots; int hopSeen = 0;            // Random Hops: recent hops (age, height); Tombola uses dots too
};
struct UiCable { int from, fromPort, to, toPort, toParam; PType type; unsigned seen = 0; std::vector<float> pulses; float flow = 0, lastV = 0, rate = 0; };   // flow: how far its moving pattern has gone   // pulses: triggers on their way along it (0 at the output, 1 at the input)

// a list that drops down from a button: pick one item (an optional title row can't be picked)
struct Dropdown { std::vector<std::string> items; int x = 0, y = 0, w = 0, cur = -1, title = -1, danger = -1; std::function<void(int)> pick; std::vector<int> sw; int rows = 1, colW = 60; bool stay = false; };   // stay: picking doesn't close it (trying colours)   // sw: a palette colour shown beside each item

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
  std::function<bool(UiMod*, const std::string&)> onAction;
  // QWERTY MIDI: the key on each of the 36 notes (C-0 to B-2), named by the app; the note waiting for a key, or -1
  bool shiftHeld = false;   // Shift is down (the app says): NOTE LOOPER's lines move freely
  bool borders = false;   // THEME > BORDERS: the line round each module
  bool shadows = true;    // THEME > SHADOWS   // THEME > SHADOWS: a drop shadow under each module
  std::string qwKeys[36]; int qwLearn = -1;
  bool hasQwerty() const;      // a QWERTY MIDI that's on: the computer keys play it
  void qwertyPlay(int i);      // note i (0 = C-0) on every QWERTY MIDI   // the app's own handling of some buttons (LOAD FILE, RECORD...); true if it took it

  // building a patch
  UiMod* add(const std::string& type, int x, int y, int id = 0);   // id: put back with the number it had (undo)
  void remove(UiMod* u);
  void removeGently(UiMod* u);   // the × button: bridges an effect's chain, lets its sound ring out
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
  // a right click (or Ctrl-click): a module's own menu (DUPLICATE)
  void contextAt(float x, float y);
  std::function<void(UiMod*)> onDuplicate;   // the app copies it (settings, contents, sounds; no cables)
  int lastTouched = -1;                      // the module last tapped (Ctrl/Cmd + D duplicates it)
  void pointerMove(float x, float y);
  void pointerUp(float x, float y);
  void status(const std::string& s, float t = 2) { statusMsg = s; statusT = t; }
  void openDropdown(Dropdown d) { dd = std::make_unique<Dropdown>(std::move(d)); menuOpen = false; tp.open = false; }
  // the shared tempo: a small panel with a slider, - and + buttons and tap tempo; it stays open until you tap somewhere else
  void openTempo(int x, int y) { tp.open = true; tp.x = x; tp.y = y; dd.reset(); menuOpen = false; }
  // the sound browser: a folder's sound files; tap one to hear it, LOAD puts it in the module. Stays open until you tap away
  struct Browser { bool open = false; std::string dir; struct Entry { std::string name; bool folder; }; std::vector<Entry> items;
    int target = 0, slot = 0, sel = -1, playing = -1; float scroll = 0; std::string err;
    // about the picked sound (worked out on another thread): how long, mono or stereo, its rate, and what analyseSound found
    bool autoPreview = true, wide = false;   // PREVIEW: tapping a sound plays it. <>: the wide browser, for long names
    std::string infoPath; bool infoReady = false, infoFailed = false; SoundInfo info; int channels = 0; float fileRate = 0; double secs = 0; } br;
  std::function<void(const std::string&)> onInfo;
  std::function<void()> onBrowseChanged;
  // the sound editor (EDIT SOUND on SAMPLE, GRAIN LOOPER, STRETCH): choose a part, change it (reverse, normalise, fades,
  // silence, delete) with its own undo, hear it (once or looping), and KEEP ALL or KEEP PART as the module's sound
  struct Editor { bool open = false; int target = 0, slot = 0; std::string name; std::vector<float> d, r; float sr = 48000;   // r: the right of a stereo sound (else empty)
    std::vector<float> mipLo, mipHi;   // every 256 samples' lowest and highest, for drawing long sounds quickly
    double a = 0, b = 0, v0 = 0, v1 = 0; bool playing = false, loop = false, unkept = false, closeAsked = false;
    struct Snap { std::vector<float> d, r; double a, b; }; std::vector<Snap> undo, redo; } ed;
  struct EdBtn { int x, y, w, id; }; std::vector<EdBtn> edBtns; int edHintY = 0;
  void openEditor(int targetId, int slot, const std::string& name, std::vector<float>&& data, float sr, std::vector<float>&& right = {});
  void closeEditor(bool force = true);
  void editorWheel(float sx, float wheel);   // zoom in or out around the pointer
  void editorKey(int key);                   // Escape closes, Space plays
  void editorUndo(int dir);                  // the editor's own undo (-1) and redo (+1)
  // play the sound from a to e (samples), going round if loop (empty: stop)
  std::function<void(const std::vector<float>&, const std::vector<float>&, float, double, double, bool)> onEditPlay;
  std::function<void(int, int, std::vector<float>&&, std::vector<float>&&, float, const std::string&)> onEditCrop;   // KEEP: this becomes the module's sound
  void openBrowser(int targetId, int slot, const std::string& dir);   // slot -1: the next empty place (KIT / MULTI)
  void browseTo(const std::string& dir);
  void browserScroll(float rows);
  void browserKey(int dir);               // up / down arrows: the next sound, heard straight away
  void closeBrowser();
  void browserPick(int i);
  void browserPreview();   // the PREVIEW switch (and Space)
  void browserLoad();      // LOAD IT (and Enter)             // a tap on a row: open a folder, or hear a sound (a second tap stops it)
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
  void drawCable(Canvas& c, int ax, int ay, int bx, int by, int col, int flowCol = -1, float flow = 0, float period = 9, float dash = 2, int tailCol = -1);
  void drawJack(Canvas& c, int x, int y, PType t, bool filled, bool lit);
  int jackY(int k) const { return 8 + k * 7 + 3; }
public:
  bool unplugSelected();   // Delete / Backspace, or the UNPLUG button
  void centreView(); bool centrePending = false; int lastH = 0;   // the view to the middle of the session (done at the next frame, when the screen's size is known)
  int trFocus = 0;          // the Tracker last touched: the arrow keys and Delete work on it
  bool trackerKey(int key); // 1 up, 2 down, 3 left, 4 right, 5 delete
  static constexpr bool jacksIn = true;
  bool cableMotion = true; float flowT = 0;   // SETTINGS: pulses travel along trigger cables, audio cables flow (off: the old header flash)
  int inJackX(const UiMod& u) const { return u.x + (jacksIn ? 4 : 0); }
  int outJackX(const UiMod& u) const { return u.x + geom(u).w - 1 - (jacksIn ? 4 : 0); }
  int sockX(const UiMod& u) const { return u.x + (jacksIn ? 4 : -2); }
  // the SWITCH socket in the footer, beside the ON / M buttons: a trigger turns the module on, off or over (a Timeline lane, say)
  bool hasSwitch(const UiMod& u) const;
  bool hasKey(const UiMod& u) const; int kyX(const UiMod& u) const;
  int swX(const UiMod& u) const;
  int swY(const UiMod& u) const;
  int minusX(const UiMod& u, int j) const;   // where a slider row's "-" sits, from the module's left
private:
  void keysPress(UiMod& u, int x, int y);
  // TIMELINE
  void drawTimeline(Canvas& c, UiMod& u, int x, int y, int w);
  void timelineDown(UiMod& u, int vx, int vy, int w);
  void timelineDrag(UiMod& u, int vx, int vy, int w);
  void timelineUp(UiMod& u, bool moved, int dir);
  std::string tlEventText(UiMod& u, int i);
  void drawTracker(Canvas& c, UiMod& u, int x, int y, int w, float dt);
  void trackerWatch(UiMod& u);
  void changed() { if (onChange) onChange(); }
  struct Drag { int kind = 0; UiMod* m = nullptr; int ox = 0, oy = 0, k = -1; float sx = 0, sy = 0, n0 = 0; int vx0 = 0, vy0 = 0;
    JackHit from; bool rev = false; int wx = 0, wy = 0; bool moved = false, held = false; float holdT = 0; int dir = 0; UiCable grab{}; int splice = -1; bool hasOrig = false; UiCable orig{}; } drag;
  enum { D_NONE, D_PAN, D_MOVE, D_PARAM, D_CABLE, D_XFADE, D_MENU, D_SCROLL, D_GRAB, D_TEMPO, D_STEP, D_BROWSE, D_PULL, D_EDIT, D_TLINE, D_NLOOP, D_HOLD };
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
  bool editorDown(float sx, float sy);
  void editorMove(float sx, float sy);
  void drawEditor(Canvas& c);
  int edX = 0, edY = 0, edW = 0, edH = 0;
  double edTimeAt(float sx) const;
  void drawBrowser(Canvas& c);
  int brX = 0, brY = 0, brW = 0, brH = 0, brRows = 0;
  std::unique_ptr<Dropdown> dd; bool menuOpen = false; int lastW = 400; float blink = 0;
  struct MenuItem { std::string type; int x, y, w; };
  std::vector<MenuItem> menuItems;
  struct BarHit { int x0, x1; std::function<void()> fn; std::string label; };
  std::vector<BarHit> barHits;
};

}  // namespace tb
