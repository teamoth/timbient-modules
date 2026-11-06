// Timbient Modules: lets Microsoft's compiler read the GCC-only attributes in these files.
#pragma once
#ifdef _MSC_VER
#define __attribute__(x)
#define _USE_MATH_DEFINES
#endif
