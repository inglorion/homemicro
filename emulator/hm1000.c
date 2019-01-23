#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xcb/xcb.h>

#include <errno.h>

// [x] Display graphics.
// [ ] Handle window close.
// [ ] Turn instructions into a table.
// [x] Change timing to ticks. 14.31818 million ticks per second.
//     This allows simulated time.

/// To avoid spending excessive CPU time on sleep system calls, we
/// synchronize real time to clock cycles in steps of 32.
#define TIME_STEP_TICKS 32

/// Nanoseconds per time step. This is rounded, but within 50ppm of
/// what the value should nominally be. Since real world crystals don't
/// generate their exact nominal frequency either, this should be fine.
#define TIME_STEP_NS 17879

/// The screen is redrawed every this many nanoseconds.
/// This is nominally 14593335, but adjusted to match TIME_STEP_NS's
/// deviation from the nominal value.
#define REDRAW_NS 14593035

/// Start of memory-mapped I/O.
#define IO_BASE 0xd000

/// Amount of RAM.
#define RAM_SIZE 0xc000

/// Starting address of ROM.
#define ROM_BASE 0xe000

#define ROM_SIZE 0x2000

#define KBDCOL 0xd002
#define KBDROW 0xd003
#define SERIR 0xd004
#define SERCR 0xd005

#define SERCR_SCL 0x40
#define SERCR_SDA 0x80

#define TWI_ADDR_SET 0x10
#define TWI_BITS_LEFT(X) (X & 7)

#define FATALF(FMT, ...) { fprintf(stderr, FMT "\n", __VA_ARGS__); exit(1); }
#define FATAL(MSG) FATALF("%s", MSG)

#define POS(ROW, COL) (ROW | ((COL) << 3))
#define KEY_BS POS(4, 1)
#define KEY_CTL POS(2, 0)
#define KEY_TAB POS(1, 1)
#define KEY_RET POS(6, 1)
#define KEY_ESC POS(0, 0)
#define KEY_SH POS(2, 7)
#define KEY_SPC POS(3, 7)
#define KEY_LEFT POS(7, 3)
#define KEY_UP POS(7, 4)
#define KEY_DOWN POS(7, 5)
#define KEY_RIGHT POS(7, 6)
#define KEY_0 POS(4, 3)
#define KEY_1 POS(0, 1)
#define KEY_2 POS(0, 2)
#define KEY_3 POS(0, 3)
#define KEY_4 POS(0, 4)
#define KEY_5 POS(0, 5)
#define KEY_6 POS(0, 6)
#define KEY_7 POS(4, 6)
#define KEY_8 POS(4, 5)
#define KEY_9 POS(4, 4)
#define KEY_BACKTICK POS(1, 0)
#define KEY_MINUS POS(4, 2)
#define KEY_EQUALS POS(4, 0)
#define KEY_LBRACKET POS(5, 2)
#define KEY_RBRACKET POS(5, 0)
#define KEY_BACKSLASH POS(5, 1)
#define KEY_SEMICOLON POS(7, 2)
#define KEY_QUOTE POS(6, 0)
#define KEY_COMMA POS(3, 0)
#define KEY_PERIOD POS(7, 0)
#define KEY_SLASH POS(7, 1)
#define KEY_A POS(2, 1)
#define KEY_B POS(3, 5)
#define KEY_C POS(3, 3)
#define KEY_D POS(2, 4)
#define KEY_E POS(1, 3)
#define KEY_F POS(2, 5)
#define KEY_G POS(2, 6)
#define KEY_H POS(5, 6)
#define KEY_I POS(6, 4)
#define KEY_J POS(6, 5)
#define KEY_K POS(6, 3)
#define KEY_L POS(6, 2)
#define KEY_M POS(6, 6)
#define KEY_N POS(3, 6)
#define KEY_O POS(5, 4)
#define KEY_P POS(5, 3)
#define KEY_Q POS(1, 2)
#define KEY_R POS(1, 4)
#define KEY_S POS(2, 3)
#define KEY_T POS(1, 5)
#define KEY_U POS(5, 5)
#define KEY_V POS(3, 4)
#define KEY_W POS(2, 2)
#define KEY_X POS(3, 2)
#define KEY_Y POS(1, 6)
#define KEY_Z POS(3, 1)
/* Maps keysyms to hm1k keyboard codes.
 * For example, keysym 13 (return) maps to row 6, column 1, which we
 * encode as 6 | (1 << 3).
 * Codes for which we don't have a key are marked as 0xff.
 */
