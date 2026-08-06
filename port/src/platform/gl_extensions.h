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

  // The array counterpart of fogCoord, for the vertex-array draw path. Resolved
  // by loadFogCoordExtension alongside glFogCoordf; a driver that has one has
  // the other, but the two are reported separately so a partial failure falls
  // back rather than crashes.
  bool hasFogCoordPointer();
  void fogCoordPointer(int stride, const void *pointer);

} // namespace orphen::port::gl
