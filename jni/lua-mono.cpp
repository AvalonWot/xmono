/*
file: lua-mono.cpp
author: skeu
description: mono API 到 lua包装的C++层函数实现
*/
#include "lua/lua.hpp"

static const luaL_Reg R[] = {
    {0, 0}
};

int luaopen_mono (lua_State *L) {
    luaL_newlib (L, R);
    return 1;
}