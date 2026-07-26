# Cold Pull Guide (INDX)

This guide explains the **Maintenance → Cold Pull (INDX)** tool: what it does, how
to run it, and what to do when it goes wrong.

A cold pull clears a partially clogged nozzle. You melt fresh filament into the
hot end, let it cool until it grips the debris inside, then yank it out — the
plug comes away with the carbonised material embedded in it.

> **Scope.** This tool targets the **CORE One INDX** (`COREONE_INDX` /
> `COREONEL_INDX`). Prusa's own built-in cold-pull wizard (`M1702`) is *not*
> enabled for INDX — the feature gate at `ProjectOptions.cmake:716` lists only
> MK3.5, MK4, XL, COREONE and COREONEL, so `M1702` compiles to an empty stub on
> INDX and the menu entry does not exist. That absence is why this tool exists.

---

## Contents

1. [When to use it](#1-when-to-use-it)
2. [Prerequisites](#2-prerequisites-required)
3. [Choosing how to run it](#3-choosing-how-to-run-it)
4. [Parameters](#4-parameters)
5. [Walkthrough](#5-walkthrough)
6. [Reading the result](#6-reading-the-result)
7. [Cancelling](#7-cancelling-part-way)
8. [Troubleshooting](#8-troubleshooting)
9. [How it works](#9-how-it-works)

---

## 1. When to use it

Good candidates:

- Under-extrusion that survives a nozzle-temperature increase
- Inconsistent flow, or extrusion that curls off to one side
- A colour or material change that keeps bleeding old filament
- Clicking from the extruder under normal print flow rates

**A cold pull only reaches the melt zone.** If filament cannot enter the hot end
at all, the blockage is *above* the melt zone and no cold pull will clear it —
see [§8](#8-troubleshooting). The procedure includes a check for exactly this,
early enough to stop before wasting twenty minutes.

Plan for **10–20 minutes**, most of it cooling. You must stay at the printer:
it stops and waits for knob presses at five points.

---

## 2. Prerequisites (required)

The dialog opens with a pre-flight gate listing these. **Continue stays disabled
until all boxes are ticked** — that is deliberate. None of these can be set or
even *read* over G-code; all are GUI-only settings, so the host has no way to
check them for you.

| Do this at the printer | Why it matters |
|---|---|
| **Settings → Filament Sensor → OFF** | Left on, the printer grabs and autoloads the filament while you are hand-inserting it. |
| **Settings → Auto Retract → OFF** | Left on, the printer can treat the filament as retracted and **silently discard** the extrusion and pull moves. The procedure appears to run normally while nothing actually moves. |
| **Remove the PTFE tube** from this tool | The pulled plug travels up and out of the top port. With the tube fitted there is nowhere for it to go. |
| **Settings → Hardware → Experimental Settings → "Serial Printing Screen" → OFF** | **Serial route only.** See the warning below. Leaving this screen prompts you to save and reboot. |
| Light-coloured PLA on hand; stay at the printer | PLA shows extracted debris clearly. Heaters switch off after ~30 minutes unattended (safety timer). |

> ### ⚠️ Why "Serial Printing Screen" matters
>
> With it **on**, the firmware treats a serial session as a print job. A
> **5-second inactivity timeout** then runs the *end-of-print* sequence in the
> middle of your procedure — nozzle wipe, **tool dock**, steppers off — while
> the host is still sending extrusion commands. The next `G1 E` hits a machine
> with no nozzle attached and the printer crashes with
> **`E move without tool`** (`planner.cpp:2177`).
>
> This is a real firmware bug, not a misconfiguration; it was reproduced on
> hardware. The tool now works around it in code (see [§9](#9-how-it-works))
> and the serial route has since completed successfully on hardware — but
> turning the setting off removes the hazard at its source, so it stays a
> prerequisite.
>
> **The G-code routes are unaffected** — a print job is driven by the media
> queue and the normal print state machine, so this timeout never applies.

Light-coloured PLA is strongly preferred. Black filament makes it nearly
impossible to judge whether the tip came out clean.

---

## 3. Choosing how to run it

**Maintenance → Cold Pull (INDX)…** offers three routes under *How to run it*.
All three run the identical procedure and show the identical prompts on the
printer's screen; only the delivery differs.

| Route | Use when | Notes |
|---|---|---|
| **Run over USB serial from here** *(default)* | You can connect the printer over USB | PrusaSlicer drives the procedure and shows live progress. Cancel restores printer state. Requires the *Serial Printing Screen* prerequisite above. |
| **Save G-code file** | You cannot or would rather not use USB serial | Writes a `.gcode` file. Copy it to a USB drive and start it like any other print. |
| **Upload G-code to the printer** | You have PrusaLink / Connect set up | Same file, uploaded over the network. Only offered when a physical printer with a print host is configured. **Never auto-starts** — you start it from the printer's menu. |

**Serial is the default and has been validated on hardware.** It gives live
progress, and its Cancel button restores printer state (heaters off, guards
re-enabled) — something the G-code routes cannot offer, since stopping a print
part-way leaves session settings behind for you to restore by hand.

The G-code routes exist for anyone who cannot connect over USB, or would rather
not. They run the identical procedure with the identical prompts, and they need
one fewer prerequisite: a print job is driven by the media queue and the normal
print state machine, so the *Serial Printing Screen* setting does not apply to
them.

---

## 4. Parameters

| Field | Range | Default | Meaning |
|---|---|---|---|
| **Nozzle** | 1–8 | 1 | The nozzle to clean, numbered as on the machine. Internally converted to the firmware's 0-based `T` index — nozzle 1 sends `T0`. |
| **Flush temp** | 200–290 °C | 290 | Melting temperature for the flush. Capped at 290 because the firmware clamps settable targets there (`HEATER_0_MAXTEMP` 305 minus a 15 °C margin); a higher value would make the heat-wait never finish. |
| **Pull temp** | 60–120 °C | 80 | Temperature at which the plug is pulled. 80 is field-verified. |

> **INDX temperatures are not comparable to other printers.** INDX measures
> nozzle temperature with a sensor **17 mm above the tip** (`nozzle_tip_position_mm`
> 28 − `temp_sensor_position_mm` 11), and only **15 %** of the resulting gradient
> is compensated (`compensation_factor = 0.15`) — see
> `src/feature/indx_hotend_temp_model/indx_hotend_temp_compensation.cpp`. The
> displayed value therefore reads *higher* than the true melt zone.
>
> Do not substitute numbers from MK4/XL cold-pull guides, which are
> thermistor-based and measure a different thing. If 80 °C will not release the
> plug, raise in **5–10 °C steps**, up to about 120.

---

## 5. Walkthrough

Prompts appear on the **printer's** screen and wait for a knob press. The
sequence is the same on all three routes.

### Before starting
Complete every item in [§2](#2-prerequisites-required).

### Prompt 1 — Setup check
> *Setup check: filament sensor OFF, auto retract OFF, PTFE tube removed.*

A last confirmation before anything heats. Press the knob to continue.

### Prompt 2 — Insert filament
> *Insert light PLA into the top port until it stops at the drive gears.*

Push PLA into the top port until it meets resistance at the gears. **Do not
force it**, and do not try to push it all the way through — the INDX extruder is
geared (~550 steps/mm) and cannot be hand-fed. The motor does the feeding.

The nozzle is still cold here, deliberately: insertion depth only reaches the
gears, so there is no reason to have you working near a hot end.

### Heating and purge (automatic)
The nozzle heats to the flush temperature, then pushes roughly 60 mm of filament
through in three passes. Watch the tip — fresh PLA should appear.

### Prompt 3 — Tip check ⚠️ *the decision point*
> *Tip check. PLA out: press knob to continue. NOTHING out: press knob then Stop print.*

- **PLA came out** → press the knob and continue.
- **Nothing came out** → press the knob, then **Stop the print immediately**.
  There is a 15-second grace window before packing begins.

Nothing extruding means the blockage is *above* the melt zone, where a cold pull
cannot reach it. Continuing wastes twenty minutes and will not help. See
[§8](#8-troubleshooting).

> **If you stop here**, the restore block at the end never runs. Afterwards send
> `M302 S170` and `M591 R`, or simply power-cycle the printer, to restore the
> cold-extrusion guard and stuck-filament detection.

### Packing and cooling (automatic)
The heater drops toward 180 °C while small extrusions pack the melt zone, then
the fan runs full and the hot end cools to 60 °C with a two-minute hold. This is
the longest phase — several minutes with nothing visible happening.

### Prompt 4 — Pull ready
> *Pull ready at 80C. Motor will yank the plug — keep hands clear.*

**Keep hands clear of the head.** The extruder retracts hard (50 mm/s) on the
knob press.

### Prompt 5 — Remove the strand
> *Pull done. Remove strand from top port. Pressing knob starts auto wipe and dock.*

Lift the strand out of the top port and set it aside **before** pressing the
knob. The printer then wipes and docks the tool automatically; doing this first
keeps the strand out of the way of those movements.

### Finishing
The end-of-print sequence runs: nozzle wipe, tool dock, steppers off. Wait for
*Finished*.

---

## 6. Reading the result

Inspect the extracted tip.

| What you see | Meaning |
|---|---|
| **Three thin strands** with visible dark debris | A good pull. The strands are the nozzle's internal geometry. |
| A clean, sharply moulded tip with no debris | The nozzle is clean — you are done. |
| A blunt, stubby tip | Pulled too warm or too soon. Repeat, raising the pull temperature 5–10 °C. |
| Filament snapped off inside | Pulled too cold. Repeat at a higher pull temperature. |

**Repeat until the tip comes out clean** — typically one to three cycles.

### Afterwards
Restore what you changed in [§2](#2-prerequisites-required):

- Filament Sensor → **on**
- Auto Retract → **on**
- Refit the PTFE tube
- *Serial Printing Screen* → back on, if you turned it off and want it

---

## 7. Cancelling part-way

**Serial route.** Click *Cancel* in the progress dialog. The worker stops at the
next step and restores printer state — heaters off, fan off, cold-extrusion
guard and stuck-filament detection re-enabled, motor current restored, steppers
released. If a prompt is showing on the printer, press the knob so the cleanup
commands can run. A second Cancel offers **Force Stop (M112)**, an emergency
stop that requires a printer reset.

**G-code routes.** Press the knob to dismiss any prompt, then **Stop** on the
printer. If you stop before the restore block, send `M302 S170` and `M591 R`
afterwards, or power-cycle.

---

## 8. Troubleshooting

### Nothing extrudes during the purge
The blockage is above the melt zone — in the heatbreak or the cold side — and a
cold pull cannot reach it. Stop the procedure. Options: remove and inspect the
tool, or contact Bondtech (who manufacture the INDX system).

### The plug will not release, or the filament snaps
Pulled too cold. Repeat with the pull temperature raised 5–10 °C. Work upward;
120 °C is the sensible ceiling.

### The tip comes out blunt with no debris
Pulled too warm — the plug released before gripping anything. Lower the pull
temperature 5–10 °C, or make sure the deep-cool phase completed.

### Printer shows `E move without tool` and crashes
The tool was docked mid-procedure while commands were still being sent. On the
serial route this means *Serial Printing Screen* is still on — see the warning
in [§2](#2-prerequisites-required). Reset the printer (the crash dump is saved
and safe to discard), turn that setting off, reboot, and prefer a G-code route.

### The procedure appears to run but nothing moves
**Auto Retract is still on.** The planner discards E moves when it considers the
filament retracted, so everything looks normal while nothing happens. Turn it
off and start again.

### The printer grabs the filament as I insert it
**Filament Sensor is still on**, triggering autoload. Turn it off and start
again.

### Heaters switched off mid-procedure
A prompt was left unanswered for ~30 minutes and the safety timer turned the
heaters off. Restart the procedure and stay at the machine.

### Serial route: "No Prusa printer found on USB serial"
Check the cable, and close anything else holding the port — PrusaLink, another
slicer, OctoPrint, a serial monitor. Override the port with the
`PRUSASLICER_MAINTENANCE_PORT` environment variable if auto-detection picks
wrong.

---

## 9. How it works

The recipe, and why each step is shaped the way it is:

| Phase | Commands | Rationale |
|---|---|---|
| Pick tool | `T<n> M0` | `M0` bypasses tool mapping. **Heating requires a latched tool** — an unlatched hot end never becomes thermally managed, so the heater will not arm. |
| Prepare | `M302 S0`, `M83`, `M591 S0` | Allow cold extrusion (`EXTRUDE_MINTEMP` is 170 and would otherwise silently strip the pull moves), relative E, and disable stuck-filament detection so the deliberate stall does not trip it. |
| Flush | `M109 S290`, 3 × `G1 E20 F120` | Melt and displace old material. |
| Pack | `M104 S180`, 8 × `G1 E2 F60` with dwells | Small extrusions while cooling pack the melt zone so the plug forms a complete cast of the nozzle interior. |
| Deep cool | `M106 S255`, `M109 R60`, `G4 S120` | The plug must grip before the pull. `M109 R` regulates *at* the target and can exit early if the cooling slope flattens, so a fixed dwell follows it. |
| Reheat | `M109 R80` | Just enough softening to release from the walls while still gripping the debris. |
| Pull | `M906 E650`, `G1 E-40 F3000`, `G1 E-30 F1200` | Raised motor current for the stiff plug; a fast yank, then a slower feed to bring the strand up and out. |
| Restore | `M906 E550`, `M591 R`, `M302 S170` | Undo every session change. |

### Why the serial route sends `G4 P1` first

A defence against the firmware bug in [§2](#2-prerequisites-required). Buddy
treats any `G` command — and `M73`/`M74`/`M109`/`M190` — arriving over serial as
the start of a print. The inactivity timestamp is stamped when that command is
*queued*, but the only code that refreshes it runs solely from `State::Printing`,
which cannot be reached until the arming command *finishes*.

Arming with a long-blocking command such as `M109` therefore means the first
timeout check already sees the entire heat-up as elapsed time — instantly
exceeding the 5-second limit and triggering the end-of-print teardown.

Sending `G4 P1` (a 1 ms dwell) first arms the state machine with a command that
completes immediately, so the state reaches `Printing` with a fresh timestamp.
From then on the timer refreshes continuously, and long prompts are safe.

As a second layer, the tool re-issues `T<n>` before each block of E moves. If
anything docks the tool unexpectedly, the re-pick repopulates it and the next
extrusion cannot hit the assert.

### Firmware references

Verified against Prusa-Firmware-Buddy `6.6.3+15625` (commit `ff6658da4`):

| Behaviour | Location |
|---|---|
| `HAS_COLDPULL` excludes INDX | `ProjectOptions.cmake:716` |
| Serial-print arming | `src/common/serial_printing.cpp:56-99` |
| 5-second timeout | `src/common/serial_printing.hpp:33` |
| End-of-print teardown | `src/common/marlin_server.cpp:983` |
| `E move without tool` assert | `lib/Marlin/Marlin/src/module/planner.cpp:2177` |
| Auto-retract planner hook | `lib/Marlin/Marlin/src/module/planner.cpp:2196` |
| M0 message limit (`MAX_CMD_SIZE` 96) | `include/marlin/Configuration_COREONE_adv.h:578` |
| Safety timer | `src/feature/safety_timer/safety_timer.hpp:40` |

---

## Appendix — running the G-code by hand

The generated file is plain, readable G-code with a documented header; you can
open and edit it. Two conventions to preserve if you do:

- **`M0` messages must be ≤ 95 characters** for the whole line, must start with
  a plain word, and must contain no semicolons. Longer lines are cropped
  mid-sentence and raise a warning — which matters most for the safety-critical
  prompts.
- **Do not reorder the tool pick.** `T<n>` must come before any heating, or the
  hot end never becomes thermally managed and the heater will not arm.
