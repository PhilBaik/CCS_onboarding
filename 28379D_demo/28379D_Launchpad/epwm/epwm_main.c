//#############################################################################
//
// FILE:    epwm_main.c
//
// TITLE:   ePWM basics: one module, two outputs
//
// Target:  TMS320F28379D LaunchPad (LAUNCHXL-F28379D)
//          Probe the pins on the BoosterPack header (scope or logic analyzer).
//
// Description:
//   - ePWM1 runs an up-count time base at 10 kHz.
//       ePWM1A = GPIO0 = J4 pin 40, duty set by dutyA_pct (default 25 %)
//       ePWM1B = GPIO1 = J4 pin 39, duty set by dutyB_pct (default 75 %)
//   - Both outputs are SET at TBCTR = 0 and CLEARED when TBCTR reaches the
//     compare value (CMPA or CMPB), so the rising edges line up and the
//     falling edges are independent.
//   - Change dutyA_pct and dutyB_pct in the CCS Expressions window. The new
//     compare values go through the shadow registers and take effect at the
//     next TBCTR = 0, so the pulse width never changes in mid-period.
//
// Hardware:
//   If a BOOSTXL-3PHGANINV is plugged in, its gate driver stays disabled
//   (nEn_uC is pulled up) until GPIO124 is driven low. This example never
//   drives GPIO124. The debugger freezes the outputs while the CPU is halted.
//
//#############################################################################

#include "device.h"
#include "driverlib.h"

// EPWMCLK = SYSCLK / 2 = 100 MHz (TBCLK prescalers are set to 1 below)
#define EPWMCLK_HZ          (DEVICE_SYSCLK_FREQ / 2UL)
#define PWM_FREQ_HZ         10000UL

// Up-count: one period = TBPRD + 1 counts, so TBPRD = 9999 for 10 kHz
#define PWM_TBPRD           ((EPWMCLK_HZ / PWM_FREQ_HZ) - 1UL)

// The duty limits keep CMPx away from 0 and TBPRD, where the SET at TBCTR = 0
// and the CLEAR at CMPx fall on the same count.
#define DUTY_MIN_PCT        1.0f
#define DUTY_MAX_PCT        99.0f

// PRIMARY INPUTS: change in the CCS Expressions window
volatile float    dutyA_pct = 25.0f;
volatile float    dutyB_pct = 75.0f;

// Read-only: compare values written to the hardware
volatile uint16_t cmpA = 0U;
volatile uint16_t cmpB = 0U;

//=============================================================================
// dutyToCmp()
// Convert a duty cycle in percent to a compare count (clamped).
//=============================================================================
static uint16_t dutyToCmp(float duty_pct)
{
    if(duty_pct < DUTY_MIN_PCT)
    {
        duty_pct = DUTY_MIN_PCT;
    }
    if(duty_pct > DUTY_MAX_PCT)
    {
        duty_pct = DUTY_MAX_PCT;
    }

    return (uint16_t)(((float)(PWM_TBPRD + 1UL) * duty_pct) / 100.0f);
}

//=============================================================================
// setupGPIOs()
// Route ePWM1A and ePWM1B to their pins.
//=============================================================================
void setupGPIOs(void)
{
    GPIO_setPinConfig(GPIO_0_EPWM1A);               // J4 pin 40
    GPIO_setPadConfig(0U, GPIO_PIN_TYPE_STD);

    GPIO_setPinConfig(GPIO_1_EPWM1B);               // J4 pin 39
    GPIO_setPadConfig(1U, GPIO_PIN_TYPE_STD);
}

