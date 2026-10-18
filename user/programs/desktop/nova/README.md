# Nova NTK color slice in NEWOS desktop

This directory carries an adapted, deliberately small piece of BoredOS Nova's
NTK color module. The source provenance is
[`BoredOS/nova`](https://github.com/BoredOS/nova/tree/d586f1bfc4687172ca097298ba3c91d6f7d40069/ntk)
at pinned commit `d586f1bfc4687172ca097298ba3c91d6f7d40069` (GPL-3.0-only).
The original notices remain in each derived source/header, and the complete
upstream GPL-3.0 text is included as `LICENSE`.

`ntk_color.c` and `ntk_color.h` are derived from Nova's `ntk/ntk_color.c` and
`ntk/ntk_color.h`. `ntk_types.h` is a minimal NEWOS compatibility adaptation
of Nova's type header, retaining only `NtkColor`; it omits Nova's UI, polling,
and OS-facing declarations. The floating-point color operations are omitted
because NEWOS currently uses a no-FPU userspace ABI. `ntk_color_lerp_u8` is an
integer-only 8-bit-weight counterpart to Nova's float interpolation; the
hexadecimal formatter is also adapted to avoid hosted `snprintf`.

The existing NEWOS desktop invokes this component to draw gradients on its
own pixel canvas, then submits frames with NEWOS's existing framebuffer write
syscall. No Nova compositor, event loop, socket protocol, widget tree, or
window-manager runtime is included or implied. The combined desktop executable
contains GPL-3.0-only Nova-derived code; do not treat this directory or the
combined executable as MIT-only. Other NEWOS files retain their own existing
license notices and terms.
