// HOVER HELP: the texts ("TITLE|what it does"), looked up for what the pointer rests on
#pragma once
#include "../engine/engine.h"
#include <string>

namespace tb {
std::string helpModule(const std::string& type);                    // a module, from its title bar
std::string helpParam(const ModuleDef* d, const ParamDef& q);       // a setting (its row)
std::string helpPort(const ModuleDef* d, bool out, int k);          // a socket
std::string helpOther(const std::string& what);                     // M, S, B, ON, FOLD, SWITCH, the top bar...
}
