//#############################################################################
//
// FILE:    led_toggle_main.c
//
// TITLE:   LED toggle (hello world)
//
// Target:  TMS320F28379D LaunchPad (LAUNCHXL-F28379D). No BoosterPack needed.
//
// Description:
//   - Blinks the two on-board user LEDs alternately, 0.5 s each.
//       D10 (blue) = GPIO31
//       D9  (red)  = GPIO34
//   - Both LEDs are ACTIVE LOW: writing 0 turns the LED on, 1 turns it off.
//   - The delay is a blocking software delay (DEVICE_DELAY_US). The next
//     examples replace it with hardware timing (ePWM, ePWM interrupt).
//   - blinkCount increments once per half period; watch it in the CCS
//     Expressions window.
//
//#############################################################################

#include "device.h"
#include "driverlib.h"

#define LED_BLUE_GPIO           31U     // D10, active low
#define LED_RED_GPIO            34U     // D9,  active low

#define LED_ON                  0U
#define LED_OFF                 1U

#define BLINK_HALF_PERIOD_US    500000UL    // 0.5 s

volatile uint32_t blinkCount = 0U;

//=============================================================================
// setupLEDs()
// Configure the two LED pins as outputs. The level is written BEFORE the pin
// becomes an output, so the LED never flashes to the wrong state at start-up.
//=============================================================================
void setupLEDs(void)
{
    // D10 (blue): start ON
    GPIO_setPinConfig(GPIO_31_GPIO31);
    GPIO_writePin(LED_BLUE_GPIO, LED_ON);
    GPIO_setDirectionMode(LED_BLUE_GPIO, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(LED_BLUE_GPIO, GPIO_PIN_TYPE_STD);

    // D9 (red): start OFF, so the two LEDs alternate
    GPIO_setPinConfig(GPIO_34_GPIO34);
    GPIO_writePin(LED_RED_GPIO, LED_OFF);
    GPIO_setDirectionMode(LED_RED_GPIO, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(LED_RED_GPIO, GPIO_PIN_TYPE_STD);
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

    setupLEDs();

    for(;;)
    {
        GPIO_togglePin(LED_BLUE_GPIO);
        GPIO_togglePin(LED_RED_GPIO);
        blinkCount++;

        DEVICE_DELAY_US(BLINK_HALF_PERIOD_US);
    }
}

// end of file