//=============================================================================
// setupPWM()
// Configure ePWM1: time base, counter compare, action qualifier.
//=============================================================================
void setupPWM(void)
{
    cmpA = dutyToCmp(dutyA_pct);
    cmpB = dutyToCmp(dutyB_pct);

    // Stop all time bases while configuring, restart them together at the end
    SysCtl_disablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);

    //-------------------------------------------------------------------------
    // Time Base sub-module: up-count, TBCLK = EPWMCLK
    //-------------------------------------------------------------------------
    EPWM_setTimeBaseCounterMode(EPWM1_BASE, EPWM_COUNTER_MODE_UP);
    EPWM_setClockPrescaler(EPWM1_BASE,
                           EPWM_CLOCK_DIVIDER_1,
                           EPWM_HSCLOCK_DIVIDER_1);
    EPWM_setPeriodLoadMode(EPWM1_BASE, EPWM_PERIOD_DIRECT_LOAD);
    EPWM_setTimeBasePeriod(EPWM1_BASE, (uint16_t)PWM_TBPRD);
    EPWM_setTimeBaseCounter(EPWM1_BASE, 0U);

    // Free-running: no phase shift, no synchronisation input
    EPWM_setPhaseShift(EPWM1_BASE, 0U);
    EPWM_disablePhaseShiftLoad(EPWM1_BASE);

    //-------------------------------------------------------------------------
    // Counter Compare sub-module
    // CMPA and CMPB are shadowed: a new value is copied to the active register
    // when TBCTR = 0.
    //-------------------------------------------------------------------------
    EPWM_setCounterCompareValue(EPWM1_BASE, EPWM_COUNTER_COMPARE_A, cmpA);
    EPWM_setCounterCompareValue(EPWM1_BASE, EPWM_COUNTER_COMPARE_B, cmpB);
    EPWM_setCounterCompareShadowLoadMode(EPWM1_BASE,
                                         EPWM_COUNTER_COMPARE_A,
                                         EPWM_COMP_LOAD_ON_CNTR_ZERO);
    EPWM_setCounterCompareShadowLoadMode(EPWM1_BASE,
                                         EPWM_COUNTER_COMPARE_B,
                                         EPWM_COMP_LOAD_ON_CNTR_ZERO);

    //-------------------------------------------------------------------------
    // Action Qualifier sub-module
    //   output A: HIGH at TBCTR = 0, LOW at TBCTR = CMPA (counting up)
    //   output B: HIGH at TBCTR = 0, LOW at TBCTR = CMPB (counting up)
    //-------------------------------------------------------------------------
    EPWM_setActionQualifierAction(EPWM1_BASE,
                                  EPWM_AQ_OUTPUT_A,
                                  EPWM_AQ_OUTPUT_HIGH,
                                  EPWM_AQ_OUTPUT_ON_TIMEBASE_ZERO);
    EPWM_setActionQualifierAction(EPWM1_BASE,
                                  EPWM_AQ_OUTPUT_A,
                                  EPWM_AQ_OUTPUT_LOW,
                                  EPWM_AQ_OUTPUT_ON_TIMEBASE_UP_CMPA);

    EPWM_setActionQualifierAction(EPWM1_BASE,
                                  EPWM_AQ_OUTPUT_B,
                                  EPWM_AQ_OUTPUT_HIGH,
                                  EPWM_AQ_OUTPUT_ON_TIMEBASE_ZERO);
    EPWM_setActionQualifierAction(EPWM1_BASE,
                                  EPWM_AQ_OUTPUT_B,
                                  EPWM_AQ_OUTPUT_LOW,
                                  EPWM_AQ_OUTPUT_ON_TIMEBASE_UP_CMPB);

    // Start the time base
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);
}

//=============================================================================
// main()
//=============================================================================
void main(void)
{
    // Clocks (200 MHz SYSCLK), watchdog, peripheral clocks
    Device_init();

    // Unlock the GPIO registers and set every pin to its default state
    Device_initGPIO();

    setupGPIOs();
    setupPWM();

    // Background task: copy the duty inputs to the compare registers.
    // No ISR is needed; the shadow registers make the update glitch-free.
    for(;;)
    {
        cmpA = dutyToCmp(dutyA_pct);
        cmpB = dutyToCmp(dutyB_pct);

        EPWM_setCounterCompareValue(EPWM1_BASE, EPWM_COUNTER_COMPARE_A, cmpA);
        EPWM_setCounterCompareValue(EPWM1_BASE, EPWM_COUNTER_COMPARE_B, cmpB);

        DEVICE_DELAY_US(1000);
    }
}

// end of file