/* TODO: Add control, shift, and the arrow keys. */
const uint8_t key_map[] = {
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  KEY_BS, KEY_TAB, KEY_RET, 0xff, 0xff, KEY_RET, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, KEY_ESC, 0xff, 0xff, 0xff, 0xff,
  KEY_SPC, KEY_1, KEY_QUOTE, KEY_3,
  KEY_4, KEY_5, KEY_7, KEY_QUOTE,
  KEY_9, KEY_0, KEY_8,
  KEY_EQUALS, KEY_COMMA, KEY_MINUS, KEY_PERIOD,
  KEY_SLASH, KEY_0, KEY_1, KEY_2,
  KEY_3, KEY_4, KEY_5, KEY_6,
  KEY_7, KEY_8, KEY_9,
  KEY_SEMICOLON, KEY_SEMICOLON, KEY_COMMA, KEY_EQUALS,
  KEY_PERIOD, KEY_SLASH, KEY_2, KEY_A,
  KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
  KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q,
  KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y,
  KEY_Z, KEY_LBRACKET, KEY_BACKSLASH, KEY_RBRACKET,
  KEY_6, KEY_MINUS, KEY_BACKTICK, KEY_A,
  KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
  KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q,
  KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y,
  KEY_Z, KEY_LBRACKET, KEY_BACKSLASH, KEY_RBRACKET,
  KEY_BACKTICK, 0xff,
};

#define FLAG_C 1
#define FLAG_Z 2
#define FLAG_I 4
#define FLAG_D 8
#define FLAG_B 16
#define FLAG_V 64
#define FLAG_N 128

typedef struct hm1k_state_s hm1k_state;
typedef uint8_t (*hm1k_read_byte_fn) (hm1k_state*, uint16_t);
typedef void (*hm1k_write_byte_fn) (hm1k_state*, uint16_t, uint8_t);
typedef void (*hm1k_op_fn) (hm1k_state*);

struct hm1k_state_s {
  uint8_t a, p, s, x, y;
  uint16_t pc;
  uint8_t *cartridge;
  uint8_t cartridge_bits;
  size_t cartridge_addr;
  size_t cartridge_size;
  uint8_t *ram;
  uint8_t *rom;
  hm1k_read_byte_fn io_read[0x1000];
  hm1k_write_byte_fn io_write[0x1000];
  uint8_t kbdrow, sercr, serir, twi_addr, twi_status;
  uint8_t keyboard[8];
  struct timespec last_sync, next_redraw;
  unsigned long ticks;
};

typedef void (*hm1k_op) (hm1k_state *s, uint8_t op);

inline static void add_ticks(hm1k_state *s, unsigned long ticks) {
  s->ticks += ticks;
}

/** Wait until real time agrees with the number of CPU cycles we
 * have counted.
 * This updates s->last_sync to the real time synchronized to, and
 * s->ticks to the remaining ticks not accounted for.
 */
static void sync_time(hm1k_state *s) {
  if (s->ticks < TIME_STEP_TICKS) return;
  unsigned long new_nsec = s->last_sync.tv_nsec +
    (s->ticks / TIME_STEP_TICKS) * TIME_STEP_NS;
  if (new_nsec > 999999999UL) {
    s->last_sync.tv_sec += new_nsec / 1000000000UL;
    new_nsec = new_nsec % 1000000000UL;
  }
  s->last_sync.tv_nsec = new_nsec;
  int n = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,
                          &s->last_sync, NULL);
  if (n) {
    fprintf(stderr, "clock_nanosleep: %s\n", strerror(n));
  }
  s->ticks = s->ticks % TIME_STEP_TICKS;
}

static void randomize(void *start, size_t len) {
  size_t i;
  for (i = 0; i < len; ++i) {
    ((char*) start)[i] = rand();
  }
}

static uint8_t io_read_default(hm1k_state *s, uint16_t addr) {
  fprintf(stderr, "Read from unpopulated I/O address %04x\n", addr);
  return rand();
}

static void io_write_default(hm1k_state *s, uint16_t addr, uint8_t val) {
  // fprintf(stderr, "Write %02x to unpopulated I/O address %04x\n", val, addr);
}

static uint8_t read_kbdcol(hm1k_state *s, uint16_t addr) {
  return s->keyboard[s->kbdrow];
}

static uint8_t read_serir(hm1k_state *s, uint16_t addr) {
  return s->serir;
}

