---
tags:
  - project/Electrolyzer
  - type/hardware
  - status/active
---

# 3-Phase Interleaved Synchronous Buck Converter

*Later revision of [[Electrolyzer/CCS/electrolyzer_CCS/3ph_int_buck_electrolyzer/README|3ph_int_buck_electrolyzer]]:
600 kHz ISR (vs. 100 kHz), retuned `piV_Ki`/`piI_Ki`, and the earlier current-loop-bandwidth TODO closed.*

**Target:** TMS320F28379D LaunchPad + BOOSTXL-3PHGANINV + external AMC1301 isolated voltage sensor
**Connector mapping:** J1↔J1, J2↔J2, J3↔J3, J4↔J4, plus J7 for voltage sensor

---

## Objective

Bring-up and closed-loop validation of a 3-phase interleaved GaN buck converter with:

1. **Per-phase current measurement** via INA240 shunt amplifiers on the BOOSTXL
2. **Isolated output voltage measurement** via AMC1301 with a resistive divider
3. **Cascaded voltage–current control** (outer voltage PI → inner sigma/delta current PI)
4. **Safe, predictable behavior during startup and transients**

Test conditions:
- V_in = 12 V
- R_load = 9.6 Ω
- F_sw = 100 kHz per phase
- Target V_out = 1.2–2 V

---

## Hardware Setup

### Board Connection

Plug the BOOSTXL-3PHGANINV directly onto the F28379D LaunchPad, matching connectors J1–J4. An external AMC1301 voltage sensor board connects to J7.

### PWM and Control Pins (LaunchPad site 1)

| Pin | GPIO | Signal | Role |
|-----|------|--------|------|
| J4.40 | 0 | ePWM1A | Phase A high-side |
| J4.39 | 1 | ePWM1B | Phase A low-side |
| J4.38 | 2 | ePWM2A | Phase B high-side |
| J4.37 | 3 | ePWM2B | Phase B low-side |
| J4.36 | 4 | ePWM3A | Phase C high-side |
| J4.35 | 5 | ePWM3B | Phase C low-side |
| J4.34 | 24 | OT | Over-temperature input (active low) |
| J2.13 | 124 | nEn_uC | Gate driver enable (active LOW) |

### ADC Pins

| Signal | LaunchPad Pin | ADC Module / Channel |
|--------|--------------|---------------------|
| Phase A current (iA) | J3 pin 27 | ADCC / IN2 |
| Phase B current (iB) | J3 pin 28 | ADCB / IN2 |
| Phase C current (iC) | J3 pin 29 | ADCA / IN2 |
| DC bus voltage (VDC) | J3 pin 23 | ADCD / IN14 |
| **V_out OUTP** | **J7 pin 69** | **ADCA / IN4** |
| **V_out OUTN** | **J7 pin 66** | **ADCA / IN5** |

### Voltage Sensor Chain

```
V_out ──► [Resistor divider: 182 Ω / 16.5 Ω] ──► AMC1301 (isolated, gain 8.2)
                                                        │
                                            (OUTP, OUTN → ADCINA4, ADCINA5)
```
- Divider ratio: 16.5 / 198.5 = 0.0831
- Sensor chain gain (R_ratio × AMC1301): 0.6816
- Applied calibration factor: ×1.0327 (compensates R/gain tolerances; see Calibration)
- Safe max V_out: ~3 V (before AMC1301 input exceeds 250 mV)

---

## Control Architecture

### Cascaded Voltage–Current Loop

```
                      ┌──────────────────────────────────────────────────┐
                      │                                                    │
vOut_ref ─►(−)─►[piV PI]─► iSigma_ref ─►(−)─►[piI PI]─► dSigma ──┐         │
       ▲                         ▲                                │         │
       │                         │                      Inverse   ▼         │
     lpf_vOut                  lpf_iA/B/C            Transform + Level     │
   (MA N=6)                  (IIR, 40 kHz)           Shift + Duty Clamp    │
       ▲                         ▲                                │         │
       │                         │                                ▼         │
       │    ◄──────── Plant (buck + load) ◄── PWM ◄───── CMPA_A/B/C         │
       │                                                                    │
       └────────────────────────────────────────────────────────────────────┘
```

