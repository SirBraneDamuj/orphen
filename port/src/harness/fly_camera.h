#pragma once

#include "ported/psm2/psm2_runtime.h"
#include "ported/render/original_view_projection.h"
#include "runtime/input_state.h"

namespace orphen::harness
{

  // F1: a camera detached from the game's, for looking at a scene from
  // somewhere the game camera never goes. PC-only; nothing in the original
  // corresponds to it.
  //
  // It runs at the render rate on wall-clock time, never on the simulation
  // step, so the simulation cannot see it and --frames runs are unaffected.
  //
  // The view it builds is in the original's view-space convention -- +x right,
  // +y down, +z into the screen, the same as FUN_0020bec8's -- so everything the
  // renderer draws through the game camera's matrices draws through these
  // unchanged, including the billboard pass, whose quads arrive already in the
  // game camera's view space.
  class FlyCamera
  {
  public:
    // Put the camera exactly where the game camera is, looking where it looks.
    // Roll is dropped: the fly camera has none.
    void snapTo(const orphen::ported::render::ViewProjection &gameCamera);
    void placeAt(const orphen::ported::psm2::Vec3 &eye, float yawRadians, float pitchRadians);
    // Keep the heading and pitch and back the eye off along the view until a
    // sphere of `radius` around `centre` sits in the middle of the picture with
    // room around it. The entity tree's click.
    void frame(const orphen::ported::psm2::Vec3 &centre, float radius);

    // WASD along the view, Q/E down and up, the mouse to look, the wheel to
    // change speed, Shift and Ctrl for fast and slow. See InputSnapshot.
    void update(float deltaSeconds, const orphen::port::InputSnapshot &input);

    // Row-major, row-vector, game space -> view space, like ViewProjection::view.
    orphen::ported::render::Matrix4 view() const;

    // Takes another camera's view space to this one's: inverse(otherView) *
    // view(). The billboard pass's quads arrive in the game camera's view
    // space, and this is the modelview that puts them back in the world.
    orphen::ported::render::Matrix4 viewFrom(const orphen::ported::render::Matrix4 &otherView) const;

    // Ready for glLoadMatrixf, in the same form glCameraFor returns: the
    // modelview takes the renderer's viewer-space vertices. Unlike the game
    // camera this fills the whole window rather than a 4:3 box.
    orphen::ported::render::GlCamera glCamera(int framebufferWidth,
                                              int framebufferHeight,
                                              float nearPlane,
                                              float farPlane) const;

    const orphen::ported::psm2::Vec3 &eye() const { return eye_; }
    float speed() const { return speed_; }
    float yawRadians() const { return yawRadians_; }
    float pitchRadians() const { return pitchRadians_; }

  private:
    orphen::ported::psm2::Vec3 eye_{};
    float yawRadians_ = 0.0f;   // heading from game +x toward +y
    float pitchRadians_ = 0.0f; // positive looks up
    float speed_ = 4.0f;        // game units per second
  };

  // The game camera as an object in the world: its frustum from the near clip
  // to `farDistance`, a pyramid back to the eye, and a filled triangle on the
  // near plane's top edge so its up direction (and so any roll) reads at a
  // glance. Emits viewer-space vertices; expects the fly camera's matrices to
  // be current. Leaves depth testing enabled.
  void drawCameraFrustumGizmo(const orphen::ported::render::ViewProjection &camera, float farDistance);

} // namespace orphen::harness
