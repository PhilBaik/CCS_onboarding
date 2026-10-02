//#############################################################################
//
// FILE:    epwm_modes_main.c
//
// TITLE:   ePWM counter modes: up, down and up-down
//
// Target:  TMS320F28379D LaunchPad (LAUNCHXL-F28379D)
//          Probe the three pins on the BoosterPack header (scope or logic
//          analyzer), trigger on ePWM1A.
//
// Description:
//   Three ePWM modules run at the same frequency (10 kHz) and the same duty
//   (dutyPct, default 25 %). They differ only in the counter mode, and so in
//   where the pulse sits inside the period:
//
//       ePWM1A  GPIO0  J4 pin 40   UP       left-aligned (asymmetric)
//       ePWM2A  GPIO2  J4 pin 38   DOWN     right-aligned (asymmetric)
//       ePWM3A  GPIO4  J4 pin 36   UP-DOWN  centre-aligned (symmetric)
//
//   TBCLKSYNC starts the three time bases on the same clock edge, so the
//   traces on a scope line up.
//
//   Counter mode   TBCTR path      period (counts)   Action qualifier
//   ------------   -------------   ---------------   ----------------------
//   UP             0 -> TBPRD      TBPRD + 1         HIGH at 0, LOW at CMPA
//   DOWN           TBPRD -> 0      TBPRD + 1         HIGH at CMPA, LOW at 0
//   UP-DOWN        0 -> TBPRD -> 0 2 x TBPRD         LOW up-CMPA, HIGH down-CMPA
//
//   Because the up-down counter travels twice (up and down) per period, its
//   TBPRD is half of the up/down value for the same frequency (5000 vs 9999).
//   For up-down the pulse is 2 x CMPA wide and centred on TBCTR = 0, so its
//   edges are symmetric around the period boundary.
//
//   Change dutyPct in the CCS Expressions window. The compare registers are
//   shadowed and load at TBCTR = 0.
//
// Hardware:
//   If a BOOSTXL-3PHGANINV is plugged in, its gate driver stays disabled
//   (nEn_uC is pulled up) until GPIO124 is driven low. This example never
//   drives GPIO124.
//
//#############################################################################

#include "device.h"
#include "driverlib.h"

// EPWMCLK = SYSCLK / 2 = 100 MHz (TBCLK prescalers are set to 1 below)
#define EPWMCLK_HZ          (DEVICE_SYSCLK_FREQ / 2UL)
#define PWM_FREQ_HZ         10000UL

// UP and DOWN: period = TBPRD + 1 counts            -> TBPRD = 9999
#define TBPRD_ASYM          ((EPWMCLK_HZ / PWM_FREQ_HZ) - 1UL)

// UP-DOWN: period = 2 x TBPRD counts                -> TBPRD = 5000
#define TBPRD_SYM           ((EPWMCLK_HZ / PWM_FREQ_HZ) / 2UL)

// The duty limits keep CMPx away from 0 and the period end, where two
// action qualifier events fall on the same count.
#define DUTY_MIN_PCT        1.0f
#define DUTY_MAX_PCT        99.0f

// PRIMARY INPUT: change in the CCS Expressions window
volatile float    dutyPct    = 25.0f;

// Read-only: compare values written to the hardware
volatile uint16_t cmpUp      = 0U;
volatile uint16_t cmpDown    = 0U;
volatile uint16_t cmpUpDown  = 0U;

//=============================================================================
// dutyToCmp()
// Convert a duty cycle in percent to a compare count (clamped).
// fullScale is the number of counts that map to 100 %:
//   UP, DOWN : TBPRD + 1 (the whole period)
//   UP-DOWN  : TBPRD     (CMPA counts on each side of TBCTR = 0, so 2 x CMPA
//                         out of 2 x TBPRD)
//=============================================================================
static uint16_t dutyToCmp(float duty_pct, uint32_t fullScale)
{
    if(duty_pct < DUTY_MIN_PCT)
    {
        duty_pct = DUTY_MIN_PCT;
    }
    if(duty_pct > DUTY_MAX_PCT)
    {
        duty_pct = DUTY_MAX_PCT;
    }

    return (uint16_t)(((float)fullScale * duty_pct) / 100.0f);
}

//=============================================================================
// updateCompares()
// Recompute CMPA for all three modules from dutyPct and write it.
//=============================================================================
static void updateCompares(void)
{
    float duty = dutyPct;

    cmpUp     = dutyToCmp(duty, TBPRD_ASYM + 1UL);
    cmpDown   = dutyToCmp(duty, TBPRD_ASYM + 1UL);
    cmpUpDown = dutyToCmp(duty, TBPRD_SYM);

    EPWM_setCounterCompareValue(EPWM1_BASE, EPWM_COUNTER_COMPARE_A, cmpUp);
    EPWM_setCounterCompareValue(EPWM2_BASE, EPWM_COUNTER_COMPARE_A, cmpDown);
    EPWM_setCounterCompareValue(EPWM3_BASE, EPWM_COUNTER_COMPARE_A, cmpUpDown);
}

//=============================================================================
// setupGPIOs()
// Route ePWM1A, ePWM2A and ePWM3A to their pins.
//=============================================================================
void setupGPIOs(void)
{
    GPIO_setPinConfig(GPIO_0_EPWM1A);               // J4 pin 40
    GPIO_setPadConfig(0U, GPIO_PIN_TYPE_STD);

    GPIO_setPinConfig(GPIO_2_EPWM2A);               // J4 pin 38
    GPIO_setPadConfig(2U, GPIO_PIN_TYPE_STD);

    GPIO_setPinConfig(GPIO_4_EPWM3A);               // J4 pin 36
    GPIO_setPadConfig(4U, GPIO_PIN_TYPE_STD);
}

