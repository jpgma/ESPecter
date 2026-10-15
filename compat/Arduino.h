#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
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

/* [yield] Cede a CPU para o FreeRTOS. */
static inline void yield(void)
{
    taskYIELD();
}

/* [delay] Bloqueia a task por ms milissegundos. */
static inline void delay(unsigned long ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/* [delayMicroseconds] Espera us microssegundos. */
static inline void delayMicroseconds(unsigned int us)
{
    esp_rom_delay_us(us);
}

/* [millis] Devolve os milissegundos desde o boot. */
static inline unsigned long millis(void)
{
    return (unsigned long)(esp_timer_get_time() / 1000ULL);
}

/* [micros] Devolve os microssegundos desde o boot. */
static inline unsigned long micros(void)
{
    return (unsigned long)esp_timer_get_time();
}

typedef uint8_t byte;
typedef bool boolean;

/* [especter_pin_mode] Configura o GPIO como INPUT, OUTPUT ou INPUT_PULLUP. */
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

/* [especter_digital_write] Escreve HIGH ou LOW no GPIO. */
static inline void especter_digital_write(int pin, int value)
{
    if (pin < 0 || pin > 48) {
        return;
    }
    gpio_set_level(static_cast<gpio_num_t>(pin), value ? 1 : 0);
}

/* [especter_digital_read] Lê o GPIO. pin inválido devolve HIGH. */
static inline int especter_digital_read(int pin)
{
    if (pin < 0 || pin > 48) {
        return HIGH;
    }
    return gpio_get_level(static_cast<gpio_num_t>(pin));
}

/* [pinMode] Configura o GPIO. Delega a [especter_pin_mode]. */
static inline void pinMode(int pin, int mode)
{
    especter_pin_mode(pin, mode);
}

/* [digitalWrite] Escreve HIGH ou LOW no GPIO. Delega a [especter_digital_write]. */
static inline void digitalWrite(int pin, int value)
{
    especter_digital_write(pin, value);
}

/* [digitalRead] Lê o GPIO. Delega a [especter_digital_read]. */
static inline int digitalRead(int pin)
{
    return especter_digital_read(pin);
}

struct HardwareSerial {
    /* [HardwareSerial::begin] Ignora o baud. O log já sai pelo USB Serial/JTAG. */
    void begin(unsigned long baud)
    {
        (void)baud;
    }

    /* [HardwareSerial::print] Escreve o texto no stdout. */
    size_t print(const char *text)
    {
        return text == nullptr ? 0u : static_cast<size_t>(printf("%s", text));
    }

    /* [HardwareSerial::println] Escreve o texto e uma quebra de linha. */
    size_t println(const char *text)
    {
        return text == nullptr ? static_cast<size_t>(printf("\n")) : static_cast<size_t>(printf("%s\n", text));
    }

    /* [HardwareSerial::printf] Escreve no stdout com o formato de printf. */
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

struct EspClass {
    /* [EspClass::getFreeHeap] Devolve o heap 8-bit livre, em bytes. */
    uint32_t getFreeHeap()
    {
        return static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_8BIT));
    }

    /* [EspClass::getHeapSize] Devolve o tamanho total do heap 8-bit, em bytes. */
    uint32_t getHeapSize()
    {
        return static_cast<uint32_t>(heap_caps_get_total_size(MALLOC_CAP_8BIT));
    }
};

inline EspClass ESP;
