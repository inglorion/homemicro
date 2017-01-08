// Usage: readmem [start_address [length]]
#include "../gpio.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

// Pin mappings:
// gpio00: ce#
// gpio01: we#
// gpio02: d0
// gpio03: d1
// gpio04: d2
// gpio05: d3
// gpio06: d4
// gpio07: d5
// gpio08: d6
// gpio09: d7
// gpio10: a00
// gpio11: a01
// gpio12: a02
// gpio13: a03
// gpio14: a04
// gpio15: a05
// gpio16: a06
// gpio17: a07
// gpio18: a08
// gpio19: a09
// gpio20: a10
// gpio21: a11
// gpio22: a12
// gpio23: a13
// gpio24: a14
// gpio25: a15
// gpio26: a16
// gpio27: oe#
#define CHIP_ENABLE_PIN 0
#define WRITE_ENABLE_PIN 1
#define OUTPUT_ENABLE_PIN 27
#define FIRST_DATA_PIN 2
#define FIRST_ADDRESS_PIN 10

/** Minimum time between setting address and pulling we# low. */
#define ADDRESS_SETUP_NS 100
#define READ_DELAY_NS 300
#define WRITE_DELAY_NS 50000
/** Minimum time to keep we# low after setting data bits. */
#define WRITE_ENABLE_PULSE_NS 100
/** Minimum time between pulling we# low and setting data bits. */
#define WRITE_ENABLE_TO_DATA_DELAY_NS 50

#define log_debug(FORMAT, ...) fprintf(stderr, FORMAT, __VA_ARGS__)

static void sleep_ns_impl(unsigned long ns) {
  struct timespec ts;
  int i;
  ts.tv_sec = ns / 1000000000UL;
  ts.tv_nsec = ns % 1000000000UL;
  nanosleep(&ts, NULL);
}

static void sleep_ns(unsigned long ns) {
  // sleep_ns_impl(ns * 5000000);
  sleep_ns_impl(ns);
}

static void configure_data_pins_for_input(gpio_t *gpio) {
  int i;
  gpio_functions_t functions;
  volatile gpio_functions_t *gfsel = gpio->functions;
  copy_gpio_functions(&functions, gfsel);
  for (i = FIRST_DATA_PIN; i < FIRST_DATA_PIN + 8; i++) {
    set_gpio_pin_function(&functions, i, GPIO_FUNC_INPUT);
  }
  copy_gpio_functions(gfsel, &functions);
}

static void configure_data_pins_for_output(gpio_t *gpio) {
  int i;
  gpio_functions_t functions;
  volatile gpio_functions_t *gfsel = gpio->functions;
  copy_gpio_functions(&functions, gfsel);
  for (i = FIRST_DATA_PIN; i < FIRST_DATA_PIN + 8; i++) {
    set_gpio_pin_function(&functions, i, GPIO_FUNC_OUTPUT);
  }
  copy_gpio_functions(gfsel, &functions);
}

/** Configures the pins and sets chip enable, output enable, and write
 *  enable high (which, for those pins, means "disabled").
 */
static void configure_pins(gpio_t* gpio) {
  int i;
  gpio_functions_t functions;
  volatile gpio_functions_t *gfsel = gpio->functions;
  /* Set ce#, we#, and oe# as fast as we can. */
  set_gpio_pin_function(gfsel, CHIP_ENABLE_PIN, GPIO_FUNC_OUTPUT);
  set_gpio_pin_high(gpio, CHIP_ENABLE_PIN);
  set_gpio_pin_function(gfsel, WRITE_ENABLE_PIN, GPIO_FUNC_OUTPUT);
  set_gpio_pin_high(gpio, WRITE_ENABLE_PIN);
  set_gpio_pin_function(gfsel, OUTPUT_ENABLE_PIN, GPIO_FUNC_OUTPUT);
  set_gpio_pin_high(gpio, OUTPUT_ENABLE_PIN);

  configure_data_pins_for_input(gpio);
  copy_gpio_functions(&functions, gfsel);
  for (i = FIRST_ADDRESS_PIN; i < FIRST_ADDRESS_PIN + 17; i++) {
    set_gpio_pin_function(&functions, i, GPIO_FUNC_OUTPUT);
  }
  copy_gpio_functions(gfsel, &functions);
}

static void disable_chip(gpio_t *gpio) {
  set_gpio_pins_high(
    gpio,
    (1 << CHIP_ENABLE_PIN) | (1 << OUTPUT_ENABLE_PIN) | (1 << WRITE_ENABLE_PIN));
}  

