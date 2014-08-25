/*
file: lua-mono.cpp
author: skeu
description: mono API 到 lua包装的C++层函数实现
*/
#include <stdarg.h>
#include <string.h>
#include "lua/lua.hpp"
#include "mono/metadata/assembly.h"
#include "mono/metadata/class.h"
#include "mono/metadata/image.h"
#include "mono/metadata/threads.h"
#include "mono/metadata/mono-debug.h"
#include "mono/metadata/debug-helpers.h"
#include "mono/metadata/profiler.h"

/*
 * mono.new("image_name", "namespace", "class_name")
 * 
 */
static int l_newobj (lua_State *L) {
    if (lua_gettop (L) != 3)
        luaL_error (L, "mono.new : need 3 of argument.");
    char const *args[3];
    args[0] = lua_tostring (L, 1);
    args[1] = lua_tostring (L, 2);
    args[2] = lua_tostring (L, 3);
    for (int i = 0; i < 3; i++)
        args[i] ? 0 : luaL_error (L, "mono.new : arg %d must be string.", i);
    char const *image_name = args[0];
    MonoImage *image = mono_image_loaded (image_name);
    if (!image)
        luaL_error (L, "mono.new : arg 1 %s, not find the image.", image_name);
    MonoClass *clazz = mono_class_from_name (image, args[1], args[2]);
    if (!clazz)
        luaL_error (L, "mono.new : can not find the class.");
    MonoObject *obj = mono_object_new (mono_domain_get (), clazz);
    void *u_obj = lua_newuserdata (L, sizeof (MonoObject*));
    memcpy (u_obj, &obj, sizeof (MonoObject*));
    return 1;
}

static const luaL_Reg R[] = {
    {0, 0}
};

int luaopen_mono (lua_State *L) {
    luaL_newlib (L, R);
    return 1;
}