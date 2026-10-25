// Force-included by host tools (tools/optimize_menu_textures.py) that compile
// game sources on the PC: the data root and POSIX mkdir the Vita build provides.
#pragma once
#ifndef MKW_DATA_ROOT
#define MKW_DATA_ROOT "build/host-tools/data"
#endif
#ifdef _WIN32
#include <direct.h>
#include <sys/stat.h>
inline int mkdir(const char* path,int){return _mkdir(path);}
#endif
