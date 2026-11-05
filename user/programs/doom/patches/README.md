# DOOM 1.10 on NEWOS — patch series

Every `.c`/`.h` in the parent directory is a fork of the file of the same name
in `../data/linuxdoom-1.10.tar.gz` (id Software's released DOOM source,
`DOOM-master/linuxdoom-1.10`, GPL-2.0, sha256
`99b0efa8a4954a8af0ad79faaf8d8f27277c254b929d110455dafcee4686e4b8`).

The patches are the complete distance between upstream and the fork. They are
not a summary: applying them in order to a fresh extraction reproduces the
seven patched files byte for byte.

```sh
sha256sum -c ../data/SHA256SUMS
tar -xzf ../data/linuxdoom-1.10.tar.gz
cd DOOM-master/linuxdoom-1.10
for p in ../../patches/*.patch; do patch -p1 < "$p"; done
```

| Patch | Files | Why |
|---|---|---|
| `0001-compile-as-gnu99-on-newos-libc.patch` | `am_map.c`, `doomdef.h` | Storage class without a type (`static nexttic`, `register outcode1`) is implicit int, a gnu89 extension the engine's `-std=gnu99` build rejects outright; `SNDSERV` is turned off because it routes sound through a separate `sndserver` process NEWOS neither builds nor launches. |
| `0002-32-bit-assumptions-in-wad-loading.patch` | `r_data.c`, `p_setup.c` | An on-disk texture header field typed as a pointer grows from 4 to 8 bytes and shifts every patch entry, and several `Z_Malloc(count*4)` calls size arrays of *pointers* with 32-bit slots. Both corrupt the game silently on x86_64. |
| `0003-null-terminate-sprnames.patch` | `info.c`, `info.h` | `R_InitSpriteDefs()` counts `sprnames[]` by walking to a `NULL`, but the array has exactly `NUMSPRITES` entries; on i386 the walk happened to stop on a zero byte past the end. |
| `0004-defaults-table-and-key-bindings.patch` | `m_misc.c` | `default_t` stores `int*` and `char**` in one `int*` slot, truncating pointers; `M_LoadDefaults()` parses `default.cfg` with `scanf`, which the NEWOS libc does not implement; and `key_fire`/`key_strafe`/`key_speed` are bound to CTRL/ALT/SHIFT scancodes that NEWOS' decoded-byte keyboard can never deliver. |

## Files that are not patched

Four upstream files are replaced wholesale, because their whole content is a
driver for an API NEWOS does not have (X11, OSS/DMX, DOS timing, IP sockets).
They keep the upstream file name and the upstream function surface, and each
one says so in its header comment:

- `i_video.c` — 320x200 panel blit, palette, PS/2 keyboard and mouse, via
  `SYS_FBINFO` / `SYS_CONSOLE_GUI` / `SYS_FBWRITE` / `SYS_KBD_GRAB` /
  `SYS_KBD_READ` / `SYS_MOUSE_GET`.
- `i_system.c` — tic clock, zone base, `I_Error()`, quit paths, via
  `clock_gettime(CLOCK_MONOTONIC)` and `SYS_SLEEP`.
- `i_sound.c` — the DMX mixer is not ported. NEWOS has no PCM path reachable
  from ring 3 (no `/dev/dsp`, no audio syscall; the HDA driver is
  kernel-internal), so the file implements the whole `I_` audio contract
  against an absent device: volumes and song state are tracked as the engine
  expects, `I_StartSound()` returns "no channel", and nothing is ever claimed
  to be playing. Music is the same story one level up — there is no OPL3
  synthesiser to render MUS lumps with.
- `i_net.c` — DOOMCOM/IPX/UDP interplay is a single-player stub, which is what
  the released engine can do without a network stack.

One file is new and has no upstream counterpart: `doom_main.c`, the `/bin/doom`
entry point that the NEWOS toolbox dispatches to (it calls `D_DoomMain()` the
way upstream's `main()` did, after locating the IWAD).

## 64-bit hazards left alone on purpose

`(int)` pointer casts in `r_data.c`'s 256-byte alignment idiom,
`z_zone.c:440` (`(unsigned)block->user`), and the pointer↔index swaps in
`p_saveg.c` are untouched. NEWOS links userland at `0x400000` and the brk heap
stays inside the low 2 GiB, so those truncations cannot lose a bit in this
port. They are a documented assumption, not a bug fixed by a patch — see
`docs/development/doom.md`.
