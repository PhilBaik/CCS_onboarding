//#############################################################################
//
// FILE:    epwm_interrupt_main.c
//
// TITLE:   ePWM interrupt (ISR driven by the PWM period)
//
// Target:  TMS320F28379D LaunchPad (LAUNCHXL-F28379D)
//          Probe GPIO0 (J4 pin 40) with a scope; the LED needs no hardware.
//
// Description:
//   - ePWM1 runs an up-count time base at 10 kHz and outputs ePWM1A on GPIO0.
//   - The ePWM event trigger raises an interrupt every 10th period, when
//     TBCTR = 0. The ISR therefore runs at 1 kHz, locked to the PWM.
//   - epwm1ISR() does two things:
//       1. Toggles D10 (blue LED, GPIO31) every 500 interrupts = every 0.5 s.
//       2. Moves CMPA by 4 counts per interrupt, so the duty ramps between
//          10 % and 90 % and back, taking 2 s each way. This is the pattern of
//          a real control loop: compute in the ISR, write the new compare
//          value, and let the shadow register apply it at the next period.
//   - isrCount, cmpA and ledTicks are visible in the CCS Expressions window.
//
// Interrupt path:  ePWM1 INT -> PIE group 3, channel 1 (INT_EPWM1) -> CPU INT3
//
// Hardware:
//   If a BOOSTXL-3PHGANINV is plugged in, its gate driver stays disabled
//   (nEn_uC is pulled up) until GPIO124 is driven low. This example never
//   drives GPIO124.
//
//#############################################################################

#include "device.h"
#include "driverlib.h"

#define LED_BLUE_GPIO           31U     // D10, active low
#define LED_ON                  0U
#define LED_OFF                 1U

// EPWMCLK = SYSCLK / 2 = 100 MHz (TBCLK prescalers are set to 1 below)
#define EPWMCLK_HZ              (DEVICE_SYSCLK_FREQ / 2UL)
#define PWM_FREQ_HZ             10000UL

// Up-count: one period = TBPRD + 1 counts, so TBPRD = 9999 for 10 kHz
#define PWM_TBPRD               ((EPWMCLK_HZ / PWM_FREQ_HZ) - 1UL)

#define ISR_EVERY_N_PERIODS     10U     // interrupt every 10th PWM period (1 kHz)
#define BLINK_ISR_COUNT         500U    // toggle the LED every 500 interrupts

#define CMP_MIN                 1000U   // 10 % of 10000 counts
#define CMP_MAX                 9000U   // 90 %
#define CMP_STEP                4U      // counts per interrupt: 2000 steps = 2 s

// Read-only: watch these in the CCS Expressions window
volatile uint32_t isrCount = 0U;
volatile uint16_t cmpA     = CMP_MIN;
volatile uint16_t ledTicks = BLINK_ISR_COUNT;

__interrupt void epwm1ISR(void);