- **Outer voltage loop** (`buckVctrlEnable = 1`): regulates `lpf_vOut` to `vOut_ref`, outputs `iSigma_ref`
- **Inner current loop** (`buckCtrlEnable = 1`): regulates `iSigma` (avg of 3 phase currents) to `iSigma_ref`
- **Delta loop** (optional): balances phase currents via circulating current PI (currently disabled)

Both loops support **bumpless transfer** — on 0→1 transition of either enable flag, the integrator is back-calculated from the current operating point so there is no transient kick.

### Sigma / Delta Transformation

The three phase currents are decomposed into:
- **Sigma** (common mode): average across phases — controls total output current
- **Delta1, Delta2** (differential modes): circulating currents — balance the phases

Using the standard Clarke transform with √(2/3) scaling. Sigma and delta PIs run independently; their outputs are combined via the inverse transform to produce per-phase duties.

---

## ADC Sampling Architecture

### Per-Phase Synchronous Sampling

Each phase's current is sampled at **its own carrier peak or valley**, not at a global instant:

| Signal | Trigger | Sample Rate |
|--------|---------|-------------|
| iA | ePWM1 SOCA/B | 100 kHz (1× per switching cycle) |
| iB | ePWM2 SOCA/B | 100 kHz |
| iC | ePWM3 SOCA/B | 100 kHz |
| VDC | ePWM4 SOCA/B | 600 kHz |
| V_out (OUTP/OUTN) | ePWM4 SOCA/B | 600 kHz |

This places each phase's sample in the middle of its own PWM period, maximally distant from switching edges. The ISR runs at 6× F_sw = 600 kHz (triggered by ePWM4).

### Sampling Mode (`BUCK_ADC_SAMPLE_MODE`)

| Mode | Value | Notes |
|------|-------|-------|
| 0 | VALLEY (CTR=ZERO) | Best for duty > 50% |
| **1** | **PEAK (CTR=PRD)** | **Default — best for duty < 50%** |
| 2 | BOTH, averaged | 2× sample rate, linear phase |

Ideal for this system's 5–20% duty operating range → **mode 1 (peak)** maximizes distance from PWM edges.

---

## Filtering

### Current LPF (fed to PI)
- First-order IIR, cutoff = **40 kHz**
- Phase lag at 1 kHz: ~1.4°

### Voltage Filter (fed to PI)
- **Moving average, N = 6 samples at 600 kHz**
- Exact zeros at 100/200/300/400/500 kHz — complete rejection of switching-synchronous noise
- Group delay: 4.17 μs (linear phase)

### Heavily-Filtered Averages (for DC inspection)
- IIR LPF at 100 Hz on `iA_A`, `iB_A`, `iC_A`, `vOut_V`
- Exposes `iA_avg`, `iB_avg`, `iC_avg`, `vOut_avg` for calibration and steady-state readout

---

## Calibration

### Current Sensor

The INA240 gain was measured against a current probe on Phase A; a common gain correction was applied via `BUCK_I_RSHUNT_OHM`:
- Nominal: 5 mΩ
- **Calibrated: 5.6027 mΩ** (1.1205× correction)

The calibration was first done with `BUCK_ADC_VREF_V = 3.3`, giving 6.163 mΩ (1.2325×).
The LaunchPad ADC reference is actually 3.0 V (REF5030), so VREF was corrected to 3.0 and the
shunt value scaled by 3.0/3.3. The resulting A/count (6.54 mA/count) is unchanged.

Per-phase zero offsets manually tuned against a degaussed current probe:
- `buckIA_ZeroOffset = 2244.03`
- `buckIB_ZeroOffset = 2245.37`
- `buckIC_ZeroOffset = 2240.69`

