#include "hm1000.h"

#include "hm1000.c"

static void push_return_address(hm1k_state *s, uint16_t addr) {
  push_u16(s, addr - 1);
}

int main(int argc, char *argv[]) {
  hm1k_state state;
  uint8_t ram[RAM_SIZE];
  uint8_t rom[ROM_SIZE];

  randomize_reset_hm1000(
      &state, ram, sizeof(ram),
      rom, sizeof(rom), 0x400);
  state.a = 0x00;
  state.s = 0xff;
  push_return_address(&state, 0xfffc);
  ram[0x0400] = 0xa9;           /* lda # */
  ram[0x0401] = 0x01;
  ram[0x0402] = 0x60;           /* rts */
  while (state.pc != 0xfffc) {
    printf("pc: %04x\n", state.pc);
    step_6502(&state);
  }
  printf("a: %02x\n", state.a);
  return 0;
}
