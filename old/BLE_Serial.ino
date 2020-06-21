/**
 * 
 * old
 * 
 **/

#include "BluetoothSerial.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

#if CONFIG_FREERTOS_UNICORE
#define ARDUINO_RUNNING_CORE 0
#else
#define ARDUINO_RUNNING_CORE 1
#endif

// set debug mode
#define DEBUG_MODE 1

// configuration
// led pins
#define PIN_RED 12
#define PIN_GREEN 13
#define PIN_BLUE 14

// PWM setting
#define PWM_PREQ 5000
#define CHANNEL_RED 0
#define CHANNEL_GREEN 1
#define CHANNEL_BLUE 2
#define RESOLUTION 8

// delay setting
#define FADE_DELAY 40 // 40 ms
#define FLASH_DELAY 100

typedef enum
{
    NONE = 0,
    FLASH,
    FADE,
    DIMMER,
    COLOR_COLLECTION
} led_mode_t;

// mode tracker
volatile led_mode_t led_mode = NONE;

// r,g,b is gobal so we can get and set them at anywhere in our program
int r = 0;
int g = 0;
int b = 0;

int dimming_percent = 0;

BluetoothSerial SerialBT;

int debug(const char *fmt, ...)
{
    int rc = 0;
    if (DEBUG_MODE)
    {
        char buffer[4096];
        va_list args;
        va_start(args, fmt);
        rc = vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        Serial.println(buffer);
    }
    return 0;
}
static int biggest(int x, int y, int z)
{
    return x > y ? (x > z ? 1 : 3) : (y > z ? 2 : 3);
}

static void set_color(int rr, int gg, int bb)
{
    // set PWN dutyCycle
    ledcWrite(CHANNEL_RED, rr);
    ledcWrite(CHANNEL_GREEN, gg);
    ledcWrite(CHANNEL_BLUE, bb);
    debug("COLOR: %d, %d, %d", rr, gg, bb);
}

/*--------------------------COLOR MODE---------------------------*/
void color_collection(void)
{
    set_color(r, g, b);
    // reset led mode
    led_mode = NONE;
}

void fading_led(void)
{
    int index = biggest(r, g, b);
    int max_loop = ((1 == index) ? r : (2 == index) ? g : b);

    // mode 1
    for (int i = 0; i < max_loop; i++)
    {

        float delta_r = (float)r / g;
        float delta_g = (float)g / r;
        float delta_b = (float)b / r;
        int r_temp = r;
        int g_temp = g;
        int b_temp = b;
        // re-caculate r, g, b
        //
        if (1 == index)
        {
            r = ((0 >= r) ? 0 : r - 1);
            g = ((0 >= g) ? 0 : g_temp - (int)(delta_g * (i + 1)));
            b = ((0 >= b) ? 0 : b_temp - (int)(delta_b * (i + 1)));
        }
        else if (2 == index)
        {
            r = ((0 >= r) ? 0 : r_temp - (int)(delta_r * (i + 1)));
            g = ((0 >= g) ? 0 : g - 1);
            b = ((0 >= b) ? 0 : b_temp - (int)(delta_b * (i + 1)));
        }
        else if (3 == index)
        {
            r = ((0 >= r) ? 0 : r_temp - (int)(delta_r * (i + 1)));
            g = ((0 >= g) ? 0 : g_temp - (int)(delta_g * (i + 1)));
            b = ((0 >= b) ? 0 : b - 1);
        }
        else
        {
            // cannot come here
        }

        set_color(r, g, b);
        delay(FADE_DELAY);
        // stop if mode changed
        if (FADE != led_mode)
            return;
    }
}

// // mode 2
// for (int i=0; i< max_loop; i++) {
//     r = ((0>=r) ? 0 : r-1);
//     g = ((0>=g) ? 0 : g-1);
//     b = ((0>=b) ? 0 : b-1);

//     set_color(r, g, b);
//     delay(FADE_DELAY);
//     // stop if mode changed
//     if (FADE != led_mode) return;
// }

// reset mode when it done
led_mode = NONE;
}

void flash_led(void)
{
    if (FLASH == led_mode)
    {
        set_color(r, g, b);
        delay(FLASH_DELAY);
        set_color(0, 0, 0);
        delay(FLASH_DELAY);
    }
    // stop if mode changed
}

void dimming_led(void)
{
    // TODO
    set_color((int)r * dimming_percent / 100, (int)g * dimming_percent / 100, (int)b * dimming_percent / 100);
    // reset mode
    led_mode = NONE;
}

void setup()
{
    Serial.begin(115200);

    // configure LED PWM functionalitites
    ledcSetup(CHANNEL_RED, PWM_PREQ, RESOLUTION);
    ledcSetup(CHANNEL_GREEN, PWM_PREQ, RESOLUTION);
    ledcSetup(CHANNEL_BLUE, PWM_PREQ, RESOLUTION);

    // attach the channel to the GPIO to be controlled
    ledcAttachPin(PIN_RED, CHANNEL_RED);
    ledcAttachPin(PIN_GREEN, CHANNEL_GREEN);
    ledcAttachPin(PIN_BLUE, CHANNEL_BLUE);

    // BLE setting
    SerialBT.begin("ESP32test");
    Serial.println("The device started, now you can pair it with bluetooth!");

    // xTaskCreatePinnedToCore(
    //     taskBLEDataHandler,  // Function that should be called
    //     "BLE data",          // Name of the task (for debugging)
    //     1024,                // Stack size (bytes)
    //     NULL,                // Parameter to pass
    //     1,                   // Task priority
    //     NULL,                // Task handle
    //     ARDUINO_RUNNING_CORE // Core you want to run the task on (0 or 1)
    // );
}

void loop()
{
    // test data
    led_mode = FADE;
    r = 255;
    g = 100;
    b = 150;

    // switch ??
    if (FLASH == led_mode)
    {
        flash_led();
    }
    else if (FADE == led_mode)
    {
        fading_led();
    }
    else if (DIMMER == led_mode)
    {
        dimming_led();
    }
    else if (COLOR_COLLECTION == led_mode)
    {
        color_collection();
    }
    else
    {
        // NONE
    }
}

void taskBLEDataHandler(void *pvParameters)
{
    if (SerialBT.available())
        debug("BLE data received: %c", SerialBT.read());
}