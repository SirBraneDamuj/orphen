/*
 * Actor behavior for type 0x19 -- Orphen's bandana
 * Original: FUN_00213720 (0x00213720)
 * Reached from: FUN_00239ce0 -> PTR_FUN_0031c6c0[0x19 - 1] == 0x00213720
 * Created by:   FUN_00251e40 (0x00251e40), called from FUN_0022a418:256
 * Released by:  FUN_00213640 (0x00213640)
 *
 * Summary
 * - The bandana is not part of Orphen's mesh. It is a separate entity in
 *   reserved pool slot 4 (0x0058C610), type 0x19, model grp_001E, attached to
 *   the player's *neck* bone and simulated as two nine-link ropes whose results
 *   are written straight into the scripted bone override table.
 * - Nothing calls FUN_00213720 by name. It is only ever reached through the
 *   primary actor dispatch table, which is what identifies it as a behavior
 *   rather than as a helper -- every neighbouring entry in that table is the
 *   generic no-op FUN_00239e78 or the party-member shell FUN_0025ab68.
 * - Every field below is confirmed live against the EE dump `s01_e24.bin`.
 *
 * ---------------------------------------------------------------------------
 * Creation
 * ---------------------------------------------------------------------------
 *
 * FUN_0022a418 clears pool slots 10..255, then releases slots 2..9 by address
 * (0x58C260 .. 0x58CF48, one 0x1D8 stride apart), then:
 *
 *     FUN_00251e40(0x58beb0);          // the lead player, pool slot 0
 *
 * and FUN_00251e40 is, in full:
 *
 *     if (player.typeId != 1) return 0;              // Orphen only
 *     FUN_00229c40(0x58c610, 0x19);                  // build slot 4 as type 0x19
 *     slot4[+0x192] = poolSlotOf(player);            //   == 0
 *     slot4[+0x194] = -FUN_0020dd78(player, 7);      //   == -32, negated
 *     slot4[+0x24]  = uGpffff88b0;                   //   == 0.04
 *     slot4[+0x28]  = uGpffff88b4;                   //   == 0.08
 *     slot4[+0x20]  = 0;
 *
 * The type-1 test is the whole of the "only Orphen wears one" rule. Swap the
 * party leader and no bandana is built.
 *
 * FUN_0020dd78(entity, role) is a **bone role lookup**: it scans the model's
 * submesh table (PSC3 header +0x08, stride 0x14) for the first entry whose byte
 * at +0x0B has low nibble == role, and returns its index. This is how native
 * code finds a semantic bone in a model it was not authored against. Roles seen
 * across the executable are 1..11; on grp_0001 (Orphen):
 *
 *     role 1  = bone 33  head        role 7  = bone 32  neck
 *     role 2  = bone  9  chest       role 8  = bone  8  right foot
 *     role 3  = bone 18  finger      role 9  = bone  4  left foot
 *     role 4  = bone 14  right hand  role 10 = bone 24  left hand
 *     role 5  = bone 17  finger      role 11 = bone 16  finger
 *
 * Roles 1 and 2 are the pair FUN_00257c78 turns to look at things; role 4 is
 * the one weapons attach to. Role 7 -- the neck -- is the bandana's.
 *
 * ---------------------------------------------------------------------------
 * How an attached entity is placed: FUN_0020cdc0
 * ---------------------------------------------------------------------------
 *
 * The entity root matrix has three branches, selected by +0x192 and +0x194:
 *
 *   +0x192 < 0                 standalone. +0x20..+0x28 is a world position and
 *                              +0x5C a facing. Everything in the pool but this.
 *   +0x192 >= 0, +0x194 < 0    follow a bone's *position only*. FUN_0020dc88
 *                              transforms +0x20..+0x28 -- which is now a
 *                              bone-local offset -- by the parent's bone matrix,
 *                              and the entity keeps its own facing plus
 *                              fGpffff80cc (pi/2). **This is the bandana.**
 *   +0x192 >= 0, +0x194 >= 0   rigid: build the local matrix, then concatenate
 *                              the parent's bone matrix outright, so the child
 *                              inherits the bone's orientation too.
 *
 * So the knot does not roll with the head; it hangs at the neck bone's origin
 * plus (0, 0.04, 0.08) and faces the way the actor does. FUN_00252a18 requires
 * +0x192 negative before an entity can be an interaction candidate, which is
 * what stops the player targeting his own bandana.
 *
 * ---------------------------------------------------------------------------
 * The model: grp_001E
 * ---------------------------------------------------------------------------
 *
 * 20 submeshes / bones, 41 vertices, 20 primitives.
 *
 *     bone  0   the knot, 5 vertices, 2 primitives
 *     bones 1..9    tail A, tip at bone 1
 *     bones 10..18  tail B, tip at bone 10
 *     bone 19   an empty second root
 *
 * All 18 tail bones are **siblings under bone 0**, not two chains -- they have
 * to be, because the simulation hands each one an absolute translation relative
 * to the knot rather than a link-to-link one.
 *
 * Every primitive's first subdraw index is 0x8000. That is not a subdraw index:
 * FUN_00212058 line 106 only skips a pass on exactly -1, and FUN_002129b8 lines
 * 85-111 mask bit 15 off anything else and use the remainder as a *colour*
 * index, drawing the pass untextured and opaque. 0x8000 & 0x7FFF == 0, and
 * colour entry 0 is (191, 0, 0). The bandana is flat red by construction; its
 * model record carries tex id 0.
 *
 * **An untextured pass's colour is not a modulator.** FUN_00212058's mode 2
 * never sets TME (`uVar7 = 0x41` rather than `0x412`), so the colour register
 * goes straight to the framebuffer over 0..255 instead of through the GS's
 * (Ct * Cv) >> 7, where 0x80 means x1.0. 191 makes no sense as a modulator --
 * x1.49, which saturates to pure red -- and perfect sense as a colour, 0xBF.
 * Reading it the modulate way rendered the bandana about twice as bright as the
 * emulator does and flattened out its shading entirely.
 *
 * Every primitive also has +0x0C and +0x0D at zero. +0x0C zero means no
 * specular pass. +0x0D zero is the *maximum* light floor: draw header byte 14 is
 * its complement scaled by 1/320 (see docs/vu1_microprogram.md), so the diffuse
 * term is floored at 0.796875 and the bandana is authored to be near-flat-lit.
 * In s01_e024 that floor is a no-op, because light 0's intensity on these
 * normals already exceeds it -- so it is not what made the port too bright.
 *
 * ---------------------------------------------------------------------------
 * The simulation: FUN_00213720
 * ---------------------------------------------------------------------------
 *
 * State lives at DAT_0054EE00, outside the entity: two chains 0x140 apart, ten
 * 0x20-byte slots each. Slot 0 is the anchor rather than a simulated segment,
 * and its +0x1C doubles as the chain's gravity.
 *
 *     seg +0x00 .. +0x08   cleared at init and never written again
 *     seg +0x0C .. +0x14   world position
 *     seg +0x18            scale, seeded to 1.0
 *     seg0 +0x1C           this chain's per-frame fall distance
 *
 * On the first tick (entity +0x94 == 0) every segment is parked at z = -100.0
 * so the first frame's length constraint snaps the tail into a straight line
 * under the anchor instead of growing it out of a point.
 *
 * Then, once per frame, provided the entity's animation id (+0xA0) is 0:
 *
 *   1. Walk +0x192 to the root of the attachment chain; copy its +0x5C (facing)
 *      and +0x134 (fade level) onto the bandana.
 *   2. anchor = FUN_0020dc88(self, bone 0, DAT_00315190) -- the knot bone's own
 *      world origin, from *last* frame's palette, since FUN_0020c5a8 has not run
 *      yet this frame. DAT_00315190 is six zero floats.
 *   3. Per chain: store the anchor into slot 0, and every 64th frame
 *      (DAT_003555b4 & 0x3F) re-roll gravity to (rng & 3) * 0.004 + 0.006.
 *   4. Per segment 1..9:
 *        position.z -= gravity
 *        if |seg - prev| > 0.025: pull seg back onto that radius   <- the rope
 *        rel = seg - anchor, with  a = -facing - pi/2  and
 *          forward = rel.x * sin(a) + rel.y * cos(a)      // lines 141, [0x15]
 *          lateral = rel.x * cos(a) - rel.y * sin(a)      // lines 144, [0x16]
 *        if forward < DAT_003151a0[i] and rel.z < 0: forward = DAT_003151a0[i]
 *        lateral += sin(fmod((tick + i*128) / P, 2pi)) * (i + 5) / 4000
 *        forward += sin(fmod(tick * (i + 8) / D, 2pi)) * (i + 5) / 4000
 *        FUN_0020d8c0(self, chain*9 + (10 - i),
 *                     { 0, 0, +-lateral*30,                      // rotation
 *                       lateral +- 0.011, forward, rel.z,        // translation
 *                       seg.scale }, 1)                          // scale
 *
 * **FUN_00305218 is sinf and FUN_00305130 is cosf**, not the other way round.
 * 0x00305218's small-|x| path calls `__kernel_sin(x, 0, 0)` and its `n&3` switch
 * is fdlibm's sine; 0x00305130's takes `__kernel_cos(x, 0)`. `analyzed/
 * battle_logo_state_manager.c` and `docs/update_main_character_entity_analysis.md`
 * label them backwards. It matters here: swapping them rotates the frame a
 * quarter turn, which puts DAT_003151a0's body clamp -- up to 0.067, six times
 * the 0.011 tail spread -- on the sideways axis, so the tails drift into the
 * neck instead of trailing behind.
 *
 * `forward` works out to -(rel . facingDirection), i.e. it is positive *behind*
 * the actor. That is why every DAT_003151a0 entry is positive: the clamp pushes
 * a tail that has swung under the chin back out behind the shoulders.
 *
 * P is 512 for chain 0 and 480 for chain 1, D is 9000 and 7000, and the roll
 * sign and the +-0.011 spread flip between them -- so the two tails hang 22 mm
 * apart, mirror each other, and run at slightly different periods so they drift
 * out of phase instead of moving as one.
 *
 * DAT_003151a0 is the back-of-the-head clamp, indexed by segment:
 * {0, 0, 0.04, 0.05, 0.058, 0.063, 0.067, 0.067, 0.067, 0.067}. A segment
 * hanging below the knot may not sit further forward than its limit, which is
 * what stops a tail swinging through Orphen's shoulders when he turns.
 *
 * The override duration is 1, so every bone snaps rather than easing --
 * FUN_0020d378 takes the override branch before it ever looks at the keyframe
 * track, and grp_001E has no animation worth blending toward anyway.
 *
 * Note that the wave terms are applied to the *output* only. They never go back
 * into the segment positions, so both chains simulate identically and differ
 * only in what is drawn.
 *
 * The `entity +0x198 == 1` branch adds a random tug to each segment every
 * frame. Nothing writes +0x198 on slot 4 -- FUN_00229c40 clears the slot and
 * FUN_00251e40 does not touch it -- and the EE dump has it at zero, so that
 * path is dead in the shipped game.
 *
 * ---------------------------------------------------------------------------
 * Constants (SLUS_200.11)
 * ---------------------------------------------------------------------------
 *
 *   DAT_003520c4  0.003      initial gravity
 *   DAT_003520c8  pi/2       the yaw the anchor frame is built from
 *   DAT_003520cc  0.004      gravity re-roll step
 *   DAT_003520d0  0.006      gravity re-roll base
 *   DAT_003520d4  0.011      half the gap between the two tails
 *   DAT_003520d8  0.01       +0x198 jitter, x
 *   DAT_003520dc  0.004      +0x198 jitter, z
 *   DAT_003520e0  0.025      rope link length
 *   DAT_003520e4/ec/f4  2pi  the fmod moduli
 *   DAT_003520e8  7000.0     chain 1 forward-sweep divisor
 *   DAT_003520f0  9000.0     chain 0 forward-sweep divisor
 *   uGpffff88b0   0.04       knot offset from the neck bone, y
 *   uGpffff88b4   0.08       knot offset from the neck bone, z
 *   fGpffff80cc   pi/2       facing bias for an attached entity
 *   DAT_00315190  {0,0,0}    the local point the anchor is taken at
 *   DAT_003151a0             the forward-limit table above
 *
 * DAT_003555b4 and DAT_003555b8 have no writer anywhere in the decompiled
 * sources. Three EE dumps settle what they are: b8 == b4 * 32 exactly in all
 * three, so b4 counts frames and b8 accumulates DAT_003555bc -- the per-frame
 * tick count, nominally 0x20. A dropped frame therefore advances a wave phase
 * by the time it actually took rather than by one step.
 *
 * ---------------------------------------------------------------------------
 * Confirmation from s01_e24.bin
 * ---------------------------------------------------------------------------
 *
 *   slot 4 (0x0058C610):  type 25, +0x94 = 1, +0xA0 = 0, +0x198 = 0,
 *                         +0x20..+0x28 = (0.0, 0.04, 0.08),
 *                         +0x192 = 0, +0x194 = 0xE0 (-32),
 *                         +0x15C = 0x00DCF480 (a loaded PSC3)
 *
 *   grp_0001 bone 32's matrix at 0x00357E00 + 0*0xA80 + 32*0x40 has translation
 *   (-3.26430, -12.75000, 0.86170). Transforming (0, 0.04, 0.08) by it gives
 *   (-3.31246, -12.75000, 0.93707); the chain state at 0x0054EE00 holds its
 *   anchor at (-3.31224, -12.75000, 0.93703) and slot 4's own bone 0 sits at
 *   (-3.31242, -12.75000, 0.93703).
 *
 *   The nine links of chain 0 then step down in z by exactly 0.025 each, from
 *   0.93703 to 0.71204, and the two chains hold gravity 0.018 and 0.010 -- both
 *   members of the {0.006, 0.010, 0.014, 0.018} re-roll set.
 *
 * ---------------------------------------------------------------------------
 * Teardown: FUN_00213640
 * ---------------------------------------------------------------------------
 *
 * Guarded on `DAT_0058c610 == 0x19` so it does nothing if slot 4 holds anything
 * else. Its nested loop clears bone overrides 9..0 and then 18..9 -- bone 9
 * twice, and bone 0 (the knot, which the simulation never drives) once -- and
 * toggles bit 0 of the entity's +0x08 to hide or show it. DAT_0058c6b0 is the
 * previous enable state, so the clear only runs on a real transition.
 *
 * FUN_0025fd10 drives the same +0x08 bit, alongside bit 0x4000 of +0x04.
 */
