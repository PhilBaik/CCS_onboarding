//#############################################################################
//
// FILE:    simple_3ph_buck_main.c
//
// TITLE:   3-Phase Interleaved Synchronous Buck Converter
//
// Target:  TMS320F28379D LaunchPad + BOOSTXL-3PHGANINV
//          J1<->J1, J2<->J2, J3<->J3, J4<->J4
//
// Description:
//   - 3 centre-aligned complementary PWM outputs, 120 deg phase shifted
//       Phase A: ePWM1 (GPIO0/1)  — J4 pins 40/39
//       Phase B: ePWM2 (GPIO2/3)  — J4 pins 38/37
//       Phase C: ePWM3 (GPIO4/5)  — J4 pins 36/35
//   - Separate ePWM4 running at 6x switching frequency triggers RTI ISR
//       and fires ADC SOCA at every peak and valley of all 3 carriers.
//       ePWM4 TBPHS = rtiTBPRD-1 so its first CTR=PRD fires at t=0
//       (Phase A valley), keeping all 6 sample points phase-aligned to
//       the carrier peaks/valleys via the ePWM1→...→ePWM4 sync chain.
//   - ADC samples (ADCA/B/C IN2, ADCD IN14) captured noise-free at
//       triangle peak/valley midpoints (= average inductor current)
//   - buckTBPRD and buckDuty are volatile — change live in CCS Watch Window
//
//#############################################################################

#include "simple_3ph_buck.h"

//=============================================================================
// PRIMARY INPUTS — set these in the CCS Expressions / Watch window
// All values are in physical SI units.  The ISR converts them to register
// counts automatically — no need to know EPWMCLK or count formulas.
//=============================================================================

// Switching frequency in Hz
// ISR computes: buckTBPRD = 100e6 / (2 * buckFreq_Hz)
volatile float buckFreq_Hz = BUCK_DEFAULT_FSW_HZ;           // default 10 kHz

// Duty cycle in percent (0.0 = 0%, 100.0 = 100%)
// ISR computes: buckCMPA = buckTBPRD * buckDuty_pct / 100
volatile float buckDuty_pct = BUCK_DEFAULT_DUTY_PCT;        // default 30 %

// Symmetric dead time in nanoseconds (applied to both RED and FED)
// ISR computes: buckDBcounts = buckDeadtime_ns / 10  (10 ns/count @ 100 MHz)
volatile float buckDeadtime_ns = BUCK_DEFAULT_DEADTIME_NS;  // default 500 ns

// Safety duty limit (%) — hard clamp applied to all per-phase CMPA values
// before hardware write.  Protects voltage sensor (AMC1301 input ≤ 250 mV).
// Steady-state at 2V ref: D = 2/12 = 16.7%.  Sensor limit: 3/12 = 25%.
// Set below 25% to leave headroom; transient spikes above this are blocked.
volatile float buckDutyMax_pct = 22.0f;

// Gate driver enable: 0 = disabled (GPIO124 HIGH), 1 = enabled (GPIO124 LOW)
// nEn_uC on BOOSTXL-3PHGANINV is active LOW — polled in main() loop.
volatile uint16_t buckEnableGate = 0U;

//=============================================================================
// READ-ONLY COMPUTED REGISTER VALUES
// Written by ISR each time an SI input changes.  Inspect in CCS to verify
// conversions; do not write to these from the Expressions window.
//=============================================================================
volatile uint16_t buckTBPRD    = 0U;    // ePWM TBPRD  (counts)
volatile uint16_t buckCMPA     = 0U;    // ePWM CMPA   (counts)
volatile uint16_t buckDBcounts = 0U;    // Deadband delay (counts, both RED+FED)

//=============================================================================
// ADC result storage
//=============================================================================
volatile int16_t  adcIA  = 0;   // Phase A current (PPB counts, centred: 0 = 0 A)
volatile int16_t  adcIB  = 0;   // Phase B current (PPB counts, centred: 0 = 0 A)
volatile int16_t  adcIC  = 0;   // Phase C current (PPB counts, centred: 0 = 0 A)
volatile uint16_t adcVDC = 0;   // DC bus voltage (raw 12-bit counts)
volatile uint16_t adcVoutP = 0; // AMC1301 OUTP (raw 12-bit counts)
volatile uint16_t adcVoutN = 0; // AMC1301 OUTN (raw 12-bit counts)

// Heavily-filtered averages of the converted phase currents (Amps).
// With gates OFF, steady-state value = DC offset error; under load, =
// true DC current. Useful for inspection, calibration, and diagnostics.
volatile float    iA_avg       = 0.0f;   // Running average of iA_A (A)
volatile float    iB_avg       = 0.0f;   // Running average of iB_A (A)
volatile float    iC_avg       = 0.0f;   // Running average of iC_A (A)
volatile float    vOut_avg     = 0.0f;   // Running average of vOut_V (V)
volatile float    lpf_iAvg_fc  = 100.0f; // Cutoff for averaging LPF (Hz)

//=============================================================================
// Converted physical values (written by ISR, inspect in CCS Expressions)
//=============================================================================
volatile float iA_A  = 0.0f;    // Phase A current (A)
volatile float iB_A  = 0.0f;    // Phase B current (A)
volatile float iC_A  = 0.0f;    // Phase C current (A)
volatile float vDC_V = 0.0f;    // DC bus voltage  (V)
volatile float vOut_V = 0.0f;   // Output voltage  (V) — from AMC1301

// Zero-current offset in raw ADC counts.
// Default = 2048.0 (ideal mid-rail). At no load, set to the observed
// adcIA/IB/IC reading in CCS Expressions to null out sensor DC bias.
// Zero-offset calibration — manual tuning against degaussed current probe.
// Final calibration data (clean degaussed probe measurement):
//   Phase | Probe  | ADC_avg | Bias    | Δ counts (÷6.54 mA/ct)
//     A   |  47 mA |  48 mA  | +1 mA   | −0.15
//     B   |  43 mA |  50 mA  | +7 mA   | −1.07
//     C   |  36 mA |  37 mA  | +1 mA   | −0.15
// Sign rule: iA = (offset − adcIA) × scale.
// Reduce displayed value → decrease offset.
volatile float buckIA_ZeroOffset = 2244.03f;  // was 2244.18, −0.15 for +1 mA bias
volatile float buckIB_ZeroOffset = 2245.37f;  // was 2246.44, −1.07 for +7 mA bias
// volatile float buckIC_ZeroOffset = 2238.66f;  // Phase C — estimated (~15 mA bias)
// was 2240.84, −0.15 for +1 mA bias (probe=36 mA, ADC=37 mA, clean degauss)
volatile float buckIC_ZeroOffset = 2240.69f;

//=============================================================================
// Diagnostics
//=============================================================================
volatile uint32_t rtiIsrCount = 0U;  // increments each RTI ISR — use to verify ISR rate

// LED blink dividers
static uint32_t ledBlueCnt = 0U;
static uint32_t ledRedCnt  = 0U;
#define LED_BLUE_PERIOD     60000UL   // ~1 Hz @ 60 kHz RTI
#define LED_RED_PERIOD      30000UL   // ~2 Hz @ 60 kHz RTI

//=============================================================================
// Sigma current PI controller — Tustin (bilinear) discretization
//
// Control law (continuous):   u(s) = Kp*e(s) + Ki/s * e(s)
//
// Tustin substitution:  s → (2/Ts) * (z-1)/(z+1)
// Discrete integrator:  I[n] = I[n-1] + (Ts/2)*(e[n] + e[n-1])
// Discrete output:      dSigma[n] = Kp*e[n] + Ki*I[n]
//
// Ts = 1/(6*Fsw) — RTI ISR fires at 6× switching frequency.
//
// dSigma ∈ (-1, 1):  level-shifted to CMPA via
//   CMPA = (dSigma + 1)/2 * TBPRD
//   (-1 → D=0%, 0 → D=50%, +1 → D=100%)
//
// Set buckCtrlEnable=1 in CCS Expressions to close the loop.
// Set buckCtrlEnable=0 to revert to manual buckDuty_pct control.
//=============================================================================
// Sigma channel
volatile float    iSigma_ref    = 0.033f;   // Reference: average current per phase (A)
volatile float    iSigma        = 0.0f;   // Measured:  average current per phase (A)
volatile float    dSigma        = 0.0f;   // Sigma PI output: normalised duty (-1..1)
volatile float    piI_Kp        = 0.05f;   // Sigma proportional gain
volatile float    piI_Ki        = 94.0f; // Sigma integral gain (rad/s)
volatile float    piI_integ     = 0.0f;   // Sigma integral state
volatile float    piI_e_prev    = 0.0f;   // Sigma previous error

