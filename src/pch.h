#pragma once

#include <RE/Fallout.h>
#include <F4SE/F4SE.h>
#include <REX/REX.h>

// 常用宏定义 (ConditionBoyAE 风格)
#define MAKE_EXE_VERSION_EX(major, minor, build, sub) ((((major) & 0xFF) << 24) | (((minor) & 0xFF) << 16) | (((build) & 0xFFF) << 4) | ((sub) & 0xF))
#define MAKE_EXE_VERSION(major, minor, build)         MAKE_EXE_VERSION_EX(major, minor, build, 0)

using namespace std::literals;
using namespace REL;
using namespace REX;
using namespace F4SE;