#pragma once

#include "ported/entity/actor_dispatch_table.h"
#include "ported/entity/actor_trace.h"
#include "ported/entity/entity_pool.h"
#include "ported/entity/original_entity.h"

#include <cstdint>
#include <functional>

namespace orphen::ported::entity
{

  // Native counterpart of src/FUN_00239ce0.c (0x00239ce0), the per-frame actor
  // update loop, and the two helpers it depends on. See
  // analyzed/actor_frame_dispatch.c.
  //
  // Only the behaviors that have been ported actually run; everything else is
  // counted by the ActorTrace and left alone. An unported behavior that silently
  // did *something* would be worse than one that does nothing, because the port
  // has no reference trace to catch it.

  // Everything a behavior is allowed to reach outside its own entity. Kept as
  // callbacks so this stays free of script and runtime dependencies, the same
  // way ScriptEnvironment does it.
  struct ActorEnvironment
  {
    EntityPool *entityPool = nullptr;
    const ActorDispatchTable *dispatchTable = nullptr;

    // FUN_00266368: read one bit of the event-flag bank at DAT_00342b70. The
    // bank lives in the script state, so it arrives as a callback rather than as
    // a dependency on the script namespace.
    std::function<bool(std::uint32_t flagId)> eventFlag;

    // DAT_003555bc / iGpffffb64c, the per-frame tick count. Nominally 0x20.
    std::uint32_t frameTicks = 0x20;
  };

  // FUN_0023a068: the freeze gate every behavior opens with. Advances the
  // countdown at +0xBD and the state timer at +0xA4, and returns true when the
  // caller should return early. The last frozen frame still runs, so a behavior
  // resumes on the frame the counter reaches zero.
  bool FUN_0023a068_freeze_gate(OriginalEntity &entity, std::uint32_t frameTicks);

  // FUN_00225bc8: the shared animation-state setter.
  void FUN_00225bc8_set_animation(OriginalEntity &entity, std::uint16_t animation);

  // FUN_00225bf0: the same, plus the movement state at +0x60. Script opcode 0xA8
  // uses it to put the lead player into the state that runs its object script.
  void FUN_00225bf0_set_state_and_animation(OriginalEntity &entity,
                                            std::uint16_t state,
                                            std::uint16_t animation);

  // FUN_0023a568: the fade path, taken instead of the type handler when +0x04
  // has bit 0x800. Fades in, then out, then releases the slot.
  void FUN_0023a568_fade(EntityPool &pool, std::size_t slot, std::uint32_t frameTicks);

  // FUN_002d1ea8: type 0x3A, the treasure chest. See
  // analyzed/actor_behaviors/type_0x3A_treasure_chest.c.
  void FUN_002d1ea8_treasure_chest(OriginalEntity &entity, const ActorEnvironment &environment);

  // True when the port has a body for the behavior at this PS2 address. Used by
  // the loop and by the report; kFUN_00239e78_noOp counts as implemented,
  // because it really is a no-op and listing it as missing would drown the
  // report in noise.
  bool actorHandlerIsImplemented(std::uint32_t handlerAddress);

  // A readable name for a handler address, for the report. Returns nullptr for
  // addresses with no name yet.
  const char *actorHandlerName(std::uint32_t handlerAddress);

  // FUN_00239ce0: slots 2..255, three guards, then the type dispatch.
  void FUN_00239ce0_update_actors(const ActorEnvironment &environment, ActorTrace &trace);

} // namespace orphen::ported::entity
