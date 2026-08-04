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

    FogCoordfProc g_fogCoordf = nullptr;
    bool g_resolved = false;

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
    return g_fogCoordf != nullptr;
  }

  void fogCoord(float value)
  {
    if (g_fogCoordf != nullptr)
    {
      g_fogCoordf(value);
    }
  }

} // namespace orphen::port::gl