// Voltage loop PI — cascaded outer loop generating iSigma_ref
// Same Tustin discretization as current PI.
// Set buckVctrlEnable=1 (with buckCtrlEnable=1) to close the voltage loop.
volatile float    vOut_ref       = 2.0f;   // Voltage reference (V)
volatile float    piV_Kp         = 0.1f;   // Voltage proportional gain (A/V)
volatile float    piV_Ki         = 50.0f;  // Voltage integral gain (A/V·rad/s)
volatile float    piV_integ      = 0.0f;   // Voltage integral state
volatile float    piV_e_prev     = 0.0f;   // Voltage previous error
volatile float    piV_iMax       = 2.0f;   // Current limit — clamp iSigma_ref (A)
volatile uint16_t buckVctrlEnable = 0U;    // 0=manual iSigma_ref, 1=voltage loop sets it

// Delta channel
volatile float    iDelta1       = 0.0f;   // Measured delta-1 current — from TDelta^T row 1
volatile float    iDelta2       = 0.0f;   // Measured delta-2 current — from TDelta^T row 2
volatile float    iDelta1_ref   = 0.0f;   // Delta-1 reference (0 A = balanced phases)
volatile float    iDelta2_ref   = 0.0f;   // Delta-2 reference (0 A = balanced phases)
volatile float    dDelta1       = 0.0f;   // Delta-1 PI output (-1..1)
volatile float    dDelta2       = 0.0f;   // Delta-2 PI output (-1..1)
volatile float    piD_Kp        = 0.0f;      // Delta proportional gain (negative by design)
volatile float    piD_Ki        = 0.0f;   // Delta integral gain rad/s (negative by design)
volatile float    piD_integ1    = 0.0f;   // Delta-1 integral state
volatile float    piD_integ2    = 0.0f;   // Delta-2 integral state
volatile float    piD_e1_prev   = 0.0f;   // Delta-1 previous error
volatile float    piD_e2_prev   = 0.0f;   // Delta-2 previous error

// ---- Current LPF (first-order IIR, applied pre-PI) ----
// Transfer function: H(s) = ωc / (s + ωc),  ωc = 2π·lpf_fc
// Discretized with forward Euler: α = ωc·Ts / (1 + ωc·Ts)
// Phase lag at ω_cross: -arctan(ω_cross / ωc)
// Default fc = 20 kHz → < 3° lag for crossover ≤ 1 kHz
// Tune lpf_fc in CCS Expressions at runtime (Hz).
volatile float    lpf_fc        = 40000.0f; // LPF cutoff frequency (Hz) — 40 kHz for 10 kHz current loop bandwidth
volatile float    lpf_iA        = 0.0f;     // Filtered phase A current fed to PI (A)
volatile float    lpf_iB        = 0.0f;     // Filtered phase B current fed to PI (A)
volatile float    lpf_iC        = 0.0f;     // Filtered phase C current fed to PI (A)

// Output voltage filter — moving average of N=6 samples at 6×Fsw sampling.
// Zeros at 100k/200k/300k/400k/500k kHz → perfect ripple rejection.
// lpf_vOut_fc is retained for backward compatibility but UNUSED.
volatile float    lpf_vOut_fc   = 10000.0f; // Unused — MA filter is fixed length
volatile float    lpf_vOut      = 0.0f;     // MA-filtered output voltage (V)

// Per-phase outputs (after inverse transform + level shift)
volatile float    dPhA          = 0.0f;   // Phase A normalised duty (-1..1)
volatile float    dPhB          = 0.0f;   // Phase B normalised duty (-1..1)
volatile float    dPhC          = 0.0f;   // Phase C normalised duty (-1..1)
volatile uint16_t buckCMPA_A    = 0U;     // Phase A CMPA (counts)
volatile uint16_t buckCMPA_B    = 0U;     // Phase B CMPA (counts)
volatile uint16_t buckCMPA_C    = 0U;     // Phase C CMPA (counts)

volatile uint16_t buckCtrlEnable = 0U;    // 0 = manual (buckDuty_pct), 1 = PI control

// Internal tracker for bumpless 0→1 transition of buckCtrlEnable.
// Set to 1 inside the PI branch, reset to 0 in the manual-mode branch
// so every fresh enable triggers integrator seeding.
static uint16_t buckCtrlPrev = 0U;

//============================================================================
// Buffer for CCS Grapher — stores Phase A current in amps (float)
// Set Grapher DSP Data Type to "32-bit float" to display in amps directly.
#define BUFFER_SIZE 256
#pragma RETAIN(adcBufA)
volatile float    adcBufA[BUFFER_SIZE];
#pragma RETAIN(adcBufB)
volatile float    adcBufB[BUFFER_SIZE];
#pragma RETAIN(adcBufC)
volatile float    adcBufC[BUFFER_SIZE];
// #pragma RETAIN(adcBufA_lpf)
// volatile float    adcBufA_lpf[BUFFER_SIZE];
// #pragma RETAIN(adcBufB_lpf)
// volatile float    adcBufB_lpf[BUFFER_SIZE];
// #pragma RETAIN(adcBufC_lpf)
// volatile float    adcBufC_lpf[BUFFER_SIZE];
// #pragma RETAIN(adcBufVout)
volatile float    adcBufVout[BUFFER_SIZE];
volatile uint16_t bufIndex = 0;

