// Timbient native: sessions, in the same file format as the web version (so exported patches open here too)
#pragma once
#include "json.h"
#include "ui.h"
#include <string>
#include <vector>

namespace tb {

// the patch on screen as a session (the web version's "data" object)
Json serializePatch(Ui& ui);
// rebuild the patch from a session; returns a note about anything that couldn't come across (or "")
std::string loadPatch(Ui& ui, const Json& data);
// undo / redo: change the patch on screen into a remembered one, touching only what differs, so sequencers keep
// their place and sounds stay loaded. Sound places that changed are listed (module, place) for the app to reload
void applyPatch(Ui& ui, const Json& data, std::vector<std::pair<UiMod*, int>>& soundsChanged);
// GROUPS: some of the patch's modules (positions from their top left corner, which is x0, y0) and the cables between them.
// OUTPUT (fixed) goes in too, so cables into it are kept; empty (not an object) if there's nothing but OUTPUT
Json groupOf(Ui& ui, const std::vector<int>& ids, int& x0, int& y0);
// add a group to the patch on screen, its top left corner at (wx, wy), as one change (as undo does: nothing else stops or reloads).
// New ids; a fixed module in it means the session's own. The new modules become the selection; returns their ids
std::vector<int> addGroup(Ui& ui, const Json& g, int wx, int wy, std::vector<std::pair<UiMod*, int>>& soundsChanged, bool& unknown);
// accepts a plain patch, {timbient:'patch', data}, or {timbient:'sessions', sessions:[...]} (the first is loaded)
const Json* patchIn(const Json& file);

struct SavedSession { std::string name; double t = 0; Json data; };
struct SessionStore {
  std::string dir;                                  // the app's own folder for saved things
  std::vector<SavedSession> list;
  void load();                                      // read sessions.json
  bool save() const;                                // write sessions.json
  bool writeAutosave(const Json& data) const;       // the patch as it is now (restored with RESTORE LAST WORK)
  bool readAutosave(Json& out) const;
  std::string path(const char* f) const { return dir + f; }
};

std::string readFile(const std::string& path);
bool writeFile(const std::string& path, const std::string& text);
std::string stamp(double unixSeconds);

}  // namespace tb
