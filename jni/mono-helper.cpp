/*
file: mono-helper.cpp
author: skeu
description: 对monoAPI的一层包装模块
*/

#include <pthread.h>
#include <android/log.h>
#include "helper.h"
#include "mono-helper.h"

#define LOG_TAG "XMONODEBUG"
#define LOGD(fmt, args...)  __android_log_print(ANDROID_LOG_DEBUG,LOG_TAG, fmt, ##args)
#define LOGE(fmt, args...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG, fmt, ##args)


static pthread_key_t helper_err_key;
static pthread_once_t helper_err_key_once = PTHREAD_ONCE_INIT;
static void helper_err_free (void *p) {
    MemWriter *writer = (MemWriter*)p;
    delete writer;
}

static void make_key() {
    (void) pthread_key_create (&helper_err_key, helper_err_free);
}

/*返回一个类型是为了在一些特殊的语句中使用 如 ?:*/
static int set_helper_err (char const *fmt, ...) {
    va_list args;
    va_start (args, fmt);
    MemWriter *writer;
    (void) pthread_once (&helper_err_key_once, make_key);
    if ((writer = (MemWriter*)pthread_getspecific (helper_err_key)) == 0) {
        writer = new MemWriter ();
        (void) pthread_setspecific (helper_err_key, writer);
    }
    writer->clear ();
    writer->vsprintf (fmt, args);
    va_end (args);
    return 0;
}

char const *helper_last_err () {
    MemWriter *writer;
    (void) pthread_once (&helper_err_key_once, make_key);
    if ((writer = (MemWriter*)pthread_getspecific (helper_err_key)) == 0) {
        writer = new MemWriter ();
        (void) pthread_setspecific (helper_err_key, writer);
    }
    if (writer->getBuffSize () == 0)
        return "";
    return (char*)writer->getBuffPtr ();
}

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

MonoClass *get_base_class (char const *name) {
    for (int i = 0; i < sizeof (B) / sizeof (BaseClass); i++) {
        if (strcmp (name, B[i].name) == 0)
            return B[i].func ();
    }
    return 0;
}

void print_class_name (MonoClass *clazz) {
    LOGD ("class name : %s", mono_class_get_name (clazz));
}

void print_object_class_name (MonoObject *obj) {
    MonoClass *clazz = mono_object_get_class (obj);
    print_class_name (clazz);
}

void print_class_all_methods (MonoClass *clz) {
    void *it = 0;
    MonoMethod *m;
    while (m = mono_class_get_methods (clz, &it)) {
        char *name = mono_method_full_name (m, 1);
        LOGD ("%s", name);
        g_free (name);
    }
}

MonoClass *get_class_with_name (char const *image_name, char const *name_space, char const *class_name) {
    MonoClass *clazz = get_base_class (class_name);
    if (clazz)
        return clazz;
    MonoImage *image = mono_image_loaded (image_name);
    if (!image) {
        set_helper_err ("image : %s, can not find.", image_name);
        return 0;
    }
    clazz = mono_class_from_name (image, name_space, class_name);
    if (!clazz) {
        set_helper_err ("class : %s, can not find.", class_name);
        return 0;
    }
    return clazz;
}

MonoMethod *get_class_method (MonoClass *clz, char const *method_sig) {
    MonoMethodDesc *mdesc = mono_method_desc_new (method_sig, 0);
    if (!mdesc) {
        LOGE ("mono_method_desc_new err\n");
        return 0;
    }
    LOGD ("mono_method_desc_new be called!");
    MonoMethod *method = mono_method_desc_search_in_class (mdesc, clz);
    if (!method) {
        LOGE ("can not find method : %s\n", method_sig);
        return 0;
    }
    LOGD ("mono_method_desc_search_in_class be called!");
    return method;
}

bool get_obj_field_value (MonoObject *obj, const char *key, void *value) {
    MonoClass *clazz = mono_object_get_class (obj);
    if (!clazz) {
        set_helper_err ("can not get the obj's class");
        return false;
    }
    MonoClassField *field = mono_class_get_field_from_name (clazz, key);
    if (!field) {
        set_helper_err ("can not get the %s.%s", mono_class_get_name (clazz), key);
        return false;
    }
    void *re = 0;
    mono_field_get_value (obj, field, value);
}

void set_obj_field_value (MonoObject *obj, char const *val_name, void *value) {
    MonoClass *clz = mono_object_get_class (obj);
    MonoClassField *field = mono_class_get_field_from_name (clz, val_name);
    mono_field_set_value (obj, field, value);
}

char const *get_method_image_name (MonoMethod *method) {
    MonoClass *clazz = mono_method_get_class (method);
    if (!clazz) return 0;
    MonoImage *image = mono_class_get_image (clazz);
    if (!image) return 0;
    return mono_image_get_name (image);
}

char const *get_method_class_name (MonoMethod *method) {
    MonoClass *clazz = mono_method_get_class (method);
    return mono_class_get_name (clazz);
}

char const *get_method_namespace_name (MonoMethod *method) {
    MonoClass *clazz = mono_method_get_class (method);
    return mono_class_get_namespace (clazz);
}

MonoMethod *get_method_with_token (char const *image_name, uint32_t token) {
    MonoImage *image = mono_image_loaded (image_name);
    if (!image) {
        set_helper_err ("dont have the image : %s", image_name);
        return 0;
    }
    MonoMethod *method = (MonoMethod*)mono_ldtoken (image, token, 0, 0);
    if (!method) {
        set_helper_err ("dont have the method, token");
        return 0;
    }
    return method;
}