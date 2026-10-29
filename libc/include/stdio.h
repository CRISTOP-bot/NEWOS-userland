/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/libc/include/stdio.h
 * Description: NEWOS libc: unbuffered console output on fd 1/2 (libc/src/stdio.c).
 * Added: 2026-10-17
 */
#ifndef NEWOS_LIBC_STDIO_H
#define NEWOS_LIBC_STDIO_H

/* NEWOS libc: unbuffered console output on fd 1/2 (libc/src/stdio.c).
 *
 * One formatting engine (vsnprintf_inner) backs printf, fprintf, sprintf and
 * snprintf. It honours the flags - + space # 0, field width (also '*'),
 * precision (also '.' and '.*'), the length modifiers hh h l ll z j t L, and
 * the conversions d i u o x X p c s f F e E g G a A %%. Float conversions use
 * the FPU through __fcvt (libc/src/printf_flo.c); everything else stays
 * integer-only.
 *
 * FILE streams are thin fd wrappers (no user-space buffering): fread
 * and fwrite map straight onto read/write/lseek. setvbuf is accepted and
 * ignored because there is nothing to size. */

#include "stddef.h"

/* GCC spells the builtin va_list type __builtin_va_list; TinyCC (the on-board
 * /bin/tcc port) does not implement that builtin and ships its own stdarg.h
 * with a SysV-layout va_list[1]. Use it when compiling with TCC so NEWOS
 * headers work with both compilers. */
#ifdef __TINYC__
#include <stdarg.h>
#else
typedef __builtin_va_list va_list;
#endif

typedef struct {
    int fd;
    int err;
    int eof;
    int ungc;       /* one-byte ungetc pushback (-1 = empty) */
} FILE;

int putchar(int c);
int puts(const char *s);
int printf(const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int vsnprintf(char *dst, size_t n, const char *fmt, va_list ap);
int snprintf(char *dst, size_t n, const char *fmt, ...);
int sprintf(char *dst, const char *fmt, ...);
int vsprintf(char *dst, const char *fmt, va_list ap);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int fprintf(FILE *f, const char *fmt, ...);

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

#define FILENAME_MAX 256
#define BUFSIZ       1024
#define L_tmpnam     FILENAME_MAX

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

FILE *fopen(const char *path, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *f);
FILE *tmpfile(void);
char *tmpnam(char *s);
int fclose(FILE *f);
size_t fread(void *p, size_t s, size_t n, FILE *f);
size_t fwrite(const void *p, size_t s, size_t n, FILE *f);
int fseek(FILE *f, long off, int whence);
long ftell(FILE *f);
void rewind(FILE *f);
char *fgets(char *s, int n, FILE *f);
int fputc(int c, FILE *f);
int fputs(const char *s, FILE *f);
int getc(FILE *f);
int fgetc(FILE *f);
int putc(int c, FILE *f);
int getchar(void);
int ungetc(int c, FILE *f);
int fflush(FILE *f);
int ferror(FILE *f);
int feof(FILE *f);
void clearerr(FILE *f);
void perror(const char *s);
ssize_t getline(char **lp, size_t *cap, FILE *f);
int setvbuf(FILE *f, char *buf, int mode, size_t size);
void setbuf(FILE *f, char *buf);
int rename(const char *from, const char *to);
int remove(const char *path);

#endif

