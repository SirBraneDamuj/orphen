#include "harness/movie_player.h"

#include "harness/mpeg2_video_decoder.h"
#include "ported/movie/mv3_stream.h"
#include "ported/movie/original_movie_session.h"
#include "ported/render/original_view_projection.h"

#include <SDL_opengl.h>

#include <chrono>
#include <iostream>

namespace orphen::harness
{

  namespace
  {
    // FUN_002F2198's display window: 640x448 starting at picture row 16.
    constexpr int kVisibleFirstRow = 16;
    constexpr int kVisibleRows = 448;

    // FUN_002F2820 holds each picture for two NTSC vblanks.
    constexpr std::chrono::duration<double> kPictureDuration{1001.0 / 30000.0};
    constexpr std::chrono::duration<double> kVblankDuration{1001.0 / 60000.0};

    // FUN_002F2198's prefill: blocks until more than two chunks of each ring
    // are buffered, which on these files is three blocks.
    constexpr int kPrefillBlocks = 3;

    struct ViewportRect
    {
      int x = 0;
      int y = 0;
      int width = 0;
      int height = 0;
    };

    // The same 4:3 box MapViewer::gameViewportRect fits the game's frame into.
    ViewportRect gameViewport(int framebufferWidth, int framebufferHeight)
    {
      const float aspect = orphen::ported::render::constants::kDisplayAspect;
      int width = framebufferWidth;
      int height = static_cast<int>(static_cast<float>(framebufferWidth) / aspect + 0.5f);
      if (height > framebufferHeight)
      {
        height = framebufferHeight;
        width = static_cast<int>(static_cast<float>(framebufferHeight) * aspect + 0.5f);
      }
      return {(framebufferWidth - width) / 2, (framebufferHeight - height) / 2, width, height};
    }
  } // namespace

  MoviePlayer::MoviePlayer(orphen::port::SdlGlWindow &window, AudioDevice &audio)
      : window_(window), audio_(audio)
  {
  }

  MoviePlayer::~MoviePlayer()
  {
    if (texture_ != 0)
    {
      glDeleteTextures(1, &texture_);
    }
  }

  orphen::port::MovieOutcome MoviePlayer::play(const orphen::port::MovieRequest &request)
  {
    using orphen::port::MovieOutcome;
    using Clock = std::chrono::steady_clock;

    // The music has to be dealt with whatever happens to the picture: on
    // hardware FUN_00206840 runs even when FUN_002F1A70 fails.
    bool musicReplaced = false;
    const auto replaceMusic = [&] {
      if (!musicReplaced && request.FUN_00206840_replace_music)
      {
        request.FUN_00206840_replace_music();
      }
      musicReplaced = true;
    };

    orphen::ported::movie::Mv3Stream stream;
    Mpeg2VideoDecoder decoder;
    if (!stream.open(request.path))
    {
      std::cout << "[movie] " << request.path.filename().string() << ": " << stream.diagnostic() << '\n';
      replaceMusic();
      return MovieOutcome::Unavailable;
    }
    if (!decoder.open())
    {
      std::cout << "[movie] " << request.path.filename().string() << ": " << decoder.diagnostic() << '\n';
      replaceMusic();
      return MovieOutcome::Unavailable;
    }

    std::vector<std::uint8_t> mpeg;
    std::vector<std::int16_t> pcm;
    for (int block = 0; block < kPrefillBlocks && stream.readBlock(mpeg, pcm); ++block)
    {
    }
    decoder.feed(mpeg);
    mpeg.clear();

    // FUN_00206840's wait. The pad is not read here, so neither is Start; a
    // closed window still ends it.
    if (request.musicFadeHoldVblanks > 0)
    {
      const auto holdEnd = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                              kVblankDuration * request.musicFadeHoldVblanks);
      while (Clock::now() < holdEnd)
      {
        orphen::port::InputSnapshot input;
        window_.pollEvents(input);
        if (input.quitRequested)
        {
          replaceMusic();
          return MovieOutcome::QuitRequested;
        }
        present(false, false, 0);
      }
    }
    replaceMusic();

    // FUN_00207408: the stream starts at the movie's own BVOL.
    orphen::ported::movie::MovieSkipFade skip(request.volume, request.skipLocked);
    const bool withSound = audio_.isOpen();
    if (withSound)
    {
      audio_.stopMovie();
      audio_.setMovieGain(orphen::ported::movie::bvolGain(skip.volume()));
      audio_.queueMoviePcm(pcm);
    }
    pcm.clear();

    std::cout << "[movie] M" << (request.movieId < 10 ? "0" : "") << request.movieId << ".MV3 playing ("
              << stream.blockCount() << " blocks"
              << (request.skipLocked ? ", Start locked" : ", Start skips")
              << (withSound ? ", sound at BVOL " : ", no audio device") ;
    if (withSound)
    {
      std::cout << "0x" << std::hex << skip.volume() << std::dec;
    }
    std::cout << ")\n";

