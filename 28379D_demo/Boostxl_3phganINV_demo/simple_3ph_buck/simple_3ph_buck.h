//#############################################################################
//
// FILE:    simple_3ph_buck.h
//
// TITLE:   3-Phase Interleaved Synchronous Buck Converter
//          Hardware definitions and user-tunable parameters
//
// Target:  TMS320F28379D LaunchPad + BOOSTXL-3PHGANINV
//          J1<->J1, J2<->J2, J3<->J3, J4<->J4
//
//#############################################################################

#ifndef SIMPLE_3PH_BUCK_H
#define SIMPLE_3PH_BUCK_H

#include "device.h"
#include "driverlib.h"

//=============================================================================
// System clock
//=============================================================================
// DEVICE_SYSCLK_FREQ is defined in device.h (200 MHz for F28379D)
// EPWMCLK = SYSCLK / 2 = 100 MHz
#define EPWMCLK_FREQ_MHZ    100U   // ePWM clock in MHz

//=============================================================================
// SI-Unit Defaults — set these in CCS Expressions/Watch window at runtime
//=============================================================================
// Switching frequency in Hz
#define BUCK_DEFAULT_FSW_HZ         100000.0f    // 50 kHz

// Duty cycle in percent (0.0 to 100.0)
#define BUCK_DEFAULT_DUTY_PCT      10.0f       // 30 %

// Symmetric dead time in nanoseconds (applied to both rising and falling edges)
// EPWMCLK = 100 MHz  →  1 count = 10 ns
// 500 ns = 50 counts (0.5 us)
#define BUCK_DEFAULT_DEADTIME_NS    20.0f      // 500 ns

//=============================================================================
// SI → Register Conversion Macros
// EPWMCLK = 100 MHz (EPWMCLK_FREQ_MHZ = 100)
// Up-down counter: full period = 2 * TBPRD EPWMCLK cycles
//   TBPRD = EPWMCLK_Hz / (2 * Fsw_Hz)
// CMPA = TBPRD * (duty_pct / 100)
// Dead-band counts = deadtime_ns / 10  (10 ns per count @ 100 MHz)
//=============================================================================
#define BUCK_FSW_TO_TBPRD(fsw_hz) \
    ((uint16_t)((uint32_t)(EPWMCLK_FREQ_MHZ) * 1000000UL / \
                (2UL * (uint32_t)(fsw_hz))))

#define BUCK_PCT_TO_CMPA(tbprd, pct) \
    ((uint16_t)((float)(tbprd) * (pct) / 100.0f))

#define BUCK_NS_TO_DBCOUNTS(ns) \
    ((uint16_t)((float)(ns) / 10.0f))    // 10 ns per count @ 100 MHz EPWMCLK

//=============================================================================
// ePWM Module Assignments
//=============================================================================
// Phase A: ePWM1 (GPIO0=ePWM1A high-side, GPIO1=ePWM1B low-side) — J4 pins 40/39
// Phase B: ePWM2 (GPIO2=ePWM2A high-side, GPIO3=ePWM2B low-side) — J4 pins 38/37
// Phase C: ePWM3 (GPIO4=ePWM3A high-side, GPIO5=ePWM3B low-side) — J4 pins 36/35
// RTI:     ePWM4 (GPIO6=ePWM4A debug output) — sync chain: ePWM3→ePWM4
//          ePWM5/6 still pass sync through to nothing (ePWM7 unused)
#define BUCK_PHASE_A_BASE       EPWM1_BASE
#define BUCK_PHASE_B_BASE       EPWM2_BASE
#define BUCK_PHASE_C_BASE       EPWM3_BASE
#define BUCK_RTI_BASE           EPWM4_BASE

// ePWM4 interrupt — PIE group 3, channel 4
#define BUCK_RTI_INT            INT_EPWM4

//=============================================================================
// GPIO Assignments
//=============================================================================
// PWM outputs (mapped to BOOSTXL J4)
#define BUCK_GPIO_PHA_H         0U      // ePWM1A — Phase A high-side
#define BUCK_GPIO_PHA_L         1U      // ePWM1B — Phase A low-side
#define BUCK_GPIO_PHB_H         2U      // ePWM2A — Phase B high-side
#define BUCK_GPIO_PHB_L         3U      // ePWM2B — Phase B low-side
#define BUCK_GPIO_PHC_H         4U      // ePWM3A — Phase C high-side
#define BUCK_GPIO_PHC_L         5U      // ePWM3B — Phase C low-side

