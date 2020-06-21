/**
 * 
 * eps32 BLE serial - led control
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
#define PIN_GREEN 15
#define PIN_BLUE 14

// PWM setting
#define PWM_PREQ 5000
#define CHANNEL_RED 0
#define CHANNEL_GREEN 1
#define CHANNEL_BLUE 2
#define RESOLUTION 8

// delay setting
// TODO: update in real
#define FADE_DELAY 400 // 40 ms
#define FLASH_DELAY 500

#ifndef LED_BUILTIN
#define LED_BUILTIN 13
#endif

// BLE message
#define END_OF_DATA_CHAR '\r'
#define MAX_BLE_MSG_LENGTH 128

typedef enum
{
    NONE = 0,
    FLASH,
    FADE,
    DIMMER,
    COLOR_COLLECTION,
    OFF
} led_mode_t;

// mode tracker
volatile led_mode_t led_mode = NONE;
bool isConnected = false;

// r,g,b is gobal so we can get and set them at anywhere in our program
int r = 0;
int g = 0;
int b = 0;

int dimming_percent = 0;

BluetoothSerial SerialBT;

#if DEBUG_MODE
SemaphoreHandle_t mutex_v;
#endif

SemaphoreHandle_t mutex_led_mode;

led_mode_t get_led_mode()
{
    led_mode_t temp;
    xSemaphoreTake(mutex_led_mode, portMAX_DELAY);
    temp = led_mode;
    xSemaphoreGive(mutex_led_mode);
    return temp;
}

void set_led_mode(led_mode_t mode)
{
    xSemaphoreTake(mutex_led_mode, portMAX_DELAY);
    led_mode = mode;
    xSemaphoreGive(mutex_led_mode);
    return;
}

int debug(const char *fmt, ...)
{
    int rc = 0;
#if DEBUG_MODE
    {
        char buffer[4096];
        va_list args;
        va_start(args, fmt);
        rc = vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);

        xSemaphoreTake(mutex_v, portMAX_DELAY);
        Serial.println(buffer);
        xSemaphoreGive(mutex_v);
    }
    return rc;
#endif
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
    set_led_mode(NONE);
}

void fading_led(void)
{
    int index = biggest(r, g, b);
    int max_loop = ((1 == index) ? r : (2 == index) ? g : b);

    // // mode 2
    for (int i = 0; i < max_loop; i++)
    {
        r = ((0 >= r) ? 0 : r - 1);
        g = ((0 >= g) ? 0 : g - 1);
        b = ((0 >= b) ? 0 : b - 1);

        set_color(r, g, b);
        delay(FADE_DELAY);
        // stop if mode changed
        if (FADE != get_led_mode())
            return;
    }

    // reset mode when it done
    set_led_mode(NONE);
}

void flash_led(void)
{
    if (FLASH == get_led_mode())
    {
        set_color(r, g, b);
        vTaskDelay(FLASH_DELAY);
        set_color(0, 0, 0);
        vTaskDelay(FLASH_DELAY);
    }
    // stop if mode changed
}

void dimming_led(void)
{
    // TODO
    set_color((int)r * dimming_percent / 100, (int)g * dimming_percent / 100, (int)b * dimming_percent / 100);
    // reset MODE
    set_led_mode(NONE);
    debug("Dimmer, reset Led mode: %d", (int)get_led_mode());
}

/*--------------------------Parse MSG---------------------------*/
void parse_lte_msg(const char *msg)
{
    // char temp[MAX_BLE_MSG_LENGTH + 1] = {0};

#ifdef __cplusplus
    char *temp = (char *)malloc(MAX_BLE_MSG_LENGTH);
#else
    char *temp = malloc(MAX_BLE_MSG_LENGTH);
#endif
    memset(temp, 0, MAX_BLE_MSG_LENGTH);
    strncpy(temp, msg, MAX_BLE_MSG_LENGTH);

    if (NULL != strstr(temp, "flash"))
    {
        set_led_mode(FLASH);
    }
    else if (NULL != strstr(temp, "fade"))
    {
        set_led_mode(FADE);
    }
    else if (NULL != strstr(temp, "dimmer"))
    {
        set_led_mode(DIMMER);
        // get_dimming_percent(); dimmer 0 .
        // FORMAT: dimmer 0 .
        char str_temp[20];
        int percent_temp;
        if (2 == sscanf(strstr(temp, "dimmer"), "%s %d", str_temp, &percent_temp))
        {
            dimming_percent = percent_temp;
            debug("Parse message, dimmer percent: %d", percent_temp);
        }
        else
            set_led_mode(NONE);
    }
    else if (NULL != strstr(temp, "color"))
    {
        set_led_mode(COLOR_COLLECTION);
        // get_dimming_percent(); dimmer 0 .
        // FORMAT: color 100 50 222 .
        char str_temp[20];
        int r_temp, g_temp, b_temp;
        if (4 == sscanf(strstr(temp, "color"), "%s %d %d %d", str_temp, &r_temp, &g_temp, &b_temp))
        {
            r = r_temp;
            g = g_temp;
            b = b_temp;
            debug("Parse message, color: %d %d %d", r_temp, g_temp, b_temp);
        }
        else
            set_led_mode(NONE);
    }
    else if (NULL != strstr(temp, "off"))
    {
        set_led_mode(OFF);
    }
    else
        ;
    // not change led mode
    // led_mode = NONE;

    debug("Parse message, Led mode: %d", (int)get_led_mode());

    free(temp);
}

