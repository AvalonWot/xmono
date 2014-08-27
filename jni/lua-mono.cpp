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
 * 将由lua传入的参数表转化为void *[]参数表
 * L : 含有参数表的lua_State
 * base : 参数表在lua_State中的起始位置(靠近栈底的一边)
 * method : 参数表将要应用到的MonoMethod*
 */
static void *call_method (lua_State *L, int base, MonoObject *thiz, MonoMethod *method) {
    MonoMethodSignature *sig = mono_method_signature (method);
    if (!sig)
        luaL_error (L, "can not get the method's signature.");
    int n = mono_signature_get_param_count (sig);
    int cur_n = lua_gettop (L) - base + 1;
    if (cur_n != n) {
        /*Fixme : mono_method_full_name 的返回值需要显示 g_free*/
        luaL_error (L, "%s need %d arguments, but get %d.", 
            mono_method_full_name (method, 1), n, cur_n);
    }
    void **args = new void*[n];
    void *iter = 0;
    MonoType *param_type;
    for (int i = 0; (param_type = mono_signature_get_params (sig, &iter)) != 0; i++) {
        /*reference 类型的参数 无论是否加ref关键字, 都是传递改类型对象指针的指针*/
        if (mono_type_is_reference (param_type) || mono_type_is_byref (param_type)) {
            void *p = lua_touserdata (L, base + i);
            if (!p)
                luaL_error (L, "%s : %d arg need a reference type.");
            args[i] = p;
            continue;
        }
        /*剩下的都是value类型*/

    }
}

/*
 * mono.get_class("image_name", "namespace", "class_name")
 * "namespace" : 若没有, 则为空字符串
 * 返回 : MonoClass*(userdata)
 */
static int l_get_class (lua_State *L) {
    char const *image_name = luaL_checkstring (L, 1);
    char const *np = luaL_checkstring (L, 2);
    char const *name = luaL_checkstring (L, 3);
    MonoClass *clazz = get_class_with_name (image_name, np, name);
    if (!clazz)
        luaL_error (L, "%s", helper_last_err ());
    void *u_clazz = lua_newuserdata (L, sizeof (MonoClass*));
    memcpy (u_clazz, clazz, sizeof (MonoClass*));
    return 1;
}

/*
 * mono.new(class, "ctor_sig", ...)
 * class 为MonoClass指针(userdata)
 * "ctor_sig" : 带有函数签名的.ctor 比如 : .ctor(int, int), 
 *              若使用默认构造函数(无参数), 该参数为空字符串
 * ... : 为构造函数所需实参
 * 返回 : MonoObject*(userdata)
 */
static int l_newobj (lua_State *L) {
    MonoClass *clazz = (MonoClass*)lua_touserdata (L, 1);
    luaL_argcheck (L, clazz != 0, 1, "class is null.");
    char const *ctor_sig = luaL_checkstring (L, 2);
    MonoMethod *ctor = get_class_method (clazz, ctor_sig);
    if (!ctor)
        luaL_error (L, "class %s can not find the %s.", mono_class_get_name (clazz), ctor_sig);
    MonoObject *obj = mono_object_new (mono_domain_get (), clazz);
    call_method (L, 3, obj, ctor);
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