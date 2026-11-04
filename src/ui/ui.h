// Timbient native: the screen side. It draws the patch and turns mouse and touch into commands for the engine.
// It never touches the running patch directly, only the values the engine publishes.
#pragma once
#include "../engine/engine.h"
#include "canvas.h"
#include <string>
#include <vector>

namespace tb {

struct UiMod { Module* m = nullptr; int x = 0, y = 0; bool fold = false; };
struct UiCable { int from, fromPort, to, toPort, toParam; PType type; };

class Ui {
public:
  explicit Ui(Engine& e) : eng(e) {}
  Engine& eng;
  std::vector<UiMod> mods; std::vector<UiCable> cables;
  int viewX = 0, viewY = 0, nextId = 1;
  std::string statusMsg; float statusT = 0;
  bool wantFullscreen = false, isFullscreen = false;

  // building a patch
  UiMod* add(const std::string& type, int x, int y);
  void connect(UiMod* a, const std::string& outPort, UiMod* b, const std::string& inPort);
  void modulate(UiMod* src, UiMod* dst, const std::string& paramKey);
  void set(UiMod* m, const std::string& k, float v);
  void demoPatch();

  // each frame
  void draw(Canvas& c, float dt);
  // input, in canvas pixels
  void pointerDown(float x, float y);
  void pointerMove(float x, float y);
  void pointerUp(float x, float y);
  void status(const std::string& s, float t = 2) { statusMsg = s; statusT = t; }

private:
  struct Geom { int jr, vy, py, h, w; };
  Geom geom(const UiMod& u) const;
  UiMod* hit(int wx, int wy, int& lx, int& ly);
  void drawModule(Canvas& c, UiMod& u);
  void drawCable(Canvas& c, int ax, int ay, int bx, int by, int col);
  int jackY(int k) const { return 8 + k * 7 + 3; }
  struct Drag { int kind = 0; UiMod* m = nullptr; int ox = 0, oy = 0, k = -1; float sx = 0, sy = 0, n0 = 0; int vx0 = 0, vy0 = 0; } drag;
  std::vector<std::pair<int, int>> fsRect;
};

}  // namespace tb
