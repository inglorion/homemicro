// Usage: writecart file [start_address [length]]
#include "../gpio.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

// Pin mappings:
// gpio02: vcc
// gpio03: scl
// gpio27: sda
#define VCC_PIN 2
#define SCL_PIN 3
#define SDA_PIN 4

static void sleep_ns_impl(unsigned long ns) {
  struct timespec ts;
  int i;
  ts.tv_sec = ns / 1000000000UL;
  ts.tv_nsec = ns % 1000000000UL;
  nanosleep(&ts, NULL);
}

static void sleep_ns(unsigned long ns) {
  // sleep_ns_impl(ns * 5000000);
  // sleep_ns_impl(ns * 100);
  sleep_ns_impl(ns);
}

/** Configures the pins and sets chip enable, output enable, and write
 *  enable high (which, for those pins, means "disabled").
 */
static void configure_pins(gpio_t* gpio) {
  int i;
  gpio_functions_t functions;
  volatile gpio_functions_t *gfsel = gpio->functions;
  /* Set scl and sda high and vcc low as fast as we can. */
  set_gpio_pin_function(gfsel, VCC_PIN, GPIO_FUNC_OUTPUT);
  set_gpio_pin_low(gpio, VCC_PIN);
  set_gpio_pin_function(gfsel, SCL_PIN, GPIO_FUNC_OUTPUT);
  set_gpio_pin_high(gpio, SCL_PIN);
  set_gpio_pin_function(gfsel, SDA_PIN, GPIO_FUNC_OUTPUT);
  set_gpio_pin_high(gpio, SDA_PIN);
}

static void disable_chip(gpio_t *gpio) {
  set_gpio_pin_low(gpio, VCC_PIN);
  set_gpio_pins_high(gpio, (1 << SCL_PIN) | (1 << SDA_PIN));
}

static void enable_chip(gpio_t *gpio) {
  set_gpio_pins_high(gpio, (1 << SCL_PIN) | (1 << SDA_PIN) | (1 << VCC_PIN));
}

static uint8_t recv_byte(gpio_t *gpio) {
  int i;
  uint8_t result = 0;
  
  set_gpio_pin_high(gpio, SDA_PIN);
  set_gpio_pin_function(gpio->functions, SDA_PIN, GPIO_FUNC_INPUT);
  for (i = 0; i < 8; i++) {
    result <<= 1;
    sleep_ns(5000);
    set_gpio_pin_high(gpio, SCL_PIN);
    sleep_ns(2500);
    result |= get_gpio_pin_value(gpio, SDA_PIN);
    sleep_ns(2500);
    set_gpio_pin_low(gpio, SCL_PIN);
  }
  set_gpio_pin_function(gpio->functions, SDA_PIN, GPIO_FUNC_OUTPUT);
  return result;
}

static void send_ack(gpio_t *gpio) {
  set_gpio_pin_low(gpio, SDA_PIN);
  sleep_ns(5000);
  set_gpio_pin_high(gpio, SCL_PIN);
  sleep_ns(5000);
  set_gpio_pin_low(gpio, SCL_PIN);
}

static int send_byte(gpio_t *gpio, uint8_t value) {
  int i;
  for (i = 0; i < 8; i++) {
    set_gpio_pin_value(gpio, SDA_PIN, (value >> 7));
    sleep_ns(5000);
    set_gpio_pin_high(gpio, SCL_PIN);
    sleep_ns(5000);
    set_gpio_pin_low(gpio, SCL_PIN);
    value <<= 1;
  }
  set_gpio_pin_high(gpio, SDA_PIN);
  set_gpio_pin_function(gpio->functions, SDA_PIN, GPIO_FUNC_INPUT);
  sleep_ns(5000);
  set_gpio_pin_high(gpio, SCL_PIN);
  sleep_ns(2500);
  i = get_gpio_pin_value(gpio, SDA_PIN);
  sleep_ns(2500);
  set_gpio_pin_low(gpio, SCL_PIN);
  sleep_ns(2500);
  set_gpio_pin_function(gpio->functions, SDA_PIN, GPIO_FUNC_OUTPUT);
  sleep_ns(2500);
  return i;
}

