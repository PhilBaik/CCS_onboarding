//#############################################################################
//
// FILE:    gpio_main.c
//
// TITLE:   GPIO input and output
//
// Target:  TMS320F28379D LaunchPad (LAUNCHXL-F28379D). No BoosterPack needed.
//
// Description:
//   - Output: D9 (red, GPIO34) follows the variable redLedOn. Set it to 1 in
//     the CCS Expressions window to turn the LED on.
//   - Input:  GPIO32 (J1 pin 2) is an input with the internal pull-up and a
//     6-sample input filter. D10 (blue, GPIO31) mirrors it:
//         pin open (pulled high)      -> D10 off
//         pin wired to GND            -> D10 on
//     Use a jumper wire from J1 pin 2 to GND (J3 pin 22, next to it).
//   - Both LEDs are ACTIVE LOW (0 = on), so the input level can be written to
//     the LED pin as it is.
//   - inputLevel shows the value read from GPIO32 (1 = high, 0 = low).
//
//#############################################################################

#include "device.h"
#include "driverlib.h"

#define LED_BLUE_GPIO       31U     // D10, active low
#define LED_RED_GPIO        34U     // D9,  active low
#define INPUT_GPIO          32U     // J1 pin 2

#define LED_ON              0U
#define LED_OFF             1U

// Write from the CCS Expressions window: 1 = D9 on, 0 = D9 off
volatile uint16_t redLedOn   = 0U;

// Read-only: level seen on GPIO32 (1 = high, 0 = low)
volatile uint16_t inputLevel = 1U;

//=============================================================================
// setupGPIOs()
// Two outputs (LEDs) and one input (GPIO32).
//=============================================================================
void setupGPIOs(void)
{
    //-------------------------------------------------------------------------
    // D10 (blue) output, start OFF
    //-------------------------------------------------------------------------
    GPIO_setPinConfig(GPIO_31_GPIO31);
    GPIO_writePin(LED_BLUE_GPIO, LED_OFF);
    GPIO_setDirectionMode(LED_BLUE_GPIO, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(LED_BLUE_GPIO, GPIO_PIN_TYPE_STD);

    //-------------------------------------------------------------------------
    // D9 (red) output, start OFF
    //-------------------------------------------------------------------------
    GPIO_setPinConfig(GPIO_34_GPIO34);
    GPIO_writePin(LED_RED_GPIO, LED_OFF);
    GPIO_setDirectionMode(LED_RED_GPIO, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(LED_RED_GPIO, GPIO_PIN_TYPE_STD);

    //-------------------------------------------------------------------------
    // GPIO32 input with pull-up
    // The filter takes 6 samples, 510 SYSCLK cycles (2.55 us) apart, and only
    // accepts a new level when all 6 agree. Pulses shorter than about 15 us
    // are ignored. This rejects noise spikes; it does not debounce a
    // mechanical contact (bounce lasts milliseconds). The sampling period
    // setting is shared by GPIO32 to GPIO39.
    //-------------------------------------------------------------------------
    GPIO_setPinConfig(GPIO_32_GPIO32);
    GPIO_setDirectionMode(INPUT_GPIO, GPIO_DIR_MODE_IN);
    GPIO_setPadConfig(INPUT_GPIO, GPIO_PIN_TYPE_PULLUP);
    GPIO_setQualificationMode(INPUT_GPIO, GPIO_QUAL_6SAMPLE);
    GPIO_setQualificationPeriod(INPUT_GPIO, 510U);
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

    for(;;)
    {
        // Input -> blue LED (pin low = LED on, because the LED is active low)
        inputLevel = (uint16_t)GPIO_readPin(INPUT_GPIO);
        GPIO_writePin(LED_BLUE_GPIO, inputLevel);

        // Variable -> red LED
        GPIO_writePin(LED_RED_GPIO, (redLedOn != 0U) ? LED_ON : LED_OFF);

        DEVICE_DELAY_US(1000);
    }
}

// end of file
