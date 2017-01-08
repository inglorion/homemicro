#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/** Base address for older models. */
#define RPI_GPIO_BASE 0x20200000

/** Base address for newer models. */
#define RPI2_GPIO_BASE 0x3f200000

/** Set this to RPI_GPIO_BASE or RPI2_GPIO_BASE depending on your model. */
#define GPIO_BASE RPI2_GPIO_BASE

/** How many bytes to mmap. */
#define GPIO_LENGTH 0x1000

/** Start of the GFSEL registers, relative to GPIO_BASE. */
#define GFSEL_BASE 0

/** Start of the GPSET register, relative to GPIO_BASE. */
#define GPSET_BASE 0x1c

/** Start of the GPCLR register, relative to GPIO_BASE. */
#define GPCLR_BASE 0x28

/** Start of the GPLEV register, relative to GPIO_BASE. */
#define GPLEV_BASE 0x34

/** Number of pins that can be used as GPIO pins. */
#define NUM_GPIO_PINS 28

/** Super simple debugging implementation. */
#ifdef NDEBUG
# define log_debug(FORMAT, ...)
#else
# define log_debug(FORMAT, ...) fprintf(stderr, FORMAT, __VA_ARGS__)
#endif

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

/** Copies GPIO function selection settings from src to dest. */
void copy_gpio_functions(
     volatile gpio_functions_t *dest,
     const volatile gpio_functions_t *src) {
  size_t i;
  for (i = 0; i < 3; i++) {
    dest->registers[i] = src->registers[i];
  }
}

/** Gets the function for the given GPIO pin. */
gpio_pin_function_t get_gpio_pin_function(gpio_functions_t *gfsel, int pin) {
  size_t word_offset = pin / 10;
  size_t bit_offset = (pin % 10) * 3;
  return (gfsel->registers[word_offset] >> bit_offset) & 7;
}

/**
 * @return 1 when the GPIO pin is high, 0 if low.
 */
int get_gpio_pin_value(gpio_t *gpio, int pin) {
  return (*gpio->levels >> pin) & 1;
}

/**
 * Releases resources held by the gpio_t. This should be called when we're
 * done using GPIO pins, before exiting the program. After this, the gpio_t
 * can no longer be used for anything other than init_gpio.
 */
int fini_gpio(gpio_t *gpio) {
  munmap((void*) gpio->map, GPIO_LENGTH);
  close(gpio->fd);
  return 0;
}

/**
 * Performs the required setup to start using GPIO pins and stores some relevant
 * information in the pointed-to gpio_t struct. This function must be called
 * before any of the other gpio functions can be used. It allocates resources
 * which must be released by calling fini_gpio(). init_gpio() returns 0 when
 * successful, or a nonzero value if an error occurs.
 */
int init_gpio(gpio_t *gpio) {
  volatile void *gpio_map;
  int memfd = open("/dev/mem", O_RDWR | O_SYNC);
  if (memfd == -1) {
    perror("init_gpio: /dev/mem");
    return -1;
  }
  gpio_map = mmap(NULL, GPIO_LENGTH, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, GPIO_BASE);
  if (!gpio_map) {
    perror("init_gpio: mmap");
    close(memfd);
    return -1;
  }

  gpio->fd = memfd;
  gpio->map = gpio_map;
  log_debug("GPIO mapped at %p\n", gpio_map);
  gpio->functions = (gpio_functions_t*) (gpio_map + GFSEL_BASE);
  gpio->set = (gpio_set_t*) (gpio_map + GPSET_BASE);
  gpio->clear = (gpio_clear_t*) (gpio_map + GPCLR_BASE);
  gpio->levels = (uint32_t*) (gpio_map + GPLEV_BASE);
  log_debug("GPIO levels at %p\n", gpio->levels);
  return 0;
}

/**
 * Sets the function of the given GPIO pin.
 * Not all gpio_pin_function_t values are valid for all GPIO pins.
 */
void set_gpio_pin_function(
     volatile gpio_functions_t *gfsel,
     int pin,
     gpio_pin_function_t function) {
  size_t word_offset = pin / 10;
  size_t bit_offset = (pin % 10) * 3;
  gfsel->registers[word_offset] =
       gfsel->registers[word_offset] & ~(7 << bit_offset) | (function << bit_offset);
}

/**
 * Sets a GPIO pin to its high value (logic 1).
 * The pin must have been configured as an output pin.
 */
void set_gpio_pin_high(gpio_t *gpio, int pin) {
  gpio->set->registers[0] = (1 << pin);
}

/**
 * Sets a GPIO pin to its low value (logic 0).
 * The pin must have been configured as an output pin.
 */
void set_gpio_pin_low(gpio_t *gpio, int pin) {
  gpio->clear->registers[0] = (1 << pin);
}

/**
 * Sets a GPIO pin to its high value (logic 1) if value is nonzero,
 * or to its low value (logic 0) if value is zero.
 * The pin must have been configured as an output pin.
 */
void set_gpio_pin_value(gpio_t *gpio, int pin, int value) {
  if (value == 0) set_gpio_pin_low(gpio, pin);
  else set_gpio_pin_high(gpio, pin);
}

/**
 * Sets pins to their high value (logic 1) if the corresponding bit in
 * pins is set to 1 (the least significant bit corresponds to GPIO pin
 * 0, the next bit to GPIO pin 1, and so on).  The pins must have been
 * configured as output pins.  Pins for which the corresponding bit in
 * pins is 0 are not affected.
 */
void set_gpio_pins_high(gpio_t *gpio, uint32_t pins) {
  gpio->set->registers[0] = pins;
}

/**
 * Sets pins to their low value (logic 0) if the corresponding bit in
 * pins is set to 1 (the least significant bit corresponds to GPIO pin
 * 0, the next bit to GPIO pin 1, and so on).  The pins must have been
 * configured as output pins.  Pins for which the corresponding bit in
 * pins is 0 are not affected.
 */
void set_gpio_pins_low(gpio_t *gpio, uint32_t pins) {
  gpio->clear->registers[0] = pins;
}
