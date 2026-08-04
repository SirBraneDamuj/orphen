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
    bool previousMapRequested = false;
    bool nextMapRequested = false;

    // Left click: fire a ray through this pixel and report every entity triangle
    // along it, drawn or not. Window pixels, origin top-left.
    bool probeRequested = false;
    int probeX = 0;
    int probeY = 0;

    // Free-viewer camera, used only when no player is active.
    float rotateX = 0.0f;
    float rotateY = 0.0f;
    float zoom = 0.0f;

    // Game input.
    bool jumpRequested = false;
    float moveX = 0.0f;
    float moveY = 0.0f;

    // Raw pad bits as the original sees them, post-CONCAT11 inversion in
    // FUN_0023b5d8. Low byte is byte-swapped relative to the usual PS2
    // constants: 0x04 is L1, 0x08 is R1; 0x0400 is R3.
    std::uint16_t rawHeldPad = 0;    // DAT_003555f4
    std::uint16_t rawPressedPad = 0; // DAT_003555f6

    // Analog stick, in the original's units. fGpffffb678 is compared against
    // 40.0 by the camera and 100.0 by the grounded player state, and
    // FUN_00253488 scales by it directly, so full deflection is 128.
    float stickAngle = 0.0f;     // fGpffffb674
    float stickMagnitude = 0.0f; // fGpffffb678
  };

} // namespace orphen::port