### Voltage Sensor

Calibrated against a DMM via duty sweep at V_in = 12 V, R_load = 9.6 Ω:
- Nominal sensor gain: 0.6816 (= 16.5/198.5 × 8.2)
- **Calibration factor: 1.0327** (captures resistor/AMC tolerances)
- Effective sensor gain: 0.7039 (with VREF = 3.0 V)

Like the current sensor, this was fitted as 1.136 with VREF = 3.3 V and rescaled by 3.0/3.3
when VREF was corrected. V_out readings are unchanged.

### DC Bus Voltage

`vDC_V` has no calibration factor. Until VREF was corrected it read about 10 % high; with
VREF = 3.0 V it uses the nominal 100 kΩ / 4.22 kΩ divider (18.1 mV/count, 74.1 V full scale).
It is monitoring only and does not enter any control loop.

---

## Safety Features

### Duty Cycle Hard Clamp
`buckDutyMax_pct = 22%` — per-phase CMPA is hard-clamped before hardware write. Prevents V_out from exceeding ~2.6 V (within 3 V AMC1301 limit). Cannot be bypassed by integrator windup or delta corrections.

### Current Reference Clamp
`piV_iMax = 2 A` — voltage PI output (iSigma_ref) is clamped with back-calculation anti-windup.

### PI Anti-Windup
Back-calculation method on all PI controllers (sigma, delta1, delta2, voltage). Integrator is reset to boundary value when output saturates.

### Manual Mode Reset
When `buckCtrlEnable = 0`, all PI integrator states are continuously reset to zero, so the next enable starts clean (and bumpless transfer seeds from current operating point).

### Gate Driver Gate
`buckEnableGate` drives the BOOSTXL's `nEn_uC` pin. Default 0 (disabled) at startup.

---

## Runtime Variables (CCS Expressions)

### Control Enables
| Variable | Default | Purpose |
|----------|---------|---------|
| `buckEnableGate` | 0 | Gate driver enable (0=off, 1=on) |
| `buckCtrlEnable` | 0 | Current loop (0=manual, 1=PI) |
| `buckVctrlEnable` | 0 | Voltage loop (0=manual iSigma_ref, 1=PI) |

### PWM Parameters
| Variable | Default | Purpose |
|----------|---------|---------|
| `buckFreq_Hz` | 100000 | Switching frequency (Hz) |
| `buckDuty_pct` | 10 | Manual duty (%) |
| `buckDeadtime_ns` | 20 | Dead time (ns) — short for GaN |
| `buckDutyMax_pct` | 22 | Hard duty clamp (%) |

### Voltage Loop
| Variable | Default | Purpose |
|----------|---------|---------|
| `vOut_ref` | 2.0 | Target output voltage (V) |
| `piV_Kp` | 0.1 | Voltage P gain |
| `piV_Ki` | 50 | Voltage I gain (rad/s) |
| `piV_iMax` | 2.0 | Current reference clamp (A) |

### Current Loop (Sigma)
| Variable | Default | Purpose |
|----------|---------|---------|
| `iSigma_ref` | 0.2 | Current reference (A) — manual when voltage loop off |
| `piI_Kp` | 0.05 | Sigma P gain |
| `piI_Ki` | 94 | Sigma I gain (rad/s) |

### Current Loop (Delta — balancing)
| Variable | Default | Purpose |
|----------|---------|---------|
| `iDelta1_ref`, `iDelta2_ref` | 0 | Balance targets (normally 0) |
| `piD_Kp`, `piD_Ki` | 0 | Delta gains (currently disabled) |

### Filtering
| Variable | Default | Purpose |
|----------|---------|---------|
| `lpf_fc` | 40000 | Current LPF cutoff (Hz) |
| `lpf_iAvg_fc` | 100 | Slow averaging LPF cutoff (Hz) |

### Monitoring (Read-Only)

