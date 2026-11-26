// Timbient native: the screen side. It draws the patch and turns mouse and touch into commands for the engine.
// It never touches the running patch directly, only the values the engine publishes.
#pragma once
#include "../engine/engine.h"
#include "../engine/analyse.h"
#include "canvas.h"
#include "json.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <map>

namespace tb {

struct UiMod; struct UiCable;
void keysCell(const UiMod& u, int& cw, int& ch);   // NOTE GRID: how big its buttons are
struct UiMod {
  int colScroll = 0;   // TRACKER: the first of the columns on show
  const std::vector<UiCable>* cables = nullptr; std::vector<float> spec, specPk, specPkT, specRing; float clearAsk = 0, shakeT = 0;   // spec: STRETCH's spectrum as shown (gliding)
  int valK = -1; float valT = 0;   // valK, valT: the row - or + last moved, and how much longer its value shows   // clearAsk: CLEAR was tapped once (a second tap within this long clears)   // its Ui's cables (a row can wait on one)
  int lastTake = 0, fbSent = -1; float recGiveT = 0;   // SAMPLER: the last take copied out; the feedback warning sent; when it was last given somewhere to record
  float holdL = 0, holdR = 0, holdLT = 0, holdRT = 0;   // OUTPUT's meters: the loudest moment, held
  Module* m = nullptr; int x = 0, y = 0; bool fold = false;
  float hist[48] = {0}; int hi = 0; float ht = 0;   // recent values, for drawing the LFO's line
  int sixPlayed = 0; float sixHold = 0; int sixDown[64] = {0}, sixLast[64] = {0}; bool sixKnown[64] = {false}; float sixAcc[64] = {0};   // sixLast, sixAcc: a knob sending positions, where it was and how far it has gone since the last step
     // SIXTEEN: the last step it played; how long FOLLOW waits after a page was chosen; knobs held down (2: turned while held)
  float lit[64] = {0};                               // Note Grid pads that were just pressed
  float scroll = 0, hold = 0; int seqVer = 0, entered = 0;   // the Tracker: first row on show, a pause in FOLLOW after an edit
  // Sample / Grain Looper: the sounds held (Grain Looper uses only the first), each kept as a file named by id
  struct Snd { std::string id, name; float root = 60; std::vector<float> peaks; bool missing = false; double secs = 0; };   // secs: how long it is
  static constexpr int SLOTS = 8; Snd snd[SLOTS]; int shown = 0;
  std::string dev;                                                       // MIDI OUT: the device chosen (empty: the first one there)
  std::vector<std::pair<float, float>> dots; int hopSeen = 0;            // Random Hops: recent hops (age, height); Tombola uses dots too
  std::string text; int noteW = 98, noteH = 40; float noteScroll = 0;    // NOTES: what's typed, the box's size (it can be dragged bigger), how far it's scrolled
};
struct UiCable { int from, fromPort, to, toPort, toParam; PType type; unsigned seen = 0; std::vector<float> pulses; float flow = 0, lastV = 0, rate = 0; };   // flow: how far its moving pattern has gone   // pulses: triggers on their way along it (0 at the output, 1 at the input)

// a list that drops down from a button: pick one item (an optional title row can't be picked)
struct Dropdown { std::vector<std::string> items; int x = 0, y = 0, w = 0, cur = -1, title = -1, title2 = -1, danger = -1; std::function<void(int)> pick; std::vector<int> sw; std::function<void(Dropdown&)> live; int rows = 1, colW = 60; bool stay = false; };   // stay: picking doesn't close it (trying colours); live: brings its items up to date each frame (the MIDI DEVICES lights)   // sw: a palette colour shown beside each item

class Ui {
public:
  explicit Ui(Engine& e) : eng(&e) {}
  bool viewOnly = false; std::string viewTitle;   // the web player: the patch to look at, nothing to change (no bar buttons, menus or help)
  Engine* eng;   // the engine this session plays in (BLEND TO moves the screen to another one)
  void setEngine(Engine& e) { eng = &e; }
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
  bool tapRow(UiMod* u, int k);   // a tap on a speed row (three or more in rhythm set it); true when it did
  void insertInto(const UiCable& c, int moduleId);
  // ---- MIDI LEARN: a knob, fader, encoder or button on a MIDI controller moves a control (right-click a control: MIDI LEARN) ----
  // kind: 0 CC, 1 NOTE, 2 NRPN, 3 PITCH BEND. mode (CC): 0 absolute, 1 relative round 64, 2 relative two's complement (1 up,
  // 127 down), 3 relative sign and size (1 up, 65 down), 4 absolute 14-bit (CC n then n+32). key: a row's key, or #bypass, #mute, #solo.
  // A global mapping (EVERY SESSION) names its module by type and which of that type (gNth), as ids change from session to session
  struct MidiMap { std::string dev; int ch = 0, kind = 0, num = 0, mode = 0, mod = 0; std::string key; bool global = false; std::string gType; int gNth = 0;
    float hw = -1; bool engaged = false; int fb = -1; float acc = 0, keep = 0; int msb = -1; bool down = false, lsbSeen = false; };
  std::vector<MidiMap> midiMaps;
  std::vector<std::string> midiDevNames;   // the MIDI inputs, by number (the app keeps this up to date)
  bool midiFeedback = true;                // send each mapped control's value back to the device (when it has an output of the same name)
  std::function<bool(const std::string& dev, const unsigned char* b, int n)> onMidiSend;   // the app sends these to the device's output (false: it has none)
  std::function<void()> onGlobalMaps;      // the EVERY SESSION mappings changed: the app keeps them in its settings
  void midiIn(int dev, unsigned char st, unsigned char d1, unsigned char d2);   // a message from a device (on the screen's thread)
  void midiFrame(float dt);                 // each frame: learning, and values going back to the devices
  void startLearn(UiMod* u, const std::string& key);
  void cancelLearn() { learnMod = 0; learnSeen.clear(); }
  bool sixLearning() const { return sixLearn != 0; } void cancelSixLearn() { sixLearn = 0; }   // SIXTEEN's LEARN 64 STEPS
  bool learning() const { return learnMod != 0; }
  bool learningRow(const UiMod& u, const std::string& key) const { return learnMod == u.m->id && learnKey == key; }
  const MidiMap* mapFor(const UiMod& u, const std::string& key) const;
  void forgetMap(const UiMod& u, const std::string& key);
  void openMidiMap(int x, int y);
  // SETTINGS > MIDI: DEVICES (every input and output, a light while it sends, on/off), MONITOR (what has just arrived), MIDI MAP
  struct MidiDev { std::string name; bool input = true, on = true, open = false, inUse = false, clock = false; float seen = 99; };
  std::vector<MidiDev> midiDevs;            // the app keeps this up to date
  std::vector<std::string> midiLog;         // the latest messages, in words, newest last
  std::function<void(const std::string& name)> onMidiDevToggle;
  void openMidiMenu(int x, int y);
  void openMidiDevices(int x, int y);
  void openMidiMonitor(int x, int y);
  const Dropdown* openList() const { return dd.get(); }   // the list that's open (for the tests)
  void closeList() { dd.reset(); }           // the list: every mapping, its kind, EVERY SESSION, forget; values back on or off
  Json midiMapsJson(bool global) const;
  void midiMapsLoad(const Json& a, bool global, const std::map<int, UiMod*>* idmap);
  std::string mapSource(const MidiMap& m) const;
  std::string mapTarget(const MidiMap& m) const;
 private:
  int learnMod = 0; std::string learnKey; float learnT = 0, learnWait = -1, midiQuiet = 0, fbT = 0; struct Seen { int dev; unsigned char st, d1, d2; }; std::vector<Seen> learnSeen;
  struct Nrpn { int pm = -1, pl = -1, dm = -1; } nrpn[8][16];
  void learnDecide();
  UiMod* mapMod(MidiMap& m);
  void mapApply(MidiMap& m, float v, int delta, int press);   // v: an absolute value 0..1 (-1: none); delta: relative steps; press: 1 down, 0 up, -1 none
  float mapValue(MidiMap& m);               // the control's value now, 0..1
  void mapSend(MidiMap& m, float v);
 public:
   // put a module into a cable (as dropping it on one does, once you say yes)
  void removeGently(UiMod* u);   // the × button: bridges an effect's chain, lets its sound ring out
  bool connect(UiMod* a, int outPort, UiMod* b, int inPort);
  bool connect(UiMod* a, const std::string& outPort, UiMod* b, const std::string& inPort);
  bool modulate(UiMod* src, UiMod* dst, int paramIndex);
  void unplug(const UiCable& c);
  void set(UiMod* m, const std::string& k, float v);
  void clearPatch();
  // BLEND TO: the session being blended away from keeps playing in its own engine; its modules and cables are set aside here
  // (not taken out of that engine), so CANCEL can put them back just as they were
  struct Stash { std::vector<std::unique_ptr<UiMod>> mods; std::vector<UiCable> cables; int viewX = 0, viewY = 0, nextId = 1; std::vector<MidiMap> maps; Engine* eng = nullptr; bool held = false; };
  void stashPatch(Stash& s);     // the patch moves into s; the screen is left empty (the engine still has it all)
  void unstashPatch(Stash& s);   // and back (the screen's patch, if any, should be stashed or cleared first)
  std::string sessionTitle;      // the top bar's middle: the session's name (or NEW SESSION), with a • if it has changes not yet saved
  bool sessionChanged = false;
  float blendAt = -1;            // how far a blend has got (0..1), or -1: none. The top bar shows it, with NOW and CANCEL
  std::string blendName;         // the session being blended to
  std::function<void()> onBlendNow, onBlendCancel, onBlendPause;
  std::function<void(float)> onBlendSet;   // dragging the bar, a MIDI knob (0..1): the blend goes there and waits
  bool blendPaused = false, blendWas = false;
  int blendBarX0 = 0, blendBarX1 = 0;      // where the bar is (for dragging, and right-click: MIDI LEARN)
  void startLearnBlend();                  // MIDI LEARN for the blend: an EVERY SESSION mapping of its own (no module)
  void demoPatch();
  UiMod* byId(int id) { if (id == PAD_ID) return pad.get(); for (auto& u : mods) if (u->m->id == id) return u.get(); return nullptr; }
  const UiMod* byId(int id) const { if (id == PAD_ID) return pad.get(); for (auto& u : mods) if (u->m->id == id) return u.get(); return nullptr; }

