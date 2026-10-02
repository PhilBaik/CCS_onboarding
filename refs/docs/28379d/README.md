# LAUNCHXL-F28379D reference notes

Working summary of the TI documents in this folder, for firmware work on the
LAUNCHXL-F28379D (board **Rev 2.0** unless noted). Source pages are cited as
`UG p.N` (SPRUI77C) and `QSG p.N` (SPRUI73A). When this file and a PDF disagree,
the PDF wins; fix this file.

## Files

| File | TI lit. no. | What it is | Use it for |
|---|---|---|---|
| `SPRUI77C_LAUNCHXL-F28379D_Overview_UG.pdf` | SPRUI77C (Mar 2019), 30 p. | LaunchPad user's guide: jumpers, boot switch, pin-mux tables, Rev 2.0 schematics, BOM | **Primary source** for everything below |
| `SPRUI73A_LAUNCHXL-F28379D_Quick_Start_Guide.pdf` | SPRUI73A, 7 p. (2 useful) | Quick start card | Colour BoosterPack pin map (QSG p.1), feature list (QSG p.2) |
| `SPRACN0F_C2000_Essential_Guide_AppNote.pdf` | SPRACN0F (Mar 2023), 70 p. | *The Essential Guide for Developing With C2000 Real-Time MCUs* | Background: HRPWM, deadband, CMPSS trips, CLA, X-BAR, CLB. Not board-specific |

BoosterPack notes: [../3ph_boostxl/README.md](../3ph_boostxl/README.md) (BOOSTXL-3PhGaNInv).

Not in this folder but referenced: device data manual **SPRS880**, TRM **SPRUHM8**
(boot-mode table, register details), silicon errata **SPRZ412**. Rev 1.1 and 2.0
schematics/gerbers ship in C2000Ware under `boards/LaunchPads/LAUNCHXL_F28379D`.

---

## 1. Device and board at a glance

| Item | Value |
|---|---|
| MCU | TMS320F28379D, 337-ball nFBGA (U1/U14) |
| CPU | 2 × C28x @ 200 MHz + 2 × CLA @ 200 MHz, FPU, TMU, VCU |
| Memory | 1 MB flash, 204 KB RAM |
| PWM | 24 ePWM channels (ePWM1–12), 16 HRPWM with 150 ps edge resolution |
| ADC | 4 × ADC (A–D), 16-bit diff. / 12-bit single-ended, selectable per ADC |
| Comparators | 8 windowed CMPSS with 12-bit DAC references; 3 buffered DACs (DACOUTA/B/C) |
| Comms | SCI, SPI, I2C, CAN, McBSP, USB |
| Crystal | **10 MHz** (Q1). The TMDSCNCD28379D controlCARD uses 20 MHz |
| SYSCLK | 200 MHz = 10 MHz × 40 (IMULT) / 2, LSPCLK = 50 MHz (`device.h`) |
| Debug probe | On-board XDS100v2 (FT2232H), galvanically isolated (ISO7240/ISO7231) |
| USB | Mini USB-B, 500 mA fuse |

Sources: QSG p.2, UG p.2, UG p.15 (Q1 10 MHz), `Common/28379D_demo/Boostxl_3phganINV_demo/simple_3ph_buck/device/device.h`.

### Mandatory project setting

Add predefined symbol **`_LAUNCHXL_F28379D`** (Build → C2000 Compiler → Advanced
Options → Predefined Symbols). Without it, C2000Ware `device.h` assumes the 20 MHz
controlCARD crystal: SYSCLK comes out at 100 MHz, and the LED, SCI and CAN pin
defines point at controlCARD pins (UG p.5, FAQ 5 p.27). `Common/28379D_demo/Boostxl_3phganINV_demo/simple_3ph_buck`
already defines it.

---

## 2. Power and isolation jumpers

The board has a USB/debug side and a device side. JP1/JP2/JP3 bridge them.

| Jumper | Connects | Default |
|---|---|---|
| JP1 | 3.3 V from the USB-side regulator → device +3V3 | installed |
| JP2 | GND, USB side ↔ device side | installed |
| JP3 | 5 V from USB → device +5V | installed |
| JP4 / JP5 | +3V3 / +5V to BoosterPack site 2 headers (J5/J7), per sheet P06 | check on board |
| JP6 | Enables U12 3.3 V → 5 V boost | **open** |
| J10 | External 3.3 V input (1×3) | — |
| J16 | External 5 V input (1×3, Rev 2.0 only) | — |

**3.3 V supply** (UG Table 1, p.6)

| Cfg | JP1 | JP2 | Source | Isolated? |
|---|---|---|---|---|
| 1 | on | on | USB | no |
| 2 | off | off | external 3.3 V on BP header or J10 | yes |

**5 V supply** (UG Table 2, p.6)

