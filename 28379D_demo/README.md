# 28379D_demo

Demo firmware for the TMS320F28379D LaunchPad (LAUNCHXL-F28379D). Every project in this
folder builds with a plain CCS install: no C2000Ware, Motor Control SDK or Digital Power SDK
is needed, because the TI driver files each project uses are copied into the project itself.

Demos are grouped by the BoosterPack they need, one subfolder per board. Examples that need
only the LaunchPad are in `28379D_Launchpad/`; start there if you are new to the board.

| Board folder | Project | What it does |
|--------------|---------|--------------|
| [`28379D_Launchpad/`](28379D_Launchpad/README.md) | `led_toggle/`, `gpio/`, `epwm/`, `epwm_interrupt/`, `epwm_modes/` | Small single-topic examples: LEDs, GPIO in/out, ePWM, ePWM interrupt, ePWM counter modes. LaunchPad only, no BoosterPack. |
| `Boostxl_3phganINV_demo/` | [`simple_3ph_buck/`](Boostxl_3phganINV_demo/simple_3ph_buck/README.md) | 3-phase interleaved GaN buck on BOOSTXL-3PHGANINV with cascaded voltage/current PI. Derived from `electrolyzer_CCS/3ph_interleaved_buck_closed`. |

---

## 1. What you need

**Software**
- CCS 21.0.1 (Theia-based), with the C2000 compiler **TI v25.11.1.LTS**. The CCS installer
  ships it; check under *Settings → Compilers* if a build complains about the compiler.

**Hardware (for `simple_3ph_buck`)**
- LAUNCHXL-F28379D LaunchPad and a mini-USB cable
- BOOSTXL-3PHGANINV on BoosterPack site 1 (J1–J4 matched to J1–J4)
- AMC1301 isolated voltage sensor board on J7
- Bench supply for V_in (12 V in the test plan) and a resistive load