//=============================================================================
// setupEPWM()
// Configure one ePWM module for the given counter mode. Everything is the
// same for the three modules except the counter mode, the period, the start
// value of the counter and the action qualifier.
//=============================================================================
static void setupEPWM(uint32_t base, EPWM_TimeBaseCountMode mode,
                      uint16_t tbprd, uint16_t cmpa)
{
    //-------------------------------------------------------------------------
    // Time Base sub-module, TBCLK = EPWMCLK
    //-------------------------------------------------------------------------
    EPWM_setTimeBaseCounterMode(base, mode);
    EPWM_setClockPrescaler(base,
                           EPWM_CLOCK_DIVIDER_1,
                           EPWM_HSCLOCK_DIVIDER_1);
    EPWM_setPeriodLoadMode(base, EPWM_PERIOD_DIRECT_LOAD);
    EPWM_setTimeBasePeriod(base, tbprd);

    // A down-counter starts at the top, the other two at 0
    EPWM_setTimeBaseCounter(base,
                            (mode == EPWM_COUNTER_MODE_DOWN) ? tbprd : 0U);

    // Free-running: no phase shift, no synchronisation input
    EPWM_setPhaseShift(base, 0U);
    EPWM_disablePhaseShiftLoad(base);

    //-------------------------------------------------------------------------
    // Counter Compare sub-module
    // CMPA is shadowed: a new value is copied to the active register when
    // TBCTR = 0.
    //-------------------------------------------------------------------------
    EPWM_setCounterCompareValue(base, EPWM_COUNTER_COMPARE_A, cmpa);
    EPWM_setCounterCompareShadowLoadMode(base,
                                         EPWM_COUNTER_COMPARE_A,
                                         EPWM_COMP_LOAD_ON_CNTR_ZERO);

    //-------------------------------------------------------------------------
    // Action Qualifier sub-module: this is where the three types differ
    //-------------------------------------------------------------------------
    switch(mode)
    {
        case EPWM_COUNTER_MODE_UP:
            // Left-aligned: pulse starts at the beginning of the period
            EPWM_setActionQualifierAction(base,
                                          EPWM_AQ_OUTPUT_A,
                                          EPWM_AQ_OUTPUT_HIGH,
                                          EPWM_AQ_OUTPUT_ON_TIMEBASE_ZERO);
            EPWM_setActionQualifierAction(base,
                                          EPWM_AQ_OUTPUT_A,
                                          EPWM_AQ_OUTPUT_LOW,
                                          EPWM_AQ_OUTPUT_ON_TIMEBASE_UP_CMPA);
            break;

        case EPWM_COUNTER_MODE_DOWN:
            // Right-aligned: pulse ends at the end of the period
            EPWM_setActionQualifierAction(base,
                                          EPWM_AQ_OUTPUT_A,
                                          EPWM_AQ_OUTPUT_HIGH,
                                          EPWM_AQ_OUTPUT_ON_TIMEBASE_DOWN_CMPA);
            EPWM_setActionQualifierAction(base,
                                          EPWM_AQ_OUTPUT_A,
                                          EPWM_AQ_OUTPUT_LOW,
                                          EPWM_AQ_OUTPUT_ON_TIMEBASE_ZERO);
            break;

        case EPWM_COUNTER_MODE_UP_DOWN:
        default:
            // Centre-aligned: pulse is centred on TBCTR = 0
            EPWM_setActionQualifierAction(base,
                                          EPWM_AQ_OUTPUT_A,
                                          EPWM_AQ_OUTPUT_LOW,
                                          EPWM_AQ_OUTPUT_ON_TIMEBASE_UP_CMPA);
            EPWM_setActionQualifierAction(base,
                                          EPWM_AQ_OUTPUT_A,
                                          EPWM_AQ_OUTPUT_HIGH,
                                          EPWM_AQ_OUTPUT_ON_TIMEBASE_DOWN_CMPA);
            break;
    }
}

//=============================================================================
// setupPWMs()
// Configure the three modules, then start their time bases together.
//=============================================================================
void setupPWMs(void)
{
    cmpUp     = dutyToCmp(dutyPct, TBPRD_ASYM + 1UL);
    cmpDown   = dutyToCmp(dutyPct, TBPRD_ASYM + 1UL);
    cmpUpDown = dutyToCmp(dutyPct, TBPRD_SYM);

    // Stop all time bases while configuring
    SysCtl_disablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);

    setupEPWM(EPWM1_BASE, EPWM_COUNTER_MODE_UP,
              (uint16_t)TBPRD_ASYM, cmpUp);
    setupEPWM(EPWM2_BASE, EPWM_COUNTER_MODE_DOWN,
              (uint16_t)TBPRD_ASYM, cmpDown);
    setupEPWM(EPWM3_BASE, EPWM_COUNTER_MODE_UP_DOWN,
              (uint16_t)TBPRD_SYM, cmpUpDown);

    // Start all three time bases on the same clock edge
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
    setupPWMs();

    // Background task: copy dutyPct to the compare registers.
    // No ISR is needed; the shadow registers make the update glitch-free.
    for(;;)
    {
        updateCompares();

        DEVICE_DELAY_US(1000);
    }
}

// end of file