  // each frame
  void draw(Canvas& c, float dt);
  // input, in canvas pixels
  void pointerDown(float x, float y);
  // a right click (or Ctrl-click): a module's own menu (DUPLICATE)
  void contextAt(float x, float y);
  std::function<void(UiMod*)> onDuplicate;   // the app copies it (settings, contents, sounds; no cables)
  // SELECTING several modules: Shift-drag on empty space draws a box round them, Shift-click a title adds one or takes it away.
  // Dragging any of their titles moves them all; Delete, Cmd/Ctrl+D (duplicate), C (copy) and the right-click menu work on them
  // together. A tap on empty space lets them go
  std::vector<int> picked;
  bool isPicked(int id) const { for (int p : picked) if (p == id) return true; return false; }
  void pickToggle(int id);
  void pickAll();
  std::vector<int> pickedOrTouched() const;   // the selection, or else the module last touched (OUTPUT is never copied or deleted)
  void deletePicked();
  static bool octaveJumps(const ModuleDef* d, const std::string& key);   // a tap on this setting offers -24, -12, 0, +12, +24                        // asks first
  std::function<void(const std::vector<int>&)> onDuplicateMany, onCopy, onSaveGroup;   // the app: copies with the cables between them; the clipboard; a group file
  std::function<void()> onPaste;              // Cmd/Ctrl+V, or PASTE in the right-click menu: the copied modules, at the pointer
  std::function<void(int, int)> onGroups;     // ADD > GROUPS: the app's list of saved groups
  int dropouts = 0; std::string dropLast;   // (dropLast: the newest, said when the count is tapped)   // sound dropouts since the count was last cleared (shown in the top bar; the app counts them, a tap there clears them)
  float lastPX = 0, lastPY = 0;               // where the pointer last was (canvas pixels): pasting goes there
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
    std::map<std::string, std::pair<float, std::string>> where;   // each folder looked through: how far down, and the sound picked there
    bool autoPreview = true, wide = false;   // PREVIEW: tapping a sound plays it. <>: the wide browser, for long names
    bool sessions = false, blendFirst = false;   // the SESSIONS picker (sessions, not sounds: OPEN or BLEND TO); blendFirst: ENTER blends (it came from BLEND TO)
    std::string infoPath; bool infoReady = false, infoFailed = false; SoundInfo info; int channels = 0; float fileRate = 0; double secs = 0; } br;
  std::function<void(const std::string&)> onInfo;
  std::function<void()> onBrowseChanged;
  // SESSIONS: the app's own picker (no Mac or Windows window, so nothing else wakes up): a session to OPEN or BLEND TO, or
  // OTHER FOLDER... for the system's window
  void openSessions(const std::string& dir, bool blend);
  std::function<void(const std::string&, bool)> onSessionPick;   // (its path, true: BLEND TO)
  std::function<void(bool)> onSessionOther;
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
  std::function<void(const std::string&, const std::vector<float>&, const std::vector<float>&, float)> onEditExport;   // EXPORT WAV: the sound as it is in the editor, to a file
  void openBrowser(int targetId, int slot, const std::string& dir);   // slot -1: the next empty place (KIT / MULTI)
  void browseTo(const std::string& dir);
  void browserScroll(float rows);
  void browserKey(int dir);               // up / down arrows: the next sound, heard straight away
  void closeBrowser();
  void browserPick(int i);
  void browserPreview();   // the PREVIEW switch (and Space)
  void sessionGo(bool blend);   // SESSIONS: OPEN (or BLEND TO) the one picked
  void browserLoad();      // LOAD IT (and Enter)             // a tap on a row: open a folder, or hear a sound (a second tap stops it)
  std::function<void(const std::string&)> onPreview;                     // play a file to the speakers (empty: stop)
  std::function<void(int, int, const std::string&)> onBrowseLoad;        // put a file in a module's place
  std::function<void()> onBrowseFolder;                                  // ask the system for a folder
  std::function<void(int, int)> onBrowseFile;
  std::function<bool(const std::string&, std::vector<Browser::Entry>&)> onList;   // a folder's contents (the app does this with SDL)                            // the system's own file picker instead
  // a small box for typing a name
  struct Ask { std::string title, text; std::function<void(const std::string&)> done; bool dice = false; int check = -1; std::string checkLabel; };   // check: a tick box (-1: none)
  int askChecked = -1;
  // NOTES: the one being typed into (its id; 0: none), where the cursor is, and the editing keys (the app's own keys wait meanwhile)
  // Editing works as in a text editor: noteAnc is the other end of the selection (the same as noteCur: none)
  int noteEdit = 0, noteWas = 0; size_t noteCur = 0, noteAnc = 0; bool noteFollow = true;
  bool typing() const { return ask || noteEdit; }
  // NOTEPAD: one note of your own, not in any session (a set list, reminders): a panel floating over the patch, shown or hidden
  // from the top bar, moved by its title, resized by its corner; it stays whatever session is opened. The app saves it
  static constexpr int PAD_ID = -77;
  std::unique_ptr<UiMod> pad; bool padOpen = false, padDirty = false; int padX = PAD_UNSET, padY = PAD_UNSET; static constexpr int PAD_UNSET = -99999;   // (not placed yet: top right)
  UiMod* padMod();                        // (made the first time it's needed)
  void noteChanged() { if (noteEdit == PAD_ID) padDirty = true; else changed(); }
  bool padAt(float sx, float sy) const;   // is this point on the panel
  enum { NK_SHIFT = 1, NK_WORD = 2, NK_LINE = 4 };   // noteKey's mods: extend the selection; by word (Alt/Option); to the line or note's ends (Cmd/Ctrl)
  void noteKey(int k, int mods = 0);      // 1 up 2 down 3 left 4 right 5 backspace 6 delete 7 enter 8 home 9 end 27 escape (stop)
  std::string noteText() const;           // what's selected (to copy)
  void notePaste(const std::string& t);   // typed or pasted in, in place of the selection
  void noteCut();                         // the selection out
  void noteSelectAll();
  void noteUndo(bool redo = false);       // the typing in this note (while it's open: the patch's own undo waits)
  bool noteWheel(float sx, float sy, float dy);   // the mouse wheel over a note scrolls it (false: not over one)
  struct NoteStep { std::string text; size_t cur; };
  std::vector<NoteStep> noteUndos, noteRedos; int noteLastKind = 0; int noteClickId = 0; size_t noteClickPos = 0; float noteClickT = -9;
  size_t notePosAt(const UiMod& u, int lx, int ly, int vy) const;   // the place in a note's text nearest a point (lx, ly from the module's corner)
  void noteRecord(int kind);              // (keep the text as it was, before a change: typing joins up, other changes stand alone)
  void noteBegin(int id, size_t pos);     // start typing in a note, the cursor here
  void notePress(UiMod* u, int id, int lx, int ly, int vy);   // a press in a note's text (a second, quickly: a word)
  struct NoteLine { size_t start, len; };
  std::vector<NoteLine> noteLines(const UiMod& u) const;   // wrapped to the box's width   // the tick box as the name box closed with OK (for its done)
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
  std::string inputName;  // AUDIO IN: the input device's name (shown on the module)
  bool helpOn = true;       // HELP (the ? in the top bar): a little note on whatever the pointer rests on
  std::function<void(bool)> onHelpToggle;   // the app keeps the choice
  float hoverT = 0, hoverX = -100, hoverY = -100; int infoX0 = 0, infoX1 = 0;   // how long the pointer has rested, and where; the AUDIO % reading's place
  std::string helpAt(float sx, float sy);
  void setAddMenu(bool open);                                   // open or close the ADD menu (for tests)
  bool menuItemAt(const std::string& type, int& x, int& y) const;   // where a module's name sits in the open ADD menu   // the note for what's at this point of the screen ("TITLE|text"), or nothing
  void drawHelp(Canvas& c, float dt);
  bool jacksIn = true;     // sockets inside the frame (false: on its edges, half outside)
  bool headOut = false;    // a module's only output sits in its title bar
  bool outInHead(const UiMod& u) const { return headOut && u.m->def->outs.size() == 1; }
  int outJackY(const UiMod& u, int k) const { return outInHead(u) ? 5 : jackY(k); }
  bool cableMotion = true; float flowT = 0;   // SETTINGS: pulses travel along trigger cables, audio cables flow (off: the old header flash)
  int inJackX(const UiMod& u) const { return u.x + (jacksIn ? 4 : 0); }
  int outJackX(const UiMod& u) const { return u.x + geom(u).w - 1 - (outInHead(u) && jacksIn ? 11 : jacksIn ? 4 : 0); }
  int sockX(const UiMod& u) const { return u.x + (jacksIn ? 4 : -2); }
  // the SWITCH socket in the footer, beside the ON / M buttons: a trigger turns the module on, off or over (any trigger)
  bool hasSwitch(const UiMod& u) const;
  bool hasKey(const UiMod& u) const; int kyX(const UiMod& u) const;
  bool feedsBack(int id, int port) const;
  std::vector<int> shownRows(const UiMod& u) const;   // its rows, in the order shown (folded ones left out)
  std::vector<std::string> cutOff() const;            // names and choices too long for their module   // does this module's sound find its way, by the cables, back into this input of its own?
  int swX(const UiMod& u) const;
  int swY(const UiMod& u) const;
  int minusX(const UiMod& u, int j) const;   // where a slider row's "-" sits, from the module's left
private:
  void keysPress(UiMod& u, int x, int y);
  // SIXTEEN: its grid (the page on show: the one chosen, or the one playing with FOLLOW), tapping and dragging its steps, and the
  // controller it learned (LEARN 64 STEPS: a knob of page 1 turned, then pushed)
  void drawSixteen(Canvas& c, UiMod& u, int x, int y, int w, float dt);
  void sixPress(UiMod& u, int x, int y);
  int sixPageShown(const UiMod& u) const;
public:
  void sixSet(UiMod& u, int st, int what, float v);   // what: 0 its note (a scale step), 1 its length (steps), 2 on (1) or off (0)
  bool sixteenMidi(int dev, unsigned char st, unsigned char d1, unsigned char d2);   // true: it was for a SIXTEEN
  void startSixLearn(UiMod* u);
private:
  int sixLearn = 0, sixStage = 0; float sixLearnT = 0, sixWait = -1; std::vector<Seen> sixSeen;
  void drawTracker(Canvas& c, UiMod& u, int x, int y, int w, float dt);
  void trackerWatch(UiMod& u);
  void changed() { if (onChange) onChange(); }
  struct Drag { int kind = 0; UiMod* m = nullptr; int ox = 0, oy = 0, k = -1; float sx = 0, sy = 0, n0 = 0; int vx0 = 0, vy0 = 0;
    JackHit from; bool rev = false; int wx = 0, wy = 0; bool moved = false, held = false; float holdT = 0, holdSum = 0; int dir = 0; UiCable grab{}; int splice = -1; bool hasOrig = false; UiCable orig{};
    struct At { int id, x, y; }; std::vector<At> group; } drag;   // group: the selection being moved, where each started
  enum { D_NONE, D_PAN, D_MOVE, D_PARAM, D_CABLE, D_XFADE, D_MENU, D_SCROLL, D_GRAB, D_TEMPO, D_STEP, D_BROWSE, D_PULL, D_EDIT, D_NLOOP, D_WANDER, D_EUCLID, D_BLEND, D_NOTESIZE, D_PADMOVE, D_NOTESEL, D_BOX, D_SIX };
  UiMod* tapM = nullptr; int tapK = -1; std::vector<double> tapTimes;   // TAP on speed rows
  float wanderTapT = -9; int wanderTapK = -1; UiMod* wanderTapM = nullptr;   // the Tracker's column numbers: a second tap soon after sets RANDOM NOTE back to 0
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
