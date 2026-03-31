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
3. Optionally enable the 5 mm brim for better bed adhesion.
4. Click OK. The tower will appear on the bed with per-layer temperature commands already set.
5. Slice and print.

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
3. Optionally enable the 5 mm brim for better bed adhesion.
4. Click OK. The chevron pattern will appear with per-layer PA commands (auto-detected for your firmware).
5. Slice and print.

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

> **Note:** The PA command is auto-detected from your printer profile: `M572 S` for Prusa printers (except MINI), `M900 K` for MINI and Marlin firmware, and `SET_PRESSURE_ADVANCE` for Klipper.

---

## 4. Retraction

**What it does:** Generates two cylindrical towers separated by a gap. The printer must retract filament when traveling between the towers, so any stringing between them indicates the retraction settings need adjustment. Each layer group uses a different retraction distance.

**How to use it:**

1. Go to **Calibration → Retraction**.
2. Set the start and end retraction distances, step size, tower height, diameter, and spacing.
   - Defaults are derived from your current printer profile's retraction length (±1 mm).
   - Typical range: 0.2 mm to 2.0 mm for direct drive, 1.0 mm to 8.0 mm for Bowden.
3. Optionally enable the 5 mm brim for better bed adhesion.
4. Click OK. The towers will appear on a 1 mm base plate with per-layer M207 retraction commands.
5. Slice and print.

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
3. Optionally enable the 5 mm brim for better bed adhesion.
4. Click OK. The specimen will appear with vase mode, calculated base speed, and per-layer M220 commands.
5. Slice and print.

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

## 6. Flow Rate (YOLO-style)

**What it does:** Generates 11 flat T-shaped pads side by side, each printed with a slightly different extrusion width (from -5% to +5% of your current setting). This lets you visually compare surface quality to find the optimal flow rate without iterative test prints.

**How to use it:**

1. Go to **Calibration → Flow Rate**.
2. Adjust the number of steps, step percentage, and pad dimensions if desired.
3. Click OK. The pads will appear arranged on the bed, each labelled with its offset (e.g. `-3%`, `0%`, `+2%`).
4. Slice and print.

**How to evaluate:**

1. Examine the **top surface** of each pad:
   - **Too little flow** (negative pads): gaps between lines, rough/sparse surface, infill visible through top
   - **Too much flow** (positive pads): ridged, bumpy surface with excess material
   - **Correct flow**: smooth, flat, uniform top surface
2. Run your finger across the pads — the correct one feels smoothest.
3. If the best pad is `-2%`, reduce your extrusion multiplier by 2%.

---

## 7. Fan Speed

**What it does:** Generates a tower with two vertical columns, horizontal bridge shelves, overhang wedges, cones, and a standalone thin cylinder for stringing evaluation. The base level has only shelves; wedges and cones appear from the second level onward. Fan speed varies via per-layer M106 commands. All automatic fan control is disabled so the M106 commands are the sole fan speed control.

> **Note:** The tower's printed labels always show a linear 0%-100% range regardless of your custom start/end settings. The actual fan speeds match your chosen range — use the level number to determine the corresponding fan speed.

**How to use it:**

1. Go to **Calibration → Fan Speed**.
2. Set start fan speed (default 0%), end speed (default 100%), and step size (default 10%).
3. Optionally enable the 5 mm brim for better bed adhesion.
4. Click OK. The tower will appear with per-layer fan speed commands.
5. Slice and print.

**How to evaluate:**

At each level, examine:

- **Bridge quality**: Look at the horizontal shelves bridging between the two columns. Sag or drooping means insufficient cooling.
- **Overhang quality**: Check the wedge overhangs extending from the left column. Curling or drooping indicates the fan speed is too low.
- **Cone detail**: The small cones test fine feature cooling. Blobs or deformation mean more cooling is needed.
- **Stringing**: Check between the standalone cylinder and the main tower. Less stringing at higher fan speeds.
- **Layer adhesion**: At very high fan speeds, layers may not bond well. If you can peel layers apart, the fan speed is too high.

Find the level with the best balance of bridge quality, overhang sharpness, and layer adhesion. That's your optimal fan speed for this filament.

> **Note:** The fan speed test disables PrusaSlicer's automatic cooling system (including bridge fan speed) so that only the calibration M106 commands control the fan. Your filament's fan settings will be restored when you discard changes or switch presets.

