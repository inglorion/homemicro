#ifndef HOMEMICRO_EMULATOR_XCB
#define HOMEMICRO_EMULATOR_XCB

#include <stdbool.h>
#include <stdint.h>
#include <xcb/xcb.h>

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
  uint8_t oldgfx[8000];
} xcb_data;

void get_window_size(xcb_data *gui,
                     unsigned int *width,
                     unsigned int *height);
int init_xcb(xcb_data *gui);
void resize(xcb_data *gui,
            unsigned int width,
            unsigned int height);
void update_display(xcb_data *gui, const uint8_t *ram, bool force_redraw);
void xcb_generate_pixmap(xcb_data *gui);

#endif /* ndef HOMEMICRO_EMULATOR_XCB */