**Currents (A):**
- `iA_A`, `iB_A`, `iC_A` — raw instantaneous
- `lpf_iA`, `lpf_iB`, `lpf_iC` — filtered (fed to PI)
- `iA_avg`, `iB_avg`, `iC_avg` — heavy 100 Hz average (for DC inspection)
- `iSigma`, `iDelta1`, `iDelta2` — transformed

**Voltages (V):**
- `vOut_V` — raw instantaneous
- `lpf_vOut` — MA-filtered (fed to voltage PI)
- `vOut_avg` — heavy 100 Hz average
- `vDC_V` — DC bus voltage

**PI Outputs:**
- `dSigma`, `dDelta1`, `dDelta2` — PI outputs in normalized units (−1, +1)
- `dPhA`, `dPhB`, `dPhC` — per-phase duties in 0–1 scale

**Hardware Registers:**
- `buckTBPRD`, `buckCMPA`, `buckCMPA_A/B/C`, `buckDBcounts`

**Grapher Buffers:**
- `adcBufA`, `adcBufB`, `adcBufC` — per-phase current (A)
- `adcBufVout` — filtered output voltage (V)

---

## Recommended Startup Sequence

1. Verify PWM waveforms on scope with `buckEnableGate = 0`
2. Set desired `buckFreq_Hz` and `buckDuty_pct` (start low, e.g., 10%)
3. `buckEnableGate = 1` → converter starts switching
4. Observe `iA_avg`, `iB_avg`, `iC_avg`, `vOut_avg` — verify sensible values
5. `buckCtrlEnable = 1` with reasonable `iSigma_ref` (bumpless handoff)
6. `buckVctrlEnable = 1` with `vOut_ref = 2.0` (bumpless handoff to voltage loop)
7. (Optional) Enable delta loop with small `piD_Kp`, `piD_Ki` for phase balancing

---

## Files

| File | Purpose |
|------|---------|
| `simple_3ph_buck_main.c` | Main source — init, ISR, control loops |
| `simple_3ph_buck.h` | Defines, externs, hardware constants |
| `CPU1_RAM/`, `CPU1_FLASH/` | Build outputs |
| `device/` | `device.c/.h`, `driverlib.h`, code-start branch (C2000Ware v26.02.00.00, vendored) |
| `device/driverlib/` | F2837xD DriverLib sources, built with the project (C2000Ware v26.02.00.00, vendored) |
| `cmd/` | Linker files from C2000Ware; `.bss` moved from RAMLS5 to RAMGS2–4 for the ADC log buffers |
| `targetConfigs/` | CCS target configuration (XDS100v2, LaunchPad) |

### Build setup

Self-contained: needs only CCS with the C2000 compiler (TI v25.11.1.LTS). No C2000Ware or
Motor Control SDK install is required. Import the folder into CCS as an existing project.

- Configurations: `CPU1_RAM` (default, load to RAM for debug) and `CPU1_FLASH` (adds `_FLASH`)
- Defines: `CPU1`, `_LAUNCHXL_F28379D` (10 MHz crystal → 200 MHz SYSCLK)
- Options kept from the earlier project: `-O4`, `--opt_for_speed=5`, `--fp_mode=relaxed`,
  FPU32 / TMU0 / VCU2, stack `0x380`

---

## Current Status

- [x] 3-phase interleaved PWM with 120° phase shift
- [x] Per-phase synchronous ADC sampling
- [x] Isolated voltage measurement via AMC1301
- [x] Inner sigma current loop with bumpless transfer
- [x] Outer voltage loop with bumpless transfer and current limit
- [x] Delta transform available (loop currently disabled)
- [x] Moving average filter on voltage (ripple-synchronous nulling)
- [x] Duty limit for safety
- [x] Calibrated per-phase current offsets + voltage gain
- [ ] Delta channel tuning for active phase balancing
- [ ] Push current loop bandwidth toward 10 kHz target
