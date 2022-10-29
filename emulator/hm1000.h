#ifndef HOMEMICRO_HM1000
#define HOMEMICRO_HM1000

#include <stdint.h>

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
extern const uint8_t key_map[128];

#endif /* ndef HOMEMICRO_HM1000 */

