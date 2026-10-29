/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/libc/src/setjmp.c
 * Description: setjmp/longjmp for x86-64 SysV: save and restore rsp, the return
 * Added: 2026-10-27
 */
/* setjmp/longjmp for x86-64 SysV.
 *
 * jmp_buf layout (one unsigned long each):
 *   [0] rsp   [1] rbx   [2] rbp   [3] r12   [4] r13   [5] r14   [6] r15
 *   [7] return address (the value (%rsp) held at the setjmp call site)
 *
 * longjmp reloads the registers, restores rsp, writes the saved return address
 * over the return slot and lets `ret` resume at the setjmp call site with the
 * requested value. Every field is read while rdi still points at the buffer,
 * before rsp moves: the buffer normally lives in a frame that the restored
 * stack pointer is about to expose again.
 *
 * A longjmp does not unwind C++/cleanup state and does not restore the FPU
 * image; userland shares one FPU state per thread and the kernel saves it on
 * every scheduler handoff, so nothing is lost here. */

#include "../include/setjmp.h"

__asm__(
    ".text\n"
    ".globl setjmp\n"
    ".type  setjmp, @function\n"
    "setjmp:\n"
    "_setjmp:\n"
    "    movq   %rsp, 0(%rdi)\n"
    "    movq   %rbx, 8(%rdi)\n"
    "    movq   %rbp, 16(%rdi)\n"
    "    movq   %r12, 24(%rdi)\n"
    "    movq   %r13, 32(%rdi)\n"
    "    movq   %r14, 40(%rdi)\n"
    "    movq   %r15, 48(%rdi)\n"
    "    movq   (%rsp), %rax\n"
    "    movq   %rax, 56(%rdi)\n"
    "    xorl   %eax, %eax\n"
    "    ret\n"
    ".size setjmp, .-setjmp\n"
    ".globl longjmp\n"
    ".type  longjmp, @function\n"
    "longjmp:\n"
    "_longjmp:\n"
    "    movq   56(%rdi), %rdx\n"      /* saved return address */
    "    movq   0(%rdi), %rcx\n"       /* saved stack pointer  */
    "    testl  %esi, %esi\n"
    "    jne    1f\n"
    "    movl   $1, %esi\n"            /* longjmp(env, 0) yields 1 */
    "1:\n"
    "    movq   8(%rdi), %rbx\n"
    "    movq   16(%rdi), %rbp\n"
    "    movq   24(%rdi), %r12\n"
    "    movq   32(%rdi), %r13\n"
    "    movq   40(%rdi), %r14\n"
    "    movq   48(%rdi), %r15\n"
    "    movq   %rcx, %rsp\n"
    "    movq   %rdx, (%rsp)\n"
    "    movl   %esi, %eax\n"
    "    ret\n"
    ".size longjmp, .-longjmp\n"
    ".globl _setjmp\n"
    ".globl _longjmp\n"
);
