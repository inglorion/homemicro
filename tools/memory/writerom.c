// Usage: writerom [file [start_address [length]]]
#include "../gpio.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
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

/** Sector size. The code assumes this is a power of two. */
#define SECTOR_SIZE 4096
#define SECTOR_MASK ~(SECTOR_SIZE - 1)

/** Value that indicates two sectors contain the same bytes. */
#define SECTORS_EQUAL ~0

/** Minimum time between setting address and pulling we# low. */
#define ADDRESS_SETUP_NS 100
#define ERASE_SECTOR_NS 50000000
#define READ_DELAY_NS 300
#define WRITE_DELAY_NS 50000
/** Minimum time to keep we# low after setting data bits. */
#define WRITE_ENABLE_PULSE_NS 100
/** Minimum time between pulling we# low and setting data bits. */
#define WRITE_ENABLE_TO_DATA_DELAY_NS 100

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
  // sleep_ns_impl(ns * 100);
  sleep_ns_impl(ns);
}

/** Compares the first size bytes of two sectors.
 *  Returns the index of the first byte that differs, or
 *  SECTORS_EQUAL if the contents of the sectors is the same.
 */
static size_t compare_sectors(const uint8_t *a, const uint8_t *b, size_t size, size_t start) {
  size_t i;
  for (i = start; i < size; i++) {
    if (a[i] != b[i]) return i;
  }
  return SECTORS_EQUAL;
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

/** Returns true if a needs to be erased to change its contents to be.
 * This is true iff there are bits set to 1 in b that are set to 0 in a.
 */
static bool need_erase(const uint8_t *a, const uint8_t *b, size_t size) {
  size_t i;
  for (i = 0; i < size; i++) {
    if (b[i] & ~a[i]) return true;
  }
  return false;
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
  /* ce# low, oe# low, we# high */
  set_gpio_pins_high(gpio, (1 << WRITE_ENABLE_PIN));
  set_gpio_pins_low(gpio, (1 << CHIP_ENABLE_PIN) | (1 << OUTPUT_ENABLE_PIN));
  set_address(gpio, address);
  sleep_ns(READ_DELAY_NS);
  return (uint8_t) ((*gpio->levels >> FIRST_DATA_PIN) & 0xff);
}

static void read_bytes(gpio_t *gpio, uint32_t start_address, uint8_t *buffer, size_t size) {
  uint32_t address = start_address;
  size_t i;
  for (i = 0; i < size; i++) {
    buffer[i] = read_byte(gpio, address++);
  }
}

static void write_byte_aux(gpio_t *gpio, uint32_t address, uint8_t value) {
  /* ce# and we# must be high during the address transition.
     oe# must be high so that we can drive the data bus. */
  set_gpio_pins_high(gpio, (1 << CHIP_ENABLE_PIN) | (1 << WRITE_ENABLE_PIN) | (1 << OUTPUT_ENABLE_PIN));
  set_address(gpio, address);
  configure_data_pins_for_output(gpio);
  set_gpio_pin_low(gpio, CHIP_ENABLE_PIN);
  sleep_ns(ADDRESS_SETUP_NS);
  /* pull we# low, this latches the address. */
  set_gpio_pin_low(gpio, WRITE_ENABLE_PIN);
  configure_data_pins_for_output(gpio);
  set_data(gpio, value);
  sleep_ns(WRITE_ENABLE_TO_DATA_DELAY_NS);
  sleep_ns(WRITE_ENABLE_PULSE_NS);
  /* pull we# high, this latches the data and performs the write. */
  set_gpio_pin_high(gpio, WRITE_ENABLE_PIN);
  /* wait for the write to complete before returning. */
  sleep_ns(WRITE_DELAY_NS);
  configure_data_pins_for_input(gpio);
}

static void write_byte(gpio_t *gpio, uint32_t address, uint8_t value) {
  write_byte_aux(gpio, 0x5555, 0xaa);
  write_byte_aux(gpio, 0x2aaa, 0x55);
  write_byte_aux(gpio, 0x5555, 0xa0);
  write_byte_aux(gpio, address, value);
}

static void write_bytes(gpio_t *gpio, uint32_t start_address, uint8_t *data, size_t size) {
  uint32_t address = start_address;
  size_t i;
  for (i = 0; i < size; i++) {
    write_byte(gpio, address++, data[i]);
  }
}

static void erase_sector(gpio_t *gpio, uint32_t address) {
  address &= SECTOR_MASK;
  write_byte_aux(gpio, 0x5555, 0xaa);
  write_byte_aux(gpio, 0x2aaa, 0x55);
  write_byte_aux(gpio, 0x5555, 0x80);
  write_byte_aux(gpio, 0x5555, 0xaa);
  write_byte_aux(gpio, 0x2aaa, 0x55);
  write_byte_aux(gpio, address, 0x30);
  sleep_ns(ERASE_SECTOR_NS);
}

int main(int argc, char *argv[]) {
  uint32_t address, address_mod = 0, changed;
  uint8_t data;
  uint32_t start_address = 0, end_address;
  uint32_t length = 0x200;
  gpio_t gpio;
  gpio_functions_t saved_functions;
  uint32_t saved_levels;
  FILE *input = NULL;
  uint8_t buffer[SECTOR_SIZE], sector[SECTOR_SIZE];
  size_t bytes_read, difference_index;
  int attempts;

  if (argc > 2) {
    start_address = atol(argv[2]);
  }
  
  if (argc > 3) {
    length = atol(argv[3]);
  }
  end_address = start_address + length - 1;
  
  if (start_address & ~SECTOR_MASK) {
    fprintf(
        stderr,
        "start address %u is not on a sector boundary"
        " (needs to be a multiple of %u)",
        start_address, SECTOR_SIZE);
    return 1;
  }

  if (argc > 1) {
    input = fopen(argv[1], "rb");
    if (!input) {
      perror(argv[1]);
      return 1;
    }
  }
  
  if (init_gpio(&gpio)) return 1;
  copy_gpio_functions(&saved_functions, gpio.functions);
  configure_pins(&gpio);
  enable_chip(&gpio);

  address = start_address;
  while (address <= end_address) {
    /* Get next SECTOR_SIZE bytes of input. */
    if (input) {
      bytes_read = fread(buffer, 1, SECTOR_SIZE, input);
      if (ferror(input)) {
        fprintf(stderr, "Error reading input - aborting\n");
        return 1;
      }
      if (bytes_read == 0) break;
    } else {
      for (bytes_read = 0; bytes_read < SECTOR_SIZE; bytes_read++) {
        buffer[bytes_read] = (uint8_t) (bytes_read & 0xff);
      }
    }

    /* Make sure we don't process past end_address. */
    if (address + bytes_read > end_address)
      bytes_read = end_address - address + 1;
    log_debug("Read %u bytes of input\n", bytes_read);

    attempts = 3;
    for (;;) {
      /* Read an equal number of bytes from the chip. */
      log_debug("Checking contents of sector %08x\n", address);
      read_bytes(&gpio, address, sector, bytes_read);
      if (compare_sectors(sector, buffer, bytes_read, 0) == SECTORS_EQUAL) {
        log_debug("Sector %08x verified\n", address);
        break;
      } else if (attempts <= 0) {
        fprintf(stderr, "Sector %08x still incorrect; giving up\n", address);
        return 1;
      }

      if (need_erase(sector, buffer, bytes_read)) {
        log_debug("Erasing and writing sector %08x\n", address);
        erase_sector(&gpio, address);
        write_bytes(&gpio, address, buffer, bytes_read);
      } else {
        log_debug("Updating sector %08x\n", address);
        difference_index = 0;
        changed = 0;
        for (;;) {
          difference_index = compare_sectors(
              sector,
              buffer,
              bytes_read,
              difference_index);
          if (difference_index == SECTORS_EQUAL) break;
          write_byte(&gpio,
                     address + difference_index,
                     buffer[difference_index]);
          ++difference_index;
          ++changed;
        }
        log_debug("%u bytes changed\n", changed);
      }
      attempts--;
    }

    address += bytes_read;
  }

  disable_chip(&gpio);
  copy_gpio_functions(gpio.functions, &saved_functions);
  fini_gpio(&gpio);
  return 0;
}