static void handle_new_sercr(hm1k_state *s, uint8_t val) {
#define set_sda_low() { s->serir &= 0xfe; }
  /* Nothing interesting happens when SCL goes low or stays low. */
  if ((val & SERCR_SCL) == 0) return;

  const bool scl_rising = (s->sercr & SERCR_SCL) == 0;
  if (scl_rising) {
    /* Rising SCL means we're clocking in a bit. */
    const bool addr_set = s->twi_status & TWI_ADDR_SET;
    const bool waiting_for_ack = (s->twi_status & 15) == 7;
    /* Shift incoming bit into serir. We start by setting it to the value
     * of the top bit in sercr, but allow clients to pull it low. */
    s->serir = (s->serir << 1) | (s->sercr >> 7);
    if (waiting_for_ack) {
      if (s->cartridge && s->twi_addr >> 1 == 0x50) {
        if (addr_set) {
          /* Acknowledge address sent to cartridge. */
          if (s->twi_addr == 0xa0) set_sda_low();
        } else  {
          /* Acknowledge cartridge addresses. */
          set_sda_low();
          s->twi_status |= TWI_ADDR_SET;
          if (s->twi_addr == 0xa0) {
            s->cartridge_addr = 0;
            s->cartridge_bits = 0;
          }
        }
      }
      s->twi_status |= 8;
    } else {
      if (addr_set && s->twi_addr == 0xa1) {
        static uint8_t data = 0;
        if (s->cartridge_bits == 0) {
          data = s->cartridge[s->cartridge_addr];
          // printf("cartridge %08x: %02x\n", s->cartridge_addr, data);
          s->cartridge_addr = (s->cartridge_addr + 1) % s->cartridge_size;
          s->cartridge_bits = 8;
        }
        s->serir &= 0xfe | (data >> 7);
        data <<= 1;
        --s->cartridge_bits;
      }
      if ((s->twi_status & 7) == 0) {
        if (addr_set) {
          if (s->twi_addr == 0xa0)
            s->cartridge_addr = (s->cartridge_addr << 8) | s->serir;
        } else {
          s->twi_addr = s->serir;
        }
      }
      --s->twi_status;
    }
  } else {
    /* SCL was high and continues to be high. */
    const uint8_t new_sda = val & SERCR_SDA;
    /* If SDA is unchanged, nothing interesting happens. */
    if (new_sda == (s->sercr & SERCR_SDA)) return;
    if (new_sda == 0) {
      /* Start condition. */
      /* Address not set, expecting 8 data bits and an ack. */
      s->twi_status = 15;
    } else {
      /* Stop condition. */
      s->twi_status = 0;
    }
  }
#undef set_sda_low
}

/* Sets s->kbdrow based on index of first unset bit in val. */
static void write_kbdrow(hm1k_state *s, uint16_t addr, uint8_t val) {
  uint8_t n = 0;
  if (val == 0xff) return;
  while (val & 1) {
    ++n;
    val >>= 1;
  }
  s->kbdrow = n;
}

static void write_sercr(hm1k_state *s, uint16_t addr, uint8_t val) {
  handle_new_sercr(s, val);
  s->sercr = val;
}

static uint8_t load_u8_nosync(hm1k_state *s, uint16_t addr) {
  if (addr < RAM_SIZE) return s->ram[addr];
  if (addr >= ROM_BASE) return s->rom[addr - ROM_BASE];
  return s->io_read[addr & 0x0fff](s, addr);
}

static uint8_t load_u8(hm1k_state *s, uint16_t addr) {
  sync_time(s);
  return load_u8_nosync(s, addr);
}

static uint16_t load_u16(hm1k_state *s, uint16_t addr) {
  sync_time(s);
  return (uint16_t) load_u8_nosync(s, addr) |
    (uint16_t) load_u8_nosync(s, addr + 1) << 8;
}

static void store_u8(hm1k_state *s, uint16_t addr, uint8_t val) {
  sync_time(s);
  if (addr < RAM_SIZE) {
    s->ram[addr] = val;
    return;
  }
  if (addr < ROM_BASE) s->io_write[addr & 0x0fff](s, addr, val);
}

static void init_6502(hm1k_state *s, uint8_t *data) {
 size_t i;
  randomize(s, sizeof(hm1k_state));
  s->ram = data;
  s->cartridge = NULL;
  s->twi_status = 0;
  for (i = 0; i < 0x1000; i++) {
    s->io_read[i] = io_read_default;
  }
  for (i = 0; i < 0x1000; i++) {
    s->io_write[i] = io_write_default;
  }
  clock_gettime(CLOCK_MONOTONIC, &s->last_sync);
  s->next_redraw = s->last_sync;
  s->ticks = 0;
}

static void reset(hm1k_state *s) {
  add_ticks(s, 7);
  sync_time(s);
  s->a = rand();
  s->p = rand();
  s->s = rand();
  s->x = rand();
  s->y = rand();
  /* b and i are set, rest undefined. */
  s->p |= 0x34;
  s->pc = load_u16(s, 0xfffc);
}

static uint8_t set_nz(hm1k_state *s, uint8_t val) {
  s->p &= 0x7d;
  s->p |= (val & 0x80) | ((val == 0) ? FLAG_Z : 0);
  return val;
}

