# BOOSTXL-3PhGaNInv reference notes

Working summary of the TI documents in this folder, for using the BOOSTXL-3PhGaNInv
(PCB SENS007 Rev A) on the LAUNCHXL-F28379D. Sources: `UG p.N` (SLUUBP1A) and
`SCH sheet N` (SLURAY0A). LaunchPad pin names come from
[../28379d/README.md](../28379d/README.md). When this file and a PDF disagree, the
PDF wins; fix this file.

## Files

| File | TI lit. no. | What it is |
|---|---|---|
| `SLUUBP1A_BOOSTXL-3PhGaNInv_EVM_UG.pdf` | SLUUBP1A (Apr 2018), 17 p. | EVM user's guide: features, jumper, power-up cautions. The software section targets the F28069M + MotorWare and does not apply to the F28379D |
| `SLURAY0A_BOOSTXL-3PhGaNInv_Schematic.pdf` | SLURAY0A, 7 p. | Schematic Rev A. Sheets: 1 block diagram, 2 power/TMP302, 3 LMG5200 stage, 4 sensing/OCP, 5 LaunchPad interface |

BOM, gerbers and layout are not here; they are on the BOOSTXL-3PhGaNInv tool page
(UG §3.1). Layout background: TIDA-00909 (LMG5200), TIDA-00913 (INA240).

---

## 1. Board at a glance

| Item | Value |
|---|---|
| Topology | 3-phase inverter, three LMG5200 GaN half-bridge modules (80 V / 10 A, driver integrated) |
| DC input | **12–60 V, 48 V nominal** (UG §1.1). The J4 silkscreen reads "48VDC (20-48VDC)". LMG5200 abs. max 80 V |
| Current | ±10 A peak nominal; ±16.5 A theoretical range of the sense chain |
| Phase current sense | In-line 5 mΩ shunt (R55/R61/R69) + **INA240A1** (gain 20 V/V), output mid-point = VREF/2 = 1.65 V |
| Voltage sense | VA, VB, VC, VDC through 100 kΩ / 4.22 kΩ dividers |
| Protection | Hardware OCP at **+12 A, 1 A hysteresis** (UG §1.3.2). PCB over-temperature switch TMP302 → OT (active low) |
| On-board rails | LM5017 buck: VBUS → 5 V (106 kHz, LED D5). LP38691 LDO: 5 V → 3.3 V (LED D6). REF3333: 3.3 V reference |
| Logic level | 3.3 V I/O (U4 SN74AVC8T245 VCCB = 3.3 V via R22, default) |
| Bulk capacitance | C1 220 µF + 3.3 µF per phase (C5–C7, C10–C12, C21–C23) |
| Connectors | J4 DC input (2-pin), J3 motor/phase output (3-pin: VA, VB, VC), J1–J4 BoosterPack interface |

---

## 2. Jumper: 3.3 V to the LaunchPad

The 2-pin jumper feeds the board's 3.3 V rail to interface pin 1 (LaunchPad +3V3),
300 mA max (UG §2.2). The UG calls it **J6**; the schematic (sheet 2) labels it
**J5** (PEC02SAAN, net `3V3_LaunchPad_IN`). Check the silkscreen.

| Jumper | Fitted (default) | Removed |
|---|---|---|
| J5/J6 | BoosterPack 3.3 V drives LaunchPad +3V3 | not connected |

**With the LaunchPad on USB power (JP1/JP2/JP3 fitted), remove this jumper.**
Otherwise the board's LDO and the LaunchPad's USB-side regulator both drive +3V3.
To power the LaunchPad from the BoosterPack instead, fit the jumper and remove JP1
and JP2 on the LaunchPad (isolated configuration). Interface pin 21 (+5 V) is not
connected on this board, so the LaunchPad 5 V rail is never used.

---

## 3. Interface pinout and LaunchPad mapping

The board plugs into either LaunchPad site; signal positions are identical. Only
the pins listed are used; all others are not connected on the BoosterPack.

### Analog side (BoosterPack J1/J3)

Every analog line has a 20 Ω series resistor and 2200 pF to GND at the connector.
VDC, VA, VB and VC also have clamp diodes (D1–D4) to 3V3.