// nEn_uC — Gate driver enable from MCU (J2 pin 13 = GPIO124)
// Active LOW: drive LOW to enable gate driver outputs, HIGH to disable.
// Must be driven LOW before PWM switching reaches the GaN half-bridges.
#define BUCK_GPIO_NEN_UC        124U

// Over-temperature (OT, active low) input from BOOSTXL (J4 pin 34 = GPIO24)
#define BUCK_GPIO_OT            24U

// LED heartbeat (on-board LaunchPad LEDs)
#define BUCK_GPIO_LED_BLUE      31U     // Blue LED D10
#define BUCK_GPIO_LED_RED       34U     // Red LED D9

// ADC-start debug toggle — GPIO58 (J2 pin 15 on LaunchPad)
// Pulses HIGH when ePWM7 SOCA fires; returns LOW when conversion is complete.
// Probe this pin to verify ADC timing against the PWM carriers on a scope.
#define BUCK_GPIO_ADC_DBG       58U

// ePWM4 ISR debug toggle — GPIO6 (ePWM4A pin, J8 pin 80 on LaunchPad)
// Configured as plain GPIO output — toggled every ISR call.
// Produces a square wave at 6×Fsw — use to verify ePWM4 RTI firing rate
// and phase alignment against ePWM1/2/3 on the scope.
#define BUCK_GPIO_EPWM4_DBG     6U

//=============================================================================
// ADC Configuration
// Current sensors on J3: ADCA/B/C IN2, Vdc on ADCD IN14
//=============================================================================
// Phase A current  — ADCC channel 2  (J3 pin 27: ADCINC2)
#define BUCK_IA_ADC_BASE        ADCC_BASE
#define BUCK_IA_RESULT_BASE     ADCCRESULT_BASE
#define BUCK_IA_ADC_CH          ADC_CH_ADCIN2
#define BUCK_IA_ADC_SOC         ADC_SOC_NUMBER0
#define BUCK_IA_ADC_PPB         ADC_PPB_NUMBER1

// Phase B current  — ADCB channel 2  (J3 pin 28: ADCINB2)
#define BUCK_IB_ADC_BASE        ADCB_BASE
#define BUCK_IB_RESULT_BASE     ADCBRESULT_BASE
#define BUCK_IB_ADC_CH          ADC_CH_ADCIN2
#define BUCK_IB_ADC_SOC         ADC_SOC_NUMBER0
#define BUCK_IB_ADC_PPB         ADC_PPB_NUMBER1

// Phase C current  — ADCA channel 2  (J3 pin 29: ADCINA2)
#define BUCK_IC_ADC_BASE        ADCA_BASE
#define BUCK_IC_RESULT_BASE     ADCARESULT_BASE
#define BUCK_IC_ADC_CH          ADC_CH_ADCIN2
#define BUCK_IC_ADC_SOC         ADC_SOC_NUMBER0
#define BUCK_IC_ADC_PPB         ADC_PPB_NUMBER1

// DC bus voltage   — ADCD channel 14 (J3 pin 23: ADCIN14)
#define BUCK_VDC_ADC_BASE       ADCD_BASE
#define BUCK_VDC_RESULT_BASE    ADCDRESULT_BASE
#define BUCK_VDC_ADC_CH         ADC_CH_ADCIN14
#define BUCK_VDC_ADC_SOC        ADC_SOC_NUMBER0
#define BUCK_VDC_ADC_PPB        ADC_PPB_NUMBER1

// Output voltage   — AMC1301 differential outputs read as two single-ended
// channels on ADCA, software differential: vOut = (VOUTP - VOUTN) * scale
// OUTP on ADCINA4 (J7 pin 69), OUTN on ADCINA5 (J7 pin 66)
#define BUCK_VOUTP_ADC_BASE     ADCA_BASE
#define BUCK_VOUTP_RESULT_BASE  ADCARESULT_BASE
#define BUCK_VOUTP_ADC_CH       ADC_CH_ADCIN4
#define BUCK_VOUTP_ADC_SOC      ADC_SOC_NUMBER1

