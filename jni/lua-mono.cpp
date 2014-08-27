/*
file: lua-mono.cpp
author: skeu
description: mono API 到 lua包装的C++层函数实现
*/
/*
 * 该模块的函数可能会mono-helper中的函数会有功能划分上的矛盾,
 * 原则上 该模块创建的函数, 只应该检测参数, 然后交由mono-helper
 * 中的函数来执行逻辑, 获取输出并返回.
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
#include "mono-helper.h"

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
 * mono.get_class("image_name", "namespace", "class_name")
 */
static int l_get_class (lua_State *L) {
    char const *image_name = luaL_checkstring (L, 1);
    char const *np = luaL_checkstring (L, 2);
    char const *name = luaL_checkstring (L, 3);
    MonoClass *clazz = get_class_with_name (L, image_name, np, name);
    void *u_clazz = lua_newuserdata (L, sizeof (MonoClass*));
    memcpy (u_clazz, clazz, sizeof (MonoClass*));
    return 1;
}

/*
 * mono.new(class, "ctor_sig", ...)
 */
static int l_newobj (lua_State *L) {
    MonoClass *clazz = (MonoClass*)lua_touserdata (L, 1);
    luaL_argcheck (L, clazz != 0, 1, "class is null.");
    char const *ctor_sig = luaL_checkstring (L, 2);
    MonoMethod *ctor = get_class_method (clazz, ctor_sig);
    if (!ctor)
        luaL_error ("class %s can not find the %s.", mono_class_get_name (clazz), ctor_sig);
    MonoMethodSignature *sig = mono_method_signature (ctor);
    int n = mono_signature_get_param_count (sig);
    if (lua_gettop (L) - 2 != n) {
        luaL_error ("class %s's %s need %d arguments, but get %d.", 
            mono_class_get_name (clazz), ctor_sig, n, lua_gettop (L) - 2);
    }
    MonoObject *obj = mono_object_new (mono_domain_get (), clazz);
    void *u_obj = lua_newuserdata (L, sizeof (MonoObject*));
    memcpy (u_obj, &obj, sizeof (MonoObject*));
    return 1;
}

static const luaL_Reg R[] = {
    {"new", l_newobj},
    {"get_class", l_get_class},
    {0, 0}
};

int luaopen_mono (lua_State *L) {
    luaL_newlib (L, R);
    return 1;
}