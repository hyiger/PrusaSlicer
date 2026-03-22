# Calibration Guide

This guide explains the calibration tools available in the **Calibration** menu and how to use each one to tune your printer and filament settings.

Run the calibrations in the order listed below — each one builds on the results of the previous.

---

## 1. Temperature Tower

**What it does:** Generates a multi-tier tower where each tier is printed at a different nozzle temperature. The tower includes overhangs (45° and 35°), bridging gaps, vertical and horizontal holes, cones, and a surface protrusion bar — all features that are sensitive to temperature.

**How to use it:**

1. Go to **Calibration → Temperature**.
2. Set the start temperature (highest, bottom tier), end temperature (lowest, top tier), and step size.
   - Defaults are read from your currently selected filament profile.
   - A step of 5°C is typical.
3. Click OK. The tower will appear on the bed with per-layer temperature commands already set.
4. Slice and print.

**How to evaluate:**

Examine each tier for:

- **Overhangs**: Look for drooping or curling on the 45° and 35° angled surfaces. Lower temperatures generally improve overhang quality.
- **Bridging**: Check the gap between the cutout walls. Strings or sag indicate the temperature is too high.
- **Stringing**: Look at the cones and holes. Fine strings between features mean the temperature is too high.
- **Layer adhesion**: Try snapping a tier off. If layers separate easily, the temperature is too low.
- **Surface quality**: Check the flat surfaces and the protrusion bar. Rough or blobby surfaces suggest too-high temperature.

Choose the tier that shows the best overall balance and set your filament temperature to that value.

---

## 2. Extrusion Multiplier

**What it does:** Generates a 40×40×40 mm cube printed in spiral vase mode with a single classic perimeter and no bottom layers. This produces a single-wall box whose thickness you can measure directly.

**How to use it:**

1. Go to **Calibration → Extrusion Multiplier**.
2. Optionally enable or disable the 5 mm brim.
3. Click OK. The cube will appear with vase mode and classic perimeters already configured.
4. Slice and print.

**How to evaluate:**

1. After printing, use digital calipers to measure the wall thickness at several points around the cube, at mid-height. Avoid corners and the seam.
2. Take 4-8 measurements and average them.
3. Calculate the new extrusion multiplier:

```
new_multiplier = expected_width / measured_width × current_multiplier
```

For example, if your extrusion width is set to 0.45 mm and you measure 0.48 mm with a multiplier of 1.0:

```
new_multiplier = 0.45 / 0.48 × 1.0 = 0.9375
```

4. Update the extrusion multiplier in your filament profile and re-print to verify.

---

## 3. Pressure Advance

**What it does:** Generates a chevron (V-shape) pattern tower where each layer group is printed with a different Pressure Advance value. Pressure Advance compensates for the delay between the extruder motor pushing filament and it actually flowing from the nozzle.

**How to use it:**

1. Go to **Calibration → Pressure Advance**.
2. Set the start PA, end PA, and step size.
   - For direct drive extruders, try 0.0 to 0.1 with a step of 0.005.
   - For Bowden extruders, try 0.0 to 2.0 with a step of 0.05.
3. Click OK. The chevron pattern will appear with per-layer M572 commands.
4. Slice and print.

**How to evaluate:**

Examine the chevron tips at each height:

- **Too little PA**: The corners will have bulging or rounded tips with excess material.
- **Too much PA**: The corners will show gaps or under-extrusion at the tips, and the lines may be thin.
- **Correct PA**: The chevron tips will be sharp and clean, with consistent line width throughout the arm.

Note the height of the best-looking layer, then calculate the PA value:

```
PA = start_PA + (layer_number / layers_per_level) × step
```

The layer count for each level (default 4 layers) is printed from bottom to top. Set the optimal PA value in your printer firmware configuration.

> **Note:** This test uses `M572` (Klipper/Prusa firmware). Marlin firmware uses `M900` for Linear Advance — you may need to adjust the G-code accordingly.

---

## 4. Retraction

**What it does:** Generates two cylindrical towers separated by a gap. The printer must retract filament when traveling between the towers, so any stringing between them indicates the retraction settings need adjustment. Each layer group uses a different retraction distance.

**How to use it:**

1. Go to **Calibration → Retraction**.
2. Set the start and end retraction distances, step size, tower dimensions, and spacing.
   - Defaults are read from your current printer profile.
   - Typical range: 0.2 mm to 2.0 mm for direct drive, 1.0 mm to 8.0 mm for Bowden.
3. Click OK. The towers will appear with per-layer M207 retraction commands.
4. Slice and print.

**How to evaluate:**

Look at the space between the two towers at each height:

- **Too little retraction**: Visible strings or blobs between the towers.
- **Too much retraction**: Gaps or under-extrusion at the start of each layer (after the travel move), or clicking sounds from the extruder.
- **Correct retraction**: Clean travel moves with no stringing and consistent extrusion after each retract.

Find the lowest retraction distance that produces clean results — using more retraction than necessary increases print time and can cause clogs.

> **Note:** This test uses `M207` (firmware retraction). Ensure firmware retraction is enabled in your printer settings.

---

## 5. Max Volumetric Flow Rate

**What it does:** Generates a serpentine (E-shaped) specimen designed for spiral vase mode printing. Each layer is printed at a progressively higher speed using M220 speed overrides, which increases the volumetric flow rate. This determines the maximum flow your hotend can sustain before under-extrusion occurs.

**How to use it:**

1. Go to **Calibration → Max FlowRate**.
2. Set the start flow rate, end flow rate, and step size in mm³/s.
   - Typical range: 5 to 20 mm³/s for standard hotends.
   - Use 5 to 35 mm³/s for high-flow hotends.
3. Click OK. The specimen will appear with vase mode, calculated base speed, and per-layer M220 commands.
4. Slice and print.

**How to evaluate:**

Watch the print in progress and examine the result:

- The print starts at the lowest flow rate (bottom) and increases toward the top.
- At some point the extruder will start **clicking**, **skipping steps**, or the **walls will become thin and rough** — this is where the hotend can no longer melt filament fast enough.
- Note the Z height where quality degrades. Calculate the flow rate:

```
max_flow = start_flow + (z_height / level_height) × step
```

Set your maximum volumetric flow rate in the filament profile to slightly below this value (e.g., 90% of the measured maximum) for a safety margin.

**Tips:**

- Print at the temperature you determined from the temperature tower test.
- The result is specific to each filament/hotend/temperature combination.
- Higher temperatures generally allow higher flow rates but may reduce print quality.

---

## General Tips

- **Print order**: Temperature → Extrusion Multiplier → Pressure Advance → Retraction → Max Flow Rate.
- **One variable at a time**: Only change the setting you are calibrating. Use your established values for everything else.
- **Re-calibrate when changing**: filament brand/type, nozzle size, hotend, or extruder.
- **Document your results**: Note the optimal values for each filament so you don't need to re-test.
- **Brim**: Use the brim checkbox for filaments with poor bed adhesion (e.g., PETG, TPU). Disable it for PLA on a clean textured sheet.