#define BUCK_VOUTN_ADC_BASE     ADCA_BASE
#define BUCK_VOUTN_RESULT_BASE  ADCARESULT_BASE
#define BUCK_VOUTN_ADC_CH       ADC_CH_ADCIN5
#define BUCK_VOUTN_ADC_SOC      ADC_SOC_NUMBER2

// ADC sampling point selection.
// Each phase current is sampled at ITS OWN carrier peak/valley (per-phase
// synchronous sampling) — Phase A from ePWM1, B from ePWM2, C from ePWM3.
// VDC and output voltage stay on ePWM4 (sampled at every peak/valley at 6×Fsw).
//
// All ePWMs are up-down count (TBCTR counts 0↔TBPRD):
//   CTR=ZERO = carrier VALLEY (counter at minimum)
//   CTR=PRD  = carrier PEAK   (counter at maximum)
//
//   0 = sample at carrier VALLEY (SOCA at CTR=ZERO)
//   1 = sample at carrier PEAK   (SOCB at CTR=PRD)
//   2 = sample at BOTH valley and peak (averaged in ISR)
//
// NOTE on duty vs edge proximity:
//   For small duty, PWM edges occur at CTR=CMPA (small value, near VALLEY).
//   → Sample at PEAK (mode 1) for maximum distance from switching edges.
//   For duty > 50%, edges are near PEAK; sample at VALLEY (mode 0).
#define BUCK_ADC_SAMPLE_MODE    1

#if (BUCK_ADC_SAMPLE_MODE == 0)
// VALLEY: SOCA triggers at CTR=ZERO
#define BUCK_IA_ADC_TRIGGER     ADC_TRIGGER_EPWM1_SOCA
#define BUCK_IB_ADC_TRIGGER     ADC_TRIGGER_EPWM2_SOCA
#define BUCK_IC_ADC_TRIGGER     ADC_TRIGGER_EPWM3_SOCA
#define BUCK_VDC_ADC_TRIGGER    ADC_TRIGGER_EPWM4_SOCA
#define BUCK_VOUT_ADC_TRIGGER   ADC_TRIGGER_EPWM4_SOCA
#elif (BUCK_ADC_SAMPLE_MODE == 1)
// PEAK: SOCB triggers at CTR=PRD
#define BUCK_IA_ADC_TRIGGER     ADC_TRIGGER_EPWM1_SOCB
#define BUCK_IB_ADC_TRIGGER     ADC_TRIGGER_EPWM2_SOCB
#define BUCK_IC_ADC_TRIGGER     ADC_TRIGGER_EPWM3_SOCB
#define BUCK_VDC_ADC_TRIGGER    ADC_TRIGGER_EPWM4_SOCB
#define BUCK_VOUT_ADC_TRIGGER   ADC_TRIGGER_EPWM4_SOCB
#elif (BUCK_ADC_SAMPLE_MODE == 2)
// Peak (SOCA) + Valley (SOCB), averaged in ISR
#define BUCK_IA_ADC_TRIGGER     ADC_TRIGGER_EPWM1_SOCA
#define BUCK_IA_ADC_TRIGGER_B   ADC_TRIGGER_EPWM1_SOCB
#define BUCK_IB_ADC_TRIGGER     ADC_TRIGGER_EPWM2_SOCA
#define BUCK_IB_ADC_TRIGGER_B   ADC_TRIGGER_EPWM2_SOCB
#define BUCK_IC_ADC_TRIGGER     ADC_TRIGGER_EPWM3_SOCA
#define BUCK_IC_ADC_TRIGGER_B   ADC_TRIGGER_EPWM3_SOCB
#define BUCK_VDC_ADC_TRIGGER    ADC_TRIGGER_EPWM4_SOCA
#define BUCK_VDC_ADC_TRIGGER_B  ADC_TRIGGER_EPWM4_SOCB
#define BUCK_VOUT_ADC_TRIGGER   ADC_TRIGGER_EPWM4_SOCA
#define BUCK_VOUT_ADC_TRIGGER_B ADC_TRIGGER_EPWM4_SOCB
// Second-sample SOC numbers (valley) for each module
#define BUCK_IA_ADC_SOC_B       ADC_SOC_NUMBER1      // ADCC
#define BUCK_IB_ADC_SOC_B       ADC_SOC_NUMBER1      // ADCB
#define BUCK_IC_ADC_SOC_B       ADC_SOC_NUMBER5      // ADCA (SOC1-4 taken by VOUT)
#define BUCK_VDC_ADC_SOC_B      ADC_SOC_NUMBER1      // ADCD
#define BUCK_VOUTP_ADC_SOC_B    ADC_SOC_NUMBER3      // ADCA
#define BUCK_VOUTN_ADC_SOC_B    ADC_SOC_NUMBER4      // ADCA
#else
#error "BUCK_ADC_SAMPLE_MODE must be 0, 1, or 2"
#endif