| Cfg | JP2 | JP3 | JP6 | Source | Isolated? |
|---|---|---|---|---|---|
| 1 | on | on | off | USB | no |
| 2 | off | off | on | U12 boost from 3.3 V (no other 5 V source allowed) | yes |
| 3 | off | off | off | external 5 V on BP header or J16 | yes |

- Never fit JP6 together with JP3: the USB 5 V and the U12 output would fight.
- For true PC isolation (for example with a power stage on the BoosterPack), remove
  JP1, JP2 and JP3 and supply the device side externally.
- Power LEDs: D4 (green, USB side) and D1 (green, device side). Both lit = both domains powered.

---

## 3. Boot switch S1 and reset

| S1 position | Signal |
|---|---|
| 1 | GPIO84 |
| 2 | GPIO72 |
| 3 | TRSTn |

Boot-mode table printed on schematic sheet P07 (UG p.14). UP = 1: switches 1 and 2
pull to +3V3 through 820 Ω (2.2 kΩ pull-down when off); switch 3 connects TRST to
the XDS100 `JTAG_TRST` (pulled down when off).

| TRST (S1.3) | GPIO72 (S1.2) | GPIO84 (S1.1) | Boot mode |
|---|---|---|---|
| 1 | x | x | Emulation boot (debugger) |
| 0 | 0 | 0 | Parallel I/O |
| 0 | 0 | 1 | SCI (unusable on this board, see below) |
| 0 | 1 | 0 | Wait |
| 0 | 1 | 1 | GetMode → flash by default |

- **Debugging (JTAG)**: S1.3 (TRSTn) must be **UP**. The probe will not connect with
  it down (UG §5.3 p.7, FAQ 3 p.27).
- **Standalone boot from flash**: 1-UP, 2-UP, 3-DOWN (GetMode). This is the setting
  UG §4.1 (p.5) gives for the factory demo.
- What GetMode does beyond the default flash entry is set by OTP; see SPRUHM8 (Boot ROM chapter).
- **SCI boot is not possible on this board.** The SCIA pins on the XDS100 are not SCI
  boot pins, and the bootable SCI pins are not brought out (UG §2.1 p.3).
- S3 = device reset push-button. RST is also on J2 pin 16 and J6 pin 56.

---

## 4. On-board peripherals

| Function | Pins | Notes |
|---|---|---|
| UART backchannel (SCIA) | **GPIO42 = SCITXDA** (MCU→PC), **GPIO43 = SCIRXDA** (PC→MCU) | Through the isolated XDS100v2 FTDI; the PC sees a "USB Serial Port". The examples assume 115200 8N1 with SYSCLK = 200 MHz |
| LED D10 (blue) | **GPIO31** | **Active low**: write 0 = on. Open-drain buffer SN74LVC2G07, 680 Ω to +3V3 |
| LED D9 (red) | **GPIO34** | **Active low**, same circuit |
| CAN (J12) | **CANB: GPIO12 = CANTXB, GPIO17 = CANRXB** | SN65HVD234 transceiver, 120 Ω termination on board. J12 (1×3) carries CANH, CANL, GND; check the pin order on the silkscreen |
| QEP1 (J14) | GPIO20 = EQEP1A, GPIO21 = EQEP1B, GPIO99 = EQEP1I | 5 V inputs, TXB0106 level shifter. J14: 1 A, 2 B, 3 I, 4 +5V, 5 GND |
| QEP2 (J15) | GPIO54 = EQEP2A, GPIO55 = EQEP2B, GPIO57 = EQEP2I | Same as J14 |
| PWM-DAC outputs | GPIO159 → DAC1, GPIO160 → DAC2, GPIO157 → DAC3, GPIO158 → DAC4 | 1 kΩ / 100 nF RC (f_c ≈ 1.6 kHz). DAC1/2 = J4 pins 32/31, DAC3/4 = J8 pins 72/71 |
| ADC reference | **3.0 V** on all four ADCs: REF5030 → OPA350 buffers U19/U11 drive VREFHIA and VREFHIB; VREFHID is tied to A and VREFHIC to B (sheet P07). VREFLOA–D = GND | ADC full scale = 0–3.0 V, **not** 3.3 V. A 1.65 V mid-rail signal reads 2253 counts, not 2048 |
| ADCD (J21, 2×4) | ADCIND0–3 on J21. ADCIND4/5 come from the THS4531 diff. amp, fed from SMA J19/J20 (not fitted) | Intended for 16-bit differential mode (UG §5.1) |
| Expansion J9 (bottom) | DF40C-60DP, 60 pins: EMIF1, SPI, I2C | Rev 2.0: GPIO29/40/41/52/104/105 go to the BP header by default; move them to J9 with 0 Ω resistors (§6) |

