#pragma once
// The release version (the VERSION file at the repo root, passed in by CMakeLists.txt) and the
// commit the exe was built from.
#ifndef TS4_VERSION
#define TS4_VERSION "0.0.0"
#endif
#ifndef TS4_GIT
#define TS4_GIT "unknown"
#endif
#define TS4_WIDEN2(x) L##x
#define TS4_WIDEN(x) TS4_WIDEN2(x)
#define TS4_VERSION_W TS4_WIDEN(TS4_VERSION)
