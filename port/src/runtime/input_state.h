#pragma once

#include <cstdint>

namespace orphen::port
{

  // Platform-independent input for one simulation step. platform/ fills this in;
  // runtime/ and ported/ consume it. It lived in platform/sdl_gl_window.h, which
  // forced harness and runtime headers to include an SDL header to see it.
  struct InputSnapshot
  {
    // Harness controls.
    bool quitRequested = false;
    bool resetRequested = false;
    bool toggleWireframeRequested = false;
    bool toggleHudRequested = false;
    // In-world debug drawing: collision boxes, entity labels, origin axes.
    bool toggleDebugOverlayRequested = false;
    // 'O': DAT_003555dd bit 7, the debug menu's SCR SUBPROC DISP entry.
    bool toggleSubprocDisplayRequested = false;
    // Held 'P', or the pad's right trigger. The original's debug fast forward:
    // FUN_002000c0 skips its whole vsync-wait block while R2 is held and the
    // cheat flag DAT_003555db is set, so the simulation runs uncapped with
    // nothing presented in between. Harness control, not game input -- the
    // simulation must not be able to see it.
    bool fastForwardHeld = false;
    bool previousMapRequested = false;
    bool nextMapRequested = false;
    // 'G': dump a pose/draw-list snapshot of the current frame to stdout and to
    // a file, and photograph the frame next to it. For reporting a glitch that
    // only shows up in play, where there is no frame number to capture at.
    bool captureSnapshotRequested = false;

    // Left click: fire a ray through this pixel and report every entity triangle
    // along it, drawn or not. Window pixels, origin top-left.
    bool probeRequested = false;
    int probeX = 0;
    int probeY = 0;

    // Free-viewer camera yaw, used only when no player is active. It rides the
    // same J/L keys as the game's L1/R1 rather than having bindings of its own.
    // Pitch and zoom used to live here on I/K and Q/E; they were map-viewer
    // holdovers with no pad button behind them and have been removed.
    float rotateX = 0.0f;

    // Game input.
    bool jumpRequested = false;
    float moveX = 0.0f;
    float moveY = 0.0f;

    // Raw pad bits as the original sees them, post-CONCAT11 inversion in
    // FUN_0023b5d8. Low byte is byte-swapped relative to the usual PS2
    // constants: 0x04 is L1, 0x08 is R1; 0x0400 is R3.
    std::uint16_t rawHeldPad = 0;    // DAT_003555f4
    std::uint16_t rawPressedPad = 0; // DAT_003555f6

    // The movement stick quantised onto the same four direction bits the D-pad
    // occupies, by FUN_0023b4e8, plus its newly-pressed edge. FUN_0023b5d8
    // keeps both words beside the pad ones; FUN_002462c8 reads the edge and ORs
    // it into DAT_003555f6, which is what makes the stick cycle targets.
    std::uint16_t rawStickDirection = 0;        // DAT_003555fe
    std::uint16_t rawPressedStickDirection = 0; // DAT_00355600

    // Analog stick, in the original's units. fGpffffb678 is compared against
    // 40.0 by the camera and 100.0 by the grounded player state, and
    // FUN_00253488 scales by it directly, so full deflection is 128.
    float stickAngle = 0.0f;     // fGpffffb674
    float stickMagnitude = 0.0f; // fGpffffb678
  };

} // namespace orphen::port
