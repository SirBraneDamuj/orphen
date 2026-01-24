/*
 * Controller State Management and Vibration Handler - FUN_0023af50
 *
 * This function manages PS2 controller port states, vibration/actuator timers,
 * and controller configuration changes. It's called whenever controller configuration
 * changes are detected (e.g., controller connected/disconnected, analog mode toggled).
 *
 * The function handles:
 * 1. Controller port state monitoring (via SIF RPC calls to IOP)
 * 2. Controller initialization and configuration
 * 3. Vibration/actuator timer management (countdown and state updates)
 * 4. Analog controller type detection (type 7 = DualShock with analog)
 * 5. Controller data buffer management (at 0x00571a00 + port*0x20)
 *
 * Data structures:
 * - Controller port data: 0x00571a00 + (port * 0x20) [32 bytes per port]
 * - Vibration state: 0x00571a40 + (port * 6) [6 bytes per port]
 *   - +0x00 (ushort): Large motor timer (heavy vibration)
 *   - +0x02 (ushort): Small motor timer (light vibration)
 *   - +0x04 (byte): Large motor intensity
 *   - +0x05 (byte): Controller state (-1=disconnecting, 0=normal, 1=initializing)
 * - Motor state buffer: 0x0031d098 + (port * 6) [actuator command bytes]
 *   - +0x00: Large motor command (0x00=off, 0x01=on, 0x40=reset)
 *   - +0x01: Small motor intensity (0x00-0xFF)
 *
 * Port states (from FUN_00303180):
 * - 0-1: Controller disconnected/not ready
 * - 2: Controller ready (analog mode detected)
 * - 6: Controller in stable state (normal operation)
 * - Other: Transitional states
 *
 * This function is called from process_controller_input when controller
 * configuration changes (controller type, connection state, etc.).
 *
 * Original function: FUN_0023af50
 * Address: 0x0023af50
 */

#include <stdint.h>
#include <stdbool.h>

// PS2 type definitions
typedef unsigned char byte;
typedef unsigned short ushort;

// Forward declarations for PS2 SIF/IOP RPC functions
extern long FUN_00303180(int port, int slot);                     // Get controller port state
extern uint32_t FUN_003030f8(int port, int slot, void *buffer);   // Read controller data
extern long FUN_003034e0(int port, int slot, int cmd, int param); // Controller RPC command
extern int FUN_00303568(int port, int slot, int mode, int lock);  // Set controller mode
extern uint32_t FUN_00303628(int port, int slot, void *data);     // Set actuator state (vibration)
extern int FUN_003036c8(int port, int slot, void *buffer);        // Get controller info/capabilities

// Global variables
extern ushort uGpffffaee0;    // Max controller port index (0=1 port, 1=2 ports, 0xFFFF=none)
extern byte gp0xffffaee8[2];  // Per-port initialization flags (1=needs init)
extern uint32_t gp0xffffaef0; // Controller capabilities buffer
extern ushort uGpffffb64c;    // Frame delta time for timer countdown
extern byte uGpffffb668;      // Controller configuration flags
extern byte cGpffffb6a0;      // Vibration enable flag

// Controller data buffers (in IOP shared memory region 0x00571a00)
extern byte DAT_00571a00[64];  // Controller data: port0=[0x00-0x1F], port1=[0x20-0x3F]
extern ushort DAT_00571a40[6]; // Vibration timers: [port*6+0]=large motor, [port*6+2]=small motor
extern ushort DAT_00571a42[6]; // Small motor timer array (port*6+2)
extern byte DAT_00571a44[12];  // Motor intensity values
extern byte DAT_00571a45[12];  // Controller state flags per port

// Motor command buffer (game-side memory)
extern byte DAT_0031d098[12]; // Motor commands: [port*6+0]=large, [port*6+1]=small intensity
extern byte DAT_0031d099[12]; // Small motor intensity buffer

/*
 * Update controller states and manage vibration timers
 *
 * This function is called once per frame when controllers are active.
 * It polls each controller port, updates timers, and sends vibration
 * commands to the IOP when motor states change.
 *
 * For each active port (0 to uGpffffaee0):
 * 1. Query port state via FUN_00303180
 * 2. Handle initialization if port state < 2 (disconnected)
 * 3. Read controller data via FUN_003030f8
 * 4. Check for analog controller (type 7)
 * 5. Decrement vibration timers (large/small motor)
 * 6. Send actuator commands when timer state changes
 * 7. Handle controller reconnection/disconnection events
 */
