#include "platform/gl_extensions.h"

#include <SDL_video.h>

namespace orphen::port::gl
{
  namespace
  {

    using FogCoordfProc = void(
#ifdef _WIN32
        __stdcall
#endif
        *)(float);

    // glFogCoordPointer(GLenum type, GLsizei stride, const void *pointer)
    using FogCoordPointerProc = void(
#ifdef _WIN32
        __stdcall
#endif
        *)(unsigned int, int, const void *);

    FogCoordfProc g_fogCoordf = nullptr;
    FogCoordPointerProc g_fogCoordPointer = nullptr;
    bool g_resolved = false;

    constexpr unsigned int kGlFloat = 0x1406; // GL_FLOAT

  } // namespace

  bool loadFogCoordExtension()
  {
    if (g_resolved)
    {
      return g_fogCoordf != nullptr;
    }
    g_resolved = true;

    // The core 1.4 name first; EXT_fog_coord's is the same function under the
    // older name and some drivers export only that one.
    void *entry = SDL_GL_GetProcAddress("glFogCoordf");
    if (entry == nullptr)
    {
      entry = SDL_GL_GetProcAddress("glFogCoordfEXT");
    }

    g_fogCoordf = reinterpret_cast<FogCoordfProc>(entry);

    void *pointerEntry = SDL_GL_GetProcAddress("glFogCoordPointer");
    if (pointerEntry == nullptr)
    {
      pointerEntry = SDL_GL_GetProcAddress("glFogCoordPointerEXT");
    }
    g_fogCoordPointer = reinterpret_cast<FogCoordPointerProc>(pointerEntry);

    // Both or neither. The draw path is vertex arrays, so glFogCoordf alone
    // would let applyFogState select the 1/z curve and then have no way to
    // supply it -- leaving GL to fog every vertex by whatever the current fog
    // coordinate happened to be. A driver missing either one falls back to
    // GL's own eye-distance fog, which is wrong in the same way everywhere
    // instead of wrong unpredictably.
    if (g_fogCoordf == nullptr || g_fogCoordPointer == nullptr)
    {
      g_fogCoordf = nullptr;
      g_fogCoordPointer = nullptr;
      return false;
    }
    return true;
  }

  bool hasFogCoordPointer()
  {
    return g_fogCoordPointer != nullptr;
  }

  void fogCoordPointer(int stride, const void *pointer)
  {
    if (g_fogCoordPointer != nullptr)
    {
      g_fogCoordPointer(kGlFloat, stride, pointer);
    }
  }

  void fogCoord(float value)
  {
    if (g_fogCoordf != nullptr)
    {
      g_fogCoordf(value);
    }
  }

} // namespace orphen::port::gl