static uint8_t set_nvz(hm1k_state *s, uint8_t a, uint8_t b, uint8_t c) {
  s->p &= 0x3d;
  s->p |=
    ((((a & 0x80) ^ (c & 0x80)) & ((a & 0x80) ^ (b & 0x80))) >> 1) |
    (c & 0x80) | ((c == 0) ? FLAG_Z : 0);
  return c;
}

typedef uint16_t (*hm1k_mode) (hm1k_state*);

static uint16_t mode_abs(hm1k_state *s) {
  uint16_t result = load_u16(s, s->pc);
  s->pc += 2;
  return result;
}

static uint16_t mode_absx(hm1k_state *s) {
  return mode_abs(s) + s->x;
}

static uint16_t mode_absy(hm1k_state *s) {
  return mode_abs(s) + s->y;
}

static uint16_t mode_imm(hm1k_state *s) {
  return s->pc++;
}

static uint16_t mode_indx(hm1k_state *s) {
  return load_u16(s, (load_u8(s, s->pc++) + s->x) & 0xff);
}

static uint16_t mode_indy(hm1k_state *s) {
  return load_u16(s, load_u8(s, s->pc++)) + s->y;
}

static uint16_t mode_noi(hm1k_state *s) {
  FATALF("Unimplemented mode for opcode %02x at $%04x",
         load_u8(s, s->pc - 1), s->pc - 1);
  return 0;
}

static uint16_t mode_zp(hm1k_state *s) {
  return load_u8(s, s->pc++);
}

static uint16_t mode_zpi(hm1k_state *s) {
  return load_u16(s, s->pc++);
}

static uint16_t mode_zpx(hm1k_state *s) {
  return (load_u8(s, s->pc++) + s->x) & 0xff;
}

static uint16_t mode_zpy(hm1k_state *s) {
  return (load_u8(s, s->pc++) + s->y) & 0xff;
}

static const hm1k_mode modes_a[32] = {
  mode_abs,
  mode_indx,
  mode_noi,
  mode_noi,

  mode_zp,
  mode_zp,
  mode_zp,
  mode_zp,

  mode_noi,
  mode_imm,
  mode_noi,
  mode_noi,

  mode_abs,
  mode_abs,
  mode_abs,
  mode_noi,

  mode_noi,
  mode_indy,
  mode_zpi,
  mode_noi,

  mode_zp,
  mode_zpx,
  mode_zpx,
  mode_zp,

  mode_noi,
  mode_absy,
  mode_noi,
  mode_noi,

  mode_abs,
  mode_absx,
  mode_absx,
  mode_noi,
};

static const hm1k_mode modes_inc[8] = {
  mode_zp,
  mode_abs,
  mode_zpx,
  mode_absx,
};

static const hm1k_mode modes_x[8] = {
  mode_imm,
  mode_zp,
  mode_noi,
  mode_abs,
  mode_noi,
  mode_zpy,
  mode_noi,
  mode_absx,
};

static const hm1k_mode modes_y[8] = {
  mode_imm,
  mode_zp,
  mode_noi,
  mode_abs,
  mode_noi,
  mode_zpx,
  mode_noi,
  mode_absx,
};

static void cmp_impl(hm1k_state *s, uint8_t a, uint8_t b) {
  uint16_t result = (uint16_t) a + 0x100 - b;
  s->p = (s->p & 0xfe) | (result >> 8);
  set_nz(s, (uint8_t) result);
}

#define OP(NAME) static void op_ ## NAME (hm1k_state *s, uint8_t op)
OP(adc) {
  uint8_t b = load_u8(s, modes_a[op & 0x1f](s));
  uint16_t result = (uint16_t) s->a + b + (s->p & 1);
  s->p = (s->p & 0xfe) | (result >> 8);
  s->a = set_nvz(s, s->a, ~b, (uint8_t) result);
}

OP(and) {
  s->a = set_nz(s, s->a & load_u8(s, modes_a[op & 0x1f](s)));
}

OP(asl) {
  uint16_t addr = modes_a[op & 0x1f](s);
  uint16_t result = (uint16_t) load_u8(s, addr) << 1;
  s->p = (s->p & 0xfe) | (result >> 8);
  store_u8(s, addr, set_nz(s, (uint8_t) result));
}

OP(asla) {
  uint16_t result = (uint16_t) s->a << 1;
  s->p = (s->p & 0xfe) | (result >> 8);
  s->a = set_nz(s, (uint8_t) result);
}

OP(bcc) {
  int8_t off = (int8_t) load_u8(s, mode_imm(s));
  if ((s->p & FLAG_C) == 0) s->pc += off;
}

OP(bcs) {
  int8_t off = (int8_t) load_u8(s, mode_imm(s));
  if ((s->p & FLAG_C) != 0) s->pc += off;
}