//=============================================================================
// setupGPIOs()
// Configure all GPIO pins for PWM, LEDs, and status inputs
//=============================================================================
void setupGPIOs(void)
{
    //-------------------------------------------------------------------------
    // Phase A: ePWM1A (high-side) — GPIO0 — J4 pin 40
    //-------------------------------------------------------------------------
    GPIO_setMasterCore(BUCK_GPIO_PHA_H, GPIO_CORE_CPU1);
    GPIO_setPinConfig(GPIO_0_EPWM1A);
    GPIO_setPadConfig(BUCK_GPIO_PHA_H, GPIO_PIN_TYPE_STD);

    // Phase A: ePWM1B (low-side) — GPIO1 — J4 pin 39
    GPIO_setMasterCore(BUCK_GPIO_PHA_L, GPIO_CORE_CPU1);
    GPIO_setPinConfig(GPIO_1_EPWM1B);
    GPIO_setPadConfig(BUCK_GPIO_PHA_L, GPIO_PIN_TYPE_STD);

    //-------------------------------------------------------------------------
    // Phase B: ePWM2A (high-side) — GPIO2 — J4 pin 38
    //-------------------------------------------------------------------------
    GPIO_setMasterCore(BUCK_GPIO_PHB_H, GPIO_CORE_CPU1);
    GPIO_setPinConfig(GPIO_2_EPWM2A);
    GPIO_setPadConfig(BUCK_GPIO_PHB_H, GPIO_PIN_TYPE_STD);

    // Phase B: ePWM2B (low-side) — GPIO3 — J4 pin 37
    GPIO_setMasterCore(BUCK_GPIO_PHB_L, GPIO_CORE_CPU1);
    GPIO_setPinConfig(GPIO_3_EPWM2B);
    GPIO_setPadConfig(BUCK_GPIO_PHB_L, GPIO_PIN_TYPE_STD);

    //-------------------------------------------------------------------------
    // Phase C: ePWM3A (high-side) — GPIO4 — J4 pin 36
    //-------------------------------------------------------------------------
    GPIO_setMasterCore(BUCK_GPIO_PHC_H, GPIO_CORE_CPU1);
    GPIO_setPinConfig(GPIO_4_EPWM3A);
    GPIO_setPadConfig(BUCK_GPIO_PHC_H, GPIO_PIN_TYPE_STD);

    // Phase C: ePWM3B (low-side) — GPIO5 — J4 pin 35
    GPIO_setMasterCore(BUCK_GPIO_PHC_L, GPIO_CORE_CPU1);
    GPIO_setPinConfig(GPIO_5_EPWM3B);
    GPIO_setPadConfig(BUCK_GPIO_PHC_L, GPIO_PIN_TYPE_STD);

    //-------------------------------------------------------------------------
    // nEn_uC — Gate driver enable (active LOW) — GPIO124 — J2 pin 13
    // Initialise HIGH = gate driver DISABLED (safe default).
    // Write 0 (LOW) to enable gate driver outputs.
    //-------------------------------------------------------------------------
    GPIO_setMasterCore(BUCK_GPIO_NEN_UC, GPIO_CORE_CPU1);
    GPIO_setPinConfig(GPIO_124_GPIO124);
    GPIO_writePin(BUCK_GPIO_NEN_UC, 1);             // HIGH = disabled
    GPIO_setDirectionMode(BUCK_GPIO_NEN_UC, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(BUCK_GPIO_NEN_UC, GPIO_PIN_TYPE_PULLUP);

    //-------------------------------------------------------------------------
    // Over-temperature (OT, active low) input — GPIO24 — J4 pin 34
    // Active LOW from BOOSTXL (high = OK, low = fault)
    //-------------------------------------------------------------------------
    GPIO_setMasterCore(BUCK_GPIO_OT, GPIO_CORE_CPU1);
    GPIO_setPinConfig(GPIO_24_GPIO24);
    GPIO_setDirectionMode(BUCK_GPIO_OT, GPIO_DIR_MODE_IN);
    GPIO_setPadConfig(BUCK_GPIO_OT, GPIO_PIN_TYPE_PULLUP);

    //-------------------------------------------------------------------------
    // Blue LED — GPIO31 (on-board LaunchPad D10)
    //-------------------------------------------------------------------------
    GPIO_setMasterCore(BUCK_GPIO_LED_BLUE, GPIO_CORE_CPU1);
    GPIO_setPinConfig(GPIO_31_GPIO31);
    GPIO_writePin(BUCK_GPIO_LED_BLUE, 1);   // LED off (active low)
    GPIO_setDirectionMode(BUCK_GPIO_LED_BLUE, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(BUCK_GPIO_LED_BLUE, GPIO_PIN_TYPE_STD);

    //-------------------------------------------------------------------------
    // Red LED — GPIO34 (on-board LaunchPad D9)
    //-------------------------------------------------------------------------
    GPIO_setMasterCore(BUCK_GPIO_LED_RED, GPIO_CORE_CPU1);
    GPIO_setPinConfig(GPIO_34_GPIO34);
    GPIO_writePin(BUCK_GPIO_LED_RED, 1);    // LED off (active low)
    GPIO_setDirectionMode(BUCK_GPIO_LED_RED, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(BUCK_GPIO_LED_RED, GPIO_PIN_TYPE_STD);

    //-------------------------------------------------------------------------
    // ADC debug toggle — GPIO58 (LaunchPad J2 pin 15)
    // Pulses HIGH for the duration of ADC conversion (~335 ns).
    // Used to verify ADC trigger timing against PWM carriers on a scope.
    //-------------------------------------------------------------------------
    GPIO_setMasterCore(BUCK_GPIO_ADC_DBG, GPIO_CORE_CPU1);
    GPIO_setPinConfig(GPIO_58_GPIO58);
    GPIO_writePin(BUCK_GPIO_ADC_DBG, 0);    // start LOW
    GPIO_setDirectionMode(BUCK_GPIO_ADC_DBG, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(BUCK_GPIO_ADC_DBG, GPIO_PIN_TYPE_STD);

    //-------------------------------------------------------------------------
    // ePWM4A debug output — GPIO6 (LaunchPad J8 pin 80)
    // Muxed as ePWM4A hardware output — 50% duty, up-down counter at 3×Fsw.
    // Edges are exactly at SOCA moments (carrier peaks/valleys).
    // Use on scope to verify ADC sample timing against ePWM1/2/3 carriers.
    //-------------------------------------------------------------------------
    GPIO_setMasterCore(BUCK_GPIO_EPWM4_DBG, GPIO_CORE_CPU1);
    GPIO_setPinConfig(GPIO_6_EPWM4A);
    GPIO_setPadConfig(BUCK_GPIO_EPWM4_DBG, GPIO_PIN_TYPE_STD);
}

//=============================================================================
// setupPWMs()
// Configure ePWM1/2/3 as 120-degree phase-shifted synchronous buck PWMs
// and ePWM7 as the RTI / ADC trigger at 6x switching frequency
//=============================================================================
void setupPWMs(void)
{
    uint16_t phaseB_shift;
    uint16_t phaseC_shift;
    uint16_t rtiTBPRD;

    //
    // Convert SI-unit inputs to register values at startup.
    // All three computed globals (buckTBPRD, buckCMPA, buckDBcounts) are
    // written here so the ISR can use them immediately on first call.
    //
    buckTBPRD    = BUCK_FSW_TO_TBPRD(buckFreq_Hz);
    buckCMPA     = BUCK_PCT_TO_CMPA(buckTBPRD, buckDuty_pct);
    buckDBcounts = BUCK_NS_TO_DBCOUNTS(buckDeadtime_ns);

    //
    // Derive phase-shift and RTI period from buckTBPRD
    //
    // Up-down counter: full triangle period = 2*TBPRD EPWMCLK cycles.
    // TBPHS must be <= TBPRD (counter range 0..TBPRD).
    //
    // Phase B (120 deg): TBPHS = (2*TBPRD)/3. Count UP after sync.
    // Phase C (240 deg): (4*TBPRD)/3 > TBPRD — use TBPHS=(2*TBPRD)/3 and
    //   count DOWN after sync (equivalent to 240-deg point on the triangle).
    phaseB_shift = (2U * buckTBPRD) / 3U;  // 120 deg, count UP after sync
    phaseC_shift = (2U * buckTBPRD) / 3U;  // 240 deg equivalent, count DOWN after sync

    // RTI ePWM4: up-down count, period = buckTBPRD/6
    // One up-down cycle = 2*rtiTBPRD counts → fires 6× per switching period
    // CTR=ZERO = carrier peak, CTR=PRD = carrier valley
    rtiTBPRD = buckTBPRD / 6U;

    //
    // Stop all ePWM time-base clocks during configuration
    //
    SysCtl_disablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);

    //=========================================================================
    // ePWM1, ePWM2, ePWM3 — identical base configuration (3-phase buck)
    //=========================================================================
    uint32_t pwmBases[3] = {BUCK_PHASE_A_BASE, BUCK_PHASE_B_BASE, BUCK_PHASE_C_BASE};
    uint8_t i;

    for(i = 0; i < 3U; i++)
    {
        uint32_t base = pwmBases[i];

        //---------------------------------------------------------------------
        // Time Base Sub-Module
        //---------------------------------------------------------------------
        // Up-down (centre-aligned) counting mode
        EPWM_setTimeBaseCounterMode(base, EPWM_COUNTER_MODE_UP_DOWN);

        // No clock prescaler — EPWMCLK = SYSCLK/2 = 100 MHz
        EPWM_setClockPrescaler(base,
                               EPWM_CLOCK_DIVIDER_1,
                               EPWM_HSCLOCK_DIVIDER_1);

        // Load period register immediately (direct load)
        EPWM_setPeriodLoadMode(base, EPWM_PERIOD_DIRECT_LOAD);
        EPWM_setTimeBasePeriod(base, buckTBPRD);

        // Start counter at 0
        EPWM_setTimeBaseCounter(base, 0U);

        // Allow phase shift loading on sync
        // Count direction after sync is set per-phase below (after the loop)
        EPWM_enablePhaseShiftLoad(base);
        EPWM_setCountModeAfterSync(base, EPWM_COUNT_MODE_UP_AFTER_SYNC); // default; overridden for PhC

        //---------------------------------------------------------------------
        // Counter Compare Sub-Module
        //---------------------------------------------------------------------
        // Initial duty cycle (computed from buckDuty_pct via BUCK_PCT_TO_CMPA)
        EPWM_setCounterCompareValue(base, EPWM_COUNTER_COMPARE_A, buckCMPA);

        // Load CMPA shadow on CTR = zero (start of each cycle)
        EPWM_setCounterCompareShadowLoadMode(base,
                                             EPWM_COUNTER_COMPARE_A,
                                             EPWM_COMP_LOAD_ON_CNTR_ZERO);

        //---------------------------------------------------------------------
        // Action Qualifier Sub-Module
        // ePWMxA: HIGH when counting UP past CMPA, LOW when counting DOWN past CMPA
        // This produces a symmetrical centre-aligned PWM on the A output.
        // The B output is generated by the deadband module (complementary).
        //---------------------------------------------------------------------
        EPWM_setActionQualifierActionComplete(
            base,
            EPWM_AQ_OUTPUT_A,
            (EPWM_ActionQualifierEventAction)(
                EPWM_AQ_OUTPUT_LOW_UP_CMPA |
                EPWM_AQ_OUTPUT_HIGH_DOWN_CMPA));

        //---------------------------------------------------------------------
        // Deadband Sub-Module — Active-high complementary
        // EPWMxA = high-side gate (rising edge delayed)
        // EPWMxB = low-side gate  (falling edge delayed, inverted)
        //---------------------------------------------------------------------
        EPWM_setRisingEdgeDeadBandDelayInput(base, EPWM_DB_INPUT_EPWMA);
        EPWM_setFallingEdgeDeadBandDelayInput(base, EPWM_DB_INPUT_EPWMA);

        EPWM_setDeadBandDelayMode(base, EPWM_DB_RED, true);
        EPWM_setDeadBandDelayMode(base, EPWM_DB_FED, true);

        // EPWMxA polarity: active high (no invert)
        EPWM_setDeadBandDelayPolarity(base, EPWM_DB_RED,
                                      EPWM_DB_POLARITY_ACTIVE_HIGH);
        // EPWMxB polarity: inverted (active high complementary)
        EPWM_setDeadBandDelayPolarity(base, EPWM_DB_FED,
                                      EPWM_DB_POLARITY_ACTIVE_LOW);

        // Dead time from buckDeadtime_ns (converted via BUCK_NS_TO_DBCOUNTS)
        EPWM_setRisingEdgeDelayCount(base, buckDBcounts);
        EPWM_setFallingEdgeDelayCount(base, buckDBcounts);

        //---------------------------------------------------------------------
        // Sync output: pass through (will be overridden for master/slaves below)
        //---------------------------------------------------------------------
        EPWM_setSyncOutPulseMode(base, EPWM_SYNC_OUT_PULSE_ON_EPWMxSYNCIN);

        //---------------------------------------------------------------------
        // ADC triggers — per-phase synchronous current sampling
        // SOCA fires at CTR=ZERO (carrier peak), SOCB at CTR=PRD (valley)
        //---------------------------------------------------------------------
        EPWM_setADCTriggerSource(base, EPWM_SOC_A, EPWM_SOC_TBCTR_ZERO);
        EPWM_setADCTriggerEventPrescale(base, EPWM_SOC_A, 1U);
        EPWM_enableADCTrigger(base, EPWM_SOC_A);

        EPWM_setADCTriggerSource(base, EPWM_SOC_B, EPWM_SOC_TBCTR_PERIOD);
        EPWM_setADCTriggerEventPrescale(base, EPWM_SOC_B, 1U);
        EPWM_enableADCTrigger(base, EPWM_SOC_B);

        EPWM_clearADCTriggerFlag(base, EPWM_SOC_A);
        EPWM_clearADCTriggerFlag(base, EPWM_SOC_B);
    }

    //=========================================================================
    // ePWM1 — Phase A MASTER  (0 deg)
    // - Sync source: emits sync-out at CTR = 0
    // - No phase shift load needed (it IS the reference)
    //=========================================================================
    EPWM_disablePhaseShiftLoad(BUCK_PHASE_A_BASE);
    EPWM_setPhaseShift(BUCK_PHASE_A_BASE, 0U);
    EPWM_setCountModeAfterSync(BUCK_PHASE_A_BASE, EPWM_COUNT_MODE_UP_AFTER_SYNC);
    EPWM_setSyncOutPulseMode(BUCK_PHASE_A_BASE,
                             EPWM_SYNC_OUT_PULSE_ON_COUNTER_ZERO);

    //=========================================================================
    // ePWM2 — Phase B SLAVE  (120 deg)
    // SYNCIN  = ePWM1 SYNCOUT
    // TBPHS   = (2*TBPRD)/3, count UP after sync
    // SYNCOUT = pass-through → feeds ePWM3 SYNCIN
    //=========================================================================
    EPWM_setPhaseShift(BUCK_PHASE_B_BASE, phaseB_shift);
    EPWM_setCountModeAfterSync(BUCK_PHASE_B_BASE, EPWM_COUNT_MODE_UP_AFTER_SYNC);
    EPWM_setSyncOutPulseMode(BUCK_PHASE_B_BASE,
                             EPWM_SYNC_OUT_PULSE_ON_EPWMxSYNCIN);

    //=========================================================================
    // ePWM3 — Phase C SLAVE  (240 deg)
    // SYNCIN  = ePWM2 SYNCOUT (= ePWM1 sync propagated through ePWM2)
    //
    // 240-deg trick for up-down counters:
    //   (4*TBPRD)/3 > TBPRD, so it cannot be stored in TBPHS directly.
    //   Instead, load TBPHS = (2*TBPRD)/3 and count DOWN after sync.
    //   The counter then starts at (2*TBPRD)/3 counting down, which is
    //   equivalent to being at the 240-deg point of the triangle carrier
    //   — exactly 120 deg behind ePWM2 (Phase B).
    //=========================================================================
    EPWM_setPhaseShift(BUCK_PHASE_C_BASE, phaseC_shift);
    EPWM_setCountModeAfterSync(BUCK_PHASE_C_BASE, EPWM_COUNT_MODE_DOWN_AFTER_SYNC);
    EPWM_setSyncOutPulseMode(BUCK_PHASE_C_BASE,
                             EPWM_SYNC_OUT_PULSE_ON_EPWMxSYNCIN);

    //=========================================================================
    // ePWM5, ePWM6 — sync chain pass-throughs (ePWM4 is now RTI, see below)
    //
    // On F28379D the ePWM sync bus is hardwired in order:
    //   ePWM1 → ePWM2 → ePWM3 → ePWM4 → ePWM5 → ePWM6 → ePWM7
    // ePWM4 is now the RTI module so it receives the sync directly from
    // ePWM3.  ePWM5/6 are configured as pass-throughs only to keep the
    // chain intact (nothing downstream of ePWM6 is used).
    //=========================================================================
    uint32_t passBases[2] = {EPWM5_BASE, EPWM6_BASE};
    uint8_t j;
    for(j = 0; j < 2U; j++)
    {
        EPWM_setSyncOutPulseMode(passBases[j],
                                 EPWM_SYNC_OUT_PULSE_ON_EPWMxSYNCIN);
    }

    //=========================================================================
    // ePWM4 — RTI at 6× switching frequency, up-down count
    //
    // Up-down count with TBPRD = buckTBPRD/6:
    //   One full cycle = 2 * rtiTBPRD counts = buckTBPRD/3 counts
    //   → 3 full cycles per carrier period = 6 events (3 peaks + 3 valleys)
    //
    // CTR=ZERO aligns with carrier PEAKS,  CTR=PRD aligns with carrier VALLEYS.
    // SOCA fires at CTR=ZERO (peak), SOCB fires at CTR=PRD (valley).
    //
    // Sync locking:
    //   ePWM4 receives ePWM1 CTR=0 sync through the chain.
    //   On sync, TBCTR loads TBPHS = 0 and counts UP — so the first
    //   CTR=ZERO coincides with Phase A's peak (ePWM1 CTR=0).
    //=========================================================================
    // Up-down count mode
    EPWM_setTimeBaseCounterMode(BUCK_RTI_BASE, EPWM_COUNTER_MODE_UP_DOWN);

    // No prescaler
    EPWM_setClockPrescaler(BUCK_RTI_BASE,
                           EPWM_CLOCK_DIVIDER_1,
                           EPWM_HSCLOCK_DIVIDER_1);

    // Period = buckTBPRD / 6 for 6× switching frequency (up-down)
    EPWM_setPeriodLoadMode(BUCK_RTI_BASE, EPWM_PERIOD_DIRECT_LOAD);
    EPWM_setTimeBasePeriod(BUCK_RTI_BASE, rtiTBPRD);

    // Counter starts at 0
    EPWM_setTimeBaseCounter(BUCK_RTI_BASE, 0U);

    // Phase shift: TBPHS = 0, count UP after sync.
    // Sync from ePWM1 CTR=0 loads TBCTR=0 → CTR=ZERO fires immediately
    // at Phase A peak, then every rtiTBPRD counts: peak, valley, peak, ...
    EPWM_enablePhaseShiftLoad(BUCK_RTI_BASE);
    EPWM_setPhaseShift(BUCK_RTI_BASE, 0U);
    EPWM_setCountModeAfterSync(BUCK_RTI_BASE, EPWM_COUNT_MODE_UP_AFTER_SYNC);

    // ADC SOCA at CTR=ZERO (carrier VALLEY) — always enabled
    EPWM_setADCTriggerSource(BUCK_RTI_BASE,
                             EPWM_SOC_A,
                             EPWM_SOC_TBCTR_ZERO);
    EPWM_setADCTriggerEventPrescale(BUCK_RTI_BASE, EPWM_SOC_A, 1U);
    EPWM_enableADCTrigger(BUCK_RTI_BASE, EPWM_SOC_A);

    // ADC SOCB at CTR=PRD (carrier PEAK) — always enabled.
    // Both triggers fire; BUCK_VDC_ADC_TRIGGER and BUCK_VOUT_ADC_TRIGGER
    // in the header pick which one drives each SOC conversion.
    EPWM_setADCTriggerSource(BUCK_RTI_BASE,
                             EPWM_SOC_B,
                             EPWM_SOC_TBCTR_PERIOD);
    EPWM_setADCTriggerEventPrescale(BUCK_RTI_BASE, EPWM_SOC_B, 1U);
    EPWM_enableADCTrigger(BUCK_RTI_BASE, EPWM_SOC_B);

    // Action Qualifier — ePWM4A: debug output
    // HIGH at CTR=ZERO (peak), LOW at CTR=PRD (valley)
    EPWM_setActionQualifierAction(BUCK_RTI_BASE,
                                  EPWM_AQ_OUTPUT_A,
                                  EPWM_AQ_OUTPUT_HIGH,
                                  EPWM_AQ_OUTPUT_ON_TIMEBASE_ZERO);
    EPWM_setActionQualifierAction(BUCK_RTI_BASE,
                                  EPWM_AQ_OUTPUT_A,
                                  EPWM_AQ_OUTPUT_LOW,
                                  EPWM_AQ_OUTPUT_ON_TIMEBASE_PERIOD);

    // Interrupt at CTR=ZERO and CTR=PRD (both peak and valley).
    // 3 peaks + 3 valleys per switching cycle = 6× Fsw ISR rate.
    EPWM_setInterruptSource(BUCK_RTI_BASE, EPWM_INT_TBCTR_ZERO_OR_PERIOD);
    EPWM_setInterruptEventCount(BUCK_RTI_BASE, 1U);
    EPWM_enableInterrupt(BUCK_RTI_BASE);

    // Clear stale flags from configuration before going live
    EPWM_clearEventTriggerInterruptFlag(BUCK_RTI_BASE);
    EPWM_clearADCTriggerFlag(BUCK_RTI_BASE, EPWM_SOC_A);
    EPWM_clearADCTriggerFlag(BUCK_RTI_BASE, EPWM_SOC_B);

    //=========================================================================
    // Re-enable time-base clocks — all ePWMs start simultaneously
    //=========================================================================
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);
}

//=============================================================================
// setupADCs()
// Configure ADCA/B/C (current sensors) and ADCD (Vdc)
// All SOCs triggered by ePWM4 SOCA
//=============================================================================
void setupADCs(void)
{
    uint32_t adcBases[4] = {ADCA_BASE, ADCB_BASE, ADCC_BASE, ADCD_BASE};
    uint8_t i;

    //
    // Common ADC module setup
    //
    for(i = 0; i < 4U; i++)
    {
        // 12-bit resolution, single-ended
        ADC_setMode(adcBases[i],
                    ADC_RESOLUTION_12BIT,
                    ADC_MODE_SINGLE_ENDED);

        // ADC clock = SYSCLK / 4 = 50 MHz (max for F2837xD ADC)
        ADC_setPrescaler(adcBases[i], ADC_CLK_DIV_4_0);

        // Flag at end of conversion — result is in the register when ADCINTFLG
        // sets.  The flag latches in ADCINTFLG and holds until explicitly
        // cleared; continuous mode re-arms it automatically each cycle so the
        // spin-wait in the ISR always finds it set after the conversion done.
        ADC_setInterruptPulseMode(adcBases[i], ADC_PULSE_END_OF_CONV);

        // Power up the ADC
        ADC_enableConverter(adcBases[i]);

        // Set all SOCs to high priority
        ADC_setSOCPriority(adcBases[i], ADC_PRI_ALL_HIPRI);
    }

    // Allow 1.5 ms for ADC power-up
    DEVICE_DELAY_US(1500U);

    //-------------------------------------------------------------------------
    // Phase A current — ADCC, channel IN2 (J3 pin 27)
    // Triggered by ePWM1 (per-phase synchronous sampling)
    //-------------------------------------------------------------------------
    ADC_setupSOC(BUCK_IA_ADC_BASE,
                 BUCK_IA_ADC_SOC,
                 BUCK_IA_ADC_TRIGGER,
                 BUCK_IA_ADC_CH,
                 BUCK_ADC_SAMPLE_WINDOW);

    ADC_setupPPB(BUCK_IA_ADC_BASE, BUCK_IA_ADC_PPB, BUCK_IA_ADC_SOC);
    // OFFCAL = 0: F2837xD OFFCAL is 10-bit (-512..+511), cannot hold -2048.
    // Zero-offset subtraction is handled in software via buckI_ZeroOffset.
    ADC_setPPBCalibrationOffset(BUCK_IA_ADC_BASE, BUCK_IA_ADC_PPB, 0);

    //-------------------------------------------------------------------------
    // Phase B current — ADCB, channel IN2 (J3 pin 28)
    // Triggered by ePWM2 (per-phase synchronous sampling)
    //-------------------------------------------------------------------------
    ADC_setupSOC(BUCK_IB_ADC_BASE,
                 BUCK_IB_ADC_SOC,
                 BUCK_IB_ADC_TRIGGER,
                 BUCK_IB_ADC_CH,
                 BUCK_ADC_SAMPLE_WINDOW);

    ADC_setupPPB(BUCK_IB_ADC_BASE, BUCK_IB_ADC_PPB, BUCK_IB_ADC_SOC);
    ADC_setPPBCalibrationOffset(BUCK_IB_ADC_BASE, BUCK_IB_ADC_PPB, 0);

    //-------------------------------------------------------------------------
    // Phase C current — ADCA, channel IN2 (J3 pin 29)
    // Triggered by ePWM3 (per-phase synchronous sampling)
    //-------------------------------------------------------------------------
    ADC_setupSOC(BUCK_IC_ADC_BASE,
                 BUCK_IC_ADC_SOC,
                 BUCK_IC_ADC_TRIGGER,
                 BUCK_IC_ADC_CH,
                 BUCK_ADC_SAMPLE_WINDOW);

    ADC_setupPPB(BUCK_IC_ADC_BASE, BUCK_IC_ADC_PPB, BUCK_IC_ADC_SOC);
    ADC_setPPBCalibrationOffset(BUCK_IC_ADC_BASE, BUCK_IC_ADC_PPB, 0);

    //-------------------------------------------------------------------------
    // Output voltage OUTP — ADCA, channel IN4 (J7 pin 69)
    // Triggered by ePWM4 (6× Fsw) — relies on LPF for ripple rejection
    //-------------------------------------------------------------------------
    ADC_setupSOC(BUCK_VOUTP_ADC_BASE,
                 BUCK_VOUTP_ADC_SOC,
                 BUCK_VOUT_ADC_TRIGGER,
                 BUCK_VOUTP_ADC_CH,
                 BUCK_ADC_SAMPLE_WINDOW);

    //-------------------------------------------------------------------------
    // Output voltage OUTN — ADCA, channel IN5 (J7 pin 66)
    //-------------------------------------------------------------------------
    ADC_setupSOC(BUCK_VOUTN_ADC_BASE,
                 BUCK_VOUTN_ADC_SOC,
                 BUCK_VOUT_ADC_TRIGGER,
                 BUCK_VOUTN_ADC_CH,
                 BUCK_ADC_SAMPLE_WINDOW);

#if (BUCK_ADC_SAMPLE_MODE == 2)
    //-------------------------------------------------------------------------
    // Mode 2: second-sample SOCs triggered by SOCB (valley)
    //-------------------------------------------------------------------------
    // Phase A current (ADCC) — valley from ePWM1 SOCB
    ADC_setupSOC(BUCK_IA_ADC_BASE, BUCK_IA_ADC_SOC_B,
                 BUCK_IA_ADC_TRIGGER_B, BUCK_IA_ADC_CH, BUCK_ADC_SAMPLE_WINDOW);
    // Phase B current (ADCB) — valley from ePWM2 SOCB
    ADC_setupSOC(BUCK_IB_ADC_BASE, BUCK_IB_ADC_SOC_B,
                 BUCK_IB_ADC_TRIGGER_B, BUCK_IB_ADC_CH, BUCK_ADC_SAMPLE_WINDOW);
    // Phase C current (ADCA) — valley from ePWM3 SOCB
    ADC_setupSOC(BUCK_IC_ADC_BASE, BUCK_IC_ADC_SOC_B,
                 BUCK_IC_ADC_TRIGGER_B, BUCK_IC_ADC_CH, BUCK_ADC_SAMPLE_WINDOW);
    // DC bus voltage (ADCD) — valley from ePWM4 SOCB
    ADC_setupSOC(BUCK_VDC_ADC_BASE, BUCK_VDC_ADC_SOC_B,
                 BUCK_VDC_ADC_TRIGGER_B, BUCK_VDC_ADC_CH, BUCK_ADC_SAMPLE_WINDOW);
    // Output voltage OUTP (ADCA) — valley from ePWM4 SOCB
    ADC_setupSOC(BUCK_VOUTP_ADC_BASE, BUCK_VOUTP_ADC_SOC_B,
                 BUCK_VOUT_ADC_TRIGGER_B, BUCK_VOUTP_ADC_CH, BUCK_ADC_SAMPLE_WINDOW);
    // Output voltage OUTN (ADCA) — valley from ePWM4 SOCB
    ADC_setupSOC(BUCK_VOUTN_ADC_BASE, BUCK_VOUTN_ADC_SOC_B,
                 BUCK_VOUT_ADC_TRIGGER_B, BUCK_VOUTN_ADC_CH, BUCK_ADC_SAMPLE_WINDOW);
#endif

    //-------------------------------------------------------------------------
    // DC bus voltage — ADCD, channel IN14 (J3 pin 23)
    // Triggered by ePWM4 (6× Fsw)
    //-------------------------------------------------------------------------
    ADC_setupSOC(BUCK_VDC_ADC_BASE,
                 BUCK_VDC_ADC_SOC,
                 BUCK_VDC_ADC_TRIGGER,
                 BUCK_VDC_ADC_CH,
                 BUCK_ADC_SAMPLE_WINDOW);

    ADC_setupPPB(BUCK_VDC_ADC_BASE, BUCK_VDC_ADC_PPB, BUCK_VDC_ADC_SOC);
    ADC_setPPBCalibrationOffset(BUCK_VDC_ADC_BASE, BUCK_VDC_ADC_PPB, 0);

    //-------------------------------------------------------------------------
    // ADCA ADCINT1 — end-of-conversion flag for SOC0
    //
    // ePWM4 SOCA and ePWM4 INT fire at the SAME instant (CTR=PRD).
    // The ISR begins executing before the ADC conversion completes
    // (~335 ns at 50 MHz ADCCLK, 12-bit, 14-cycle window).
    //
    // Solution: configure ADCINT1 on ADCA to flag when SOC0 conversion
    // is done.  The ISR polls this flag before reading any ADC result,
    // guaranteeing all 4 modules have finished (they all start and end
    // at the same time).
    //
    // Continuous mode is OFF so the flag only sets when a fresh
    // SOCA-triggered conversion completes — never exits on a stale flag.
    //-------------------------------------------------------------------------
    ADC_setInterruptSource(ADCA_BASE, ADC_INT_NUMBER1, ADC_SOC_NUMBER0);
    // Continuous mode OFF: ADCINT1 only flags when a fresh SOCA-triggered
    // conversion completes.  The ISR spin-wait therefore always waits for
    // the actual current conversion — never exits on a stale previous flag.
    ADC_disableContinuousMode(ADCA_BASE, ADC_INT_NUMBER1);
    ADC_enableInterrupt(ADCA_BASE, ADC_INT_NUMBER1);
    ADC_clearInterruptOverflowStatus(ADCA_BASE, ADC_INT_NUMBER1);
    ADC_clearInterruptStatus(ADCA_BASE, ADC_INT_NUMBER1);
}

//=============================================================================
// setupInterrupts()
// Register and enable the ePWM7 RTI interrupt
//=============================================================================
void setupInterrupts(void)
{
    // Initialize PIE and clear flags
    Interrupt_initModule();
    Interrupt_initVectorTable();

    // Register rtiISR as the ePWM4 interrupt handler (PIE group 3, ch 4)
    Interrupt_register(BUCK_RTI_INT, &rtiISR);

    // Enable the ePWM4 interrupt in PIE group 3
    Interrupt_enable(BUCK_RTI_INT);

    // Enable CPU INT3 (PIE group 3 — all ePWM interrupts)
    Interrupt_enableInCPU(INTERRUPT_CPU_INT3);

    // Enable global interrupts and real-time debug
    EINT;
    ERTM;

}

//=============================================================================
// rtiISR()
// Real-Time Interrupt — fires at 6x switching frequency (300 kHz @ 50 kHz Fsw)
// Triggered by ePWM4 INT at each peak/valley of the interleaved carriers
//
// Responsibilities:
//   1. Read ADC results (all conversions complete before ISR fires)
//   2. Convert SI inputs → register counts; update hardware on change
//   3. Toggle LED heartbeat
//   4. Clear interrupt flags and ACK PIE
//=============================================================================
__interrupt void rtiISR(void)
{
    //-------------------------------------------------------------------------
    // 1. Wait for ADC conversion to complete, then read results
    //
    // ePWM4 fires SOCA (ADC trigger) and INT (this ISR) at the same instant
    // (CTR = PRD).  The ADC conversion takes ~335 ns to complete (14-cycle
    // acquisition + 13-cycle SAR at 50 MHz ADCCLK).  The ISR enters
    // faster than that (~1 us dispatch), so we must spin-wait.
    //
    // ADCA ADCINT1 is configured to flag when ADCA SOC0 finishes.  Since
    // all 4 ADC modules are triggered simultaneously and use the same sample
    // window, when ADCA is done they are all done.
    //-------------------------------------------------------------------------
    // Debug: GPIO58 HIGH = SOCA fired, ADC conversion in progress
    //GPIO_writePin(BUCK_GPIO_ADC_DBG, 1U);

    while(ADC_getInterruptStatus(ADCA_BASE, ADC_INT_NUMBER1) == false)
    {
        // spin — waits for the current conversion to complete (~335 ns)
    }

    // Debug: GPIO58 LOW = conversion done, result is valid in result registers
    // Must go LOW before clearing the flag so the falling edge marks true EOC.
    // Continuous mode is OFF so the flag only sets when a fresh SOCA-triggered
    // conversion completes — not re-armed from the previous cycle.
    //GPIO_writePin(BUCK_GPIO_ADC_DBG, 0U);

    ADC_clearInterruptOverflowStatus(ADCA_BASE, ADC_INT_NUMBER1);
    ADC_clearInterruptStatus(ADCA_BASE, ADC_INT_NUMBER1);

    adcIA  = (int16_t)READ_IA_PPB();
    adcIB  = (int16_t)READ_IB_PPB();
    adcIC  = (int16_t)READ_IC_PPB();
    adcVDC   = READ_VDC();
    adcVoutP = READ_VOUTP();
    adcVoutN = READ_VOUTN();
#if (BUCK_ADC_SAMPLE_MODE == 2)
    // Average peak and valley samples for all channels
    // Current SOC_B has no PPB — read raw result (PPB offset is 0, so values match)
    adcIA    = (adcIA  + (int16_t)ADC_readResult(BUCK_IA_RESULT_BASE,  BUCK_IA_ADC_SOC_B))  >> 1;
    adcIB    = (adcIB  + (int16_t)ADC_readResult(BUCK_IB_RESULT_BASE,  BUCK_IB_ADC_SOC_B))  >> 1;
    adcIC    = (adcIC  + (int16_t)ADC_readResult(BUCK_IC_RESULT_BASE,  BUCK_IC_ADC_SOC_B))  >> 1;
    adcVDC   = (adcVDC + ADC_readResult(BUCK_VDC_RESULT_BASE, BUCK_VDC_ADC_SOC_B)) >> 1;
    adcVoutP = (adcVoutP + ADC_readResult(BUCK_VOUTP_RESULT_BASE, BUCK_VOUTP_ADC_SOC_B)) >> 1;
    adcVoutN = (adcVoutN + ADC_readResult(BUCK_VOUTN_RESULT_BASE, BUCK_VOUTN_ADC_SOC_B)) >> 1;
#endif


    // Convert raw counts to physical units.
    // Subtract buckI_ZeroOffset to centre around 0 A (hardware OFFCAL cannot
    // hold -2048 — F2837xD field is only 10-bit, range -512..+511).
    // Set buckI_ZeroOffset to the observed no-load adcIA reading to trim bias.
    iA_A  = -1*((float)adcIA  - buckIA_ZeroOffset) * BUCK_I_SCALE_A_PER_COUNT;
    iB_A  = -1*((float)adcIB  - buckIB_ZeroOffset) * BUCK_I_SCALE_A_PER_COUNT;
    iC_A  = -1*((float)adcIC  - buckIC_ZeroOffset) * BUCK_I_SCALE_A_PER_COUNT;

    // Heavily-filtered averages of converted phase currents (Amps) for
    // inspection, zero-offset verification, and DC diagnostics.
    {
        float iavg_Ts    = 1.0f / (6.0f * buckFreq_Hz);
        float iavg_alpha = 6.2832f * lpf_iAvg_fc * iavg_Ts /
                           (1.0f + 6.2832f * lpf_iAvg_fc * iavg_Ts);
        iA_avg   += iavg_alpha * (iA_A   - iA_avg);
        iB_avg   += iavg_alpha * (iB_A   - iB_avg);
        iC_avg   += iavg_alpha * (iC_A   - iC_avg);
        vOut_avg += iavg_alpha * (vOut_V - vOut_avg);
    }

    // adcVDC is raw (0..4095); scale by V/count = VREF*(Rtop+Rbot)/(ADC_FS*Rbot)
    vDC_V = (float)adcVDC * BUCK_VDC_SCALE_V_PER_COUNT;

    // Output voltage: software differential (OUTP - OUTN), then invert sensor gain
    {
        float vOutP_V   = (float)adcVoutP * BUCK_VOUT_V_PER_COUNT;
        float vOutN_V   = (float)adcVoutN * BUCK_VOUT_V_PER_COUNT;
        vOut_V          = (vOutP_V - vOutN_V) / BUCK_VOUT_SENSOR_GAIN;

        // Moving average filter on vOut — N=6 samples at 6×Fsw = 600 kHz
        // Places exact zeros at 100k, 200k, 300k (interleaved ripple),
        // 400k, 500k — perfect rejection of all switching-synchronous noise.
        // Linear phase: group delay = (N-1)/2 = 2.5 samples = 4.17 μs.
        static float    vOut_MA_buf[6] = {0.0f};
        static uint16_t vOut_MA_idx    = 0U;
        static float    vOut_MA_sum    = 0.0f;

        vOut_MA_sum            -= vOut_MA_buf[vOut_MA_idx];
        vOut_MA_buf[vOut_MA_idx] = vOut_V;
        vOut_MA_sum            += vOut_V;
        vOut_MA_idx             = (vOut_MA_idx + 1U) % 6U;
        lpf_vOut                = vOut_MA_sum * (1.0f / 6.0f);
    }

    // -------------------------------------------------------------------------
    // First-order IIR low-pass filter on phase currents (measurement pre-filter)
    // α = ωc·Ts / (1 + ωc·Ts),  ωc = 2π·lpf_fc
    // iA_A/iB_A/iC_A remain the raw (unfiltered) values for monitoring/logging.
    // lpf_iA/B/C are the filtered currents fed into the PI controllers.
    // -------------------------------------------------------------------------
    {
        float lpf_Ts    = 1.0f / (6.0f * buckFreq_Hz);
        float lpf_alpha = 6.2832f * lpf_fc * lpf_Ts /
                          (1.0f + 6.2832f * lpf_fc * lpf_Ts);
        lpf_iA += lpf_alpha * (iA_A - lpf_iA);
        lpf_iB += lpf_alpha * (iB_A - lpf_iB);
        lpf_iC += lpf_alpha * (iC_A - lpf_iC);
    }

    // Store Phase A current (amps) in grapher buffer
    // Grapher DSP Data Type must be set to "32-bit float"
    // adcBufA[bufIndex]    = lpf_iA;
    // adcBufB[bufIndex]    = lpf_iB;
    // adcBufC[bufIndex]    = lpf_iC;
    adcBufA[bufIndex]    = iA_A;
    adcBufB[bufIndex]    = iB_A;
    adcBufC[bufIndex]    = iC_A;
    adcBufVout[bufIndex] = lpf_vOut;
    bufIndex++;
    if (bufIndex >= BUFFER_SIZE) bufIndex = 0;  // circular buffer
    
    //-------------------------------------------------------------------------
    // 2. Convert SI-unit inputs to register values
    //    buckFreq_Hz, buckDuty_pct, buckDeadtime_ns are set by the user.
    //    Computed values (buckTBPRD, buckCMPA, buckDBcounts) are read-only
    //    outputs that the user can inspect in CCS.
    //-------------------------------------------------------------------------
    uint16_t newTBPRD    = BUCK_FSW_TO_TBPRD(buckFreq_Hz);
    uint16_t newCMPA     = BUCK_PCT_TO_CMPA(newTBPRD, buckDuty_pct);
    uint16_t newDBcounts = BUCK_NS_TO_DBCOUNTS(buckDeadtime_ns);

    //--- 2a. Duty cycle: manual or PI-controlled ---
    if(buckCtrlEnable)
    {
        // Ts = RTI ISR period = 1/(6*Fsw). At 100 kHz: Ts = 1.667 us
        float Ts     = 1.0f / (6.0f * buckFreq_Hz);
        float Ts_h   = Ts * 0.5f;   // Ts/2 — Tustin trapezoidal step

        //=====================================================================
        // OUTER VOLTAGE LOOP — generates iSigma_ref for the inner loop
        // Cascaded PI: vOut_ref → piV → iSigma_ref → piI → dSigma → PWM
        // Only active when buckVctrlEnable=1; otherwise iSigma_ref is manual.
        //=====================================================================
        // Detect open→closed transition for bumpless transfer
        static uint16_t buckVctrlPrev = 0U;

        if(buckVctrlEnable)
        {
            // On 0→1 transition: seed integrator from current operating point
            // so the PI output matches the existing iSigma_ref instantly.
            // iSigma_ref = Kp*e + Ki*integ  →  integ = (iSigma_ref - Kp*e) / Ki
            if(!buckVctrlPrev)
            {
                float e_v0    = vOut_ref - lpf_vOut;
                piV_integ     = (iSigma_ref - piV_Kp * e_v0) / piV_Ki;
                piV_e_prev    = e_v0;
            }

            // Voltage PI — Tustin integration
            float e_v      = vOut_ref - lpf_vOut;
            piV_integ     += Ts_h * (e_v + piV_e_prev);
            piV_e_prev     = e_v;
            iSigma_ref     = piV_Kp * e_v + piV_Ki * piV_integ;

            // Anti-windup with current limit
            if(iSigma_ref > piV_iMax)
            {
                iSigma_ref = piV_iMax;
                piV_integ  = (piV_iMax - piV_Kp * e_v) / piV_Ki;
            }
            else if(iSigma_ref < -piV_iMax)
            {
                iSigma_ref = -piV_iMax;
                piV_integ  = (-piV_iMax - piV_Kp * e_v) / piV_Ki;
            }
        }

        buckVctrlPrev = buckVctrlEnable;

        //=====================================================================
        // SIGMA CHANNEL — average current control
        //=====================================================================
        // Measured sigma: average of 3 filtered phase currents
        iSigma = (lpf_iA + lpf_iB + lpf_iC) * (1.0f / 3.0f);

        // Sigma error
        float e_s = iSigma_ref - iSigma;

        // Bumpless transfer: on 0→1 transition of buckCtrlEnable, seed the
        // integrator so that dSigma matches the manual-mode duty instantly.
        //   manual CMPA → normalised d0 = 2·(CMPA/TBPRD) − 1
        //   want:  piI_Kp·e_s + piI_Ki·piI_integ = d0
        //   →      piI_integ = (d0 − piI_Kp·e_s) / piI_Ki
        // buckCtrlPrev is reset to 0 in manual mode below, so re-enables
        // always trigger a fresh seed.
        if(!buckCtrlPrev)
        {
            float d0   = 2.0f * ((float)buckCMPA / (float)newTBPRD) - 1.0f;
            piI_integ  = (d0 - piI_Kp * e_s) / piI_Ki;
            piI_e_prev = e_s;
        }
        buckCtrlPrev = 1U;

        // Tustin PI update
        piI_integ     += Ts_h * (e_s + piI_e_prev);
        piI_e_prev     = e_s;
        dSigma         = piI_Kp * e_s + piI_Ki * piI_integ;

        // Anti-windup: back-calculate integral at saturation boundary
        if(dSigma > 1.0f)
        {
            dSigma    =  1.0f;
            piI_integ = ( 1.0f - piI_Kp * e_s) / piI_Ki;
        }
        else if(dSigma < -1.0f)
        {
            dSigma    = -1.0f;
            piI_integ = (-1.0f - piI_Kp * e_s) / piI_Ki;
        }

        //=====================================================================
        // DELTA CHANNEL — circulating current control
        //
        // Forward transform: i_delta = TDelta^T * [iA; iB; iC]
        //   i_delta1 = sqrt(2/3)*iA - (1/sqrt(6))*iB - (1/sqrt(6))*iC
        //   i_delta2 = 0*iA          - (1/sqrt(2))*iB + (1/sqrt(2))*iC
        //=====================================================================
        iDelta1 = BUCK_TDELTA_T_11*lpf_iA + BUCK_TDELTA_T_12*lpf_iB + BUCK_TDELTA_T_13*lpf_iC;
        iDelta2 = BUCK_TDELTA_T_21*lpf_iA + BUCK_TDELTA_T_22*lpf_iB + BUCK_TDELTA_T_23*lpf_iC;

        // Delta errors (refs are normally 0 for balanced phases)
        float e_d1     = iDelta1_ref - iDelta1;
        float e_d2     = iDelta2_ref - iDelta2;

        // Delta Tustin PI (gains are negative — sign absorbed in gain definition)
        piD_integ1    += Ts_h * (e_d1 + piD_e1_prev);
        piD_e1_prev    = e_d1;
        piD_integ2    += Ts_h * (e_d2 + piD_e2_prev);
        piD_e2_prev    = e_d2;
        dDelta1        = piD_Kp * e_d1 + piD_Ki * piD_integ1;
        dDelta2        = piD_Kp * e_d2 + piD_Ki * piD_integ2;

        // Anti-windup for delta channels
        if(dDelta1 > 1.0f)
        {
            dDelta1    =  1.0f;
            piD_integ1 = ( 1.0f - piD_Kp * e_d1) / piD_Ki;
        }
        else if(dDelta1 < -1.0f)
        {
            dDelta1    = -1.0f;
            piD_integ1 = (-1.0f - piD_Kp * e_d1) / piD_Ki;
        }
        if(dDelta2 > 1.0f)
        {
            dDelta2    =  1.0f;
            piD_integ2 = ( 1.0f - piD_Kp * e_d2) / piD_Ki;
        }
        else if(dDelta2 < -1.0f)
        {
            dDelta2    = -1.0f;
            piD_integ2 = (-1.0f - piD_Kp * e_d2) / piD_Ki;
        }

        //=====================================================================
        // INVERSE TRANSFORM: Tu * [dSigma; dDelta1; dDelta2] → [dPhA; dPhB; dPhC]
        // Output in 0..1 scale (directly representing per-phase duty ratio):
        //   dPh = 0.5 + 0.5*(TU_row · [dSigma; dDelta1; dDelta2])
        //       = 0.5·(1 + 1·dSigma + TU_12·dDelta1 + TU_13·dDelta2)  for Phase A
        // dSigma ∈ (-1, +1) and delta terms preserved; only the final
        // per-phase duties are shifted to (0, 1) for direct duty readout.
        //=====================================================================
        dPhA = 0.5f * (1.0f + BUCK_TU_11*dSigma + BUCK_TU_12*dDelta1 + BUCK_TU_13*dDelta2);
        dPhB = 0.5f * (1.0f + BUCK_TU_21*dSigma + BUCK_TU_22*dDelta1 + BUCK_TU_23*dDelta2);
        dPhC = 0.5f * (1.0f + BUCK_TU_31*dSigma + BUCK_TU_32*dDelta1 + BUCK_TU_33*dDelta2);

        // Clamp per-phase duties to (0, 1)
        if(dPhA > 1.0f) dPhA = 1.0f; else if(dPhA < 0.0f) dPhA = 0.0f;
        if(dPhB > 1.0f) dPhB = 1.0f; else if(dPhB < 0.0f) dPhB = 0.0f;
        if(dPhC > 1.0f) dPhC = 1.0f; else if(dPhC < 0.0f) dPhC = 0.0f;

        // Level shift: d (0..1) → CMPA
        //   0.0 → 0%, 0.5 → 50%, 1.0 → 100%
        buckCMPA_A = (uint16_t)(dPhA * (float)newTBPRD);
        buckCMPA_B = (uint16_t)(dPhB * (float)newTBPRD);
        buckCMPA_C = (uint16_t)(dPhC * (float)newTBPRD);
        buckCMPA   = buckCMPA_A;   // mirror phase A for CCS inspection
    }
    else
    {
        // Manual mode — reset all PI states for clean restart on next enable
        buckCtrlPrev = 0U;   // arms bumpless seeding for next enable
        piV_integ   = 0.0f;  piV_e_prev  = 0.0f;
        piI_integ   = 0.0f;  piI_e_prev  = 0.0f;  dSigma  = 0.0f;
        piD_integ1  = 0.0f;  piD_e1_prev = 0.0f;  dDelta1 = 0.0f;
        piD_integ2  = 0.0f;  piD_e2_prev = 0.0f;  dDelta2 = 0.0f;
        dPhA = dPhB = dPhC = 0.0f;
        buckCMPA   = newCMPA;
        buckCMPA_A = buckCMPA_B = buckCMPA_C = buckCMPA;
    }
    // Safety duty clamp — hard limit on per-phase CMPA before hardware write.
    // Protects voltage sensor from transient overshoot (e.g. delta corrections
    // pushing one phase above the safe output voltage).
    {
        uint16_t maxCMPA = (uint16_t)((float)newTBPRD * buckDutyMax_pct * 0.01f);
        if(buckCMPA_A > maxCMPA) buckCMPA_A = maxCMPA;
        if(buckCMPA_B > maxCMPA) buckCMPA_B = maxCMPA;
        if(buckCMPA_C > maxCMPA) buckCMPA_C = maxCMPA;
    }

    // Apply per-phase CMPA — shadow-loads at CTR=0 for glitch-free update
    EPWM_setCounterCompareValue(BUCK_PHASE_A_BASE, EPWM_COUNTER_COMPARE_A, buckCMPA_A);
    EPWM_setCounterCompareValue(BUCK_PHASE_B_BASE, EPWM_COUNTER_COMPARE_A, buckCMPA_B);
    EPWM_setCounterCompareValue(BUCK_PHASE_C_BASE, EPWM_COUNTER_COMPARE_A, buckCMPA_C);

    //--- 2b. Switching frequency: update period + phase shifts on change only ---
    // TBPRD in direct-load mode, phase shift registers load on next sync event.
    static uint16_t prevTBPRD = 0U;
    if(newTBPRD != prevTBPRD)
    {
        buckTBPRD = newTBPRD;

        // phaseB = (2*TBPRD)/3  → 120 deg, count UP after sync
        // phaseC = (2*TBPRD)/3  → 240 deg equivalent, count DOWN after sync
        uint16_t phaseB_shift = (2U * buckTBPRD) / 3U;
        uint16_t phaseC_shift = (2U * buckTBPRD) / 3U;
        uint16_t rtiTBPRD     = buckTBPRD / 6U;

        EPWM_setTimeBasePeriod(BUCK_PHASE_A_BASE, buckTBPRD);
        EPWM_setTimeBasePeriod(BUCK_PHASE_B_BASE, buckTBPRD);
        EPWM_setTimeBasePeriod(BUCK_PHASE_C_BASE, buckTBPRD);

        EPWM_setPhaseShift(BUCK_PHASE_B_BASE, phaseB_shift);
        EPWM_setPhaseShift(BUCK_PHASE_C_BASE, phaseC_shift);

        EPWM_setTimeBasePeriod(BUCK_RTI_BASE, rtiTBPRD);
        // TBPHS = 0: sync from ePWM1 CTR=0 resets ePWM4 to CTR=0 (peak)
        EPWM_setPhaseShift(BUCK_RTI_BASE, 0U);

        prevTBPRD = buckTBPRD;
    }

    //--- 2c. Dead time: update both RED and FED on all 3 phases on change only ---
    // (commented out — dead time is fixed at startup, not updated in real time)
    //static uint16_t prevDB = 0U;
    //if(newDBcounts != prevDB)
    //{
    //    buckDBcounts = newDBcounts;
    //
    //    EPWM_setRisingEdgeDelayCount(BUCK_PHASE_A_BASE,  buckDBcounts);
    //    EPWM_setFallingEdgeDelayCount(BUCK_PHASE_A_BASE, buckDBcounts);
    //    EPWM_setRisingEdgeDelayCount(BUCK_PHASE_B_BASE,  buckDBcounts);
    //    EPWM_setFallingEdgeDelayCount(BUCK_PHASE_B_BASE, buckDBcounts);
    //    EPWM_setRisingEdgeDelayCount(BUCK_PHASE_C_BASE,  buckDBcounts);
    //    EPWM_setFallingEdgeDelayCount(BUCK_PHASE_C_BASE, buckDBcounts);
    //
    //    prevDB = buckDBcounts;
    //}

    //-------------------------------------------------------------------------
    // 3. LED heartbeat — blink from inside ISR to confirm ISR is running
    //    Blue LED: ~1 Hz  (60000 / LED_BLUE_PERIOD)
    //    Red LED:  ~2 Hz  (60000 / LED_RED_PERIOD)
    //    Rate will scale automatically if buckFreq_Hz changes (RTI = 6×Fsw).
    //-------------------------------------------------------------------------
    rtiIsrCount++;

    ledBlueCnt++;
    if(ledBlueCnt >= LED_BLUE_PERIOD)
    {
        ledBlueCnt = 0U;
        GPIO_togglePin(BUCK_GPIO_LED_BLUE);
    }

    ledRedCnt++;
    if(ledRedCnt >= LED_RED_PERIOD)
    {
        ledRedCnt = 0U;
        GPIO_togglePin(BUCK_GPIO_LED_RED);
    }

    if(buckEnableGate)
        GPIO_writePin(BUCK_GPIO_NEN_UC, 0U);    // LOW  = gate driver enabled
    else
        GPIO_writePin(BUCK_GPIO_NEN_UC, 1U);    // HIGH = gate driver disabled

    //-------------------------------------------------------------------------
    // 4. Clear interrupt flags and acknowledge PIE group 3
    //-------------------------------------------------------------------------
    EPWM_clearEventTriggerInterruptFlag(BUCK_RTI_BASE);
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP3);
}

//=============================================================================
// main()
//=============================================================================
void main(void)
{
    //-------------------------------------------------------------------------
    // Device initialisation — clocks, watchdog, flash wait-states
    //-------------------------------------------------------------------------
    Device_init();

    //-------------------------------------------------------------------------
    // GPIO initialisation — unlock and configure all pads
    //-------------------------------------------------------------------------
    Device_initGPIO();
    setupGPIOs();

    //-------------------------------------------------------------------------
    // ADC setup
    //-------------------------------------------------------------------------
    setupADCs();

    //-------------------------------------------------------------------------
    // PWM setup
    //-------------------------------------------------------------------------
    setupPWMs();

    //-------------------------------------------------------------------------
    // Interrupt setup (must come after PWM so ePWM7 INT flag is cleared first)
    //-------------------------------------------------------------------------
    setupInterrupts();

    //-------------------------------------------------------------------------
    // Main loop — minimal background task
    // All real-time work is done in rtiISR.
    // User changes buckDuty / buckTBPRD via CCS Watch Window.
    //-------------------------------------------------------------------------
    for(;;)
    {
    }
        // Gate driver enable — controlled by buckEnableGate (set via CCS Expressions window)
        // nEn_uC (GPIO124, J2 pin 13) is ACTIVE LOW:
        //   buckEnableGate = 1  →  GPIO124 LOW  →  gate driver ENABLED
        //   buckEnableGate = 0  →  GPIO124 HIGH →  gate driver DISABLED (safe default)
        /*
        if(buckEnableGate)
            GPIO_writePin(BUCK_GPIO_NEN_UC, 0U);    // LOW  = gate driver enabled
        else
            GPIO_writePin(BUCK_GPIO_NEN_UC, 1U);    // HIGH = gate driver disabled
    } */
}

// end of file
