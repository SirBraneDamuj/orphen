#include "runtime/original_lead_player.h"

#include "runtime/psm2_ground_query.h"

#include <limits>

namespace orphen::port
{
  namespace
  {

    orphen::ported::player::OriginalTerrainSample toOriginalTerrainSample(const Psm2GroundHit &groundHit)
    {
      orphen::ported::player::OriginalTerrainSample sample;
      sample.height = groundHit.height;
      sample.leadingWord = groundHit.leadingWord;
      sample.terrainFlags = groundHit.terrainFlags;
      sample.sampledByOriginalTerrain = groundHit.sampledByOriginalTerrain;
      sample.slopeAngle = groundHit.slopeAngle;
      return sample;
    }

    Psm2TerrainQueryOptions toPsm2TerrainQueryOptions(const orphen::ported::player::OriginalTerrainQuery &query)
    {
      Psm2TerrainQueryOptions options{query.rejectTerrainMask, query.requireOriginalTerrainSample, {}};
      if (query.body.has_value())
      {
        options.body = Psm2ActorBody{query.body->feetHeight, query.body->headHeight};
      }
      return options;
    }

  orphen::ported::psm2::Vec3 chooseSpawnPoint(const orphen::ported::psm2::Psm2RuntimeState &map)
  {
    // A scene has no spawn point of its own. FUN_0022a418 copies the position
    // staged in DAT_00325340 into pool slot 0, and that was written by the
    // *previous* map's warp (FUN_0022b2c0, reached from script opcodes 0x8B and
    // 0x8C). Booting cold into a scene therefore has nothing to read, so pick
    // the walkable triangle nearest the map's horizontal centre instead --
    // world origin is often not over terrain at all, and the player then falls
    // forever. See analyzed/map_bootstrap_sequence.c.
    orphen::ported::psm2::Vec3 spawn{};
    if (!map.bounds.valid)
    {
      return spawn;
    }

    const float centreX = (map.bounds.min.x + map.bounds.max.x) * 0.5f;
    const float centreY = (map.bounds.min.y + map.bounds.max.y) * 0.5f;

    // Use the same strictness the controller does, or the spawn lands on a
    // triangle the player is not allowed to stand on and starts ungrounded.
    // "The same" now includes the body: a surface can be walkable and still be
    // somewhere the player does not fit, because a ceiling is inside them. Ask
    // once without a body to find the surface, then stand the body on it and
    // ask again -- a spot that stops answering is one the controller would
    // refuse on the first frame.
    const Psm2TerrainQueryOptions walkable{0, true, {}};
    const float bodyHeight = orphen::ported::entity::OriginalEntity{}.height58;
    const auto standableHeight = [&map, &walkable, bodyHeight](float x, float y, float reference) -> std::optional<float>
    {
      const auto loose = queryPsm2GroundAt(map, x, y, reference, walkable);
      if (!loose.has_value())
      {
        return std::nullopt;
      }
      const Psm2TerrainQueryOptions standing{0, true, Psm2ActorBody{loose->height, loose->height + bodyHeight}};
      const auto fitted = queryPsm2GroundAt(map, x, y, loose->height, standing);
      if (!fitted.has_value())
      {
        return std::nullopt;
      }
      return fitted->height;
    };

    if (const auto centreHeight = standableHeight(centreX, centreY, map.bounds.max.z))
    {
      return {centreX, centreY, *centreHeight};
    }

    // Nothing under the centre: fall back to the sampled triangle whose centroid
    // is closest to it.
    float bestDistanceSquared = std::numeric_limits<float>::max();
    bool found = false;
    for (const auto &triangle : map.derivedTriangles)
    {
      if (triangle.primitiveIndex >= map.DAT_003556b0_dRecords78.size())
      {
        continue;
      }
      const auto &record = map.DAT_003556b0_dRecords78[triangle.primitiveIndex];
      if ((record.leadingWord & 0x800) == 0)
      {
        continue; // not sampled as terrain by the original
      }

      orphen::ported::psm2::Vec3 centroid{};
      bool valid = true;
      for (const auto vertexIndex : triangle.vertexIndices)
      {
        if (vertexIndex >= map.DAT_0035569c_sectionCRecords.size())
        {
          valid = false;
          break;
        }
        const auto &position = map.DAT_0035569c_sectionCRecords[vertexIndex].position;
        centroid.x += position.x / 3.0f;
        centroid.y += position.y / 3.0f;
        centroid.z += position.z / 3.0f;
      }
      if (!valid)
      {
        continue;
      }

      // Confirm the controller's own query accepts this spot before taking it.
      const auto hitHeight = standableHeight(centroid.x, centroid.y, centroid.z + 1.0f);
      if (!hitHeight.has_value())
      {
        continue;
      }

      const float deltaX = centroid.x - centreX;
      const float deltaY = centroid.y - centreY;
      const float distanceSquared = deltaX * deltaX + deltaY * deltaY;
      if (distanceSquared < bestDistanceSquared)
      {
        bestDistanceSquared = distanceSquared;
        spawn = {centroid.x, centroid.y, *hitHeight};
        found = true;
      }
    }

    if (!found)
    {
      return {};
    }
    return spawn;
  }

  } // namespace

