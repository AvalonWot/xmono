/*
file: hook.h
author: skeu
description: arm架构下 hook 的接口
*/
#ifndef HOOK_H
#define HOOK_H

int arm_hook (void *org, void *dst, void **trampo);
void arm_unhook (void *org, void *trampo);

#endif