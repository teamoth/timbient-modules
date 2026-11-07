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