static void enable_chip(gpio_t *gpio) {
  set_gpio_pins_high(gpio, (1 << WRITE_ENABLE_PIN) | (1 << OUTPUT_ENABLE_PIN));
  set_gpio_pins_low(gpio, (1 << CHIP_ENABLE_PIN));
}  

static void disable_writing(gpio_t *gpio) {
  set_gpio_pins_high(gpio, (1 << WRITE_ENABLE_PIN));
}

static void enable_writing(gpio_t *gpio) {
  set_gpio_pins_low(gpio, (1 << WRITE_ENABLE_PIN));
}

static void set_address(gpio_t *gpio, uint32_t address) {
  set_gpio_pins_low(gpio, (~address & 0x1ffff) << FIRST_ADDRESS_PIN);
  set_gpio_pins_high(gpio, (address & 0x1ffff) << FIRST_ADDRESS_PIN);
}

static void set_data(gpio_t *gpio, uint8_t data) {
  set_gpio_pins_low(gpio, (~data & 0xff) << FIRST_DATA_PIN);
  set_gpio_pins_high(gpio, data << FIRST_DATA_PIN);
}

static void set_address_and_data(gpio_t *gpio, uint32_t address, uint8_t data) {
  set_gpio_pins_low(gpio,
                    ((~address & 0x1ffff) << FIRST_ADDRESS_PIN) |
                    ((~data & 0xff) << FIRST_DATA_PIN));
  set_gpio_pins_high(gpio,
                     ((address & 0x1ffff) << FIRST_ADDRESS_PIN) |
                     ((data & 0xff) << FIRST_DATA_PIN));
}

static uint8_t read_byte(gpio_t *gpio, uint32_t address) {
  configure_data_pins_for_input(gpio);
  /* ce# low, ce2 high, oe# low, we# high */
  set_gpio_pins_high(gpio, (1 << WRITE_ENABLE_PIN));
  set_gpio_pins_low(gpio, (1 << CHIP_ENABLE_PIN) | (1 << OUTPUT_ENABLE_PIN));
  set_address(gpio, address);
  sleep_ns(READ_DELAY_NS);
  return (uint8_t) ((*gpio->levels >> FIRST_DATA_PIN) & 0xff);
}

static void write_byte(gpio_t *gpio, uint32_t address, uint8_t value) {
  /* ce# and we# must be high during the address transition.
     oe# must be high so that we can drive the data bus. */
  set_gpio_pins_high(gpio, (1 << CHIP_ENABLE_PIN) | (1 << WRITE_ENABLE_PIN) | (1 << OUTPUT_ENABLE_PIN));
  set_address(gpio, address);
  set_gpio_pin_low(gpio, CHIP_ENABLE_PIN);
  sleep_ns(ADDRESS_SETUP_NS);
  /* pull we# low, this latches the address. */
  set_gpio_pin_low(gpio, WRITE_ENABLE_PIN);
  sleep_ns(WRITE_ENABLE_TO_DATA_DELAY_NS);
  configure_data_pins_for_output(gpio);
  set_data(gpio, value);
  sleep_ns(WRITE_ENABLE_PULSE_NS);
  /* pull we# high, this latches the data and performs the write. */
  set_gpio_pin_high(gpio, WRITE_ENABLE_PIN);
  /* wait for the write to complete before returning. */
  sleep_ns(WRITE_DELAY_NS);
  configure_data_pins_for_input(gpio);
}

int main(int argc, char *argv[]) {
  uint32_t address, address_mod = 0;
  uint8_t data;
  uint32_t start_address = 0, length = 0x200;
  uint32_t end_address;
  gpio_t gpio;
  gpio_functions_t saved_functions;
  uint32_t saved_levels;

  if (argc > 1) start_address = atol(argv[1]);
  if (argc > 2) length = atol(argv[2]);
  end_address = start_address + length - 1;
  
  if (init_gpio(&gpio)) return 1;
  copy_gpio_functions(&saved_functions, gpio.functions);
  configure_pins(&gpio);
  enable_chip(&gpio);

  for (address = start_address; address <= end_address; address++) {
    if (address_mod == 0) printf("%08x:", address);
    printf(" %02x", read_byte(&gpio, address));
    address_mod++;
    if (address_mod >= 16) {
      address_mod = 0;
      printf("\n");
    }
  }
  if (address_mod != 0) printf("\n");

  disable_chip(&gpio);
  copy_gpio_functions(gpio.functions, &saved_functions);
  fini_gpio(&gpio);
  return 0;
}
