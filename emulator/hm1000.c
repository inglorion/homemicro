#include "hm1000.h"
#include "xcb.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <errno.h>

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
    /* Usage of twi_status:
     *  - We starte the lower 4 bits at 0xf and count down for every
     *    bit we send or receive.
     *  - Once the value reaches 7, this means we have sent or received
     *    8 bits and need to send or receive an acknowledgment.
     *  - TWI_ADDR_SET indicates that we have received a TWI address.
     * twi_addr is used to store the address of the TWI device currently
     * being addressed. It is valid when TWI_ADDR_SET is set in twi_status.
     * cartridge_addr is used to store the current cartridge address.
     * It is set by the computer transmitting the bit sequence.
     * 1010xxx0 yyyyyyyy zzzzzzzz. This is also the sequence that initiates
     * a write to the cartridge. A read is performed by writing zero
     * bytes (just to set the address), then sending the sequence
     * 1010xxx1.
     */
    const bool addr_set = s->twi_status & TWI_ADDR_SET;
    const bool cart_write = (s->twi_addr & 0xf1) == 0xa0;
    const bool waiting_for_ack = (s->twi_status & 15) == 7;
    /* Shift incoming bit into serir. We start by setting it to the value
     * of the top bit in sercr, but allow clients to pull it low. */
    s->serir = (s->serir << 1) | (s->sercr >> 7);
    if (waiting_for_ack) {
      if (s->cartridge && (s->twi_addr & 0xf0) == 0xa0) {
        if (addr_set) {
          /* Acknowledge address sent to cartridge. */
          if (cart_write) set_sda_low();
        } else  {
          /* Acknowledge cartridge addresses. */
          set_sda_low();
          s->twi_status |= TWI_ADDR_SET;
          if (cart_write) {
            s->cartridge_addr = (s->twi_addr >> 1) & 7;
            s->cartridge_bits = 0;
          }
        }
      }
      s->twi_status |= 8;
    } else {
      if (addr_set && (s->twi_addr & 0xf1) == 0xa1) {
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
          if (cart_write)
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

OP(bit) {
  uint8_t val = load_u8(s, modes_a[op & 0x1f](s));
  s->p &= ~(FLAG_N | FLAG_V | FLAG_Z);
  s->p |= val & (FLAG_N | FLAG_V);
  if ((s->a & val) == 0) s->p |= FLAG_Z;
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
  s->p = load_u8(s, 0x100 + ++s->s) | 0x20;
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