// ADC sample window in SYSCLK cycles (14 = ~70 ns @ 200 MHz)
#define BUCK_ADC_SAMPLE_WINDOW  14U

//=============================================================================
// RTI Frequency
// ePWM7 runs at 6x switching frequency so it fires at every peak and valley
// of each of the 3 interleaved carriers.
//
// ePWM7 is up-count mode:
//   RTI period = Switching period / 6
//   Switching period (in EPWMCLK cycles) = 2 * TBPRD  (up-down)
//   RTI_TBPRD = (2 * buckTBPRD) / 6 = buckTBPRD / 3
//
// Computed at runtime: rtiTBPRD = buckTBPRD / 3
//=============================================================================

//=============================================================================
// Phase shift values (computed at runtime from buckTBPRD)
// Phase B: phaseB_shift = (2 * buckTBPRD) / 3   (120 deg)
// Phase C: phaseC_shift = (4 * buckTBPRD) / 3   (240 deg)
//=============================================================================

//=============================================================================
// Convenience ADC read macros
//=============================================================================
#define READ_IA_PPB()   ADC_readPPBResult(BUCK_IA_RESULT_BASE, BUCK_IA_ADC_PPB)
#define READ_IB_PPB()   ADC_readPPBResult(BUCK_IB_RESULT_BASE, BUCK_IB_ADC_PPB)
#define READ_IC_PPB()   ADC_readPPBResult(BUCK_IC_RESULT_BASE, BUCK_IC_ADC_PPB)
#define READ_VDC()      ADC_readResult(BUCK_VDC_RESULT_BASE, BUCK_VDC_ADC_SOC)
#define READ_VOUTP()    ADC_readResult(BUCK_VOUTP_RESULT_BASE, BUCK_VOUTP_ADC_SOC)
#define READ_VOUTN()    ADC_readResult(BUCK_VOUTN_RESULT_BASE, BUCK_VOUTN_ADC_SOC)

//=============================================================================
// Current and Voltage Scaling
// *** VERIFY ALL VALUES AGAINST YOUR BOARD SCHEMATIC ***
//=============================================================================
#define BUCK_ADC_VREF_V             3.0f        // ADC reference voltage (V): LaunchPad REF5030 drives VREFHI
#define BUCK_ADC_FS_COUNTS          4096.0f     // 12-bit full scale (2^12)

// Phase current sensing: shunt resistor + current sense amplifier
// BOOSTXL-3PHGANINV: 5 mΩ shunts (R55/R61/R69), INA240A1 gain = 20 V/V
// (schematic SLURAY0A sheet 4).
// Nominal shunt is 5 mΩ. Calibrated against a current probe on Phase A:
// 6.163 mΩ when VREF was entered as 3.3 V (1.2325× correction); rescaled to
// 6.163 × 3.0/3.3 = 5.6027 mΩ (1.1205×) after VREF was corrected to 3.0 V,
// so the A/count result is unchanged.
// Remaining 1.12× covers R_shunt + INA240 gain tolerance.
#define BUCK_I_RSHUNT_OHM           0.0056027f  // Shunt resistance (Ω), gain-calibrated
#define BUCK_I_AMP_GAIN             20.0f       // Amplifier gain (V/V)
// A/count = VREF / (ADC_FS × Rshunt × Gain)
// = 3.0 / (4096 × 0.0056027 × 20) = 0.006536 A/count (~6.54 mA/count)
#define BUCK_I_SCALE_A_PER_COUNT    (BUCK_ADC_VREF_V / \
                                     (BUCK_ADC_FS_COUNTS * \
                                      BUCK_I_RSHUNT_OHM  * \
                                      BUCK_I_AMP_GAIN))