| BP pin | Signal | Site 1 (J1/J3) | Site 2 (J5/J7) |
|---|---|---|---|
| 1 | +3.3 V (from the J5/J6 jumper) | +3V3 | +3V3 |
| 6 | VREF (3.3 V, through R17 20 Ω) | J1.6 = **GPIO111** | J5.46 = **GPIO94** |
| 23 | **VDC** | J3.23 = ADCIN14 | J7.63 = ADCIN15 |
| 24 | **VA** | J3.24 = ADCINC3 | J7.64 = ADCINC5 |
| 25 | **VB** | J3.25 = ADCINB3 | J7.65 = ADCINB5 |
| 26 | **VC** | J3.26 = ADCINA3 | J7.66 = ADCINA5 |
| 27 | **IA** | J3.27 = ADCINC2 | J7.67 = ADCINC4 |
| 28 | **IB** | J3.28 = ADCINB2 | J7.68 = ADCINB4 |
| 29 | **IC** | J3.29 = ADCINA2 | J7.69 = ADCINA4 |
| 30 | VREF (3.3 V) | J3.30 = ADCINA0 / DACOUTA | J7.70 = ADCINA1 / DACOUTB |
| 22 | GND | GND | GND |

- **VREF (3.3 V) is driven onto a LaunchPad GPIO pin (BP pin 6) and an ADC/DAC pin
  (BP pin 30).** Keep that GPIO an input, and leave DACOUTA (site 1) or DACOUTB
  (site 2) disabled. VREF reads at ADC full scale, because 3.3 V is above the
  LaunchPad's 3.0 V VREFHI.
- Site 1, LaunchPad Rev 1.1 only: ADCINA2 (IC) is shorted to VREFHIB. Use a Rev 2.0
  LaunchPad or site 2.

### Digital side (BoosterPack J2/J4)

| BP pin | Signal | Site 1 (J4/J2) | Site 2 (J8/J6) |
|---|---|---|---|
| 40 | PWM_AH (phase A high side) | J4.40 = GPIO0 / EPWM1A | J8.80 = GPIO6 / EPWM4A |
| 39 | PWM_AL | J4.39 = GPIO1 / EPWM1B | J8.79 = GPIO7 / EPWM4B |
| 38 | PWM_BH | J4.38 = GPIO2 / EPWM2A | J8.78 = GPIO8 / EPWM5A |
| 37 | PWM_BL | J4.37 = GPIO3 / EPWM2B | J8.77 = GPIO9 / EPWM5B |
| 36 | PWM_CH | J4.36 = GPIO4 / EPWM3A | J8.76 = GPIO10 / EPWM6A |
| 35 | PWM_CL | J4.35 = GPIO5 / EPWM3B | J8.75 = GPIO11 / EPWM6B |
| 34 | **OT** (PCB over-temp, active low, 10 kΩ pull-up) | J4.34 = GPIO24 | J8.74 = GPIO14 |
| 13 | **nEn_uC** (PWM enable, active low, 10 kΩ pull-up) | J2.13 = GPIO124 | J6.53 = GPIO26 |
| 20 | GND | GND | GND |

- The six PWM inputs have 10 kΩ pull-downs on both sides of buffer U4. If the
  LaunchPad is absent or its pins are Hi-Z, both LMG5200 inputs stay low and the
  FETs stay off.
- Each LMG5200 HI/LI input has a 20 Ω / 47 pF filter (SCH sheet 3). The board adds
  no shoot-through interlock: generate dead-band in the ePWM DB module, and check
  the LMG5200 datasheet for minimum timing.

---

## 4. Enable and protection logic

```
nEnable = nEn_uC  OR  IComp        →  U4 OE (active low)
IComp   = ICompA OR ICompB OR ICompC  (U16, SN74LVC1G332)
```

- **The gate drive runs only while nEn_uC = 0 and no comparator has tripped.** When
  either input goes high, U4 outputs go Hi-Z, the 10 kΩ pull-downs take all six
  PWM inputs low, and every FET turns off.
- nEn_uC has a pull-up (R30), so the outputs start **disabled** after reset until
  firmware drives GPIO124 (site 1) low.
- **OCP** (SCH sheet 4): each phase compares its INA240 output against CompRef. That
  reference is the REF3333 3.3 V divided by R78/R77 to 2.75 V, with hysteresis that
  trips at 2.85 V and releases at 2.75 V. That is **+12 A trip / +11 A release, in
  one direction only**: a single comparator per phase sees only the positive output
  swing, so it does not catch overcurrent of the opposite sign.
- **OCP does not latch and does not reach the MCU.** IComp is not routed to the
  connector, so firmware never learns that the stage was shut off. For latching
  protection and a fault flag, use the F28379D CMPSS on the IA/IB/IC pins with
  ePWM trip zones. To disable the board's OCP, remove R79 (UG/SCH note).
