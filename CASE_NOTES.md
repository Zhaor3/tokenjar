# CASE_NOTES — TokenJar Enclosure Reference

Rough dimensions and mechanical notes for modeling a 3D-printed case.

## Board Stack

| Layer | Dimensions (approx.) | Notes |
|-------|----------------------|-------|
| ESP32-S3 SuperMini | 22.5 × 18 × 3.5 mm | USB-C port on one short edge, antenna on the other |
| Waveshare 2" LCD | 42 × 33.5 × 2.5 mm (module), active area 32 × 40 mm | FPC connector on one long edge; mounting holes at corners |
| EC11 Encoder | 12 × 12 mm body, 20 mm shaft + knob | 5-pin through-hole footprint, M7 threaded bushing |

## Suggested Enclosure

- **Outer footprint**: ~55 × 45 × 28 mm (portrait orientation, display on front face)
- **Screen cutout**: 32.5 × 41 mm, centered on front face, offset 4 mm from top edge
- **Encoder hole**: 7 mm diameter, positioned below the screen on the front face (center ~8 mm from bottom edge)
- **USB-C slot**: 9 × 3.5 mm cutout on the bottom edge, aligned with the SuperMini's port
- **Wall thickness**: 2 mm for FDM printing

## Internal Layout (front to back)

1. Display module, IPS glass flush with front face (held by bezel lip)
2. 3–5 mm air gap for wiring
3. ESP32-S3 SuperMini, mounted perpendicular or flat on standoffs
4. EC11 shaft passes through front panel; body sits behind

## Orientation

The display is portrait (240 wide × 320 tall). The USB-C port should face downward when the device is standing upright.

## Mounting

- Display: friction-fit with a 1 mm bezel lip, or hot-glue on back corners
- SuperMini: M2 standoffs or friction slot
- Encoder: M7 nut on the front panel threaded bushing

## Ventilation

No active cooling needed — ESP32-S3 at idle/WiFi-polling draws <200 mA. A few small vents on the back panel are sufficient.