void controller_state_change_handler(void)
{
  ushort current_timer;
  bool motor_state_changed;
  short small_motor_timer;
  byte *motor_command_ptr;
  uint32_t max_port_index;
  int vibration_data_offset;
  long port_state;
  long rpc_result;
  int port_index;
  char *init_flag_ptr;
  int controller_data_offset;
  ushort *large_motor_timer_ptr;

  port_index = 0;

  // Check if any controller ports are enabled
  if (uGpffffaee0 != 0xffffffff)
  {
    vibration_data_offset = 0;

    do
    {
      // Get pointer to large motor timer for this port
      large_motor_timer_ptr = (ushort *)(&DAT_00571a40 + vibration_data_offset);

      // Query controller port state via IOP RPC
      port_state = FUN_00303180(port_index, 0);

      if (port_state < 2)
      {
        // Controller disconnected or not ready
        if (*(char *)(vibration_data_offset + 0x571a45) == '\x01')
        {
          // Was initializing - clear motor state
          *(byte *)(vibration_data_offset + 0x31d099) = 0;
        }

        // Mark port as needing initialization
        (&gp0xffffaee8)[port_index] = 1;
        *(ushort *)(&DAT_00571a42 + vibration_data_offset) = 0;
        *(byte *)(vibration_data_offset + 0x571a45) = 0;
        *large_motor_timer_ptr = 0;

        // Clear controller data buffer (16 bytes)
        motor_command_ptr = &DAT_00571a0f + port_index * 0x20;
        controller_data_offset = 0xf;
        do
        {
          *motor_command_ptr = 0xff;
          controller_data_offset = controller_data_offset - 1;
          motor_command_ptr = motor_command_ptr - 1;
        } while (-1 < controller_data_offset);

        max_port_index = (uint32_t)uGpffffaee0;
      }
      else
      {
        // Controller is in ready or stable state
        if ((port_state == 6) || (port_state == 2))
        {
          // Read controller data from IOP
          FUN_003030f8(port_index, 0, &DAT_00571a00 + port_index * 0x20);

          // Query controller capabilities
          rpc_result = FUN_003034e0(port_index, 0, 2, 0);

          if ((rpc_result == 4) &&
              (((uGpffffb668 & 0x40) == 0 &&
                (rpc_result = FUN_003034e0(port_index, 0, 4, 0xffffffffffffffff), rpc_result != 0))))
          {
            // Configure controller for analog mode with pressure sensitivity
            FUN_00303568(port_index, 0, 1, 3);
          }
          else if ((byte)(&DAT_00571a01)[port_index * 0x20] >> 4 == 7)
          {
            // Controller type 7 detected (DualShock analog controller)
            init_flag_ptr = &gp0xffffaee8 + port_index;
            motor_state_changed = false;

            if (*init_flag_ptr == '\0')
            {
              // Controller initialized - process vibration timers
              if (*(char *)(vibration_data_offset + 0x571a45) < '\x01')
              {
                // Normal operation or disconnecting state
                if (*(char *)(vibration_data_offset + 0x571a45) < '\0')
                {
                  // Disconnecting state (-1) - handle timer countdown
                  current_timer = *large_motor_timer_ptr;

                  if (*large_motor_timer_ptr == 0)
                  {
                    // Large motor timer expired
                    motor_command_ptr = (char *)(port_index * 6 + 0x31d098);

                    if (*motor_command_ptr != '\0')
                    {
                      // Turn off large motor
                      *motor_command_ptr = '\0';
                      goto LAB_motor_state_changed;
                    }
                    small_motor_timer = *(short *)(&DAT_00571a42 + vibration_data_offset);
                  }
                  else
                  {
                    // Decrement large motor timer
                    *large_motor_timer_ptr = (ushort)((uint32_t)current_timer - (uint32_t)uGpffffb64c);
                    if ((int)(((uint32_t)current_timer - (uint32_t)uGpffffb64c) * 0x10000) < 0)
                    {
                      *large_motor_timer_ptr = 0;
                    }

                    motor_command_ptr = (char *)(port_index * 6 + 0x31d098);

                    if (*motor_command_ptr == '\0')
                    {
                      // Motor was off, turn it on
                      *motor_command_ptr = '\x01';
                    LAB_motor_state_changed:
                      motor_state_changed = true;
                      small_motor_timer = *(short *)(&DAT_00571a42 + vibration_data_offset);
                    }
                    else
                    {
                      small_motor_timer = *(short *)(&DAT_00571a42 + vibration_data_offset);
                    }
                  }

                  // Process small motor timer
                  current_timer = *(ushort *)(&DAT_00571a42 + vibration_data_offset);
                  if (small_motor_timer == 0)
                    goto LAB_check_intensity;

                  *(short *)(&DAT_00571a42 + vibration_data_offset) =
                      (short)((uint32_t)current_timer - (uint32_t)uGpffffb64c);
                  if ((int)(((uint32_t)current_timer - (uint32_t)uGpffffb64c) * 0x10000) < 0)
                  {
                    *(ushort *)(&DAT_00571a42 + vibration_data_offset) = 0;
                  }

                  motor_command_ptr = (char *)(port_index * 6 + 0x31d099);
                  if (*motor_command_ptr != *(char *)(vibration_data_offset + 0x571a44))
                  {
                    // Update small motor intensity
                    *motor_command_ptr = *(char *)(vibration_data_offset + 0x571a44);
                    goto LAB_send_vibration_command;
                  }
                }
              }
              else
              {
                // Initializing state (1)
                if ((*large_motor_timer_ptr == 0) && (*(ushort *)(&DAT_00571a42 + vibration_data_offset) == 0))
                {
                LAB_check_intensity:
                  motor_command_ptr = (char *)(port_index * 6 + 0x31d099);
                  if (*motor_command_ptr == '\0')
                    goto LAB_send_vibration_command;
                  *motor_command_ptr = '\0';
                }
                else
                {
                  // Decrement both motor timers
                  controller_data_offset = (uint32_t)*large_motor_timer_ptr - (uint32_t)uGpffffb64c;
                  *large_motor_timer_ptr = (ushort)controller_data_offset;
                  if (controller_data_offset * 0x10000 < 0)
                  {
                    *large_motor_timer_ptr = 0;
                  }

                  controller_data_offset = (uint32_t)*(ushort *)(&DAT_00571a42 + vibration_data_offset) -
                                           (uint32_t)uGpffffb64c;
                  *(short *)(&DAT_00571a42 + vibration_data_offset) = (short)controller_data_offset;
                  if (controller_data_offset * 0x10000 < 0)
                  {
                    *(ushort *)(&DAT_00571a42 + vibration_data_offset) = 0;
                  }

                  motor_command_ptr = (char *)(port_index * 6 + 0x31d099);
                  if (*motor_command_ptr != '\0')
                    goto LAB_send_vibration_command;
                  *motor_command_ptr = '\x01';
                }
              LAB_send_vibration_command:
                motor_state_changed = true;
              }

            LAB_send_vibration_command:
              // Send vibration command to IOP if state changed and vibration is enabled
              if ((motor_state_changed) && (cGpffffb6a0 != '\0'))
              {
                FUN_00303628(port_index, 0, port_index * 6 + 0x31d098);
                max_port_index = (uint32_t)uGpffffaee0;
                goto LAB_next_port;
              }
            }
            else
            {
              // Port needs initialization
              *(ushort *)(&DAT_00571a42 + vibration_data_offset) = 0;
              *large_motor_timer_ptr = 0;

              if (port_state == 2)
              {
                // Controller just became ready - initialize vibration
                *init_flag_ptr = '\0';
                *(byte *)(vibration_data_offset + 0x571a45) = 1;
                *(byte *)(port_index * 6 + 0x31d098) = 0x40; // Reset command
                *(byte *)(port_index * 6 + 0x31d099) = 0;
              }
              else if (port_state == 6)
              {
                // Controller in stable state - query capabilities
                rpc_result = FUN_003036c8(port_index, 0, &gp0xffffaef0);
                if (rpc_result != 0)
                {
                  // Capabilities retrieved - mark as disconnecting
                  *(byte *)(vibration_data_offset + 0x571a45) = 0xff;
                  *init_flag_ptr = '\0';
                  *(byte *)(port_index * 6 + 0x31d098) = 0;
                  *(byte *)(port_index * 6 + 0x31d099) = 0;
                }
              }
            }
          }
          else
          {
            // Not an analog controller - disable vibration support
            *(ushort *)(&DAT_00571a42 + vibration_data_offset) = 0;
            (&gp0xffffaee8)[port_index] = 1;
            *large_motor_timer_ptr = 0;
          }
        }
        else
        {
          // Controller in unknown/transitional state - poll data
          FUN_003030f8(port_index, 0, &DAT_00571a00 + port_index * 0x20);
        }

        max_port_index = (uint32_t)uGpffffaee0;
      }

    LAB_next_port:
      port_index = port_index + 1;
      vibration_data_offset = port_index * 6;
    } while (port_index < (int)(max_port_index + 1));
  }

  return;
}