OP(beq) {
  int8_t off = (int8_t) load_u8(s, mode_imm(s));
  if ((s->p & FLAG_Z) != 0) s->pc += off;
}

OP(bmi) {
  int8_t off = (int8_t) load_u8(s, mode_imm(s));
  if ((s->p & FLAG_N) != 0) s->pc += off;
}

OP(bne) {
  int8_t off = (int8_t) load_u8(s, mode_imm(s));
  if ((s->p & FLAG_Z) == 0) s->pc += off;
}

OP(bpl) {
  int8_t off = (int8_t) load_u8(s, mode_imm(s));
  if ((s->p & FLAG_N) == 0) s->pc += off;
}

OP(brk) {
  s->p = (s->p & 0x80) | 0x14;
  s->pc = load_u16(s, 0xfffe);
}

OP(bvc) {
  int8_t off = (int8_t) load_u8(s, mode_imm(s));
  if ((s->p & FLAG_V) == 0) s->pc += off;
}

OP(bvs) {
  int8_t off = (int8_t) load_u8(s, mode_imm(s));
  if ((s->p & FLAG_V) != 0) s->pc += off;
}

OP(cmp) {
  cmp_impl(s, s->a, load_u8(s, modes_a[op & 0x1f](s)));
}

OP(clc) {
  s->p &= 0xfe;
}

OP(cld) {
  s->p &= 0xf7;
}

OP(cli) {
  s->p &= 0xfb;
}

OP(clv) {
  s->p &= 0xbf;
}

OP(cpx) {
  cmp_impl(s, s->x, load_u8(s, modes_x[(op & 0x1f) >> 2](s)));
}

OP(cpy) {
  cmp_impl(s, s->y, load_u8(s, modes_y[(op & 0x1f) >> 2](s)));
}

OP(dec) {
  uint16_t addr = modes_inc[(op & 0x18) >> 3](s);
  uint8_t val = load_u8(s, addr) - 1;
  store_u8(s, addr, val);
  set_nz(s, val);
}

OP(dex) { set_nz(s, --s->x); }

OP(dey) { set_nz(s, --s->y); }

OP(eor) { s->a = set_nz(s, s->a ^ load_u8(s, modes_a[op & 0x1f](s))); }

OP(inc) {
  uint16_t addr = modes_inc[(op & 0x18) >> 3](s);
  uint8_t val = load_u8(s, addr) + 1;
  store_u8(s, addr, val);
  set_nz(s, val);
}

OP(inx) { set_nz(s, ++s->x); }

OP(iny) { set_nz(s, ++s->y); }

OP(jmp) {
  s->pc = mode_abs(s);
  if (op == 0x6c) s->pc = load_u16(s, s->pc);
}

OP(jsr) {
  uint16_t nextpc = s->pc + 1;
  store_u8(s, 0x100 + s->s--, nextpc >> 8);
  store_u8(s, 0x100 + s->s--, nextpc & 0xff);
  s->pc = mode_abs(s);
}

OP(lda) {
  s->a = set_nz(s, load_u8(s, modes_a[op & 0x1f](s)));
}

OP(ldx) {
  s->x = set_nz(s, load_u8(s, modes_x[(op & 0x1f) >> 2](s)));
}

OP(ldy) {
  s->y = set_nz(s, load_u8(s, modes_y[(op & 0x1f) >> 2](s)));
}

OP(lsr) {
  uint8_t addr = modes_a[op & 0x1f](s);
  uint8_t val = load_u8(s, addr);
  s->p = (s->p & 0xfe) | (val & 1);
  store_u8(s, addr, set_nz(s, val >> 1));
}

OP(lsra) {
  s->p = (s->p & 0xfe) | (s->a & 1);
  s->a = set_nz(s, s->a >> 1);
}

OP(ni) {
  FATALF("Unimplemented opcode %02x at $%04x\n", op, s->pc - 1);
}

OP(nop) {}

OP(ora) { s->a = set_nz(s, s->a | load_u8(s, modes_a[op & 0x1f](s))); }

OP(pha) {
  store_u8(s, 0x100 + s->s--, s->a);
}

OP(php) {
  store_u8(s, 0x100 + s->s--, s->p);
}

OP(pla) {
  s->a = set_nz(s, load_u8(s, 0x100 + ++s->s));
}

OP(plp) {
  s->p = load_u8(s, 0x100 + ++s->s) | 0x10;
}

static uint8_t rol_impl(hm1k_state *s, uint8_t v) {
  uint16_t result = ((uint16_t) v << 1) | (s->p & 1);
  s->p = (s->p & 0xfe) | (result >> 8);
  return set_nz(s, (uint8_t) result);
}

OP(rol) {
  uint16_t addr = modes_a[op & 0x1f](s);
  uint8_t v = load_u8(s, addr);
  v = rol_impl(s, v);
  store_u8(s, addr, v);
}