// DC bus voltage sensing: resistor divider on ADCD IN14
// Verify R_top and R_bot from schematic.
// V/count = VREF × (R_top + R_bot) / (ADC_FS × R_bot)
#define BUCK_VDC_R_TOP_OHM          100000.0f   // Divider top resistor (Ω)
#define BUCK_VDC_R_BOT_OHM          4220.0f     // Divider bottom resistor (Ω)
// Full-scale: 3.0 × (100k + 4.22k) / 4.22k = 74.1 V
// Resolution: 74.1 / 4096 = ~18.1 mV/count (not calibrated)
#define BUCK_VDC_SCALE_V_PER_COUNT  (BUCK_ADC_VREF_V * \
                                     (BUCK_VDC_R_TOP_OHM + BUCK_VDC_R_BOT_OHM) / \
                                     (BUCK_ADC_FS_COUNTS * BUCK_VDC_R_BOT_OHM))

// Output voltage sensing: voltage divider → AMC1301 isolated amplifier
// Signal chain: Vout_buck → divider (R_top=182 Ω, R_bot=16.5 Ω) → AMC1301 (gain=8.2)
// V_adc_diff = Vout_buck × [R_bot/(R_top+R_bot)] × AMC1301_gain
// Vout_buck  = V_adc_diff / [R_bot/(R_top+R_bot)] / AMC1301_gain
// *** VERIFY DIVIDER AND GAIN AGAINST YOUR SENSOR BOARD SCHEMATIC ***
#define BUCK_VOUT_R_TOP_OHM         182.0f      // Divider top resistor (Ω)
#define BUCK_VOUT_R_BOT_OHM         16.5f       // Divider bottom resistor (Ω)
#define BUCK_VOUT_AMC1301_GAIN      8.2f        // AMC1301 voltage gain (V/V)

// Calibration factor — applied to nominal sensor gain to correct for
// resistor tolerances and AMC1301 gain tolerance.
// Measured via duty-sweep: vOut_avg/actual ≈ 1.136 with VREF entered as 3.3 V;
// rescaled to 1.136 × 3.0/3.3 = 1.0327 after VREF was corrected to 3.0 V,
// so the V_out result is unchanged.
#define BUCK_VOUT_CAL_FACTOR        1.0327f
// V/count for each single-ended channel: VREF / ADC_FS
#define BUCK_VOUT_V_PER_COUNT       (BUCK_ADC_VREF_V / BUCK_ADC_FS_COUNTS)
// Overall divider+amplifier gain: [R_bot/(R_top+R_bot)] * AMC1301_gain
#define BUCK_VOUT_SENSOR_GAIN       (BUCK_VOUT_R_BOT_OHM / \
                                     (BUCK_VOUT_R_TOP_OHM + BUCK_VOUT_R_BOT_OHM) * \
                                     BUCK_VOUT_AMC1301_GAIN * \
                                     BUCK_VOUT_CAL_FACTOR)

//=============================================================================
// Current-space transformation matrices
//
// Forward (measurement): i_delta = TDelta^T * [iA; iB; iC]
// TDelta^T (2×3) — Clarke transform coefficients (power-invariant scaling)
//   Row 1: [sqrt(2/3),  -1/sqrt(6), -1/sqrt(6)]
//   Row 2: [0,          -1/sqrt(2),  1/sqrt(2) ]
//=============================================================================
#define BUCK_TDELTA_T_11    ( 0.8165f)   //  sqrt(2/3)
#define BUCK_TDELTA_T_12    (-0.4082f)   // -1/sqrt(6)
#define BUCK_TDELTA_T_13    (-0.4082f)   // -1/sqrt(6)
#define BUCK_TDELTA_T_21    ( 0.0f  )
#define BUCK_TDELTA_T_22    (-0.7071f)   // -1/sqrt(2)
#define BUCK_TDELTA_T_23    ( 0.7071f)   //  1/sqrt(2)

