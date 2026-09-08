#ifndef MELEE_NATIVE_SETJMP_H
#define MELEE_NATIVE_SETJMP_H
#include_next <setjmp.h>
typedef jmp_buf melee_native_jmp_buf;
// glibc already owns __jmp_buf; map the console spelling after its headers.
#define __jmp_buf melee_native_jmp_buf
#define __setjmp(env) setjmp(*(env))
#define longjmp(env, value) longjmp(*(melee_native_jmp_buf*)(env), (value))
#endif