OP(rola) {
  s->a = rol_impl(s, s->a);
}

static uint8_t ror_impl(hm1k_state *s, uint8_t v) {
  uint8_t c = v & 1;
  v = (v >> 1) | (s->p << 7);
  s->p = (s->p & 0xfe) | c;
  return set_nz(s, v);
}

OP(ror) {
  uint16_t addr = modes_a[op & 0x1f](s);
  uint8_t v = load_u8(s, addr);
  v = ror_impl(s, v);
  store_u8(s, addr, v);
}

OP(rora) {
  s->a = ror_impl(s, s->a);
}

OP(rti) {
  // TODO: fixed flags
  s->p = load_u8(s, 0x100 + ++s->s);
  s->pc = load_u8(s, 0x100 + ++s->s);
  s->pc |= load_u8(s, 0x100 + ++s->s) << 8;
}

OP(rts) {
  s->pc = load_u8(s, 0x100 + ++s->s);
  s->pc |= load_u8(s, 0x100 + ++s->s) << 8;
  ++s->pc;
}

OP(sbc) {
  uint8_t b = load_u8(s, modes_a[op & 0x1f](s));
  uint16_t result = (uint16_t) s->a + 0xff + (s->p & 1) - b;
  s->p = (s->p & 0xfe) | (result >> 8);
  s->a = set_nvz(s, s->a, b, (uint8_t) result);
}

OP(sec) {
  s->p |= 1;
}

OP(sed) {
  s->p |= 8;
}

OP(sei) {
  s->p |= 4;
}

OP(sta) {
  store_u8(s, modes_a[op & 0x1f](s), s->a);
}

OP(stx) {
  store_u8(s, modes_x[(op & 0x1f) >> 2](s), s->x);
}

OP(sty) {
  store_u8(s, modes_y[(op & 0x1f) >> 2](s), s->y);
}

OP(tax) { s->x = set_nz(s, s->a); }
OP(tay) { s->y = set_nz(s, s->a); }
OP(tsx) { s->x = set_nz(s, s->s); }
OP(txa) { s->a = set_nz(s, s->x); }
OP(txs) { s->s = s->x; }
OP(tya) { s->a = set_nz(s, s->y); }
#undef OP

#define OP(NAME) static void op_ ## NAME (hm1k_state *s, uint8_t op) { \
  op_ni(s, op);                                                        \
}
OP(bit)
#undef OP

#define OP(CODE, NAME, MODE, CYCLES) op_ ## NAME,
static const hm1k_op ops[256] = {
#include "ops.inc"
};
#undef OP

#define OP(CODE, NAME, MODE, CYCLES) CYCLES,
static const unsigned int op_cycles[256] = {
#include "ops.inc"
};
#undef OP

static void step_6502(hm1k_state *s) {
  uint8_t op = load_u8(s, s->pc);
  // printf("%04x: %02x %02x %02x\n", s->pc, op, load_u8(s, s->pc + 1), load_u8(s, s->pc + 2));
  // disas_modes[op](s->pc, s->ram);
  ++s->pc;
  add_ticks(s, op_cycles[op]);
  ops[op](s, op);
}

typedef struct {
  xcb_connection_t *xcb;
  xcb_screen_t *screen;
  xcb_window_t win;
  unsigned int scale;
  unsigned int xoffset;
  unsigned int yoffset;
  xcb_gcontext_t gc;
  xcb_pixmap_t pixmap;
  uint8_t keymap[256];
} xcb_data;

static void xcb_generate_pixmap(xcb_data *gui) {
  uint32_t values[1];
  unsigned int i, j, n;
  xcb_rectangle_t rects[8];
  
  xcb_create_pixmap(gui->xcb,
                    gui->screen->root_depth,
                    gui->pixmap,
                    gui->screen->root,
                    gui->scale * 8,
                    gui->scale * 256);

  values[0] = gui->screen->black_pixel;
  xcb_change_gc(gui->xcb, gui->gc, XCB_GC_FOREGROUND, values);
  rects[0].x = 0;
  rects[0].y = 0;
  rects[0].width = gui->scale * 8;
  rects[0].height = gui->scale * 256;
  xcb_poly_fill_rectangle(gui->xcb, gui->pixmap, gui->gc, 1, rects);

  values[0] = gui->screen->white_pixel;
  xcb_change_gc(gui->xcb, gui->gc, XCB_GC_FOREGROUND, values);
  for (i = 0; i < 256; i++) {
    n = 0;
    for (j = 0; j < 8; j++) {
      if (i & (0x80 >> j)) {
        rects[n].x = j * gui->scale;
        rects[n].y = i * gui->scale;
        rects[n].width = gui->scale;
        rects[n].height = gui->scale;
        ++n;
      }
    }
    if (n > 0) {
      xcb_poly_fill_rectangle(gui->xcb, gui->pixmap, gui->gc, n, rects);
    }
  }
}