// Inverse (modulation): [d1;d2;d3] = Tu * [d_sigma; d_delta1; d_delta2]
// Tu (3×3) — inverse Clarke stacked with sigma pass-through
//   Row 1: [1,  sqrt(2/3),    0        ]
//   Row 2: [1, -1/sqrt(6), -1/sqrt(2)  ]
//   Row 3: [1, -1/sqrt(6),  1/sqrt(2)  ]
#define BUCK_TU_11    ( 1.0f  )
#define BUCK_TU_12    ( 0.8165f)
#define BUCK_TU_13    ( 0.0f  )
#define BUCK_TU_21    ( 1.0f  )
#define BUCK_TU_22    (-0.4082f)
#define BUCK_TU_23    (-0.7071f)
#define BUCK_TU_31    ( 1.0f  )
#define BUCK_TU_32    (-0.4082f)
#define BUCK_TU_33    ( 0.7071f)

//=============================================================================
// Extern declarations — defined in simple_3ph_buck_main.c
//=============================================================================

// ---- PRIMARY INPUTS: set these in the CCS Expressions / Watch window ----
// All three are in physical SI units — no register knowledge required.
extern volatile float    buckFreq_Hz;       // Switching frequency (Hz),  e.g. 10000.0
extern volatile float    buckDuty_pct;      // Duty cycle (%), 0.0 to 100.0, e.g. 30.0
extern volatile float    buckDeadtime_ns;   // Dead time (ns), symmetric,  e.g. 500.0
extern volatile float    buckDutyMax_pct;   // Safety duty limit (%), clamps all phases
extern volatile uint16_t buckEnableGate;    // 1 = enable gate driver (GPIO124 LOW), 0 = disable

// ---- READ-ONLY COMPUTED VALUES: inspect in CCS, do not write ----
// Updated by the ISR every time a SI input changes.
extern volatile uint16_t buckTBPRD;         // EPWM TBPRD register value  (counts)
extern volatile uint16_t buckCMPA;          // EPWM CMPA register value   (counts)
extern volatile uint16_t buckDBcounts;      // Dead-band delay register value (counts)

// ---- ADC RESULTS: read-only ----
extern volatile int16_t  adcIA;             // Phase A current (PPB counts, centred at 0 = 0 A)
extern volatile int16_t  adcIB;             // Phase B current (PPB counts, centred at 0 = 0 A)
extern volatile int16_t  adcIC;             // Phase C current (PPB counts, centred at 0 = 0 A)
extern volatile uint16_t adcVDC;            // DC bus voltage  (raw 12-bit counts)
extern volatile uint16_t adcVoutP;          // AMC1301 OUTP    (raw 12-bit counts)
extern volatile uint16_t adcVoutN;          // AMC1301 OUTN    (raw 12-bit counts)

// Filtered averages of the converted phase currents (Amps)
extern volatile float    iA_avg;            // Running average of iA_A (A)
extern volatile float    iB_avg;            // Running average of iB_A (A)
extern volatile float    iC_avg;            // Running average of iC_A (A)
extern volatile float    vOut_avg;          // Running average of vOut_V (V)
extern volatile float    lpf_iAvg_fc;       // Cutoff for averaging LPF (Hz)

// ---- CONVERTED PHYSICAL VALUES: read-only ----
extern volatile float    iA_A;              // Phase A current (A)
extern volatile float    iB_A;              // Phase B current (A)
extern volatile float    iC_A;              // Phase C current (A)
extern volatile float    vDC_V;             // DC bus voltage  (V)
extern volatile float    vOut_V;            // Output voltage  (V) — from AMC1301

// ---- CURRENT ZERO OFFSET: user-trimmable ----
// F2837xD OFFCAL register is only 10-bit (-512..+511), so -2048 cannot be
// stored in hardware. Offset subtraction is done in software instead.
// Default 2048.0 = ideal mid-rail (1.65 V). At no load, set this to the
// actual raw adcIA/IB/IC reading to zero out the sensor DC bias.
extern volatile float    buckI_ZeroOffset;  // raw counts at 0 A (default 2048.0)

