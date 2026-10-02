# 28379D_Launchpad

Small examples for the TMS320F28379D LaunchPad (LAUNCHXL-F28379D). Each one teaches a
single idea and needs **only the LaunchPad and a USB cable**; no BoosterPack is required.
Start at the top of the table and work down.

| # | Project | What it shows | Pins | Watch in CCS Expressions |
|---|---------|---------------|------|--------------------------|
| 1 | [`led_toggle/`](led_toggle/led_toggle_main.c) | Hello world: blink the two user LEDs alternately with a software delay | D10 = GPIO31, D9 = GPIO34 | `blinkCount` |
| 2 | [`gpio/`](gpio/gpio_main.c) | GPIO output and input: a variable drives D9, an input pin (with pull-up and filter) drives D10 | GPIO32 = J1 pin 2, D10, D9 | `redLedOn` (write), `inputLevel` |
| 3 | [`epwm/`](epwm/epwm_main.c) | One ePWM module, two outputs, independent duty (up-count, 10 kHz) | GPIO0 = J4 pin 40, GPIO1 = J4 pin 39 | `dutyA_pct`, `dutyB_pct` (write), `cmpA`, `cmpB` |
| 4 | [`epwm_interrupt/`](epwm_interrupt/epwm_interrupt_main.c) | ePWM interrupt: ISR locked to the PWM period blinks D10 and ramps the duty | GPIO0 = J4 pin 40, D10 | `isrCount`, `cmpA`, `ledTicks` |
| 5 | [`epwm_modes/`](epwm_modes/epwm_modes_main.c) | ePWM counter modes side by side: up (left-aligned), down (right-aligned), up-down (centred) | GPIO0 / GPIO2 / GPIO4 = J4 pin 40 / 38 / 36 | `dutyPct` (write), `cmpUp`, `cmpDown`, `cmpUpDown` |

Variables marked *(write)* are inputs: change them while the program runs. The others are
read-only. Every example file starts with a header comment that explains the behaviour in
detail, and the code uses the same DriverLib calls as
[`../Boostxl_3phganINV_demo/simple_3ph_buck`](../Boostxl_3phganINV_demo/simple_3ph_buck/README.md),
so you can read it as a stripped-down version of that project.

---

## 1. Run an example

1. **Import**: *File → Import Projects*, select the project folder (for example
   `28379D_Launchpad/led_toggle`). Import it in place; do not copy it into the workspace.
2. **Build** with the default `CPU1_RAM` configuration. A clean build ends with
   `Finished building target` and two known warnings from TI DriverLib (`can.c` #552,
   `sysctl.c` #179). Anything else comes from the example and should be looked at.
3. **Connect** the LaunchPad over USB. Keep switch **S1 position 3 (TRSTn) up**, or the
   debugger cannot connect.
4. **Debug**: CCS loads the `.out` file and stops at `main()`. Press *Resume*.
5. **Change values**: add the variables from the table to the *Expressions* view.

The general build, run and troubleshooting notes are in
[`../README.md`](../README.md). They apply to these projects as well.

---

## 2. Board facts used by the examples

| Item | Value |
|------|-------|
| LED D10 (blue) | GPIO31, **active low** (write 0 = on) |
| LED D9 (red) | GPIO34, **active low** |
| ePWM1A / 1B | GPIO0 / GPIO1 (J4 pins 40 / 39) |
| ePWM2A / 3A | GPIO2 / GPIO4 (J4 pins 38 / 36) |
| GPIO input used by `gpio` | GPIO32 = J1 pin 2; GND is next to it on J3 pin 22 |
| SYSCLK, EPWMCLK | 200 MHz, 100 MHz (`_LAUNCHXL_F28379D` selects the 10 MHz crystal setup) |

The CCS bundled board notes list the LEDs as active high and swap their GPIOs. That is
wrong; see [`../../refs/docs/28379d/README.md`](../../refs/docs/28379d/README.md#4-on-board-peripherals)
(section 4) and its corrections table. Pin numbers above follow the BoosterPack standard
used there (J1 = 1-10, J2 = 11-20, J3 = 21-30, J4 = 31-40).

**Probing the ePWM examples.** The ePWM outputs are on the BoosterPack header, so use a scope
or logic analyzer on the J4 pins. If a BOOSTXL-3PHGANINV is plugged in, its gate driver
stays disabled until GPIO124 (`nEn_uC`) is driven low, and none of these examples touches
GPIO124. Still, remove the BoosterPack if you are not using it. While the CPU is halted at
a breakpoint the ePWM time base stops and the outputs hold their level.

---

## 3. How the projects are set up

Each folder is a complete, self-contained CCS project, created by copying
`simple_3ph_buck` inside CCS and replacing the application code. They share its settings:
C2000 compiler TI v25.11.1.LTS, `CPU1` and `_LAUNCHXL_F28379D` defined, `-O4`, FPU/TMU/VCU
enabled, and the TI DriverLib files vendored in `device/` (C2000Ware v26.02.00.00). See
section 5 of [`../README.md`](../README.md) for the reasons.

```
<project>/
├── <project>_main.c   the example (edit this)
├── device/            device.c/.h, driverlib.h, DriverLib sources (vendored, leave unchanged)
├── cmd/               linker files (one per build configuration)
├── targetConfigs/     debug probe setup (on-board XDS100v2)
└── CPU1_RAM/          build output (generated, safe to delete)
```

There is no per-project header: the examples are one file each. Do not add a header named
like a DriverLib header (`gpio.h`, `epwm.h`), because the project folder is on the include
path and it would be picked up instead of the DriverLib one.

**Optimisation.** `-O4` is kept so the build output matches the other projects. For
single-stepping you can set *Optimization level* to *Off* in *Project → Properties*. This
adds two harmless linker warnings (`#10247-D`, MSGRAM sections) because unused IPC data is
no longer removed.

**Start a new example from one of these.** Duplicate a project from inside CCS (not in
Finder), delete the old `<project>_main.c` and the `CPU1_RAM/` folder, and add your own
`<new>_main.c`. The `led_toggle` project is the smallest starting point.