Sources: UG p.11 (SCIA), p.13 (LEDs, PWM-DAC, ADCD), p.12 (REF5030), p.17 (CAN, QEP);
`device.h` (LED and CAN defines); `simple_3ph_buck_main.c` (LED active-low use).

---

## 5. BoosterPack header pinout

Pin numbers follow the BoosterPack standard used in UG Tables 5–8 (p.8–9): site 1
= J1 (1–10), J2 (11–20), J3 (21–30), J4 (31–40); site 2 = J5 (41–50), J6 (51–60),
J7 (61–70), J8 (71–80). Rows are listed top to bottom as the headers sit on the
board. "Alt" lists the commonly used mux option. Full mux tables are in SPRS880.

`*` = also routable to J9 (see §6).

### Site 1, outer pair: J1 (left) and J3

| J1 pin | Signal | Alt | J3 pin | Signal | Alt |
|---|---|---|---|---|---|
| 1 | +3.3V | | 21 | +5V | |
| 2 | GPIO32 | | 22 | GND | |
| 3 | GPIO19 | SCIRXDB | 23 | ADCIN14 | CMPIN4P |
| 4 | GPIO18 | SCITXDB | 24 | ADCINC3 | CMPIN6N |
| 5 | GPIO67 | | 25 | ADCINB3 | CMPIN3N |
| 6 | GPIO111 | | 26 | ADCINA3 | CMPIN1N |
| 7 | GPIO60 | SPICLKA | 27 | ADCINC2 | CMPIN6P |
| 8 | GPIO22 | | 28 | ADCINB2 | CMPIN3P |
| 9 | GPIO105* | SCLA | 29 | ADCINA2 | CMPIN1P (Rev 1.1: shorted to VREFHIB) |
| 10 | GPIO104* | SDAA | 30 | ADCINA0 | DACOUTA |

### Site 1, inner pair: J4 and J2 (right)

| J4 pin | Signal | Alt | J2 pin | Signal | Alt |
|---|---|---|---|---|---|
| 40 | GPIO0 | EPWM1A | 20 | GND | |
| 39 | GPIO1 | EPWM1B | 19 | GPIO61 | (SPIA CS) |
| 38 | GPIO2 | EPWM2A | 18 | GPIO123 | SD1_C1 |
| 37 | GPIO3 | EPWM2B | 17 | GPIO122 | SD1_D1 |
| 36 | GPIO4 | EPWM3A | 16 | RST | |
| 35 | GPIO5 | EPWM3B | 15 | GPIO58 | SPISIMOA |
| 34 | GPIO24 | OUTPUTXBAR1 | 14 | GPIO59 | SPISOMIA |
| 33 | GPIO16 | OUTPUTXBAR7 | 13 | GPIO124 | SD1_D2 |
| 32 | DAC1 (PWM-DAC) | | 12 | GPIO125 | SD1_C2 |
| 31 | DAC2 (PWM-DAC) | | 11 | GPIO29* | OUTPUTXBAR6 |

### Site 2, outer pair: J5 (left) and J7

| J5 pin | Signal | Alt | J7 pin | Signal | Alt |
|---|---|---|---|---|---|
| 41 | +3.3V | | 61 | +5V | |
| 42 | GPIO95 | | 62 | GND | |
| 43 | GPIO139 | SCIRXDC | 63 | ADCIN15 | CMPIN4N |
| 44 | GPIO56 | SCITXDC | 64 | ADCINC5 | CMPIN5N |
| 45 | GPIO97 | | 65 | ADCINB5 | |
| 46 | GPIO94 | | 66 | ADCINA5 | CMPIN2N |
| 47 | GPIO65 | SPICLKB | 67 | ADCINC4 | CMPIN5P |
| 48 | GPIO52* | | 68 | ADCINB4 | |
| 49 | GPIO41* | SCLB | 69 | ADCINA4 | CMPIN2P |
| 50 | GPIO40* | SDAB | 70 | ADCINA1 | DACOUTB |

### Site 2, inner pair: J8 and J6 (right)

| J8 pin | Signal | Alt | J6 pin | Signal | Alt |
|---|---|---|---|---|---|
| 80 | GPIO6 | EPWM4A | 60 | GND | |
| 79 | GPIO7 | EPWM4B | 59 | GPIO66 | (SPIB CS) |
| 78 | GPIO8 | EPWM5A | 58 | GPIO131 | SD2_C1 |
| 77 | GPIO9 | EPWM5B | 57 | GPIO130 | SD2_D1 |
| 76 | GPIO10 | EPWM6A | 56 | RST | |
| 75 | GPIO11 | EPWM6B | 55 | GPIO63 | SPISIMOB |
| 74 | GPIO14 | OUTPUTXBAR3 | 54 | GPIO64 | SPISOMIB |
| 73 | GPIO15 | OUTPUTXBAR4 | 53 | GPIO26 | SD2_D2 |
| 72 | DAC3 (PWM-DAC) | | 52 | GPIO27 | SD2_C2 |
| 71 | DAC4 (PWM-DAC) | | 51 | GPIO25 | OUTPUTXBAR2 |

