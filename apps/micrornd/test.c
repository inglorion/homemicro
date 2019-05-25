#include "hm1000.h"

#include "hm1000.c"

#define START 0x0400

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
  int i;
  hm1k_state state;
  uint8_t ram[RAM_SIZE];
  uint8_t rom[ROM_SIZE];
  FILE *f;

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

  f = fopen("test.bin", "rb");
  if (!f) {
    perror("test.bin");
    fprintf(stderr, "Could not open test.bin. Have you run make?\n");
    return 1;
  }
  fread(&ram[START], 1, RAM_SIZE - START, f);
  fclose(f);

  state.s = 0xff;
  for (i = 0; i < 0x1000000; i++) {
    push_return_address(&state, 0xfffc);
    state.pc = START;
    while (state.pc != 0xfffc) {
      step_6502(&state);
    }
    printf("%c", state.a);
  }
  return 0;
}
