# Virtual Filaments Tutorial

Virtual filaments create mixed colors by alternating physical filaments layer-by-layer. A region painted with "Virtual T1+T2" prints with filament 1 on even layers and filament 2 on odd layers, producing a blended color appearance.

## Setup

### 1. Printer with multiple extruders

Select a printer profile with 2 or more extruders (e.g., Prusa CORE One with MMU3, or any multi-extruder printer).

### 2. Load filaments

Assign filament presets to each extruder slot in the sidebar. Choose colors with good contrast for visible blending:

| Slot | Example     | Color   |
|------|-------------|---------|
| T1   | PLA Cyan    | #21FFFF |
| T2   | PLA Magenta | #FB02FF |
| T3   | PLA Yellow  | #FFFF0A |
| T4   | PLA Black   | #000000 |

### 3. Enable virtual filaments

1. Go to **Print Settings** tab
2. Select **Multiple Extruders** in the left tree
3. Find the **Virtual Filaments** section
4. Check **Enable virtual filaments**

The sidebar will show a **Virtual Filaments** panel listing all auto-generated pairs with blended color previews:

```
T1 + T2 (50%)   [teal/purple blend]
T1 + T3 (50%)   [green blend]
T1 + T4 (50%)   [dark cyan blend]
T2 + T3 (50%)   [red/orange blend]
T2 + T4 (50%)   [dark magenta blend]
T3 + T4 (50%)   [olive/dark yellow blend]
```

Use the checkboxes to enable or disable individual virtual filaments.

## Painting

### 1. Open the MMU painting gizmo

Click the paint brush icon in the left toolbar, or press **N**.

### 2. Select a virtual filament color

In the **First color** dropdown, scroll past the physical extruders to find virtual filament entries:

- Extruder 1 (physical)
- Extruder 2 (physical)
- ...
- Virtual T1+T2
- Virtual T1+T3
- Virtual T2+T3
- ...

Select a virtual filament as your painting color.

### 3. Paint faces

- **Left mouse button**: Paint with first color
- **Right mouse button**: Paint with second color
- **Shift + Left mouse button**: Remove painted color
- **Alt + Mouse wheel**: Change brush size

Use **Smart fill** or **Bucket fill** tool types for faster coverage of large areas.

### 4. Slice

Click **Slice now**. In the G-code preview:

- Each layer shows the physical extruder color that the virtual filament resolves to on that layer
- Scrub through layers with the right-side slider to see the alternation
- The legend shows physical extruder colors (the actual filaments being used)

## Settings

Found in **Print Settings > Multiple Extruders > Virtual Filaments**:

| Setting | Description |
|---------|-------------|
| **Enable virtual filaments** | Master toggle. When off, virtual filaments are hidden and ignored during slicing. |
| **Advanced dithering** | Uses ordered dithering instead of contiguous layer runs for more even color distribution. Only applies to custom virtual filaments. |
| **Collapse same-color regions** | Merges adjacent painted regions that resolve to the same physical filament on a given layer, reducing unnecessary tool changes. |

## How it works

### Layer alternation

A virtual filament with a 1:1 ratio (50% blend) alternates every layer:

```
Layer 0: ████ Filament A (cyan)
Layer 1: ████ Filament B (magenta)
Layer 2: ████ Filament A (cyan)
Layer 3: ████ Filament B (magenta)
```

From a distance, the eye blends these into a mixed color (purple).

### Color blending preview

The sidebar and painting gizmo show perceptual pigment-style blended colors. This model produces realistic mixing predictions:

- Blue + Yellow = **Green** (not gray like RGB averaging)
- Red + Yellow = **Orange**
- Red + Blue = **Purple**
- Cyan + Magenta = **Blue**

### Wipe tower

Virtual filament regions create additional tool changes compared to single-material printing. The wipe tower accommodates these automatically. Each layer-to-layer alternation is a physical tool change that requires purging.

## Tips

- **Start with two filaments** to understand the behavior before using more complex combinations.
- **Use high-contrast colors** (e.g., cyan + magenta) for clearly visible results.
- **Check the G-code preview layer-by-layer** using the slider on the right side. You should see the painted regions alternate between the two component colors.
- **Thin walls and small features** may not show the blending effect well since they have fewer layers.
- **Print speed**: Virtual filaments don't change print speed, but the additional tool changes add time. Check the estimated print time before committing to a long print.

## Limitations

- Virtual filaments can only be applied by painting faces in the MMU gizmo. They cannot yet be assigned as the default extruder for a whole object.
- Advanced features from OrcaSlicer-FullSpectrum (height-weighted cadence, 3+ color gradients, per-perimeter patterns, same-layer pointillisme, surface bias offsets) are not yet implemented.
- Maximum of 15 total colors (physical + virtual) due to the triangle selector's state encoding limit.
