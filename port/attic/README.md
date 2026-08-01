# Attic

Work that was written, then deliberately disconnected from the build. Nothing
here is compiled — `port/CMakeLists.txt` does not reference it, and nothing
under `port/src/` includes it.

It lives here rather than in `port/src/` so that the active source tree is
exactly what builds. It was kept rather than deleted because the parsing and
opcode work is real and re-usable; it is the *wiring* that was wrong, not the
research.

## `runtime/scene_script_interpreter.{h,cpp}` + `runtime/scene_script_state.{h,cpp}`

~3,400 lines of SCR bytecode VM: opcode dispatch and tracing, terrain mutation,
camera/visual mutation stats, and VM state (128-word registers, 2,304 flags, a
`0x101` x `0x41` table).

Parked because it was installed as a *bootstrap requirement* — the executable
ran a script VM trace at startup, mutated terrain from scripts, and installed a
script-driven camera before anything else worked. That made every unrelated
failure look like a VM failure.

The intended path back is `port/README.md`'s next-slice #5: reintroduce SCR as
narrow, verified opcode slices tied to one concrete game behavior, not as a
broad VM the rest of the runtime depends on.

## `ported/psc3/psc3_runtime.{h,cpp}`

PSC3 model parser: submesh table at `0x08`, vertex table at `0x14` (10-byte s16
records at 1/2048 scale), draw descriptors at `0x1c` (0x18 bytes each, flag
`0x20` = skip).

Parked with the PSC3 wireframe gallery it fed. PSC3 records are still visible
through `--scene-tree`; they are just not rendered as placeholder scene objects.

This one is close to usable. It becomes relevant again when the player stops
being a debug box — but that is gated on skeletal animation binding, which is
still open research (`docs/pcsx2_animation_binding_hunt.md`).
