#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>

/** Pin function values. */
typedef enum {
  GPIO_FUNC_INPUT = 0,
  GPIO_FUNC_OUTPUT = 1,
  GPIO_FUNC_ALT5 = 2,
  GPIO_FUNC_ALT4 = 3,
  GPIO_FUNC_ALT0 = 4,
  GPIO_FUNC_ALT1 = 5,
  GPIO_FUNC_ALT2 = 6,
  GPIO_FUNC_ALT3 = 7,
} gpio_pin_function_t;

/** Function selection settings for the GPIO pins. */
typedef struct gpio_functions {
  uint32_t registers[3];
} gpio_functions_t;

/**
 * Type representing the GPCLR register.
 * We wrap this in a struct to allow the type checker to
 * distinguish it from the GPSET register.
 */
typedef struct gpio_clear {
  uint32_t registers[1];
} gpio_clear_t;

/**
 * Type representing the GPSET register.
 * We wrap this in a struct to allow the type checker to
 * distinguish it from the GPCLR register.
 */
typedef struct gpio_set {
  uint32_t registers[1];
} gpio_set_t;

/**
 * Structure holding all the information we need to work with
 * the GPIO pins. This is used as a convenience so that we don't
 * have to pass the individual values separately when calling
 * the gpio functions.
 */
typedef struct gpio {
  int fd;
  volatile void *map;
  volatile gpio_functions_t *functions;
  volatile gpio_clear_t *clear;
  volatile gpio_set_t *set;
  volatile uint32_t *levels;
} gpio_t;

void copy_gpio_functions(
       volatile gpio_functions_t *dest,
       const volatile gpio_functions_t *src);
gpio_pin_function_t get_gpio_pin_function(gpio_functions_t *gfsel, int pin);
int get_gpio_pin_value(gpio_t *gpio, int pin);
int fini_gpio(gpio_t *gpio);
int init_gpio(gpio_t *gpio);
void set_gpio_pin_function(
       volatile gpio_functions_t *gfsel,
       int pin,
       gpio_pin_function_t function);
void set_gpio_pin_high(gpio_t *gpio, int pin);
void set_gpio_pin_low(gpio_t *gpio, int pin);
void set_gpio_pin_value(gpio_t *gpio, int pin, int value);
void set_gpio_pins_high(gpio_t *gpio, uint32_t pins);
void set_gpio_pins_low(gpio_t *gpio, uint32_t pins);

#endif /* ndef GPIO_H */
