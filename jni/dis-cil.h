/*
file: dis-cil.h
author: skeu
description: 反汇编cil模块接口
*/

#ifndef DIS_CIL_H
#define DIS_CIL_H

#include "helper.h"
bool disassemble_cil (MonoImage *m, MonoMethodHeader *mh, MemWriter *writer);

#endif