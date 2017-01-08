/* Sets all GPIO pins to 0. */
#include "gpio.h"

#define NUM_GPIO_PINS 27

int main(int argc, char *argv[]) {
  gpio_t gpio;
  int i;
  volatile gpio_functions_t gfsel;
  
  init_gpio(&gpio);
  copy_gpio_functions(&gfsel, gpio.functions);
  for (i = 0; i < (NUM_GPIO_PINS + 1); i++) {
    set_gpio_pin_function(&gfsel, i, GPIO_FUNC_OUTPUT);
  }
  copy_gpio_functions(gpio.functions, &gfsel);
  set_gpio_pins_low(&gpio, (1 << (NUM_GPIO_PINS + 1)) - 1);
  fini_gpio(&gpio);
  return 0;
}
