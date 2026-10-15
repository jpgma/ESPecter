#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define HIGH 0x1
#define LOW 0x0
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05

#define PROGMEM
#define PSTR(value) (value)
#define F(value) (value)
#define pgm_read_byte(address) (*(const uint8_t *)(address))
#define pgm_read_word(address) (*(const uint16_t *)(address))
#define pgm_read_dword(address) (*(const uint32_t *)(address))
#define pgm_read_ptr(address) (*(const void **)(address))

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

#define yield() taskYIELD()
#define delay(ms) vTaskDelay(pdMS_TO_TICKS(ms))
#define delayMicroseconds(us) esp_rom_delay_us(us)
#define millis() ((unsigned long)(esp_timer_get_time() / 1000ULL))
#define micros() ((unsigned long)esp_timer_get_time())

typedef uint8_t byte;
typedef bool boolean;

static inline void especter_pin_mode(int pin, int mode)
{
    gpio_config_t cfg = {};

    if (pin < 0 || pin > 48) {
        return;
    }
    cfg.pin_bit_mask = 1ULL << pin;
    cfg.mode = (mode == OUTPUT) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT;
    cfg.pull_up_en = (mode == INPUT_PULLUP) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
}

static inline void especter_digital_write(int pin, int value)
{
    if (pin < 0 || pin > 48) {
        return;
    }
    gpio_set_level(static_cast<gpio_num_t>(pin), value ? 1 : 0);
}

static inline int especter_digital_read(int pin)
{
    if (pin < 0 || pin > 48) {
        return HIGH;
    }
    return gpio_get_level(static_cast<gpio_num_t>(pin));
}

#define pinMode(pin, mode) especter_pin_mode((pin), (mode))
#define digitalWrite(pin, value) especter_digital_write((pin), (value))
#define digitalRead(pin) especter_digital_read((pin))

struct HardwareSerial {
    void begin(unsigned long baud)
    {
        (void)baud;
    }

    size_t print(const char *text)
    {
        return text == nullptr ? 0u : static_cast<size_t>(printf("%s", text));
    }

    size_t println(const char *text)
    {
        return text == nullptr ? static_cast<size_t>(printf("\n")) : static_cast<size_t>(printf("%s\n", text));
    }

    int printf(const char *fmt, ...)
    {
        va_list args;
        int wrote;

        va_start(args, fmt);
        wrote = vprintf(fmt, args);
        va_end(args);
        return wrote;
    }
};

inline HardwareSerial Serial;
