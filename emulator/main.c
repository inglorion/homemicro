#include "hm1000.h"
#include "xcb.h"

#include "hm1000.c"

int main(int argc, char *argv[]) {
  hm1k_state state;
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
    if (!f) {
      perror("rom.bin");
      fprintf(stderr,
              "Could not open rom.bin."
              " The emulator will not work without it.\n");
      return 1;
    }
    fread(rom, 1, ROM_SIZE, f);
    fclose(f);
  }
  init_6502(&state, ram);
  state.rom = rom;
  state.io_read[SERIR - IO_BASE] = read_serir;
  state.io_write[SERCR - IO_BASE] = write_sercr;
  state.kbdrow = 0;
  memset(state.keyboard, 0xff, sizeof(state.keyboard));
  state.io_read[KBDCOL - IO_BASE] = read_kbdcol;
  state.io_write[KBDROW - IO_BASE] = write_kbdrow;
  do {
    FILE *f = fopen("cartridge.bin", "rb");
    if (!f) break;
    fseek(f, 0, SEEK_END);
    state.cartridge_size = ftell(f);
    rewind(f);
    state.cartridge = malloc(state.cartridge_size);
    if (!state.cartridge) {
      fprintf(stderr,
              "failed to allocate memory (%lu bytes) for cartridge\n",
              (unsigned long) state.cartridge_size);
      perror("malloc");
      break;
    }
    memset(state.cartridge, 0xff, state.cartridge_size);
    fread(state.cartridge, 1, state.cartridge_size, f);
    fclose(f);
  } while (0);
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