static void get_window_size(xcb_data *gui,
                            unsigned int *width,
                            unsigned int *height) {
  xcb_get_geometry_reply_t *geometry;
  xcb_get_geometry_cookie_t cookie = xcb_get_geometry(gui->xcb, gui->win);
  geometry = xcb_get_geometry_reply(gui->xcb, cookie, NULL);
  *width = geometry->width;
  *height = geometry->height;
  free(geometry);
}

static void resize(xcb_data *gui,
                   unsigned int width,
                   unsigned int height) {
  unsigned int scale = height / 220;
  gui->scale = width / 340;
  if (scale < gui->scale) gui->scale = scale;
  gui->xoffset = (width - scale * 320) / 2;
  gui->yoffset = (height - scale * 200) / 2;
  xcb_generate_pixmap(gui);
}

static int init_xcb(xcb_data *gui) {
  xcb_screen_iterator_t xcb_screen_iterator;
  const xcb_setup_t *xcb_setup;
  xcb_keysym_t *keysyms;
  int i, idx;
  uint32_t values[2];
  unsigned int width, height;
  xcb_get_keyboard_mapping_cookie_t gkm_cookie;
  xcb_get_keyboard_mapping_reply_t *keyboard_mapping;

  gui->pixmap = 0;
  gui->xcb = xcb_connect(NULL, NULL);
  if (xcb_connection_has_error(gui->xcb)) goto error;

  xcb_setup = xcb_get_setup(gui->xcb);
  xcb_screen_iterator = xcb_setup_roots_iterator(xcb_setup);
  gui->screen = xcb_screen_iterator.data;

  gui->gc = xcb_generate_id(gui->xcb);
  values[0] = gui->screen->white_pixel;
  xcb_create_gc(gui->xcb, gui->gc, gui->screen->root, XCB_GC_FOREGROUND, values);

  gui->win = xcb_generate_id(gui->xcb);
  values[0] = gui->screen->black_pixel;
  values[1] = XCB_EVENT_MASK_EXPOSURE
    | XCB_EVENT_MASK_KEY_PRESS
    | XCB_EVENT_MASK_KEY_RELEASE
    | XCB_EVENT_MASK_STRUCTURE_NOTIFY;
  xcb_create_window(gui->xcb,
                    /* depth */ XCB_COPY_FROM_PARENT,
                    gui->win,
                    gui->screen->root,
                    0, 0,
                    1440, 960,
                    /* border */ 0,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT,
                    gui->screen->root_visual,
                    XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
                    values);
  xcb_map_window(gui->xcb, gui->win);
  gui->pixmap = xcb_generate_id(gui->xcb);
  get_window_size(gui, &width, &height);
  resize(gui, width, height);
  gkm_cookie = xcb_get_keyboard_mapping(
      gui->xcb,
      xcb_setup->min_keycode,
      xcb_setup->max_keycode - xcb_setup->min_keycode + 1);
  xcb_flush(gui->xcb);
  if (xcb_connection_has_error(gui->xcb)) goto error;

  memset(gui->keymap, 0xff, 256);
  keyboard_mapping = xcb_get_keyboard_mapping_reply(gui->xcb, gkm_cookie, NULL);
  keysyms = xcb_get_keyboard_mapping_keysyms(keyboard_mapping);
  for (i = 0, idx = xcb_setup->min_keycode;
       i < xcb_get_keyboard_mapping_keysyms_length(keyboard_mapping);
       i += keyboard_mapping->keysyms_per_keycode, idx++) {
    if (keysyms[i] < 128) {
      gui->keymap[idx] = key_map[keysyms[i]];
    } else {
      switch (keysyms[i]) {
      case 0xff08:
        gui->keymap[idx] = KEY_BS;
        break;
      case 0xff09:
        gui->keymap[idx] = KEY_TAB;
        break;
      case 0xff0a:
      case 0xff0d:
        gui->keymap[idx] = KEY_RET;
        break;
      case 0xff1b:
        gui->keymap[idx] = KEY_ESC;
        break;
      case 0xff51:
        gui->keymap[idx] = KEY_LEFT;
        break;
      case 0xff52:
        gui->keymap[idx] = KEY_UP;
        break;
      case 0xff53:
        gui->keymap[idx] = KEY_RIGHT;
        break;
      case 0xff54:
        gui->keymap[idx] = KEY_DOWN;
        break;
      case 0xffe1:
      case 0xffe2:
        gui->keymap[idx] = KEY_SH;
        break;
      case 0xffe3:
      case 0xffe4:
        gui->keymap[idx] = KEY_CTL;
        break;
      }
    }
  }
  /* for (int i = 0; i < 256; i++) { */
  /*   printf("keymap[%d]: %u\n", i, gui->keymap[i]); */
  /* } */
  free(keyboard_mapping);

  return 0;
  
 error:
  if (gui->pixmap) {
    xcb_free_pixmap(gui->xcb, gui->pixmap);
    gui->pixmap = 0;
  }
  xcb_disconnect(gui->xcb);
  return -1;
}

