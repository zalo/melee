#ifndef MELEE_NATIVE_SETJMP_H
#define MELEE_NATIVE_SETJMP_H
#include_next <setjmp.h>
typedef jmp_buf melee_native_jmp_buf;
typedef jmp_buf __jmp_buf;
#define __setjmp(env) setjmp(*(env))
#define longjmp(env, value) longjmp(*(melee_native_jmp_buf*)(env), (value))
#endif