//=============================================================================
// setupGPIOs()
// ePWM1A on GPIO0, and D10 (blue LED) as an output, start OFF.
//=============================================================================
void setupGPIOs(void)
{
    GPIO_setPinConfig(GPIO_0_EPWM1A);               // J4 pin 40
    GPIO_setPadConfig(0U, GPIO_PIN_TYPE_STD);

    GPIO_setPinConfig(GPIO_31_GPIO31);
    GPIO_writePin(LED_BLUE_GPIO, LED_OFF);
    GPIO_setDirectionMode(LED_BLUE_GPIO, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(LED_BLUE_GPIO, GPIO_PIN_TYPE_STD);
}

//=============================================================================
// setupInterrupts()
// Initialise the PIE, then register and enable the ePWM1 interrupt.
// The interrupt is only raised once setupPWM() has started the time base, and
// the CPU only takes it after EINT in main().
//=============================================================================
void setupInterrupts(void)
{
    // Initialise the PIE, clear all flags, point every vector at a default ISR
    Interrupt_initModule();
    Interrupt_initVectorTable();

    // Put epwm1ISR in the PIE vector table (group 3, channel 1)
    Interrupt_register(INT_EPWM1, &epwm1ISR);

    // Enable the interrupt in the PIE and in the CPU (INT3)
    Interrupt_enable(INT_EPWM1);
}

//=============================================================================
// setupPWM()
// ePWM1: up-count at 10 kHz, output A set at TBCTR = 0 and cleared at CMPA,
// plus the event trigger that raises the interrupt.
//=============================================================================
void setupPWM(void)
{
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
    // CMPA is shadowed: a new value is copied to the active register when
    // TBCTR = 0.
    //-------------------------------------------------------------------------
    EPWM_setCounterCompareValue(EPWM1_BASE, EPWM_COUNTER_COMPARE_A, cmpA);
    EPWM_setCounterCompareShadowLoadMode(EPWM1_BASE,
                                         EPWM_COUNTER_COMPARE_A,
                                         EPWM_COMP_LOAD_ON_CNTR_ZERO);

    //-------------------------------------------------------------------------
    // Action Qualifier sub-module
    //   output A: HIGH at TBCTR = 0, LOW at TBCTR = CMPA (counting up)
    //-------------------------------------------------------------------------
    EPWM_setActionQualifierAction(EPWM1_BASE,
                                  EPWM_AQ_OUTPUT_A,
                                  EPWM_AQ_OUTPUT_HIGH,
                                  EPWM_AQ_OUTPUT_ON_TIMEBASE_ZERO);
    EPWM_setActionQualifierAction(EPWM1_BASE,
                                  EPWM_AQ_OUTPUT_A,
                                  EPWM_AQ_OUTPUT_LOW,
                                  EPWM_AQ_OUTPUT_ON_TIMEBASE_UP_CMPA);

    //-------------------------------------------------------------------------
    // Event Trigger sub-module
    // Raise the interrupt when TBCTR = 0, every ISR_EVERY_N_PERIODS-th time.
    // The ISR must clear the flag, or no further interrupt is generated.
    //-------------------------------------------------------------------------
    EPWM_setInterruptSource(EPWM1_BASE, EPWM_INT_TBCTR_ZERO);
    EPWM_setInterruptEventCount(EPWM1_BASE, ISR_EVERY_N_PERIODS);
    EPWM_enableInterrupt(EPWM1_BASE);

    // Start the time base
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);
}

//=============================================================================
// epwm1ISR()
// Runs at 1 kHz (every 10th PWM period, at TBCTR = 0).
//=============================================================================
__interrupt void epwm1ISR(void)
{
    static bool rampUp = true;
    uint16_t    cmp    = cmpA;

    isrCount++;

    //-------------------------------------------------------------------------
    // 1. Blink D10 every BLINK_ISR_COUNT interrupts (0.5 s)
    //-------------------------------------------------------------------------
    ledTicks--;
    if(ledTicks == 0U)
    {
        ledTicks = BLINK_ISR_COUNT;
        GPIO_togglePin(LED_BLUE_GPIO);
    }

    //-------------------------------------------------------------------------
    // 2. Triangle ramp on CMPA between CMP_MIN and CMP_MAX
    //-------------------------------------------------------------------------
    if(rampUp)
    {
        cmp += CMP_STEP;
        if(cmp >= CMP_MAX)
        {
            cmp    = CMP_MAX;
            rampUp = false;
        }
    }
    else
    {
        cmp -= CMP_STEP;
        if(cmp <= CMP_MIN)
        {
            cmp    = CMP_MIN;
            rampUp = true;
        }
    }

    cmpA = cmp;
    EPWM_setCounterCompareValue(EPWM1_BASE, EPWM_COUNTER_COMPARE_A, cmp);

    //-------------------------------------------------------------------------
    // 3. Clear the ePWM interrupt flag, then acknowledge PIE group 3 so the
    //    PIE can pass the next interrupt of that group to the CPU
    //-------------------------------------------------------------------------
    EPWM_clearEventTriggerInterruptFlag(EPWM1_BASE);
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP3);
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
    setupInterrupts();
    setupPWM();

    // Enable interrupts globally (INTM) and real-time debug (DBGM)
    EINT;
    ERTM;

    // All work is done in epwm1ISR()
    for(;;)
    {
    }
}

// end of file