// ---- SIGMA CURRENT PI CONTROLLER ----
extern volatile float    iSigma_ref;        // Reference average current per phase (A) — set in CCS
extern volatile float    iSigma;            // Measured average current per phase (A)
extern volatile float    dSigma;            // Sigma PI output: normalised duty (-1..1)
extern volatile float    piI_Kp;            // Sigma proportional gain (default 0.5)
extern volatile float    piI_Ki;            // Sigma integral gain rad/s (default 314)
extern volatile float    piI_integ;         // Sigma integral state
extern volatile float    piI_e_prev;        // Sigma previous error

// ---- VOLTAGE PI CONTROLLER (outer loop) ----
extern volatile float    vOut_ref;          // Voltage reference (V)
extern volatile float    piV_Kp;           // Voltage proportional gain (A/V)
extern volatile float    piV_Ki;           // Voltage integral gain (A/V·rad/s)
extern volatile float    piV_integ;        // Voltage integral state
extern volatile float    piV_e_prev;       // Voltage previous error
extern volatile float    piV_iMax;         // Current limit — clamp iSigma_ref (A)
extern volatile uint16_t buckVctrlEnable;  // 0=manual iSigma_ref, 1=voltage loop

// ---- DELTA CURRENT PI CONTROLLER ----
extern volatile float    iDelta1;           // Measured delta-1 current (A) — from TDelta^T
extern volatile float    iDelta2;           // Measured delta-2 current (A) — from TDelta^T
extern volatile float    iDelta1_ref;       // Delta-1 reference (A) — normally 0
extern volatile float    iDelta2_ref;       // Delta-2 reference (A) — normally 0
extern volatile float    dDelta1;           // Delta-1 PI output (-1..1)
extern volatile float    dDelta2;           // Delta-2 PI output (-1..1)
extern volatile float    piD_Kp;            // Delta proportional gain (default -5.0)
extern volatile float    piD_Ki;            // Delta integral gain rad/s (default -3.1416)
extern volatile float    piD_integ1;        // Delta-1 integral state
extern volatile float    piD_integ2;        // Delta-2 integral state
extern volatile float    piD_e1_prev;       // Delta-1 previous error
extern volatile float    piD_e2_prev;       // Delta-2 previous error

// ---- CURRENT LPF (first-order IIR, pre-PI measurement filter) ----
// H(s) = ωc / (s + ωc),  ωc = 2π·lpf_fc
// Phase lag at crossover ω_x: -arctan(ω_x / ωc)
// Default fc = 20 kHz → ~2.9° lag at 1 kHz crossover — tune in CCS.
extern volatile float    lpf_fc;            // LPF cutoff frequency (Hz)
extern volatile float    lpf_iA;            // Filtered phase A current fed to PI (A)
extern volatile float    lpf_iB;            // Filtered phase B current fed to PI (A)
extern volatile float    lpf_iC;            // Filtered phase C current fed to PI (A)

// ---- OUTPUT VOLTAGE LPF ----
// Same first-order IIR as current LPF: α = ωc·Ts / (1 + ωc·Ts)
// Default fc = 10 kHz — tune in CCS Expressions at runtime.
extern volatile float    lpf_vOut_fc;       // Voltage LPF cutoff frequency (Hz)
extern volatile float    lpf_vOut;          // Filtered output voltage (V)

// ---- PER-PHASE OUTPUTS ----
extern volatile float    dPhA;              // Phase A duty after inverse transform (-1..1)
extern volatile float    dPhB;              // Phase B duty after inverse transform (-1..1)
extern volatile float    dPhC;              // Phase C duty after inverse transform (-1..1)
extern volatile uint16_t buckCMPA_A;        // Phase A CMPA register value (counts)
extern volatile uint16_t buckCMPA_B;        // Phase B CMPA register value (counts)
extern volatile uint16_t buckCMPA_C;        // Phase C CMPA register value (counts)

extern volatile uint16_t buckCtrlEnable;    // 0 = manual buckDuty_pct, 1 = PI control

// ---- DIAGNOSTICS ----
extern volatile uint32_t rtiIsrCount;       // Increments every RTI ISR call

//=============================================================================
// Function prototypes
//=============================================================================
void setupGPIOs(void);
void setupPWMs(void);
void setupADCs(void);
void setupInterrupts(void);
__interrupt void rtiISR(void);

#endif  // SIMPLE_3PH_BUCK_H
