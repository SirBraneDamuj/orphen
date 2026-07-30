#include "runtime/original_lead_player.h"

#include "runtime/psm2_ground_query.h"

namespace orphen::port
{
  namespace
  {

    orphen::ported::player::OriginalTerrainSample toOriginalTerrainSample(const Psm2GroundHit &groundHit)
    {
      return {groundHit.height, groundHit.leadingWord, groundHit.terrainFlags, groundHit.sampledByOriginalTerrain};
    }

    Psm2TerrainQueryOptions toPsm2TerrainQueryOptions(const orphen::ported::player::OriginalTerrainQuery &query)
    {
      return {query.rejectTerrainMask, query.requireOriginalTerrainSample};
    }

  } // namespace

  void OriginalLeadPlayer::resetToMap(const orphen::ported::psm2::Psm2RuntimeState &map)
  {
    controller_.resetAtOrigin([&map](float originalX,
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

  void OriginalLeadPlayer::update(float deltaSeconds,
                                  const orphen::ported::psm2::Vec3 &cameraRelativeMove,
                                  bool jumpRequested,
                                  const orphen::ported::psm2::Psm2RuntimeState *map)
  {
    if (map == nullptr)
    {
      originalState_ = {};
      viewState_ = {};
      return;
    }

    const std::uint32_t jumpAction = jumpRequested ? orphen::ported::player::kOriginalMappedActionJump : 0;
    const orphen::ported::player::OriginalPlayerFrameInput input{cameraRelativeMove, jumpAction, jumpAction};
    const auto terrainSampler = [map](float originalX, float originalZ, float referenceY, const orphen::ported::player::OriginalTerrainQuery &query)
    {
      const auto groundHit = queryPsm2GroundAt(*map, originalX, originalZ, referenceY, toPsm2TerrainQueryOptions(query));
      if (!groundHit.has_value())
      {
        return std::optional<orphen::ported::player::OriginalTerrainSample>{};
      }
      return std::optional<orphen::ported::player::OriginalTerrainSample>{toOriginalTerrainSample(*groundHit)};
    };
    const auto movementBlocker = [map](float originalStartX,
                                       float originalStartZ,
                                       float originalEndX,
                                       float originalEndZ,
                                       float baseY,
                                       float height,
                                       float radius)
    {
      return queryPsm2ActiveBlockerAlong(*map,
                                         originalStartX,
                                         originalStartZ,
                                         originalEndX,
                                         originalEndZ,
                                         baseY,
                                         height,
                                         radius)
          .has_value();
    };
    controller_.update(deltaSeconds, input, terrainSampler, movementBlocker);
    refreshViewState(*map);
  }

  void OriginalLeadPlayer::refreshViewState(const orphen::ported::psm2::Psm2RuntimeState &map)
  {
    originalState_ = controller_.snapshot();
    viewState_.position = originalState_.position;
    viewState_.facingRadians = originalState_.facingRadians;
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