---

## 8. Dimensional Accuracy / Shrinkage

**What it does:** Generates an XYZ cross gauge — three 10×10 mm bars extending from a common corner along the X, Y, and Z axes. Each arm has square through-holes at 25 mm intervals that fit caliper jaws, with raised distance labels. After printing, you measure each axis to determine shrinkage compensation values.

**How to use it:**

1. Go to **Calibration → Dimensional Accuracy**.
2. Set the arm length (default 100 mm). Longer arms give more accurate shrinkage measurements.
3. Optionally enable the 5 mm brim.
4. Click OK. The gauge will appear on the bed.
5. Slice and print with your established temperature and extrusion multiplier settings.

**How to evaluate:**

1. After printing, use digital calipers to measure the distance between through-hole edges at each interval (25, 50, 75, 100 mm from the corner).
2. Measure all three axes (X, Y, Z).
3. Calculate shrinkage for each axis:

```
shrinkage_percent = (1 - measured_length / target_length) × 100
```

For example, if a 100 mm arm measures 99.5 mm:

```
shrinkage = (1 - 99.5 / 100) × 100 = 0.5%
```

4. Apply compensation in your slicer's XY size compensation setting, or scale the model by `100 / (100 - shrinkage)`.

**Tips:**

- Different filaments shrink differently — ABS/ASA shrink 0.5-1%, PLA 0.2-0.4%, PETG 0.3-0.6%.
- X and Y shrinkage may differ if your belt tensions are unequal.
- Z shrinkage is usually minimal on well-calibrated printers.
- The through-holes give inside-dimension measurements; the arm endpoints give outside-dimension measurements. Compare both.

---

---

## 9. XY Skew Correction

**What it does:** Corrects XY axis non-orthogonality (skew) by applying a shear transform to all G-code coordinates. This is a printer-level setting — not a calibration print, but a correction applied to every print once configured.

The transform is: `x' = x + (y - y_ref) × tan(angle)`, where `y_ref` is the center of the bed.

**How to measure skew:**

1. Print a large square (e.g., 150×150 mm) using the Dimensional Accuracy gauge or a simple cube.
2. Measure both diagonals (AC and BD) and one side length (AD) with calipers.
3. Calculate the skew angle:

```
k = (AC² - BD²) / (4 × AD²)
angle = arctan(k)   (in degrees)
```

For example, if AC = 212.20 mm, BD = 211.80 mm, AD = 150.00 mm:

```
k = (212.20² - 211.80²) / (4 × 150²) = (45028.84 - 44859.24) / 90000 = 0.001884
angle = arctan(0.001884) = 0.108°
```

**How to apply:**

1. Go to **Printer Settings → General → Skew Correction** (Expert mode).
2. Enter the calculated angle in the **XY Skew Correction** field (in degrees).
   - Use the sign that corrects the skew: if your diagonals show the frame is leaning right, use a negative value.
3. All subsequent sliced G-code will have the correction applied automatically.
4. Re-print the square and verify the diagonals are now equal.

**Notes:**

- The correction only affects X coordinates; Y remains unchanged.
- Arc fitting (G2/G3) is automatically disabled when skew correction is active, since shear transforms circles into ellipses.
- Typical skew values are small (±0.1° to ±0.3°). The setting range is ±5°.
- This is a per-printer setting — it persists across filament and print profile changes.

---

## General Tips

- **Recommended calibration order**: Temperature (§1) → Fan Speed (§7) → Extrusion Multiplier (§2) → Flow Rate (§6) → Pressure Advance (§3) → Retraction (§4) → Max Flow Rate (§5) → Dimensional Accuracy (§8) → Skew Correction (§9).
- **One variable at a time**: Only change the setting you are calibrating. Use your established values for everything else.
- **Re-calibrate when changing**: filament brand/type, nozzle size, hotend, or extruder.
- **Document your results**: Note the optimal values for each filament so you don't need to re-test.
- **Brim**: Use the brim checkbox for filaments with poor bed adhesion (e.g., PETG, TPU). Disable it for PLA on a clean textured sheet.
- **Skew correction**: This is a one-time printer calibration. Re-check only if you adjust belt tension or rebuild the frame.
