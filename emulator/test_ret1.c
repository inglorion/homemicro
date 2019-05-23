#include "hm1000.h"

#include "hm1000.c"

static void push(hm1k_state *s, uint8_t value) {
  s->ram[0x100 + s->s] = value;
  --s->s;
}

static void push_return_address(hm1k_state *s, uint16_t addr) {
  --addr;
  push(s, addr >> 8);
  push(s, addr & 0xff);
}

int main(int argc, char *argv[]) {
  hm1k_state state;
  uint8_t ram[RAM_SIZE];
  uint8_t rom[ROM_SIZE];

  randomize(ram, sizeof(ram));
  randomize(rom, sizeof(rom));
  rom[0x1ffc] = 0;
  rom[0x1ffd] = 0x04;
  init_6502(&state, ram);
  state.rom = rom;
  state.io_read[SERIR - IO_BASE] = read_serir;
  state.io_write[SERCR - IO_BASE] = write_sercr;
  state.kbdrow = 0;
  memset(state.keyboard, 0xff, sizeof(state.keyboard));
  state.io_read[KBDCOL - IO_BASE] = read_kbdcol;
  state.io_write[KBDROW - IO_BASE] = write_kbdrow;
  reset(&state);

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