static void send_nak(gpio_t *gpio) {
  set_gpio_pin_high(gpio, SDA_PIN);
  sleep_ns(5000);
  set_gpio_pin_high(gpio, SCL_PIN);
  sleep_ns(5000);
  set_gpio_pin_low(gpio, SCL_PIN);
}

static void restart(gpio_t *gpio) {
  set_gpio_pin_high(gpio, SDA_PIN);
  sleep_ns(8000);
  set_gpio_pin_high(gpio, SCL_PIN);
  sleep_ns(8000);
  set_gpio_pin_low(gpio, SDA_PIN);
  sleep_ns(8000);
  set_gpio_pin_low(gpio, SCL_PIN);
}

static void start(gpio_t *gpio) {
  set_gpio_pin_low(gpio, SDA_PIN);
  sleep_ns(8000);
  set_gpio_pin_low(gpio, SCL_PIN);
}

static void stop(gpio_t *gpio) {
  set_gpio_pins_low(gpio, (1 << SCL_PIN) | (1 << SDA_PIN));
  sleep_ns(8000);
  set_gpio_pin_high(gpio, SCL_PIN);
  sleep_ns(8000);
  set_gpio_pin_high(gpio, SDA_PIN);
}

int main(int argc, char *argv[]) {
  int i, n = 0;
  gpio_t gpio;
  gpio_functions_t saved_functions;
  FILE *input;
  uint32_t address = 0, address_mod = 0, bytes_to_read, length = ~0;
  /* For 24C32: 32  *
   * For 24C256: 64 */
  const uint32_t page_size = 64;
  uint8_t buffer[page_size];

  if (argc < 2) {
    fprintf(stderr, "Usage: writecart file [address [length]]\n");
    return 0x80;
  }
  input = fopen(argv[1], "rb");
  if (!input) {
    perror(argv[1]);
    return 1;
  }
  if (argc > 2) address = atol(argv[2]);
  if (argc > 3) length = atol(argv[3]);

  if (init_gpio(&gpio)) return 1;
  copy_gpio_functions(&saved_functions, gpio.functions);
  configure_pins(&gpio);
  enable_chip(&gpio);
  sleep_ns(500000000);
  for (;;) {
    start(&gpio);
    n = send_byte(&gpio, 0xa0);
    if (n) {
      fprintf(stderr, "Device did not acknowledge device address\n");
      n = 1;
      break;
    }
    n = send_byte(&gpio, (address >> 8) & 0xff);
    if (n) {
      fprintf(stderr, "Device did not acknowledge address byte 0\n");
      n = 1;
      break;
    }
    n = send_byte(&gpio, address & 0xff);
    if (n) {
      fprintf(stderr, "Device did not acknowledge address byte 1\n");
      n = 1;
      break;
    }
    bytes_to_read = page_size - (address % page_size);
    if (bytes_to_read > length) bytes_to_read = length;
    n = fread(buffer, 1, bytes_to_read, input);
    if (ferror(input)) {
      fprintf(stderr, "Error reading input - aborting\n");
      n = 1;
      break;
    }
    if (n == 0) break;
    for (i = 0; i < n; i++) {
      if (send_byte(&gpio, buffer[i])) {
        fprintf(stderr, "Device did not acknowledge data\n");
        i = ~0;
        break;
      }
    }
    if (i == ~0) {
      n = 1;
      break;
    }
    stop(&gpio);
    length -= n;
    address += n;
    for (i = 0; i < 200; i++) {
      sleep_ns(5000000);
      start(&gpio);
      n = send_byte(&gpio, 0xa1);
      (void) recv_byte(&gpio);
      send_nak(&gpio);
      stop(&gpio);
      if (!n) break;
    }
    if (n) {
      fprintf(stderr, "Device did not acknowledge write\n");
      break;
    }
    if (length == 0) break;
  }
  stop(&gpio);
  disable_chip(&gpio);
  copy_gpio_functions(gpio.functions, &saved_functions);
  fini_gpio(&gpio);
  return n;
}
