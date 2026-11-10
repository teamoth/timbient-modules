# Cross-building the 32-bit Windows version on Linux with MinGW-w64, which still supports Windows 7
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR i686)
set(CMAKE_C_COMPILER i686-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER i686-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER i686-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/i686-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# Windows 7 as the oldest it may ask for; SSE2 (every computer that runs Windows 7 has it) for speed and denormal protection
set(CMAKE_C_FLAGS_INIT "-D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -msse2 -mfpmath=sse")
set(CMAKE_CXX_FLAGS_INIT "-D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -msse2 -mfpmath=sse")
# one .exe with nothing else needed beside it
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
