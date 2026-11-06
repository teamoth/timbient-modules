// Timbient native: sessions (see session.h)
#include "session.h"
#include <algorithm>
#include "../engine/params.h"
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace tb {

std::string readFile(const std::string& path) { std::ifstream f(path, std::ios::binary); if (!f) return ""; std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
bool writeFile(const std::string& path, const std::string& text) {
  const std::string tmp = path + ".tmp"; { std::ofstream f(tmp, std::ios::binary); if (!f) return false; f << text; if (!f) return false; }
  std::remove(path.c_str()); return std::rename(tmp.c_str(), path.c_str()) == 0;   // write then swap, so a crash never leaves half a file
}
std::string stamp(double t) {
  const std::time_t tt = (std::time_t)t; std::tm lt{};
#ifdef _WIN32
  localtime_s(&lt, &tt);
#else
  localtime_r(&tt, &lt);
#endif
  static const char* M[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  char b[32]; std::snprintf(b, 32, "%d %s %02d:%02d", lt.tm_mday, M[lt.tm_mon], lt.tm_hour, lt.tm_min); return b;
}

static const char* tyName(PType t) { return t == PType::Audio ? "audio" : t == PType::Trig ? "trig" : "mod"; }

Json serializePatch(Ui& ui) {
  Json d = Json::object(); d.set("v", Json::number(2));
  Json key = Json::object(); key.set("root", Json::number(ui.eng.key.root.load())); key.set("scale", Json::number(ui.eng.key.scale.load())); d.set("key", key);
  d.set("bpm", Json::number(ui.eng.tempo.bpm.load()));
  Json mods = Json::array();
  for (auto& up : ui.mods) { UiMod& u = *up; const ModuleDef* def = u.m->def; Json m = Json::object();
    m.set("id", Json::number(u.m->id)); m.set("type", Json::string(def->type)); m.set("x", Json::number(u.x)); m.set("y", Json::number(u.y));
    Json p = Json::object(); for (size_t k = 0; k < def->params.size(); k++) if (!def->params[k].act) p.set(def->params[k].key, Json::number(u.m->p[k].load()));
    if (u.m->seq) {   // the Tracker's steps: [note, velocity] or null, up to the last one used
      int last = -1; for (int i = 0; i < Module::SEQ_MAX; i++) if (u.m->seq[i * 2].load() >= 0) last = i;
      Json sq = Json::array(); for (int i = 0; i <= last; i++) { const float nt = u.m->seq[i * 2].load(); if (nt < 0) { sq.push(Json()); continue; }
        Json st = Json::array(); st.push(Json::number(nt)); st.push(Json::number(u.m->seq[i * 2 + 1].load())); sq.push(st); }
      p.set("seq", sq); }
    if (!u.snd[0].id.empty()) p.set("audio", Json::string(u.snd[0].id));   // the first sound, as the web version keeps it
    { int last = -1; for (int k = 0; k < UiMod::SLOTS; k++) if (!u.snd[k].id.empty()) last = k;
      if (last > 0 || (last == 0 && (u.snd[0].root != 60 || !u.snd[0].name.empty()))) { Json a = Json::array();   // every sound: [id, root, name] or null
        for (int k = 0; k <= last; k++) { if (u.snd[k].id.empty()) { a.push(Json()); continue; } Json e = Json::array(); e.push(Json::string(u.snd[k].id)); e.push(Json::number(u.snd[k].root)); e.push(Json::string(u.snd[k].name)); a.push(e); }
        p.set("sounds", a); } }
    if (!u.dev.empty()) p.set("dev", Json::string(u.dev));
    m.set("p", p);
    m.set("mute", Json::boolean(u.m->mute.load())); m.set("solo", Json::boolean(u.m->solo.load())); m.set("fold", Json::boolean(u.fold)); mods.push(m); }
  d.set("mods", mods);
  Json cs = Json::array();
  for (auto& c : ui.cables) { UiMod *a = ui.byId(c.from), *b = ui.byId(c.to); if (!a || !b) continue; Json j = Json::object();
    j.set("f", Json::number(c.from)); j.set("fp", Json::string(a->m->def->outs[c.fromPort].name)); j.set("t", Json::number(c.to));
    if (c.type == PType::Mod) { j.set("tq", Json::string(b->m->def->params[c.toParam].key)); } else j.set("tp", Json::string(b->m->def->ins[c.toPort].name));
    j.set("ty", Json::string(tyName(c.type))); cs.push(j); }
  d.set("cables", cs);
  return d;
}

const Json* patchIn(const Json& f) {
  if (f.str("timbient") == "patch") return f.get("data");
  if (f.str("timbient") == "sessions") { const Json* s = f.get("sessions"); if (s && s->t == Json::ARR && !s->a.empty()) return s->a[0].get("data"); return nullptr; }
  return f.get("mods") ? &f : nullptr;
}

std::string loadPatch(Ui& ui, const Json& data0) {
  Json data = data0;   // a copy we can adjust for older sessions
  ui.clearPatch();
  if (const Json* k = data.get("key")) { if (k->get("root")) ui.eng.key.root.store((int)k->num("root")); if (k->get("scale")) ui.eng.key.scale.store((int)k->num("scale")); }
  if (data.num("bpm", 0) > 0) ui.eng.tempo.bpm.store((float)data.num("bpm"));
  Json* mods = data.getm("mods"); Json* cables = data.getm("cables"); if (!mods || mods->t != Json::ARR) return "THAT FILE HAS NO MODULES";
  // older web sessions: modules that were merged or replaced
  for (auto& m : mods->a) {
    const std::string ty = m.str("type"); Json* p = m.getm("p"); if (!p) { m.set("p", Json::object()); p = m.getm("p"); }
    if (ty == "ping") { const double r = p->num("ratio", 3.5), dc = p->num("decay", 0.35), ix = p->num("index", 0.5); m.set("type", Json::string("fm2"));
      p->set("ratio", Json::number(std::round(r * 2) / 2)); p->set("index", Json::number(std::min(10.0, (0.2 + ix * 4) / r * 3))); p->set("idec", Json::number(dc * 0.4)); p->set("isus", Json::number(0)); p->set("hold", Json::number(0.02)); p->set("rel", Json::number(dc * 1.5)); }
    if (ty == "drone") { m.set("type", Json::string("pad")); }
    if ((ty == "clock" || ty == "tracker") && !p->get("sync")) p->set("sync", Json::number(0));   // older sessions keep their own tempos
  }
  std::map<int, UiMod*> idmap; std::set<std::string> skipped; int maxId = 0;
  for (auto& m : mods->a) {
    const std::string ty = m.str("type"); const ModuleDef* def = findDef(ty);
    if (!def) { skipped.insert(ty); continue; }
    UiMod* u = nullptr;
    if (def->fixed) { for (auto& up : ui.mods) if (up->m->def == def) u = up.get(); if (u) { u->x = (int)m.num("x"); u->y = (int)m.num("y"); } }
    if (!u) u = ui.add(ty, (int)m.num("x"), (int)m.num("y"));
    if (!u) { skipped.insert(ty); continue; }
    if (const Json* p = m.get("p")) for (size_t k = 0; k < def->params.size(); k++) { const Json* v = p->get(def->params[k].key); if (v && v->t == Json::NUM) u->m->p[k].store((float)v->n); }
    if (const Json* p = m.get("p")) if (const Json* sq = p->get("seq")) if (sq->t == Json::ARR && u->m->seq)
      for (size_t i = 0; i < sq->a.size() && i < (size_t)Module::SEQ_MAX; i++) { const Json& st = sq->a[i];
        if (st.t == Json::ARR && st.a.size() >= 1 && st.a[0].t == Json::NUM) { u->m->seq[i * 2].store((float)st.a[0].n); u->m->seq[i * 2 + 1].store(st.a.size() > 1 && st.a[1].t == Json::NUM ? (float)st.a[1].n : 0.8f); } }
    if (const Json* p = m.get("p")) { const std::string aid = p->str("audio"); if (!aid.empty()) u->snd[0].id = aid;
      if (const Json* a = p->get("sounds")) if (a->t == Json::ARR) for (size_t k = 0; k < a->a.size() && k < (size_t)UiMod::SLOTS; k++) { const Json& e = a->a[k];
        if (e.t == Json::ARR && e.a.size() >= 1 && e.a[0].t == Json::STR) { u->snd[k].id = e.a[0].s; if (e.a.size() > 1 && e.a[1].t == Json::NUM) u->snd[k].root = (float)e.a[1].n; if (e.a.size() > 2 && e.a[2].t == Json::STR) u->snd[k].name = e.a[2].s;
          u->m->roots[k].store(u->snd[k].root); } } u->dev = p->str("dev"); }
    bool stopped = false;
    if (const Json* p = m.get("p")) {   // older sessions: a RUN setting became the footer's ON/OFF; Orbits gained RANGE
      if ((ty == "clock" || ty == "tracker") && p->get("run") && p->get("run")->t == Json::NUM && p->get("run")->n < 0.5) stopped = true;
      if (ty == "orbits" && !p->get("range") && p->get("rings")) u->m->p[def->paramIndex("range")].store((float)p->num("rings", 5)); }
    u->m->mute.store(m.truthy("mute") || stopped); u->m->solo.store(m.truthy("solo") && def->cat == Cat::Voice); u->fold = m.truthy("fold");   // only voices solo now
    idmap[(int)m.num("id")] = u; maxId = std::max(maxId, (int)m.num("id"));
  }
  if (!findDef("out") || std::none_of(ui.mods.begin(), ui.mods.end(), [](const std::unique_ptr<UiMod>& u) { return u->m->def->type == "out"; })) ui.add("out", 340, 60);
  int lost = 0;
  if (cables && cables->t == Json::ARR) for (auto& c : cables->a) {
    auto fi = idmap.find((int)c.num("f")), ti = idmap.find((int)c.num("t")); if (fi == idmap.end() || ti == idmap.end()) { lost++; continue; }
    UiMod *a = fi->second, *b = ti->second; const std::string ty = c.str("ty"), fp = c.str("fp"); std::string tp = c.str("tp");
    if (b->m->def->type == "out" && tp == "IN") tp = "A"; if (tp == "SWELL") tp = "TRIG";
    if (ty == "mod") { const int pk = b->m->def->paramIndex(c.str("tq")); if (pk < 0 || !ui.modulate(a, b, pk)) lost++; }
    else if (!ui.connect(a, a->m->def->outIndex(fp), b, b->m->def->inIndex(tp))) lost++;
  }
  std::string note;
  const bool fm = skipped.erase("fm2") > 0;
  if (!skipped.empty()) { note = "NOT IN THE NATIVE VERSION YET: "; bool first = true; for (auto& s : skipped) { const ModuleDef* d = findDef(s); std::string nm = d ? d->name : s; for (auto& ch : nm) ch = (char)std::toupper((unsigned char)ch); note += (first ? "" : ", ") + nm; first = false; } }
  if (fm) note += std::string(note.empty() ? "" : ". ") + "FM 2-OP HAS BEEN REMOVED, SO IT WAS LEFT OUT";
  return note;
}

void SessionStore::load() {
  list.clear(); Json f; if (!Json::parse(readFile(path("sessions.json")), f)) return;
  const Json* s = f.get("sessions"); if (!s || s->t != Json::ARR) return;
  for (auto& e : s->a) { const Json* d = e.get("data"); if (!d) continue; list.push_back({e.str("name", "SESSION"), e.num("t", 0) / 1000.0, *d}); }
}
bool SessionStore::save() const {
  Json f = Json::object(); f.set("timbient", Json::string("sessions")); Json a = Json::array();
  for (auto& s : list) { Json e = Json::object(); e.set("name", Json::string(s.name)); e.set("t", Json::number(s.t * 1000.0)); e.set("data", s.data); a.push(e); }
  f.set("sessions", a); return writeFile(path("sessions.json"), f.dump());
}
bool SessionStore::writeAutosave(const Json& d) const { return writeFile(path("autosave.json"), d.dump()); }
bool SessionStore::readAutosave(Json& out) const { return Json::parse(readFile(path("autosave.json")), out) && out.get("mods"); }

}  // namespace tb