static void update_display(xcb_data *gui, const uint8_t *ram) {
  unsigned int x, y;
  for (y = 0; y < 200; y++) {
    for (x = 0; x < 40; x++) {
      xcb_copy_area(gui->xcb, gui->pixmap, gui->win, gui->gc,
                    0,
                    ram[0x2000 + (320 * (y >> 3)) + (x << 3) + (y & 7)] * gui->scale,
                    (x << 3) * gui->scale + gui->xoffset,
                    y * gui->scale + gui->yoffset,
                    8 * gui->scale, gui->scale);
    }
  }
  xcb_flush(gui->xcb);
}

int main(int argc, char *argv[]) {
  hm1k_state state;
  uint8_t *cartridge;
  uint8_t ram[RAM_SIZE];
  uint8_t rom[ROM_SIZE];
  bool redraw;
  xcb_generic_event_t *event;
  xcb_data gui;

  if (init_xcb(&gui)) {
    FATAL("error initializing xcb");
    return -1;
  }

  randomize(ram, sizeof(ram));
  
  {
    FILE *f = fopen("rom.bin", "rb");
    fread(rom, 1, ROM_SIZE, f);
    fclose(f);
  }
  cartridge = malloc(0x20000);
  memset(cartridge, 0xff, 0x20000);
  {
    FILE *f = fopen("cartridge.bin", "rb");
    fread(cartridge, 1, 0x20000, f);
    fclose(f);
  }
  init_6502(&state, ram);
  state.rom = rom;
  state.cartridge = cartridge;
  state.cartridge_size = 0x20000;
  state.io_read[SERIR - IO_BASE] = read_serir;
  state.io_write[SERCR - IO_BASE] = write_sercr;
  state.kbdrow = 0;
  memset(state.keyboard, 0xff, sizeof(state.keyboard));
  state.io_read[KBDCOL - IO_BASE] = read_kbdcol;
  state.io_write[KBDROW - IO_BASE] = write_kbdrow;
  reset(&state);

  redraw = true;
  for (;;) {
    step_6502(&state);
    if (state.last_sync.tv_sec >= state.next_redraw.tv_sec &&
        (state.last_sync.tv_sec > state.next_redraw.tv_sec ||
         state.last_sync.tv_nsec > state.next_redraw.tv_nsec)) {
      redraw = true;
      state.next_redraw.tv_nsec += REDRAW_NS;
      if (state.next_redraw.tv_nsec >= 1000000000) {
        ++state.next_redraw.tv_sec;
        state.next_redraw.tv_nsec -= 1000000000;
      }
    }
    
    event = xcb_poll_for_event(gui.xcb);
    if (!event) {
      if (redraw) {
        update_display(&gui, ram);
        redraw = false;
      }
      continue;
    }
    switch (event->response_type & ~0x80) {
    case XCB_EXPOSE:
      redraw = true;
      break;
    case XCB_KEY_PRESS:
      {
        xcb_key_press_event_t *kpe =
          (xcb_key_press_event_t*) event;
        unsigned pos = gui.keymap[kpe->detail];
        // printf("key press: %u (%u)\n", kpe->detail, pos);
        if (pos != 0xff) {
          if (kpe->state & XCB_MOD_MASK_1) {
            // Special handling when mod1 is active.
            if (pos == KEY_Q) goto leave_event_loop;
          } else {
            state.keyboard[pos & 7] &= ~((1 << (pos >> 3)));
          }
        }
      }
      break;
    case XCB_KEY_RELEASE:
      {
        xcb_key_press_event_t *kpe =
          (xcb_key_press_event_t*) event;
        unsigned pos = gui.keymap[kpe->detail];
        if (pos != 0xff) state.keyboard[pos & 7] |= (1 << (pos >> 3));
      }
      break;
    case XCB_CONFIGURE_NOTIFY:
      {
        xcb_configure_notify_event_t *cne =
          (xcb_configure_notify_event_t*) event;
        resize(&gui, cne->width, cne->height);
        redraw = true;
      }
      break;
    case XCB_CLIENT_MESSAGE:
      goto leave_event_loop;
    }
    free(event);
    continue;
  leave_event_loop:
    free(event);
    break;
  }  

  xcb_disconnect(gui.xcb);
  return 0;
}