### Peripheral → pin lookup

| Peripheral | Signals | BP pins |
|---|---|---|
| ePWM1–3 (A/B) | GPIO0–5 | 40–35 (J4) |
| ePWM4–6 (A/B) | GPIO6–11 | 80–75 (J8) |
| SCIB | RX GPIO19 / TX GPIO18 | 3 / 4 |
| SCIC | RX GPIO139 / TX GPIO56 | 43 / 44 |
| SPIA | CLK GPIO60, SIMO GPIO58, SOMI GPIO59, CS GPIO61 | 7, 15, 14, 19 |
| SPIB | CLK GPIO65, SIMO GPIO63, SOMI GPIO64, CS GPIO66 | 47, 55, 54, 59 |
| I2CA | SCL GPIO105, SDA GPIO104 | 9, 10 |
| I2CB | SCL GPIO41, SDA GPIO40 | 49, 50 |
| SDFM1 | D1 GPIO122, C1 GPIO123, D2 GPIO124, C2 GPIO125 | 17, 18, 13, 12 |
| SDFM2 | D1 GPIO130, C1 GPIO131, D2 GPIO26, C2 GPIO27 | 57, 58, 53, 52 |
| Buffered DAC out | DACOUTA (ADCINA0), DACOUTB (ADCINA1) | 30, 70 |
| ADC, 12-bit SE | ADCINA0–5, B2–5, C2–5, ADCIN14/15 | 23–30, 63–70 |

The QSG card (p.1) notes that a software-emulated I2C is needed to match the
BoosterPack standard's I2C positions exactly, and marks interrupt-capable I/O with (!).

---

## 6. Rev 2.0: dual-mapped GPIOs (BoosterPack ↔ J9)

The default routing is to the BoosterPack header only. Moving a signal means
swapping the 0 Ω resistor (UG Table 4, p.7).

| GPIO | To BP header | To J9 |
|---|---|---|
| GPIO29 | R75 | R76 |
| GPIO40 | R67 | R68 |
| GPIO41 | R69 | R70 |
| GPIO52 | R77 | R78 |
| GPIO104 | R71 | R72 |
| GPIO105 | R73 | R74 |

Rev 1.x used 3-position jumpers J11/J13 for GPIO40/41 only (1–2 = J9, 2–3 = BP).

---

## 7. Known board issues (UG §2.1, p.3–4)

- **All revisions**: R7 in the oscillator circuit is misplaced; do not copy the
  oscillator circuit into your own design. SCI boot is unavailable.
- **Rev 1.1 only**: ADCINA2 shorted to VREFHIB (do not use it). U1 VIN+ may short
  to ADCINB4/ADCINC4. J3/J7 ADC silkscreen labels are wrong; use the schematic.
- **Rev 2.0 changes**: dimmer LEDs (680 Ω), CAN J12 moved and PGND → GND, J11/J13
  replaced by the 0 Ω tree, extra EMIF1 signals routed to J9 for SDRAM, J16 added,
  ADC conditioning updated (C40 = 180 pF, R60/R61 = 10 kΩ).

---

## 8. Corrections to the CCS bundled board notes

CCS ships `…/Resources/ai/boards/LAUNCHXL-F28379D/AGENTS.md`, which AI agents read
before configuring this board. Checked against SPRUI77C and the demo's `device.h`,
it contains these errors:

| Bundled AGENTS.md says | Correct (source) |
|---|---|
| LEDs are active high | **Active low** (open-drain SN74LVC2G07, UG p.13; demo writes 1 = off) |
| D9 red = GPIO31, D10 blue = GPIO34 | **D10 blue = GPIO31, D9 red = GPIO34** (`device.h` LED1 = 31 "LD10") |
| Emulation boot = 1-UP, 2-UP, 3-DOWN | That is the **standalone/flash** (GetMode) setting (UG §4.1, P07 table). Emulation needs 3 (TRSTn) UP |
| Flash boot = 1-UP, 2-DOWN, 3-UP | With 3 UP the device is in emulation boot regardless; 1-UP/2-DOWN with TRST = 0 would select SCI boot |
| J1 pins 3–10 = ADCIN14 … ADCINA0 | J1 is the GPIO column (GPIO19, 18, 67, 111, 60, 22, 105, 104); the ADC pins are on **J3** (UG Table 5) |
| J4/J2, J8/J6 pin 1–10 numbering | Uses local schematic numbering. Use the BP numbers 31–40 / 11–20 / 71–80 / 51–60 above |

Trust this file and the UG over the bundled AGENTS.md for pin-level detail.
