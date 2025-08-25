## SCR notes

Expanded tracked IDs (added 0x119C, 0x0526, 0x0534, 0x053D).
Signature classification (SIG_A_9e0c011e0b, SIG_B_0b0b02920c) for seeded id04_records.
Full enumeration enumerate_all_id04 with lightweight classes (A, B, UNK).
Counts summary: A=274, B=1 (the 0x133F case), UNK=230.
Seeded id04_records now 9 (newly captured additional seeded IDs).
Observations:

Signature A (9e0c011e0b) is dominant for these subproc IDs.
Only one Signature B instance (the earlier distinctive 0x133F container).

> SIG_B_0b0b02920c
> Only one Signature B instance (the earlier distinctive 0x133F container).

0x133F is the subproc that is basically "permanent" in the opening map. Maybe this indicates some kind of permanent subproc?

---

4508 = 0x119C - CUTSCENE ADVANCER?

I noticed this subproc flashes on screen briefly a lot whenever characters advance their dialog. Maybe this subproc is responsible for advancing the dialogue or other control stuff?

Update: This is actually a no-op!

9c1100009e0c011e0b04

9e opcode just immediately removes itself from the stack. 0c 01 1e = -1. -1 tells it to remove "itself" from the stack.

This is seemingly used by the cutscene queue to add no-op flag checks or waits into the queue. Still working on how exactly that works.

---

77360e040000000b0b0e080000000b0c150b9e

opcode 77 seems to be used to set properties on objects:

77 opcode
arg 1: 0x36 opcode (arg 1: 0e(immediate u32) = 0x4) - read item 4 out of the scene work list
arg 2: 0e(immediate u32) = 0x8
arg 3: 0c(immediate u8) = 0x15

Finds an object ID in the scene worklist at index 4
Looks up the relevant offset in `set_active_effect_parameter.c`:

```c
  case 0x08:
    puGpffffb0d4[0x50] = val16;
    break; // +0x0A0
```

Sets the value to 0x15

I don't know how much relevance this has, but I spent a fair amount of debugging it so I wanted to write it down.

Copilot was also not very smart about it :)

---

Attempted to patch:
`00237EC8 2AE20202`
`slti v0,s7,0x0002` -> `slti v0,s7,0x0202`

All it does is make the text scroll faster. It seems that the text still yields for something else to tell it to advance...

---

Pretty good solution for now:

`1C` opcode is a "delay". Change all following bytes to `01` to minimize the delay

`1A` opcode seems to be "wait for audio to finish". These are usually at the ends of sentences. These are a little trickier, but I think can just be replaced with `20` for right now.
