/*
 * Player Airborne State Update
 * Original: FUN_002534d8
 * Original signature: void FUN_002534d8(undefined8 param_1)
 *
 * Purpose (inferred):
 * - Handles the field player entity while it is in movement state 2, covering
 *   airborne substates 0x0C (jump/rise), 0x0D (fall), and 0x10 (landing).
 * - Contains the debug-gated midair jump restart: when the debug active flag
 *   DAT_003555da is nonzero, FUN_00251ed8 preserves action bit 0x20 in
 *   DAT_00355cc4. If action bit 0x80 is newly pressed, DAT_0035500c carries it
 *   into this function and the airborne substate is forced back to 0x0C.
 *
 * Input/control chain:
 * - FUN_002239c8 passes mapped action flags to FUN_00251ed8.
 * - FUN_00251ed8 writes:
 *     DAT_00355cc4 / uGpffffbd54 = current_action_flags & 0x20,
 *       but clears it unless DAT_003555da / cGpffffb66a is nonzero.
 *     DAT_0035500c / uGpffffb09c = newly_pressed_action_flags.
 * - This function checks DAT_00355cc4 plus DAT_0035500c & 0x80. With the
 *   default button map, 0x20 is Weapon Attack/Circle and 0x80 is Jump/Square.
 *
 * Side effects:
 * - Writes player_entity +0xA0 (airborne substate).
 * - Writes player_entity +0x44 (vertical velocity / jump vector field).
 * - Updates player_entity +0x1BB motion flags during rise/fall transitions.
 * - Calls FUN_00253488 for per-frame airborne movement, FUN_00253468 for landing
 *   cleanup, and FUN_00267d38 for animation/effect transitions.
 *
 * Notes:
 * - The normal grounded jump path is in FUN_00256bb8 and requires the entity
 *   ground/contact flag. This debug path is evaluated inside the airborne
 *   state handler, so it bypasses the grounded jump gate.
 * - The original substate is cached before the debug restart writes 0x0C; this
 *   preserves Ghidra's control flow and may cause part of the restart to become
 *   visible on the following frame when the player was already falling.
 */

typedef unsigned char byte;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned short undefined2;
typedef unsigned int undefined4;
typedef unsigned long long undefined8;

extern undefined4 DAT_0035287c; // Vertical seed written when debug midair restart is requested.
extern uint DAT_00355cc4;       // uGpffffbd54: debug-gated current action bit 0x20 latch.
extern uint DAT_0035500c;       // uGpffffb09c: newly pressed mapped action flags.
extern undefined4 DAT_00355000; // fGpffffb090: jump vector tunable in debug JUMP TEST.

extern void FUN_00253488(undefined8 player_entity);
extern void FUN_00253468(undefined8 player_entity);
extern void FUN_00252d88();
extern undefined8 FUN_00251c80(undefined8 player_entity, int substate);
extern undefined8 FUN_00255d88(undefined8 player_entity, int state);
extern void FUN_00267d38(undefined8 animation_or_effect, undefined8 player_entity);

void update_player_airborne_state(undefined8 player_entity)
{
    byte motion_flags;
    short original_substate;
    undefined4 debug_restart_vertical_seed;
    undefined8 animation_or_effect;
    int entity;
    float vertical_velocity;

    debug_restart_vertical_seed = DAT_0035287c;
    entity = (int)player_entity;
    original_substate = *(short *)(entity + 0xa0);

    if ((DAT_00355cc4 != 0) && ((DAT_0035500c & 0x80) != 0))
    {
        *(undefined2 *)(entity + 0xa0) = 0x0c;
        *(undefined4 *)(entity + 0x44) = debug_restart_vertical_seed;
    }

    if (original_substate == 0x0c)
    {
        if (*(short *)(entity + 0xa8) < 4)
        {
            FUN_00253488(player_entity);
            return;
        }

        if (*(short *)(entity + 0xa8) == 4)
        {
            if ((*(ushort *)(entity + 6) & 8) != 0)
            {
                *(undefined4 *)(entity + 0x44) = DAT_00355000;
                FUN_00253488(player_entity);
                return;
            }

            vertical_velocity = *(float *)(entity + 0x44);
        }
        else
        {
            vertical_velocity = *(float *)(entity + 0x44);
        }

        motion_flags = *(byte *)(entity + 0x1bb);
        *(byte *)(entity + 0x1bb) = motion_flags | 2;

        if (vertical_velocity < 0.0f)
        {
            *(byte *)(entity + 0x1bb) = (motion_flags & 0xef) | 2;
            *(undefined2 *)(entity + 0xa0) = 0x0d;
        }

        if ((*(uint *)(entity + 0x0c) & 1) != 0)
        {
            *(undefined2 *)(entity + 0xa0) = 0x10;
            FUN_00253468(player_entity);
            return;
        }
    }
    else
    {
        if (original_substate != 0x0d)
        {
            if (original_substate != 0x10)
            {
                FUN_00252d88(player_entity);
                return;
            }

            if ((*(ushort *)(entity + 6) & 1) != 0)
            {
                FUN_00252d88();
                return;
            }

            FUN_00253488(player_entity);
            return;
        }

        if ((*(uint *)(entity + 0x0c) & 0x400) != 0)
        {
            *(byte *)(entity + 0x1bb) = *(byte *)(entity + 0x1bb) | 0x10;
            animation_or_effect = FUN_00251c80(player_entity, 0x0d);
            FUN_00267d38(animation_or_effect, player_entity);
        }

        if ((*(uint *)(entity + 0x0c) & 1) != 0)
        {
            *(undefined2 *)(entity + 0xa0) = 0x10;
            FUN_00253468(player_entity);

            if ((*(byte *)(entity + 0x1bb) & 0x10) != 0)
            {
                return;
            }

            animation_or_effect = FUN_00255d88(player_entity, 3);
            FUN_00267d38(animation_or_effect, player_entity);
            return;
        }
    }

    FUN_00253488(player_entity);
}
