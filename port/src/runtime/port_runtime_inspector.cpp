// The fly camera's entity tree and inspector window: what the harness can say
// about the pool, read once per rendered frame. Nothing here writes the pool,
// and none of it runs on the simulation step.

#include "runtime/port_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace orphen::port
{
  namespace
  {
    namespace entity = orphen::ported::entity;
    using orphen::harness::EntityTree;
    using orphen::harness::EntityTreeEntry;
    using orphen::harness::InspectorLine;
    using orphen::harness::InspectorLines;
    using orphen::ported::psm2::Vec3;

    constexpr float kDegreesPerRadian = 57.2957795f;

    std::string hex(std::uint32_t value, int width)
    {
      std::ostringstream stream;
      stream << std::uppercase << std::hex << std::setw(width) << std::setfill('0') << value;
      return stream.str();
    }

    std::string fixed(float value, int precision = 2)
    {
      std::ostringstream stream;
      stream << std::fixed << std::setprecision(precision) << value;
      return stream.str();
    }

    std::string vec(float x, float y, float z)
    {
      return "(" + fixed(x) + ", " + fixed(y) + ", " + fixed(z) + ")";
    }

    std::string slotName(std::int32_t slot) { return slot < 0 ? "none" : std::to_string(slot); }

    const char *handlerSourceName(entity::ActorHandlerSource source)
    {
      using Source = entity::ActorHandlerSource;
      switch (source)
      {
      case Source::Streamed: return "streamed";
      case Source::Shared: return "shared";
      case Source::Secondary: return "secondary";
      case Source::Tertiary: return "tertiary";
      case Source::Primary: return "primary";
      case Source::None:
      default: return "none";
      }
    }

    const char *slotStatusName(entity::SlotStatus status)
    {
      switch (status)
      {
      case entity::SlotStatus::ScriptSpawned: return "script spawned";
      case entity::SlotStatus::Allocated: return "allocated";
      case entity::SlotStatus::Free:
      default: return "free";
      }
    }

    // "FUN_002cd0a0 (enemy)" -> "enemy". The tree has room for the noun, not
    // the address.
    std::string shortHandlerName(const char *name)
    {
      if (name == nullptr)
      {
        return {};
      }
      const std::string full(name);
      const auto open = full.find('(');
      const auto close = full.rfind(')');
      if (open == std::string::npos || close == std::string::npos || close <= open)
      {
        return full;
      }
      return full.substr(open + 1, close - open - 1);
    }

    void heading(InspectorLines &lines, std::string text)
    {
      if (!lines.empty())
      {
        lines.push_back({});
      }
      lines.push_back({std::move(text), true});
    }

    void line(InspectorLines &lines, std::string text) { lines.push_back({std::move(text), false}); }
  } // namespace

  void PortRuntime::updateFlyCamera(float deltaSeconds, const InputSnapshot &input)
  {
    // Rebuilt every frame the tree can be seen, so it is never a frame behind
    // what the click is aimed at.
    if (mapViewer_.flyCameraActive() || input.toggleFlyCameraRequested)
    {
      mapViewer_.setEntityTree(buildEntityTree());
    }
    mapViewer_.updateFlyCamera(deltaSeconds, input);
    if (const auto slot = mapViewer_.inspectedEntitySlot(); slot.has_value())
    {
      mapViewer_.setEntityInspectorLines(describeEntity(*slot));
    }
  }

  bool PortRuntime::selectEntity(std::size_t slot)
  {
    if (!mapViewer_.selectEntity(slot))
    {
      return false;
    }
    mapViewer_.setEntityInspectorLines(describeEntity(slot));
    return true;
  }

  std::string PortRuntime::entityInspectorTitle() const
  {
    const auto slot = mapViewer_.inspectedEntitySlot();
    if (!slot.has_value())
    {
      return {};
    }
    const auto &actor = entityPool_.slot(*slot);
    return "Entity " + std::to_string(*slot) + " - type 0x" +
           hex(static_cast<std::uint16_t>(actor.typeId00), 4);
  }

  orphen::harness::EntityTree PortRuntime::buildEntityTree() const
  {
    constexpr std::size_t kSlots = entity::kEntitySlotCount;
    std::array<bool, kSlots> occupied{};
    // Slot 0 is the lead and is built from its own descriptor rather than
    // allocated like the rest; FUN_00239ce0's walk starts above it.
    occupied[0] = entityPool_.status(0) != entity::SlotStatus::Free || mapViewer_.hasLeadPlayerView();
    for (std::size_t slot = 1; slot < kSlots; ++slot)
    {
      occupied[slot] = entityPool_.status(slot) != entity::SlotStatus::Free;
    }

    std::array<const SceneObjectView *, kSlots> views{};
    for (const auto &view : mapViewer_.sceneObjectViews())
    {
      if (view.slot < kSlots)
      {
        views[view.slot] = &view;
      }
    }

    // +0x192, kept only where it names an occupied slot other than itself.
    const auto parentOf = [&](std::size_t slot) -> std::optional<std::size_t> {
      const std::int16_t parent = entityPool_.slot(slot).parentSlot192;
      if (parent < 0 || static_cast<std::size_t>(parent) >= kSlots || static_cast<std::size_t>(parent) == slot ||
          !occupied[static_cast<std::size_t>(parent)])
      {
        return std::nullopt;
      }
      return static_cast<std::size_t>(parent);
    };

    std::array<std::vector<std::size_t>, kSlots> children;
    std::vector<std::size_t> roots;
    for (std::size_t slot = 0; slot < kSlots; ++slot)
    {
      if (!occupied[slot])
      {
        continue;
      }
      if (const auto parent = parentOf(slot); parent.has_value())
      {
        children[*parent].push_back(slot);
      }
      else
      {
        roots.push_back(slot);
      }
    }

    EntityTree tree;
    std::array<bool, kSlots> visited{};
    const auto label = [&](std::size_t slot) {
      const auto &actor = entityPool_.slot(slot);
      std::string text = hex(static_cast<std::uint16_t>(actor.typeId00), 4) + " ";
      if (slot == 0)
      {
        text += "lead";
      }
      else if (slot == entity::kCameraSlot && static_cast<std::uint16_t>(actor.typeId00) == entity::kCameraSlotType)
      {
        text += "camera";
      }
      else
      {
        const auto handler = actorDispatchTable_.FUN_00239ce0_resolve(actor.typeId00);
        std::string name = shortHandlerName(entity::actorHandlerName(handler.address));
        text += name.empty() ? handlerSourceName(handler.source) : name;
        // Opcode 0x66 stamps type 0x38 over the real one and parks it at
        // +0x1CE; without it every role reads the same.
        if (actor.typeId00 == 0x38)
        {
          text += " of " + hex(static_cast<std::uint16_t>(actor.originalType1ce), 4);
        }
      }
      if ((actor.descriptorFlags02 & 0x0200u) != 0)
      {
        text += " nodraw";
      }
      return text;
    };
    // An attached entity's own +0x20..+0x28 are bone-local. Its view has the
    // world point when it is drawn; when it is not, its parent's is the best
    // there is.
    const auto origin = [&](std::size_t slot, const Vec3 &parentOrigin, bool hasParent) {
      if (views[slot] != nullptr)
      {
        return views[slot]->worldOrigin;
      }
      const auto &actor = entityPool_.slot(slot);
      if (hasParent)
      {
        return parentOrigin;
      }
      return Vec3{actor.positionX20, actor.positionZ24, actor.positionY28};
    };

    // Depth first, in slot order at every level. The visited set guards a
    // +0x192 cycle, which the original would walk forever.
    struct Pending
    {
      std::size_t slot;
      int depth;
      Vec3 parentOrigin;
      bool hasParent;
    };
    std::vector<Pending> stack;
    for (auto root = roots.rbegin(); root != roots.rend(); ++root)
    {
      stack.push_back({*root, 0, {}, false});
    }
    const auto drain = [&]() {
      while (!stack.empty())
      {
        const Pending pending = stack.back();
        stack.pop_back();
        if (visited[pending.slot])
        {
          continue;
        }
        visited[pending.slot] = true;
        const auto &actor = entityPool_.slot(pending.slot);
        EntityTreeEntry entry;
        entry.slot = pending.slot;
        entry.depth = pending.depth;
        entry.label = label(pending.slot);
        entry.origin = origin(pending.slot, pending.parentOrigin, pending.hasParent);
        entry.radius = actor.radius54;
        entry.height = actor.height58;
        tree.push_back(entry);
        const auto &kids = children[pending.slot];
        for (auto child = kids.rbegin(); child != kids.rend(); ++child)
        {
          stack.push_back({*child, pending.depth + 1, entry.origin, true});
        }
      }
    };
    drain();
    // Whatever only a cycle reaches: list it at the top level rather than lose it.
    for (std::size_t slot = 0; slot < kSlots; ++slot)
    {
      if (occupied[slot] && !visited[slot])
      {
        stack.push_back({slot, 0, {}, false});
        drain();
      }
    }
    return tree;
  }

  orphen::harness::InspectorLines PortRuntime::describeEntity(std::size_t slot) const
  {
    InspectorLines lines;
    if (slot >= entity::kEntitySlotCount)
    {
      return lines;
    }
    const auto &actor = entityPool_.slot(slot);
    const SceneObjectView *view = nullptr;
    for (const auto &candidate : mapViewer_.sceneObjectViews())
    {
      if (candidate.slot == slot)
      {
        view = &candidate;
      }
    }

    heading(lines, "SLOT " + std::to_string(slot) + "  TYPE 0x" +
                       hex(static_cast<std::uint16_t>(actor.typeId00), 4));
    // FUN_0022a418 rebuilds slot 0 from its own descriptor rather than through
    // the allocator, so its status byte says nothing about it.
    line(lines, slot == 0 ? std::string("status    the lead, built outside the allocator")
                          : std::string("status    ") + slotStatusName(entityPool_.status(slot)));
    if (actor.typeId00 == 0x38)
    {
      line(lines, "role of   0x" + hex(static_cast<std::uint16_t>(actor.originalType1ce), 4) +
                      " (+1CE), body 0x" + hex(static_cast<std::uint16_t>(actor.recordId130), 4));
    }
    if (slot == entity::kCameraSlot && static_cast<std::uint16_t>(actor.typeId00) == entity::kCameraSlotType)
    {
      line(lines, "the camera slot; its position is the sound listener");
    }
    {
      const auto handler = actorDispatchTable_.FUN_00239ce0_resolve(actor.typeId00);
      // What FUN_00239ce0's tables say for the type, whether or not the walk
      // reaches this slot.
      std::string text = std::string("handler   ") + handlerSourceName(handler.source);
      if (handler.address != 0)
      {
        text += " 0x" + hex(handler.address, 8);
        line(lines, text);
        const char *name = entity::actorHandlerName(handler.address);
        line(lines, std::string("          ") + (name != nullptr ? name : "unnamed") +
                        (entity::actorHandlerIsImplemented(handler.address) ? "" : "  UNIMPLEMENTED"));
      }
      else
      {
        line(lines, text + " (unresolved)");
      }
    }

    heading(lines, "ATTACHMENT  +192");
    if (actor.parentSlot192 >= 0)
    {
      line(lines, "parent    " + std::to_string(actor.parentSlot192) + "  bone " +
                      std::to_string(actor.attachBone194) +
                      (actor.attachBone194 < 0 ? " (position only)" : " (whole matrix)"));
    }
    else
    {
      line(lines, "parent    none");
    }
    {
      std::string kids;
      for (std::size_t other = 0; other < entity::kEntitySlotCount; ++other)
      {
        if (other != slot && entityPool_.status(other) != entity::SlotStatus::Free &&
            entityPool_.slot(other).parentSlot192 == static_cast<std::int16_t>(slot))
        {
          kids += (kids.empty() ? "" : ", ") + std::to_string(other);
        }
      }
      line(lines, "children  " + (kids.empty() ? std::string("none") : kids));
    }

    heading(lines, "TRANSFORM");
    line(lines, "pos +20   " + vec(actor.positionX20, actor.positionZ24, actor.positionY28));
    if (view != nullptr)
    {
      line(lines, "world     " + vec(view->worldOrigin.x, view->worldOrigin.y, view->worldOrigin.z));
    }
    line(lines, "facing    " + fixed(actor.facingRadians5c * kDegreesPerRadian, 1) + " deg  (+5C " +
                    fixed(actor.facingRadians5c, 3) + ")");
    line(lines, "velocity  " + vec(actor.velocityX3c, actor.velocityZ40, actor.verticalVelocity44));
    line(lines, "floor +4C " + fixed(actor.groundHeight4c) + "  prev " + fixed(actor.previousGroundHeight50));
    line(lines, "contact   0x" + hex(actor.collisionFlags0c, 8) +
                    ((actor.collisionFlags0c & 1u) != 0 ? " grounded" : " airborne"));
    line(lines, "size      radius " + fixed(actor.radius54) + "  height " + fixed(actor.height58));
    line(lines, "hit vol   radius " + fixed(actor.hitVolumeRadius11c) + "  height " +
                    fixed(actor.hitVolumeHeight120));
    if (actor.scale14c != 1.0f || actor.scaleZ150 != 1.0f || actor.rotationX154 != 0.0f ||
        actor.rotationY158 != 0.0f)
    {
      line(lines, "scale     " + fixed(actor.scale14c) + " / " + fixed(actor.scaleZ150) + "  tilt " +
                      fixed(actor.rotationX154, 3) + ", " + fixed(actor.rotationY158, 3));
    }

    heading(lines, "STATE");
    line(lines, "state +60 " + std::to_string(actor.state60) + "  timer +A4 " +
                    std::to_string(actor.stateResetA4));
    line(lines, "anim +A0  " + std::to_string(actor.animationA0) + "  cursor +A8 " +
                    std::to_string(actor.timelineCursorA8));
    line(lines, "pose +AC  " + std::to_string(actor.poseColumnAc) + "  from " +
                    std::to_string(actor.previousPoseColumnAe) + "  blend " + fixed(actor.animationBlend13c));
    if (actor.freezeTimerBd != 0)
    {
      line(lines, "frozen    " + std::to_string(actor.freezeTimerBd) + " frames (+BD)");
    }

    heading(lines, "FLAGS");
    line(lines, "+02 " + hex(actor.descriptorFlags02, 4) + "  +04 " + hex(actor.halfword04, 4) + "  +06 " +
                    hex(actor.flags06, 4) + "  +08 " + hex(actor.halfword08, 4));
    line(lines, "+6C " + hex(actor.flagWord6c, 8) + "  +70 " + hex(actor.flagWord70, 8));
    line(lines, "+96 " + hex(actor.battleFlags96, 2) + "  +AA " + hex(actor.flagsAa, 4));
    {
      std::string notes;
      if ((actor.descriptorFlags02 & 0x0200u) != 0)
      {
        notes += " nodraw";
      }
      if ((actor.halfword08 & 0x40u) != 0)
      {
        notes += " overlay-bucket";
      }
      if ((actor.halfword04 & 0x100u) != 0)
      {
        notes += " no-physics";
      }
      if ((actor.halfword04 & 0x800u) != 0)
      {
        notes += " fading";
      }
      if (!notes.empty())
      {
        line(lines, "         " + notes);
      }
    }

    if (actor.maxHitPoints128 != 0 || actor.attackPower12c != 0 || actor.defence12e != 0)
    {
      heading(lines, "COMBAT");
      line(lines, "hp        " + std::to_string(actor.staggerTimer12a) + " / " +
                      std::to_string(actor.maxHitPoints128) + "  pending " + std::to_string(actor.pendingDamageBe));
      line(lines, "attack    " + std::to_string(actor.attackPower12c) + "  defence " +
                      std::to_string(actor.defence12e));
      line(lines, "last hit  by " + slotName(actor.lastAttackerSlotCc) + "  reaction " +
                      std::to_string(actor.hitReactionBc));
    }

    heading(lines, "LINKS");
    line(lines, "interact  " + slotName(actor.interactTarget68) + " (+68)  blocked by " +
                    slotName(actor.blockedBy64) + " (+64)");
    line(lines, "placement " + slotName(actor.placementRecordIndex98) + " (+98)  record id " +
                    std::to_string(actor.recordId130) + " (+130)");
    if (actor.lightSlot195 >= 0)
    {
      line(lines, "light     " + std::to_string(actor.lightSlot195) + " (+195)");
    }

    heading(lines, "MODEL");
    if (const auto descriptor = descriptorTable_.FUN_00229980_resolve(static_cast<std::uint16_t>(actor.typeId00));
        descriptor.has_value())
    {
      std::string text = "descriptor 0x" + hex(descriptor->recordAddress, 8) + "  model " +
                         std::to_string(descriptor->modelIndex0x00);
      line(lines, text);
      if (descriptor->modelRecordAddress != 0)
      {
        if (const auto record = descriptorTable_.readModelRecord(descriptor->modelRecordAddress);
            record.has_value())
        {
          line(lines, "mesh      grp_" + hex(record->meshId0x00, 4) + "  tex_" + hex(record->texId0x02, 4));
        }
      }
    }
    else
    {
      line(lines, "descriptor unresolved");
    }
    if (view != nullptr)
    {
      line(lines, std::string("drawn     ") + (view->model != nullptr ? "model" : "box only") + "  bones " +
                      std::to_string(view->bonePalette.size()) + "  texture slot " +
                      std::to_string(view->textureSlot));
      line(lines, "fade +134 " + std::to_string(view->fadeLevel) + "  tint +138 " + hex(view->fadeColor138, 6) +
                      "  bias +133 " + std::to_string(view->depthBias133));
    }
    else
    {
      line(lines, "drawn     no");
    }
    return lines;
  }

} // namespace orphen::port
