#pragma once

// Windows ships opengl32.dll with only the GL 1.1 entry points exported. Every
// function added after 1.1 -- glFogCoordf among them, which arrived in 1.4 --
// has to be fetched at run time from the driver instead of linked against.
// SDL_GL_GetProcAddress does that portably, so this stays one small file rather
// than a dependency on GLEW or GLAD.
//
// Only what the renderer actually needs is resolved here. The port is
// deliberately fixed-function GL 1.1 otherwise; this is not the start of a
// general extension layer.

namespace orphen::port::gl
{

  // GL 1.4 / EXT_fog_coord tokens. SDL_opengl.h pulls in the platform gl.h,
  // which on Windows stops at 1.1 and does not define these.
  inline constexpr unsigned int kFogCoordinateSource = 0x8450; // GL_FOG_COORD_SRC
  inline constexpr unsigned int kFogCoordinate = 0x8451;       // GL_FOG_COORD
  inline constexpr unsigned int kFragmentDepth = 0x8452;       // GL_FRAGMENT_DEPTH

  // Resolves glFogCoordf once and caches the result, including the failure.
  // Safe to call every frame. Returns false when neither the core nor the EXT
  // entry point is available, in which case fogCoord() is a no-op and callers
  // must fall back to GL's own eye-distance fog.
  bool loadFogCoordExtension();

  // Only meaningful after loadFogCoordExtension() has returned true.
  void fogCoord(float value);

  // GL_FOG_COORD_ARRAY, the client-state token for the array form.
  inline constexpr unsigned int kFogCoordinateArray = 0x8457;

  // GL 1.3 / ARB_texture_env_combine tokens, for the same reason: the platform
  // gl.h stops at 1.1. No new entry points are needed -- glTexEnvi/glTexEnvf are
  // GL 1.0 -- so these are the whole of what the combiner costs.
  //
  // They exist to reproduce the GS's texture function, `(Ct * Cv) >> 7`, whose
  // vertex colour runs 0..255 for a multiplier of 0..1.99. GL_MODULATE clamps
  // both operands to 1.0 and so can only darken; GL_COMBINE with GL_RGB_SCALE 2
  // gets the missing octave back.
  inline constexpr unsigned int kCombine = 0x8570;        // GL_COMBINE
  inline constexpr unsigned int kCombineRgb = 0x8571;     // GL_COMBINE_RGB
  inline constexpr unsigned int kCombineAlpha = 0x8572;   // GL_COMBINE_ALPHA
  inline constexpr unsigned int kRgbScale = 0x8573;       // GL_RGB_SCALE
  inline constexpr unsigned int kAlphaScale = 0x0D1C;     // GL_ALPHA_SCALE
  inline constexpr unsigned int kPrimaryColor = 0x8577;   // GL_PRIMARY_COLOR
  inline constexpr unsigned int kSource0Rgb = 0x8580;     // GL_SOURCE0_RGB
  inline constexpr unsigned int kSource1Rgb = 0x8581;     // GL_SOURCE1_RGB
  inline constexpr unsigned int kSource0Alpha = 0x8588;   // GL_SOURCE0_ALPHA
  inline constexpr unsigned int kSource1Alpha = 0x8589;   // GL_SOURCE1_ALPHA

  // The array counterpart of fogCoord, for the vertex-array draw path. Resolved
  // by loadFogCoordExtension alongside glFogCoordf; a driver that has one has
  // the other, but the two are reported separately so a partial failure falls
  // back rather than crashes.
  bool hasFogCoordPointer();
  void fogCoordPointer(int stride, const void *pointer);

} // namespace orphen::port::gl