- **OT**: TMP302B open-drain output, pulled up to 3.3 V, low when the PCB is too
  hot. The trip and hysteresis settings come from the R46/R47/R51/R52 straps. The
  populated values are in the BOM, which is not in this folder.

---

## 5. Sensing scale factors

All values are nominal from the schematic. **The LaunchPad ADC reference is
VREFHI = 3.0 V** (REF5030; all four VREFHI pins tied, see the 28379d README), so
12-bit full scale is 3.0 V, not 3.3 V.

| Signal | Transfer at the connector | ADC scale (VREFHI 3.0 V, 12-bit) | Measurable range |
|---|---|---|---|
| VDC, VA, VB, VC | V_pin = V × 4.22 / 104.22 = **0.04049 × V** | **18.09 mV/count** | 0–74.1 V (the 3.3 V clamp is reached at 81.5 V) |
| IA, IB, IC | V_pin = 1.65 V ± 0.1 V/A × I (5 mΩ × 20) | **7.32 mA/count**, zero = **2253 counts** | **−16.5 A … +13.5 A** (asymmetric because 1.65 V sits off the 1.5 V centre of a 3.0 V ADC) |

- Voltage filter: 4.05 kΩ (Thevenin) × 33 nF → f_c ≈ **1.2 kHz**. The phase
  voltages are filtered averages, not switching waveforms.
- Current path: no RC filter before the ADC apart from the 20 Ω / 2200 pF at the
  connector. The INA240 settles about 2 µs after a switching edge (UG §1.3.1), so
  sample away from edges, for example at the PWM period midpoint.
- Current sign: the UG (§2.3) says IA/IB/IC are inverted relative to the
  BOOSTXL-DRV8301. The demo uses `i = (offset − adc) × scale`, measured against a
  current probe.

---

## 6. Power-up checklist

1. LaunchPad on USB → remove the BoosterPack 3.3 V jumper (J5/J6).
2. Seat the board on the site the firmware expects. Check that no bottom-side solder
   joints touch the LaunchPad jumpers or headers (UG §2.3 caution).
3. Flash and halt with nEn_uC high (the default) and PWMs configured, dead-band set,
   and outputs low.
4. Apply VBUS (J4) with the supply current-limited, read VDC, then release nEn_uC.

---

## 7. Notes on `Common/28379D_demo/Boostxl_3phganINV_demo/simple_3ph_buck`

Checked against this schematic (2026-10-02). All items were resolved in the demo the
same day.

| Item | In the demo | Per schematic | Status |
|---|---|---|---|
| `BUCK_ADC_VREF_V` | 3.3 V | **3.0 V** (LaunchPad VREFHI). Every scale is about 10 % high: VDC reads 10 % high uncalibrated, and the 1.2325× current and 1.136× V_out cal factors each include this 1.10 | **Fixed: VREF = 3.0 V.** Cal factors rescaled by 3.0/3.3 so I and V_out read the same as before (shunt 6.163 → 5.6027 mΩ, V_out cal 1.136 → 1.0327). `vDC_V` (no cal factor, monitoring only) now reads about 10 % lower, i.e. correct |
| Current zero default | 2048 counts | 1.65 / 3.0 × 4096 = **2253** | **Kept.** Unused at run time: the trimmed offsets (IA 2244.03, IB 2245.37, IC 2240.69) sit 8–13 counts below 2253, consistent with INA240 and REF3333 offset |
| Comment "10 mΩ shunt, INA240A2" | | **5 mΩ, INA240A1** (gain 20; the A2 variant has gain 50) | Fixed |
| Comment "R42/R43/R44" shunts | | Shunts are **R55/R61/R69** | Fixed |
| `nEn_uC` comment "J4 pin 13" | | **J2 pin 13** (GPIO124) | Fixed (code and project README pin table) |
| ePWM4 debug comment "GPIO6 … J4 pin 34" | | GPIO6 is **J8 pin 80**; J4 pin 34 is GPIO24 = OT | Fixed; macro renamed `BUCK_GPIO_EPWM7_DBG` → `BUCK_GPIO_EPWM4_DBG` |
| Current scale comment "≈ 8.06 mA/count" | | Uses the nominal 5 mΩ; with the calibrated 6.163 mΩ the scale is **6.54 mA/count** | Fixed |
| AMC1301 divider comment "R1=200k, R2=10k" | | Defines and README use **182 Ω / 16.5 Ω** (external sensor board, not on this schematic) | Fixed |
