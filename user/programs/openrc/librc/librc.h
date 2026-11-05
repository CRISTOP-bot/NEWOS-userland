/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/openrc/librc/librc.h
 * Description: librc: shared OpenRC core for the NEWOS service manager.
 * Added: 2026-10-29
 */
/* librc: shared core of the NEWOS OpenRC implementation.
 *
 * This is the NEWOS port of OpenRC's librc utility layer. The data model,
 * on-disk layout (/etc/init.d, /etc/runlevels, /etc/conf.d, /etc/rc.conf,
 * /run/openrc) and the depend() vocabulary (need/use/before/after/provide)
 * follow upstream OpenRC semantics. The one deliberate deviation is that
 * runlevels hold indirection files instead of symlinks, because the NEWOS
 * VFS has no symlinks; rc-update writes the target path as the file body
 * and consumers resolve it, which preserves the add/show/remove contract.
 */

#ifndef LIBRC_H
#define LIBRC_H

#include "../../../lib/nshlib.h"

#define RC_MAX_PATH     512
#define RC_MAX_NAME     64
#define RC_MAX_LINE     256
#define RC_MAX_VARS     64
#define RC_MAX_SERVICES 48
#define RC_MAX_DEPS     8

#define RC_CONF_PATH    "/etc/rc.conf"
#define RC_RUNLEVEL_DIR "/etc/runlevels"
#define RC_INITD_DIR    "/etc/init.d"
#define RC_CONFD_DIR    "/etc/conf.d"
#define RC_STATE_DIR    "/run/openrc"
#define RC_STARTED_DIR  "/run/openrc/started"
#define RC_CURRENT      "/run/openrc/current"

#define RC_VERSION      "0.1.0 (NEWOS OpenRC)"

/* One init script discovered in a runlevel, with its parsed depend()
 * keywords. Strings are ';' separated lists ("" when absent). */
typedef struct {
    char name[RC_MAX_NAME];
    char path[RC_MAX_PATH];         /* resolved /etc/init.d path */
    char need[RC_MAX_LINE];
    char use[RC_MAX_LINE];
    char before[RC_MAX_LINE];
    char after[RC_MAX_LINE];
    char provide[RC_MAX_LINE];
    char keyword[RC_MAX_LINE];      /* environment keywords (upstream) */
    int  has_start;                 /* script defines start() { */
    int  has_stop;                  /* script defines stop() { */
} rc_service_t;

/* ---- filesystem helpers ---------------------------------------------- */
int  rc_file_exists(const char *path);
long rc_read_file(const char *path, char *buf, long max);
int  rc_write_file(const char *path, const char *data);
int  rc_mkdir_path(const char *path);        /* single level, ok if exists */
int  rc_mkdir_p(const char *path);           /* recursive */
/* Resolve a runlevel indirection file (or a plain script path). Returns 0
 * when the entry points at an existing init script; `out` receives it. */
int  rc_resolve_target(const char *entry_path, const char *name,
                       char *out, long outlen);
/* Copy `src` up to `cap` bytes, trimming trailing spaces/tabs/CR/LF/NULs. */
void rc_trim_copy(char *dst, const char *src, long cap);
/* Value of one "<key>=" field of a /run/openrc state record ("" when the
 * key is absent). Records are "pid=N runlevel=L started=Y-M-D H:M:S". */
void rc_state_field(const char *body, const char *key, char *out, long cap);

/* ---- /etc/rc.conf key-value store ------------------------------------ */
void rc_conf_load(const char *path);
const char *rc_conf_get(const char *key);   /* "" when unset */

/* ---- text helpers ----------------------------------------------------- */
int  rc_has(const char *hay, const char *needle);   /* substring */
/* Extract one depend() keyword line: "after GETTY provide localmount" with
 * the wanted keyword yields the remainder ("provide localmount"). */
int  rc_dep_list(const char *script, const char *kw, char *out, long cap);
/* Append "name;" to a ';'-list if not already present. */
void rc_list_add(char *list, const char *name, long cap);
int  rc_list_has(const char *list, const char *name);
/* Split next ';' token from *cursor into tok; returns 0 at end. */
int  rc_list_next(const char **cursor, char *tok, long cap);

/* ---- runlevel scanning ------------------------------------------------ */
/* Fill `svcs` with the resolved init scripts of one runlevel directory.
 * Returns the count (capped at RC_MAX_SERVICES). */
int  rc_scan_runlevel(const char *level, rc_service_t *svcs, int max);
/* Parse one init script into a service record (depend blocks + start()). */
void rc_parse_service(rc_service_t *svc, const char *path);

/* ---- console output (einfo style) ------------------------------------- */
void rc_einfo(const char *what);            /* " * what" with color mark */
void rc_ewarn(const char *what);
void rc_eerror(const char *what);

#endif /* LIBRC_H */
