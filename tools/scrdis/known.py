"""Unreferenced bodies whose purpose has been established, keyed by scene and offset.

The sweep in `Program.sweep` only says a body decodes and that nothing reachable
names it. An entry here records what it turned out to be and how that was
checked, so the explorer can show it in place of the generic warning.
"""
from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Known:
    label: str   # a few words, shown beside the name
    note: str    # what it is and how that was established (HTML allowed)


KNOWN: dict[tuple[int, int, int], Known] = {
    (1, 24, 0x063C): Known(
        "dead: NPC patrol test",
        "An NPC body (no subproc id, like 186 of the 190 <code>make_npc</code> bodies). Runs a four-point "
        "loop, (-1.04, -1.99) &rarr; (1.04, -1.99) &rarr; (1.0, -11.25) &rarr; (-1.0, -11.25), and carries "
        "the lead while the lead stands on it; interacting stops it and restores the type. Probably meant "
        "for Zeus (<code>work[5]</code>, type 6), whose ride <code>tick</code> also handles. Nothing starts it: "
        "no <code>0x65</code>/<code>0x66</code>/<code>0xB7</code> names it, no debug-menu entry writes "
        "<code>+0x130</code>, and on hardware no type 0x38 entity exists after load. JP <code>scr8</code> has "
        "the same body, also unreferenced. Forced on in PCSX2 (Zeus slot 22: <code>+0x130 = 0x63C</code>, "
        "<code>+0x1CE = 6</code>, type 0x38) it runs the loop as described."),
    (1, 24, 0x070B): Known(
        "dead: light spin test",
        "Spins the scene light: ramp 0 &rarr; 359&deg; at 5&deg; a frame into "
        "<code>set_light_direction_polar</code>, one turn every 72 frames. Nothing installs it, and on hardware "
        "all 62 slots are empty after load. JP <code>scr8</code> has the same body (subproc 235 there), also "
        "unreferenced; JP also has a third, a <code>work[7]</code> debug party menu, that the US build dropped. "
        "Forced on in PCSX2 (slot 0 of <code>DAT_00355CF4</code> = blob + 0x70B) the light direction at "
        "<code>DAT_003439C8</code> turns as described."),
}


def lookup(scene: tuple[int, int] | None, offset: int) -> Known | None:
    if scene is None:
        return None
    return KNOWN.get((scene[0], scene[1], offset))