  void OriginalLeadPlayer::resetToMap(const orphen::ported::psm2::Psm2RuntimeState &map,
                                      const std::optional<orphen::ported::psm2::Vec3> &spawnOverride)
  {
    const orphen::ported::psm2::Vec3 spawn = spawnOverride.has_value() ? *spawnOverride : chooseSpawnPoint(map);
    controller_.resetAt(spawn, [&map](float originalX,
                                     float originalZ,
                                     float referenceY,
                                     const orphen::ported::player::OriginalTerrainQuery &query)
                              {
      const auto groundHit = queryPsm2GroundAt(map, originalX, originalZ, referenceY, toPsm2TerrainQueryOptions(query));
      if (!groundHit.has_value())
      {
        return std::optional<orphen::ported::player::OriginalTerrainSample>{};
      }
      return std::optional<orphen::ported::player::OriginalTerrainSample>{toOriginalTerrainSample(*groundHit)}; });
    refreshViewState(map);
  }

  void OriginalLeadPlayer::update(std::uint32_t frameTicks,
                                  const orphen::ported::psm2::Vec3 &movementRequest,
                                  float stickMagnitude,
                                  bool jumpRequested,
                                  bool debugMidairJumpHeld,
                                  bool interactPressed,
                                  const orphen::ported::psm2::Psm2RuntimeState *map,
                                  const orphen::ported::player::OriginalInteractionProbe &interactionProbe)
  {
    if (map == nullptr)
    {
      originalState_ = {};
      viewState_ = {};
      return;
    }

    const std::uint32_t jumpAction = jumpRequested ? orphen::ported::player::kOriginalMappedActionJump : 0;
    orphen::ported::player::OriginalPlayerFrameInput input{};
    input.cameraRelativeMove = movementRequest;
    input.mappedHeldActions = jumpAction;
    input.mappedPressedActions = jumpAction;
    input.interactPressed = interactPressed;
    input.stickMagnitude = stickMagnitude;
    input.debugMidairJumpHeld = debugMidairJumpHeld;
    const auto terrainSampler = [map](float originalX, float originalZ, float referenceY, const orphen::ported::player::OriginalTerrainQuery &query)
    {
      const auto groundHit = queryPsm2GroundAt(*map, originalX, originalZ, referenceY, toPsm2TerrainQueryOptions(query));
      if (!groundHit.has_value())
      {
        return std::optional<orphen::ported::player::OriginalTerrainSample>{};
      }
      return std::optional<orphen::ported::player::OriginalTerrainSample>{toOriginalTerrainSample(*groundHit)};
    };
    controller_.update(frameTicks, input, terrainSampler, interactionProbe);
    refreshViewState(*map);
  }

  void OriginalLeadPlayer::refreshViewState(const orphen::ported::psm2::Psm2RuntimeState &map)
  {
    originalState_ = controller_.snapshot();
    viewState_.position = originalState_.position;
    viewState_.facingRadians = originalState_.facingRadians;
    viewState_.state = originalState_.state;
    viewState_.animationId = originalState_.animationId;
    viewState_.substateFrame = originalState_.substateFrame;
    viewState_.collisionFlags = originalState_.collisionFlags;
    viewState_.verticalVelocity = originalState_.verticalVelocity;
    viewState_.bodyHeight = originalState_.bodyHeight;
    viewState_.grounded = originalState_.grounded;
    viewState_.running = originalState_.running;
    if (originalState_.grounded)
    {
      viewState_.groundHit = queryPsm2GroundAt(map, originalState_.position.x, originalState_.position.y, originalState_.position.z);
    }
    else
    {
      viewState_.groundHit.reset();
    }
  }

} // namespace orphen::port
