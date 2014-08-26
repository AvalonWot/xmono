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

struct BaseClass {
    char const *name;
    MonoClass *(*func)();
};

static const BaseClass B[] = {
    {"object", mono_get_object_class},
    {"byte", mono_get_byte_class},
    {"void", mono_get_void_class},
    {"boolean", mono_get_boolean_class},
    {"sbyte", mono_get_sbyte_class},
    {"int16", mono_get_int16_class},
    {"uint16", mono_get_uint16_class},
    {"int32", mono_get_int32_class},
    {"uint32", mono_get_uint32_class},
    {"intptr", mono_get_intptr_class},
    {"uintptr", mono_get_uintptr_class},
    {"int64", mono_get_int64_class},
    {"uint64", mono_get_uint64_class},
    {"single", mono_get_single_class},
    {"double", mono_get_double_class},
    {"char", mono_get_char_class},
    {"string", mono_get_string_class},
    {"enum", mono_get_enum_class},
    {"array", mono_get_array_class},
    {"thread", mono_get_thread_class},
    {"exception", mono_get_exception_class}
};

static MonoClass *get_base_class (char const *name) {
    for (int i = 0; i < sizeof (B) / sizeof (BaseClass); i++) {
        if (strcmp (name, B[i].name) == 0)
            return B[i].func ();
    }
    return 0;
}

/*
 * 该函数不用检测返回值
 */
static MonoClass *get_class_with_name (lua_State *L, char const *image_name, char const *np, char const *name) {
    MonoClass *clazz = get_base_class (name);
    if (clazz)
        return clazz;
    MonoImage *image = mono_image_loaded (image_name);
    if (!image)
        luaL_error (L, "image : %s, can not find.", image_name);
    clazz = mono_class_from_name (image, np, name);
    if (!clazz)
        luaL_error (L, "class : %s, can not find.");
    return clazz;
}

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
    MonoClass *clazz = get_class_with_name (L, args[0], args[1], args[2]);
    MonoObject *obj = mono_object_new (mono_domain_get (), clazz);
    void *u_obj = lua_newuserdata (L, sizeof (MonoObject*));
    memcpy (u_obj, &obj, sizeof (MonoObject*));
    return 1;
}

static const luaL_Reg R[] = {
    {"new", l_newobj},
    {0, 0}
};

int luaopen_mono (lua_State *L) {
    luaL_newlib (L, R);
    return 1;
}