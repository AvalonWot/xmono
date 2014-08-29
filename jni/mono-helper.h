/*
file: mono-helper.h
author: skeu
description: 对monoAPI的一层包装模块接口
*/
#include <stdint.h>
#include "mono/metadata/assembly.h"
#include "mono/metadata/class.h"
#include "mono/metadata/image.h"
#include "mono/metadata/threads.h"
#include "mono/metadata/mono-debug.h"
#include "mono/metadata/debug-helpers.h"

/*mono头文件中未声明 但实际却导出的函数和结构*/
extern "C" void g_free (void const *p);
extern "C" char *mono_pmip (void *ip);

typedef struct {
    uint32_t eip;          // pc 
    uint32_t ebp;          // fp
    uint32_t esp;          // sp
    uint32_t regs [16];
    double fregs [8];   //arm : 8
} MonoContext;
typedef int (*MonoStackFrameWalk) (MonoDomain*, MonoContext*, MonoJitInfo*, void*);
extern "C" void mono_walk_stack (MonoDomain *domain, void *jit_tls, MonoContext *start_ctx, MonoStackFrameWalk func, void *user_data);

void print_class_name (MonoClass *clazz);

void print_object_class_name (MonoObject *obj);

void print_class_all_methods (MonoClass *clz);

MonoClass *get_base_class (char const *name);

MonoClass *get_class_with_name (char const *image_name, char const *name_space, char const *class_name);

MonoMethod *get_class_method (MonoClass *clz, char const *method_sig);

bool get_obj_field_value (MonoObject *obj, const char *key, void *value);

void set_obj_field_value (MonoObject *obj, char const *val_name, void *value);

char const *get_method_image_name (MonoMethod *method);

char const *get_method_class_name (MonoMethod *method);

char const *get_method_namespace_name (MonoMethod *method);

MonoMethod *get_method_with_token (char const *image_name, uint32_t token);

char const *helper_last_err ();