    if (texture_ == 0)
    {
      glGenTextures(1, &texture_);
      glBindTexture(GL_TEXTURE_2D, texture_);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    YuvPicture picture;
    std::vector<std::uint8_t> rgba;
    int textureWidth = 0;
    MovieOutcome outcome = MovieOutcome::Played;
    // Start pressed on any display refresh since the last picture. The
    // original reads the pad once per picture; this keeps a press that lands
    // on the in-between refresh from being lost.
    std::uint16_t pressedSinceLastPicture = 0;
    auto clockStart = Clock::now();

    for (std::int64_t index = 0;; ++index)
    {
      // sceMpegGetPicture: feed blocks until the decoder gives one up.
      bool havePicture = decoder.nextPicture(picture);
      while (!havePicture && !decoder.finished())
      {
        if (stream.readBlock(mpeg, pcm))
        {
          decoder.feed(mpeg);
          mpeg.clear();
          if (withSound)
          {
            audio_.queueMoviePcm(pcm);
          }
          pcm.clear();
        }
        else
        {
          decoder.endOfStream();
        }
        havePicture = decoder.nextPicture(picture);
      }
      // sceMpegIsEnd.
      if (!havePicture)
      {
        break;
      }

      const auto step = skip.FUN_002f2198_skip(pressedSinceLastPicture);
      pressedSinceLastPicture = 0;
      if (step.endMovie)
      {
        outcome = MovieOutcome::Skipped;
        break;
      }
      if (withSound)
      {
        audio_.setMovieGain(orphen::ported::movie::bvolGain(skip.volume()));
      }

      ipuCscToRgba(picture, kVisibleFirstRow, kVisibleRows, rgba);
      glBindTexture(GL_TEXTURE_2D, texture_);
      if (textureWidth != picture.width)
      {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, picture.width, kVisibleRows, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, rgba.data());
        textureWidth = picture.width;
      }
      else
      {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, picture.width, kVisibleRows, GL_RGBA,
                        GL_UNSIGNED_BYTE, rgba.data());
      }

      // FUN_002F2820: a picture that is already late resets the clock to
      // itself rather than being dropped, so a stall slows the film down.
      const auto shownAt =
          clockStart + std::chrono::duration_cast<Clock::duration>(kPictureDuration * index);
      if (Clock::now() > shownAt + std::chrono::duration_cast<Clock::duration>(kPictureDuration))
      {
        clockStart = Clock::now() - std::chrono::duration_cast<Clock::duration>(kPictureDuration * index);
      }
      const auto nextAt =
          clockStart + std::chrono::duration_cast<Clock::duration>(kPictureDuration * (index + 1));

      do
      {
        orphen::port::InputSnapshot input;
        window_.pollEvents(input);
        if (input.quitRequested)
        {
          outcome = MovieOutcome::QuitRequested;
          break;
        }
        pressedSinceLastPicture = static_cast<std::uint16_t>(pressedSinceLastPicture | input.rawPressedPad);
        present(true, step.drawFade, step.fadeAlpha);
      } while (Clock::now() < nextAt);

      if (outcome == MovieOutcome::QuitRequested)
      {
        break;
      }
    }

    // FUN_002074C8. Read the level first; stopping clears it.
    const float peak = withSound ? audio_.moviePeak() : 0.0f;
    const double secondsMixed =
        withSound ? static_cast<double>(audio_.movieFramesPlayed()) / orphen::ported::movie::kPcmSampleRate
                  : 0.0;
    if (withSound)
    {
      audio_.stopMovie();
    }
    std::cout << "[movie] M" << (request.movieId < 10 ? "0" : "") << request.movieId << ".MV3 "
              << (outcome == MovieOutcome::Skipped         ? "skipped"
                  : outcome == MovieOutcome::QuitRequested ? "closed"
                                                           : "finished");
    if (withSound)
    {
      std::cout << " -- " << secondsMixed << " s of sound mixed, peak " << peak;
    }
    else
    {
      std::cout << " -- silent, no audio device";
    }
    std::cout << '\n';
    return outcome;
  }

  void MoviePlayer::present(bool havePicture, bool drawFade, std::uint8_t fadeAlpha)
  {
    window_.beginFrame(0.0f, 0.0f, 0.0f);
    const ViewportRect view = gameViewport(window_.width(), window_.height());

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glViewport(view.x, view.y, view.width, view.height);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, 1.0, 1.0, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_FOG);
    glDisable(GL_BLEND);

    if (havePicture)
    {
      glEnable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, texture_);
      glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
      glBegin(GL_QUADS);
      glTexCoord2f(0.0f, 0.0f);
      glVertex2f(0.0f, 0.0f);
      glTexCoord2f(1.0f, 0.0f);
      glVertex2f(1.0f, 0.0f);
      glTexCoord2f(1.0f, 1.0f);
      glVertex2f(1.0f, 1.0f);
      glTexCoord2f(0.0f, 1.0f);
      glVertex2f(0.0f, 1.0f);
      glEnd();
      glDisable(GL_TEXTURE_2D);
    }

    // FUN_002F2758's quad: ALPHA_1 0x44, (Cs - Cd) * As + Cd, black at
    // alpha / 0x80.
    if (drawFade)
    {
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glColor4f(0.0f, 0.0f, 0.0f, static_cast<float>(fadeAlpha) / 128.0f);
      glBegin(GL_QUADS);
      glVertex2f(0.0f, 0.0f);
      glVertex2f(1.0f, 0.0f);
      glVertex2f(1.0f, 1.0f);
      glVertex2f(0.0f, 1.0f);
      glEnd();
    }

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopAttrib();
    window_.swapBuffers();
  }

} // namespace orphen::harness
