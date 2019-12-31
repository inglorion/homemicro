#include "xcb.h"
#include "hm1000.h"

#include <stdlib.h>
#include <string.h>

void get_window_size(xcb_data *gui,
                     unsigned int *width,
                     unsigned int *height) {
  xcb_get_geometry_reply_t *geometry;
  xcb_get_geometry_cookie_t cookie = xcb_get_geometry(gui->xcb, gui->win);
  geometry = xcb_get_geometry_reply(gui->xcb, cookie, NULL);
  *width = geometry->width;
  *height = geometry->height;
  free(geometry);
}

int init_xcb(xcb_data *gui) {
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

void resize(xcb_data *gui,
            unsigned int width,
            unsigned int height) {
  unsigned int scale = height / 220;
  gui->scale = width / 340;
  if (scale < gui->scale) gui->scale = scale;
  gui->xoffset = (width - scale * 320) / 2;
  gui->yoffset = (height - scale * 200) / 2;
  xcb_generate_pixmap(gui);
}

void update_display(xcb_data *gui, const uint8_t *ram) {
  unsigned int idx, x, y;
  uint8_t gfx;
  for (y = 0; y < 200; y++) {
    for (x = 0; x < 40; x++) {
      idx = (320 * (y >> 3)) + (x << 3) + (y & 7);
      gfx = ram[0x2000 + idx];
      if (gui->oldgfx[idx] == gfx) continue;
      xcb_copy_area(gui->xcb, gui->pixmap, gui->win, gui->gc,
                    0,
                    (size_t) gfx * gui->scale,
                    (x << 3) * gui->scale + gui->xoffset,
                    y * gui->scale + gui->yoffset,
                    8 * gui->scale, gui->scale);
      gui->oldgfx[idx] = gfx;
    }
  }
  xcb_flush(gui->xcb);
}

void xcb_generate_pixmap(xcb_data *gui) {
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