// define two tasks for Blink & AnalogRead
void TaskBLE(void *pvParameters);
void taskLed(void *pvParameters);
void taskCheckMode(void *pvParameters);

// the setup function runs once when you press reset or power the board
void setup()
{

    // initialize serial communication at 115200 bits per second:
    Serial.begin(115200);

    // configure LED PWM functionalitites
    ledcSetup(CHANNEL_RED, PWM_PREQ, RESOLUTION);
    ledcSetup(CHANNEL_GREEN, PWM_PREQ, RESOLUTION);
    ledcSetup(CHANNEL_BLUE, PWM_PREQ, RESOLUTION);

    // attach the channel to the GPIO to be controlled
    ledcAttachPin(PIN_RED, CHANNEL_RED);
    ledcAttachPin(PIN_GREEN, CHANNEL_GREEN);
    ledcAttachPin(PIN_BLUE, CHANNEL_BLUE);

#if DEBUG_MODE
    mutex_v = xSemaphoreCreateMutex();
#endif
    mutex_led_mode = xSemaphoreCreateMutex();

    // BLE setting
    SerialBT.begin("ESP32test");
    debug("The device started, now you can pair it with bluetooth!");

    // Now set up two tasks to run independently.
    xTaskCreatePinnedToCore(
        TaskBLE,   //
        "TaskBLE", // A name just for humans
        5000,      // This stack size can be checked & adjusted by reading the Stack Highwater
        NULL,      //
        1,         // Priority, with 3 (configMAX_PRIORITIES - 1) being the highest, and 0 being the lowest.
        NULL,
        ARDUINO_RUNNING_CORE);

    xTaskCreatePinnedToCore(
        taskLed,   //
        "taskLed", //
        1024,      // Stack size
        NULL,      //
        1,         // Priority
        NULL,
        ARDUINO_RUNNING_CORE);

#if DEBUG_MODE
    xTaskCreatePinnedToCore(
        taskCheckMode,   //
        "taskCheckMode", //
        1024,            // Stack size
        NULL,            //
        1,               // Priority
        NULL,
        ARDUINO_RUNNING_CORE);
#endif
    // Now the task scheduler, which takes over control of scheduling individual tasks, is automatically started.
}

void loop()
{
    // Empty. Things are done in Tasks.
}

/*---------------------- Tasks ---------------------*/

void TaskBLE(void *pvParameters)
{
    (void)pvParameters;
    // char data[MAX_BLE_MSG_LENGTH] = {0};
    char *data = (char *)malloc(MAX_BLE_MSG_LENGTH);
    char inChar;
    int i = 0;

    for (;;)
    {
        if (SerialBT.available())
        {
            inChar = (char)SerialBT.read();
            // data[i] = inChar;
            // debug("%c", inChar);
            // i += 1;

            if ('\n' != inChar)
            {
                data[i] = inChar;
                i += 1;
            }

            if (END_OF_DATA_CHAR == inChar)
            {
                data[i + 1] = '\0';
                debug("BLE data received: %s", data);
                parse_lte_msg((const char *)data);
                i = 0;
            }
        }

        // max length
        if (i == (MAX_BLE_MSG_LENGTH - 2))
        {
            data[0] = '\0';
            i = 0;
        }
    }

    free(data);
}

void taskLed(void *pvParameters)
{
    (void)pvParameters;

    for (;;)
    {
        // switch ??
        if (FLASH == get_led_mode())
        {
            flash_led();
        }
        else if (FADE == get_led_mode())
        {
            fading_led();
        }
        else if (DIMMER == get_led_mode())
        {
            dimming_led();
        }
        else if (COLOR_COLLECTION == get_led_mode())
        {
            color_collection();
        }
        else if (OFF == get_led_mode())
        {
            // OFF
        }
        else
        {
            // NONE
        }
    }
}

void taskCheckMode(void *pvParameters)
{
    (void)pvParameters;
    led_mode_t old_mode = NONE;
    bool oldConnectionState = false;
    for (;;)
    {
        isConnected = SerialBT.hasClient();

        if (old_mode != get_led_mode())
        {
            old_mode = get_led_mode();
            debug("DEBUG LED MODE: %d", (int)get_led_mode());
        }
        if (oldConnectionState != isConnected)
        {
            oldConnectionState = isConnected;
            debug("BLE status: %s", (isConnected == true) ? "Connected" : "Disconnected");
        }
        vTaskDelay(500);
    }
}
