/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/libc/include/setjmp.h
 * Description: Non-local jumps: the six SysV callee-saved registers, the stack
 *   pointer and the return address. No signal mask (NEWOS has no signal
 *   delivery yet), so sigsetjmp is not provided.
 * Added: 2026-10-27
 */
#ifndef NEWOS_LIBC_SETJMP_H
#define NEWOS_LIBC_SETJMP_H

/* Non-local jumps: the six SysV callee-saved registers, the stack pointer and
 * the return address. No signal mask (NEWOS has no signal delivery yet), so
 * sigsetjmp is not provided. */

typedef unsigned long jmp_buf[8];
typedef jmp_buf _jmp_buf;

int setjmp(jmp_buf env);
int _setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val) __attribute__((__noreturn__));
void _longjmp(jmp_buf env, int val) __attribute__((__noreturn__));

#endif