> **BoosterPack 3.3 V jumper:** with the LaunchPad on USB power (JP1/JP2/JP3 fitted), remove
> the BOOSTXL-3PHGANINV 3.3 V jumper (J6 in the UG, J5 on the schematic). Otherwise the
> BoosterPack LDO and the LaunchPad regulator both drive +3V3. Details:
> [`refs/docs/3ph_boostxl/README.md`](../refs/docs/3ph_boostxl/README.md#2-jumper-33-v-to-the-launchpad).

You can build and step through the code with only the LaunchPad attached. The gate driver
stays disabled until you enable it from the debugger (see section 4).

---

## 2. Folder layout

```
Common/
├── refs/docs/                          board notes and TI PDFs (see References)
└── 28379D_demo/
    ├── README.md                       this file
    ├── 28379D_Launchpad/               LaunchPad-only examples, one CCS project per folder
    │   └── led_toggle/, gpio/, epwm/, epwm_interrupt/, epwm_modes/   (same layout as below,
    │                                   minus the .h file and README.md)
    └── Boostxl_3phganINV_demo/         demos for BOOSTXL-3PHGANINV
        └── simple_3ph_buck/
            ├── simple_3ph_buck_main.c  application code: init, ISR, control loops (edit this)
            ├── simple_3ph_buck.h       defines, externs, hardware constants
            ├── README.md               converter design, variables, startup sequence
            ├── device/                 device.c/.h, driverlib.h, code-start branch ┐ copied from C2000Ware
            │   └── driverlib/          F2837xD DriverLib sources (built with project) ┘ v26.02.00.00
            ├── cmd/                    linker files (one per build configuration)
            ├── targetConfigs/          debug probe setup (on-board XDS100v2)
            └── CPU1_RAM/, CPU1_FLASH/  build output (generated, safe to delete)
```

Only `<project>_main.c` and `<project>.h` hold application logic. Application files are
named after their project, so it stays clear which project a file belongs to when several are
open in CCS. Treat `device/` as vendored TI code: leave it unchanged unless you are
deliberately updating the C2000Ware version.

---

## 3. First build

1. **Import**: *File → Import Projects*, select
   `Common/28379D_demo/Boostxl_3phganINV_demo/simple_3ph_buck`. Import the folder in place; do not
   copy it into the workspace.
2. **Pick a build configuration**:
   - `CPU1_RAM` (default): program is loaded into RAM by the debugger. Use this for
     development. It is lost on power-off.
   - `CPU1_FLASH`: program is written to flash and runs standalone after reset.
3. **Build** (hammer icon, or right-click the project → *Build Project*).

A clean `CPU1_RAM` build ends with `Finished building target: "simple_3ph_buck.out"` and
two known warnings from TI DriverLib, which are safe to ignore: `can.c` #552 and
`sysctl.c` #179 (unused local variables in TI's code).

---

## 4. Run on the board

1. Connect the LaunchPad over USB. The green power LEDs (D1, D4) should light.
2. Keep switch **S1 position 3 (TRSTn) up**, or the debugger cannot connect.
3. Start a debug session on the project. CCS loads the `.out` file and stops at `main()`.
   Press *Resume* to run.
4. Add the control variables to the *Expressions* view and change them live. The full list
   is in [`simple_3ph_buck/README.md`](Boostxl_3phganINV_demo/simple_3ph_buck/README.md#runtime-variables-ccs-expressions).

> **Before powering the converter**, follow the
> [recommended startup sequence](Boostxl_3phganINV_demo/simple_3ph_buck/README.md#recommended-startup-sequence).
> The firmware boots with `buckEnableGate = 0` (gate driver off) and both control loops
> off. Check the PWM on a scope first, then enable the gate driver, then the current loop,
> then the voltage loop, one at a time.

---

## 5. How the projects are set up

**Why the TI files are copied in.** The original project pulled DriverLib, device support
and linker files from the Motor Control SDK install. The code only ever used DriverLib and
`device.c`, so those files were copied from C2000Ware v26.02.00.00 into `device/`. The
project now has no external product references, so it builds on any machine with CCS.

**Build settings worth knowing** (same in both configurations):

| Setting | Value | Why |
|---------|-------|-----|
| `CPU1` | defined | `device.h` requires it; this code runs on CPU1 only |
| `_LAUNCHXL_F28379D` | defined | LaunchPad has a 10 MHz crystal; without it the PLL setup assumes 20 MHz and the CPU runs at half speed |
| `_FLASH` | `CPU1_FLASH` only | `device.c` copies `.TI.ramfunc` from flash to RAM and sets flash wait states |
| Optimisation | `-O4`, `--opt_for_speed=5`, `--fp_mode=relaxed` | the control ISR runs at a high rate |
| FPU / TMU / VCU | fpu32 / tmu0 / vcu2 | F28379D hardware accelerators |
| Stack | `0x380` | kept from the original project |

**One change to the TI linker files.** The four ADC log buffers (`adcBuf*`, 256 floats each,
2 KW total) did not fit in RAMLS5, so `.bss` was moved to RAMGS2–RAMGS4 in both files under
`cmd/`. If you add large global arrays, check the `.map` file in the build folder.

---

## 6. Starting a new project from this one

Duplicate `simple_3ph_buck` from inside CCS (project context menu, copy under a new name)
instead of copying the folder in Finder. A Finder copy keeps the name `simple_3ph_buck`
inside `.project`, and CCS will not import two projects with the same name.

Do not edit `.project`, `.cproject` or `.ccsproject` by hand. Change include paths, defines
and options in *Project → Properties*.

---

## 7. Troubleshooting

| Symptom | Likely cause |
|---------|--------------|
| Debugger cannot connect | S1 position 3 (TRSTn) is down, or the USB cable is power-only |
| Timing and PWM frequency off by 2× | `_LAUNCHXL_F28379D` missing from the build configuration |
| Link error #10099 "program will not fit" | A section outgrew its memory block; read the `.map` file and adjust `cmd/` |
| Program gone after power cycle | Expected for `CPU1_RAM`; build and load `CPU1_FLASH` for standalone use |
| ADC values look wrong | ADC reference on the LaunchPad is 3.0 V (REF5030), not 3.3 V. `BUCK_ADC_VREF_V` and the calibration factors in `simple_3ph_buck.h` go together: if you change one, rescale the others |

---

## References

Board notes, checked against the TI PDFs stored next to them:
- [`refs/docs/28379d/README.md`](../refs/docs/28379d/README.md): LaunchPad jumpers, boot switch,
  BoosterPack header pinout, ADC reference, known board issues
- [`refs/docs/3ph_boostxl/README.md`](../refs/docs/3ph_boostxl/README.md): BOOSTXL-3PHGANINV
  pin mapping, enable/OCP logic, sensing scale factors, power-up checklist

TI documents not stored locally:
- TMS320F2837xD Technical Reference Manual (SPRUHM8)
- TMS320F2837xD Data Manual (SPRS880)
- LAUNCHXL-F28379D User's Guide (SPRUI77)
- C2000Ware on GitHub: <https://github.com/TexasInstruments/c2000ware-core-sdk>
