/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/newpkg/newpkg_internal.h
 * Description: newpkg internal glue: shared state + cross-module prototypes.
 * Added: 2026-10-30
 */
/* Private glue between newpkg_main.c and its sibling translation units
 * (newpkg_util.c, newpkg_pkg.c, newpkg_cmds.c). Never installed and never
 * included from outside this directory. It carries the shared file-scope
 * buffers (defined exactly once, in newpkg_main.c), the DB_DIR/CHUNK
 * constants and the prototypes of every function that crossed the split.
 */

#ifndef NEWPKG_INTERNAL_H
#define NEWPKG_INTERNAL_H

#define DB_DIR "/var/lib/newpkg"
#define CHUNK  512

/* Shared file-scope state (definitions: newpkg_main.c). */
extern u8 g_io[CHUNK];
extern u8 g_meta[NEWPKG_META_MAX];
extern u8 g_man[NEWPKG_MANIFEST_MAX];
extern char g_line[512];
extern char g_path[NEWPKG_PATH_MAX];

/* Manifest entry tables (offsets into g_man + parsed fields). */
extern u32 g_size[NEWPKG_FILES_MAX];
extern u32 g_crc[NEWPKG_FILES_MAX];
extern u32 g_mode[NEWPKG_FILES_MAX];
extern u32 g_nfiles;

extern u32 g_meta_len;
extern u32 g_man_len;

/* newpkg_util.c: low-level helpers (no package logic). */
int read_full(int fd, u8 *buf, u64 len);
int write_full(int fd, const u8 *buf, u64 len);
int path_exists(const char *path);
int mkdir_p(const char *dir);
int mkdir_parent(const char *file);
void print_hex8(u32 v);
void print_mode(u32 mode);
int valid_name(const char *name);

/* newpkg_pkg.c: package/database core. */
int load_package(const char *path);
void entry_path(u32 k);
void db_paths(const char *name, char *info, char *files);
int db_read_key(const char *name, const char *key, char *out, u64 cap);
int db_find_owner(const char *path, char *owner, u64 cap);
int check_deps(const char *pkg_name);
int stream_payload(int pkg_fd, int write_mode, u32 *done_count);
void rollback(const char *pkg, u32 count);

/* newpkg_cmds.c: install-side commands. The query commands (search,
 * depends, conflicts, clean, repo, changelog) and cmd_usage stay static
 * in newpkg_main.c next to main(). */
int cmd_info(const char *path);
int cmd_verify(const char *path);
int cmd_install(const char *path, int force);
int cmd_remove(const char *name);
int cmd_list(void);
int cmd_files(const char *name);
int cmd_upgrade(void);

#endif
