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
 * 
 * 该调用只针对Static methods, 和 non-Static methods, 不针对generic methods
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
                luaL_error (L, "%s : %d arg need a reference type.", mono_method_full_name (method, 1), i);
            args[i] = p;
            continue;
        }
        /*剩下的都是value类型*/
        switch (mono_type_get_type (param_type)) {
            case MONO_TYPE_BOOLEAN: {
                int b = lua_toboolean (L, base + i);
                MonoObject *obj = mono_value_box (mono_domain_get (), mono_get_boolean_class (), &b);
                args[i] = mono_object_unbox (obj);
                break;
            }
            case MONO_TYPE_CHAR: {
                /*char 用int来表示*/
                int b = luaL_checkint (L, base + i);
                MonoObject *obj = mono_value_box (mono_domain_get (), mono_get_char_class (), &b);
                args[i] = mono_object_unbox (obj);
                break;
            }
            case MONO_TYPE_I1: {
                int b = luaL_checkint (L, base + i);
                MonoObject *obj = mono_value_box (mono_domain_get (), mono_get_sbyte_class (), &b);
                args[i] = mono_object_unbox (obj);
                break;
            }
            case MONO_TYPE_U1: {
                unsigned long l = luaL_checkunsigned (L, base + i);
                MonoObject *obj = mono_value_box (mono_domain_get (), mono_get_byte_class (), &l);
                args[i] = mono_object_unbox (obj);
                break;
            }
            case MONO_TYPE_I2: {
                int b = luaL_checkint (L, base + i);
                MonoObject *obj = mono_value_box (mono_domain_get (), mono_get_int16_class (), &b);
                args[i] = mono_object_unbox (obj);
                break;
            }
            case MONO_TYPE_U2: {
                unsigned long l = luaL_checkunsigned (L, base + i);
                MonoObject *obj = mono_value_box (mono_domain_get (), mono_get_uint16_class (), &l);
                args[i] = mono_object_unbox (obj);
                break;
            }
            case MONO_TYPE_I4: {
                int b = luaL_checkint (L, base + i);
                MonoObject *obj = mono_value_box (mono_domain_get (), mono_get_int32_class (), &b);
                args[i] = mono_object_unbox (obj);
                break;
            }
            case MONO_TYPE_VALUETYPE:
            case MONO_TYPE_U4: {
                unsigned long l = luaL_checkunsigned (L, base + i);
                MonoObject *obj = mono_value_box (mono_domain_get (), mono_get_uint32_class (), &l);
                args[i] = mono_object_unbox (obj);
                break;
            }
            case MONO_TYPE_I8: {
                void *u = lua_touserdata (L, base + i);
                if (!u)
                    luaL_error (L, "%s : %d arg need Int64.", mono_method_full_name (method, 1), i);
                MonoObject *obj = mono_value_box (mono_domain_get (), mono_get_int64_class (), u);
                args[i] = mono_object_unbox (obj);
                break;
            }
            case MONO_TYPE_U8: {
                void *u = lua_touserdata (L, base + i);
                if (!u)
                    luaL_error (L, "%s : %d arg need UInt64.", mono_method_full_name (method, 1), i);
                MonoObject *obj = mono_value_box (mono_domain_get (), mono_get_uint64_class (), u);
                args[i] = mono_object_unbox (obj);
                break;
            }
            case MONO_TYPE_R4: {
                /*这里的精度损失由使用lua的人负责*/
                float f = (float)lua_tonumber (L, base + i);
                MonoObject *obj = mono_value_box (mono_domain_get (), mono_get_single_class (), &f);
                args[i] = mono_object_unbox (obj);
                break;
            }
            case MONO_TYPE_R8: {
                double d = (double)lua_tonumber (L, base + i);
                MonoObject *obj = mono_value_box (mono_domain_get (), mono_get_double_class (), &d);
                args[i] = mono_object_unbox (obj);
                break;
            }
            case MONO_TYPE_I: {
                void *u = lua_touserdata (L, base + i);
                if (!u)
                    luaL_error (L, "%s : %d arg need IntPtr.", mono_method_full_name (method, 1), i);
                MonoObject *obj = mono_value_box (mono_domain_get (), mono_get_intptr_class (), u);
                args[i] = mono_object_unbox (obj);
                break;
            }
            case MONO_TYPE_U: {
                void *u = lua_touserdata (L, base + i);
                if (!u)
                    luaL_error (L, "%s : %d arg need UIntPtr.", mono_method_full_name (method, 1), i);
                MonoObject *obj = mono_value_box (mono_domain_get (), mono_get_uintptr_class (), u);
                args[i] = mono_object_unbox (obj);
                break;
            }
            case MONO_TYPE_MVAR:
                luaL_error (L, "generic method dont be supported.");
            case MONO_TYPE_PTR:
                luaL_error (L, "dont support the ptr type.");
            default:
                luaL_error (L, "unknow method args type : 0x%02X", mono_type_get_type (param_type));
        }
    }
    MonoObject *ex = 0;
    MonoObject *ret = mono_runtime_invoke (method, thiz, args, &ex);
    if (ex)
        luaL_error (L, "An exception was thrown in method.");
    //MonoType *ret_type = mono_signature_get_return_type (sig);
    return ret;
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
    lua_pushlightuserdata (L, obj);
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