        CURPOS = $80
        BUFPTR = $82
        SHOWADDR = $84
        SHOWCTR = $86
        POLLKEYX = $f8
        POLLKEYY = $f9
        PUTINTAD = $fa
        PUTINTDG = $fb
        PUTINTPS = $fc
        SAVEA = $fd
        SAVEX = $fe
        SAVEY = $ff
        IRQHNDLR = $0200
        NMIHNDLR = $0204
        CARTBSZ = $0206
        RAMTOP = $021e
        KBDSTATE = $0220
        BUF = $0240
        IOBASE = $d000
        KBDCOL = $d002
        KBDROW = $d003
        ROMBASE = $e000
        FONTBASE = $e800
        SERIR = $d004
        SERCR = $d005

* = ROMBASE
        ;; Feature bits.
        FEAT_HM1000 = $01
        FEATURES = FEAT_HM1000
        .byt FEATURES, $00, $00

        ;; ROM function jump table.
        jmp cls
        jmp clshome
        jmp showchr
        jmp showspc
        jmp show
        jmp showint
        jmp showuint
        jmp newline
        jmp setcur
        jmp col0
        jmp scrollup
        jmp loadcart
        jmp savecart
        jmp twistart
        jmp twistop
        jmp twiresta
        jmp twisendb
        jmp twigetb
        jmp twiack
        jmp twinak
        jmp qkbrow
        jmp pollkey
        jmp pollchr
        jmp waitkey
        jmp waitchr
        jmp shchr
        jmp copy
        jmp copy8

        .dsb FONTBASE-*, $ff
_charset:
#include "8x8font.inc"
_start:
        ;; Set NMI handler and IRQ handler.
        sei
        lda #$40                ; rti
        sta IRQHNDLR
        sta NMIHNDLR
        cli
        cld

        ;; Set serial control register to #$7f.
        lda #$7f
        sta SERCR

        ;; TODO: set sercr instead?
        ;; Set serial input register to #$ff.
        lda #$ff
        ;; sta SERIR
        sta SERCR

        ;; Initialize constants.
        lda #>BUF
        sta BUFPTR + 1

        ;; Memory check. First, we work work from $bfff down and write
        ;; #$aa to every address, until the value at $0220 has that value
        ;; or we reach $01ff (whichever comes first).
        ;; Then, from every address from $0220 up to $bfff, we (1) check
        ;; that the value at that address is #$aa, and (2) after we set it
        ;; to #$55, we read it as #$55. We store the highest address for
        ;; which this is true at RAMTOP.
        lda #0
        sta $a0
        sta $0220
        ldx #$bf
        stx $a1
        ldy #$ff
        sei
setaa:
        lda #$aa
        sta ($a0), y
        lda $0220
        cmp #$aa
        beq setaa_done
        dey
        bne setaa
        lda #$aa
        sta ($a0), y
        dey
        dex
        stx $a1
        cpx #1
        bcs setaa

setaa_done:
        ldy #$20
        ldx #$02
        stx $a1
memcheck:
        lda ($a0), y
        cmp #$aa
        bne memcheck_done
        eor #$ff
        sta ($a0), y
        lda ($a0), y
        cmp #$55
        bne memcheck_done
        iny
        bne memcheck
        inx
        stx $a1
        cpx #$c0
        bcc memcheck

memcheck_done:
        tya
        sec
        sbc #1
        sta RAMTOP
        txa
        sbc #0
        sta RAMTOP + 1

        ;; Set NMI and IRQ handler again, just in case the memory check
        ;; overwrote them.
        lda #$40                ; rti
        sta IRQHNDLR
        sta NMIHNDLR
        cli

        jsr cls
        lda #1
        tax
        dex
        jsr setcur

        ;; Show memcheck result in the bottom right corner of the screen.
        lda #$55
        sta $3f3d
        lda RAMTOP
        sta $3f3e
        lda RAMTOP + 1
        sta $3f3f

        ;; Also show as number.
        lda #<BUF
        sta BUFPTR
        lda #>BUF
        sta BUFPTR + 1
        lda RAMTOP
        sec
        adc #0
        sta $a0
        lda RAMTOP + 1
        adc #0
        sta $a1
        jsr showuint
        jsr showspc
        lda #<bytess
        sta SHOWADDR
        lda #>bytess
        sta SHOWADDR + 1
        lda #5
        sta SHOWCTR
        jsr show
        jsr showspc
        lda #$4f
        jsr showchr
        lda #$4b
        jsr showchr
        jsr newline

        lda #0
        sta $c0
try_cart:
        lda #0
        sta $a0
        sta $a1
        sta $a2
        sta $a3
        sta $a4
        sta $a6
        lda #$2a
        sta $a7
        lda #$14
        sta $a5
        jsr loadcart
        cmp #0
        beq cloaded
        dec $c0
        bne try_cart

cloaded:
        lda $2a00
        cmp #$48
        bne cartfail
        lda $2a01
        cmp #$4d
        bne cartfail
        lda $2a02
        bne cartfail
        lda $2a03
        cmp #$1
        bne cartfail

        lda $2a04
        sta $c0
        lda $2a05
        sta $c1
        lda $2a08
        sta $a0
        lda $2a09
        sta $a1
        lda #0
        sta $a2
        sta $a3
        lda $2a0a
        sta $a4
        lda $2a0b
        sta $a5
        lda $2a0c
        sta $a6
        lda $2a0d
        sta $a7
        jsr loadcart
        cmp #0
        bne cartfail
        lda #$aa
        sta $2007
        jmp ($c0)

cartfail:
        lda #<cartfls
        sta SHOWADDR
        lda #>cartfls
        sta SHOWADDR + 1
        lda #CARTFLL
        sta SHOWCTR
        jsr show
        jmp _end

adjcur:
;;; Scrolls so that the cursor position is within screen bounds.
        lda CURPOS + 1
        cmp #$3f
        bcc adjcurok
        bne adjcurscroll
        lda CURPOS
        cmp #$3f
        bcc adjcurok
adjcurscroll:
        lda #1
        jsr scrollup
        jmp adjcur
adjcurok:
        rts

loadcart:
;;; Loads data from a cartridge.
;;; In:
;;; $a0..$a3  Location of first byte to load.
;;; $a4..$a5  Number of bytes to load.
;;; $a6..$a7  Address to write first byte to.
;;; Out:
;;; a       0 if successful. Any other value indicates an error.
;;; Clobbers x.
        jsr twistart
        jsr cart_set_location
        cmp #0
        bne loadcart_done
        jsr twiresta
        cmp #0
        bne loadcart_done
        lda #$a1
        jsr twisendb
        cmp #0
        bne loadcart_done
        dec $a4
        bne loadcart_loop
        lda $a5
        beq loadcart_last
loadcart_loop:
        jsr twigetb
        ldx #0
        sta ($a6,x)
        jsr twiack
        inc $a6
        bne loadcart_skip_msb
        inc $a7
loadcart_skip_msb:
        dec $a4
        bne loadcart_not_last
        lda $a5
        beq loadcart_last
loadcart_not_last:
        lda #$ff
        cmp $a4
        bne loadcart_loop
        dec $a5
        jmp loadcart_loop

loadcart_last:
        jsr twigetb
        ldx #0
        sta ($a6,x)
        jsr twinak
        lda #0

loadcart_done:
        tax
        jsr twistop
        txa
        rts

cart_set_location:
;;; Sets the location for the next cartridge operation.
;;; Preconditions:
;;;  - twistart must have been called.
;;; In:
;;; $a0..$a3  Location to set.
;;; Out:
;;; a       0 if successful. Any other value indicates an error.
;;; Clobbers x.
        lda $a2
        asl
        ora #$a0
        jsr twisendb
        cmp #0
        bne cart_set_location_done
        lda $a1
        jsr twisendb
        cmp #0
        bne cart_set_location_done
        lda $a0
        jsr twisendb
cart_set_location_done:
        rts

cls:
;;; Clears the screen.
;;; Clobbers a, y, x.
        ldy #0
        sty $a0
        ldx #$20
        stx $a1
        lda #0
cls_loop:
        sta ($a0), y
        iny
        bne cls_loop
        inx
        stx $a1
        cpx #$3f
        bcc cls_loop
cls_loop1:
        sta ($a0), y
        iny
        cpy #$40
        bcc cls_loop1
        rts

clshome:
;;; Clears the screen and sets the cursor to 0, 0.
        jsr cls
        lda #0
        tax
        jmp setcur

cmp16:
;;; Compares two 16-bit numbers.
;;; In:
;;; $a0..$a1  first number
;;; $a2..$a3  second
;;; Out:
;;; flags     z if first == second
;;;           c if first >= second
        lda $a0
        cmp $a2
        beq cmp16_eq
        lda $a1
        sbc $a3
        ora #2
        rts
cmp16_eq:
        lda $a1
        sbc $a3
        rts

col0:
;;; Sets the cursor to column 0 on the current line.
        ;; We find the line the cursor is on by starting at an address
        ;; corresponding to line 25 (one after the last valid line),
        ;; then moving up one line at a time until the address becomes
        ;; less than or equal to the cursor position. There are 320
        ;; bytes in a line of text; we shift everything right 6 bits
        ;; so that the size of our lines is 5.
        ;; Instead of right shifting the cursos position 6 times, we
        ;; left shift 2 times and use CURPOS + 1.
        ;; line 3, column 7.
        ;; 320 * 3 + 8 * 7 = 1016. + 8192 = 9208. $23f8
        ;; target: 960 + 8192 = 9152. $23c0
        asl CURPOS
        rol CURPOS + 1
        ;; $47dc
        asl CURPOS
        rol CURPOS + 1
        ;; $8fb8
        ;; Starting position: (#$2000 + (25 * 320)) >> 6: $fd.
        ;; We actually start 1 below that, to get the effect of a
        ;; less-or-equal comparison instead of a less-than comparison.
        lda #$fc
col0l:
        cmp CURPOS + 1
        bcc col0c
        sbc #5                  ; Carry is set, so this subtracts 5.
        ;; $8e
        bcs col0l               ; Always taken.
col0c:
        adc #1                  ; Carry is clear, so this adds 1.
        ;; $8f
        ;; The new cursor position should be a, shifted left 6 times.
        ;; Instead, we shift right 2 times.
        lsr
        ;; $47
        ror CURPOS
        ;; $80
        lsr
        ;; $23
        sta CURPOS + 1
        lda CURPOS
        ror
        ;; $c0
        and #$c0
        sta CURPOS
        rts

copy8:
;;; Copies 8 bytes from ($a2,3) to ($a0,1).
;;; Clobbers y.
        ldy #8
copy:
;;; Copies y bytes from ($a2,3) to ($a0,1).
        dey
        lda ($a2), y
        sta ($a0), y
        cpy #0
        bne copy
        rts

newline:
        jsr col0
        lda CURPOS
        clc
        adc #$40
        sta CURPOS
        lda CURPOS + 1
        adc #1
        sta CURPOS + 1
        jmp adjcur

pollchr:
;;; Polls the keyboard as per pollkey. If a key press was
;;; detected, the shift state is used to shift the return
;;; value as appropriate.
;;; Out:
;;; a   character, or 0 if no key press.
;;; Clobbers: x, y.
        jsr pollkey
        beq pollchrd
        tax
        lda KBDSTATE + 2
        and #$80
        bne pollchrn
        txa
        jsr shchr
        tax
pollchrn:
        txa
pollchrd:
        rts

pollkey:
;;; Polls the keyboard for new key presses.
;;; KBDSTATE is updated to reflect any keys that were
;;; released.
;;; If any keys were pressed since the last update
;;; to KBDSTATE, one key is chosen, it is recorded
;;; as pressed, and its code is returned.
;;; Out:
;;; a   If a keypress was detected, the key code.
;;;     If no keypress was detected, 0.
;;; Clobbers x, y.
        ldy #7
        lda #$80
        sta POLLKEYX
        lda #$7f
        sta POLLKEYY
pollkeyl:
        sta KBDROW
        nop
        lda KBDCOL
        tax
        ora KBDSTATE, y
        sta KBDSTATE, y
        txa
        eor KBDSTATE, y
        beq pollkeyn
        ;; Key pressed.
        ;; If we already recorded a key, do nothing.
        ldx POLLKEYX
        bpl pollkeyn
        ;; Pick one bit in a that's nonzero.
        sta POLLKEYX
        lda #$80
        ldx #7
pollkeyb:
        asl POLLKEYX
        bcs pollkeyf
        dex
        lsr
        bne pollkeyb
pollkeyf:
        ;; Store that bit in POLLKEYX.
        stx POLLKEYX
        ;; Flip that bit in KBDSTATE, y.
        eor KBDSTATE, y
        sta KBDSTATE, y
        ;; Encode row in $b1, too.
        tya
        asl
        asl
        asl
        ora POLLKEYX
        sta POLLKEYX
pollkeyn:
        sec
        ror POLLKEYY
        lda POLLKEYY
        dey
        bpl pollkeyl
        ldy POLLKEYX
        bpl pollkeym
        lda #0
        rts
pollkeym:
        lda kbmap, y
        rts

putint:
        ;; In:
        ;; $a0..$a1  value to convert.
        ;; BUFPTR    address to write next character to.
        ;; Out:
        ;; BUFPTR    address one beyond the last written character.
        lda $a1
        bpl putuint
        ldy #0
        lda #$2d
        sta (BUFPTR), y
        inc BUFPTR
        sec
        lda #0
        sbc $a0
        sta $a0
        lda #0
        sbc $a1
        sta $a1
putuint:
        ;; In:
        ;; $a0..$a1  value to convert.
        ;; BUFPTR    address to write next character to.
        ;; Out:
        ;; BUFPTR    address one beyond the last written character.
        ;; Locals:
        ;; $a2..$a3  value we're currently comparing to.
        ;; PUTINTAD  value to add to digit if test passes.
        ;; PUTINTDG  value of current digit so far.
        ;; PUTINTPS  position in uinttbl.
        ;;
        ;; The algorithm works like this:
        ;; $a2..$a3 goes through the sequence 40000, 20000, 10000,
        ;; 8000, 4000, 2000, 1000, 800, 400, 200, 100, 80, 40, 20, 10.
        ;; PUTINTAD tracks the first digit of this, so 4, 2, 1, 8, 4, 2, 1,
        ;; and so on.
        ;; PUTINTDG starts at 0.
        ;; On every iteration, we compare $a0..$a1 to $a2..$a3. If
        ;; $a0..$a1 is greater or equal, we subtract $a2..$a3 from it
        ;; and add PUTINTAD to PUTINTDG.
        ;; Every time PUTINTAD changes from 1 to 8, if PUTINTDG is not
        ;; equal to 0, we emit PUTINTDG and set it to #$30.
        ;;
        ;; For example, if the number we're converting is 60014, the
        ;; first steps are:
        ;; Compare 60014 to 40000. It's greater, so we subtract and
        ;; get 20014, and PUTINTDG becomes 4.
        ;; Compare 20014 to 20000. It's greater, so we subtract and
        ;; get 14, and PUTINTDG becomes 6.
        ;; Compare 14 and 10000. It's less, so we don't subtract and
        ;; don't change PUTINTDG.
        ;; Now PUTINTAD changes from 1 to 8, so we emit the 6 we have
        ;; accumulated in PUTINTDG and set PUTINTDG back to 0.
        ;; We compare to 8000, 4000, 2000, and 1000, and our number
        ;; is smaller in every case, so we end up emitting 0 for the
        ;; next digit. Same for the one one after that. Then we get to
        ;; 80, 40, 20, 10. In the last case, our number is greater, so
        ;; the digit we emit is 1, and the number we're left with is 4.
        ;; The last number, we just emit.

        ;; Initialize local variables.
        lda uinttbl + 6
        sta $a2
        lda uinttbl + 7
        sta $a3
        lda #4
        sta PUTINTAD
        lda #0
        sta PUTINTDG
        lda #6
        sta PUTINTPS
putintl:
        jsr cmp16
        bcc putintlt
        jsr sub16
        lda PUTINTDG
        ora PUTINTAD
        sta PUTINTDG
putintlt:
        lsr PUTINTAD
        beq putintdg
        lsr $a3
        ror $a2
        bcc putintl         ; always taken
putintdg:
        ldy #0
        lda PUTINTDG
        beq putintsk
        ora #$30
        sta (BUFPTR), y
        inc BUFPTR
        lda #$30
        sta PUTINTDG
putintsk:
        dec PUTINTPS
        dec PUTINTPS
        bmi putintls
        ldy PUTINTPS
        lda uinttbl, y
        sta $a2
        iny
        lda uinttbl, y
        sta $a3
        lda #8
        sta PUTINTAD
        bpl putintl         ; always taken
putintls:
        lda $a0
        ora #$30
        sta (BUFPTR), y
        inc BUFPTR
        rts

qkbrow:
;;; Queries a keyboard row.
;;; In:
;;; a   keyboard row to query (mask with one bit clear, others set)
;;; Out:
;;; a   state of keyboard row
        sta KBDROW
        nop
        lda KBDCOL
        rts

savecart:
;;; Saves data to a cartridge.
;;; In:
;;; $a0..$a3  Location (on cartridge) to save to.
;;; $a4..$a5  Number of bytes to save.
;;; $a6..$a7  Address of first byte to save.
;;; Out:
;;; a       0 if successful. Any other value indicates an error.
;;; Clobbers x.
        ;; We save data in blocks of CARTBSZ bytes.
        ;; Compute the size of the first. The computation
        ;; requires that CARTBSZ be a power of 2 and computes
        ;; the maximum number of bytes that can be written from
        ;; start without crossing into the next block:
        ;;
        ;; start and (CARTBSZ - 1)
        ;;
        ;; Then, it sets the size to be written to the minimum of
        ;; that value and the size that was actually passed in.
        lda CARTBSZ
        sec
        sbc #1
        and $a0
        sta $a8
        lda CARTBSZ + 1
        sbc #0
        and $a1
        sta $a9
        lda CARTBSZ
        sec
        sbc $a8
        sta $a8
        lda CARTBSZ + 1
        sbc $a9
        sta $a9
savecart0:
        lda $a4
        cmp $a8
        lda $a5
        sbc $a9
        ;; At this point, carry clear means that the number of bytes
        ;; we were asked to write ($a4..$a5) is less than the number
        ;; of bytes that fit in the block ($a8..$a9). We set $a8..$a9
        ;; to the lesser value.
        bcs savecart1
        lda $a4
        sta $a8
        lda $a5
        sta $a9
savecart1:
        lda $a8
        sta $aa
        lda $a9
        sta $ab
        jsr saveblk
        bne savecart2
        ;; Compute new start address.
        lda $a0
        clc
        adc $aa
        sta $a0
        lda $a1
        adc $ab
        sta $a1
        lda $a2
        adc #0
        sta $a2
        lda $a3
        adc #0
        sta $a3
        ;; Compute remaining bytes to write.
        lda $a4
        sec
        sbc $aa
        sta $a4
        lda $a5
        sbc $ab
        sta $a5
        ;; If no bytes left to write, we're done.
        ora $a4
        beq savecart2
        ;; Now that we are aligned to a block boundary,
        ;; write up to CARTBSZ bytes at a time.
        lda CARTBSZ
        sta $a8
        lda CARTBSZ + 1
        sta $a9
        jmp savecart0
savecart2:
        rts

;;; Saves a single block of data to the cartridge.
;;; In:
;;; $a0..$a3  Location (on cartridge) to save to.
;;; $a6..$a7  Address of first byte to save.
;;; $a8..$a9  Number of bytes to save.
;;; Out:
;;; a       0 if successful. Any other value indicates an error.
saveblk:
        jsr cartnext
        cmp #0
        bne saveblk2
saveblk0:
        ldy #0
        lda ($a6), y
        jsr twisendb
        cmp #0
        bne saveblk2
        inc $a6
        bne saveblk1
        inc $a7
saveblk1:
        dec $a8
        bne saveblk0
        lda $a9
        beq saveblk2
        dec $a9
        jmp saveblk0
saveblk2:
        tax
        jsr twistop
        txa
        rts

cartnext:
;;; Waits until the cartridge is ready and sets the
;;; next address. If more than 10ms elapse and the cartridge
;;; does not acknowledge the new address, the routine
;;; returns with the a register set to a nonzero value.
;;;
;;; Postconditions:
;;;  - If a is 0: The cartridge is ready to receive bytes to be
;;;    written.
;;;  - Otherwise: The cartridge did not acknowledge the new
;;;    address.
;;;
;;; In:
;;; $a0..$a3  Cartridge location to set.
        ldx #$96
cartnext0:
        jsr twistop
        jsr twistart
        jsr cart_set_location
        cmp #0
        beq cartnextd
        dex
        bne cartnext0
cartnextd:
        rts

sub16:
        sec
sbc16:
;;; Subtracts two 16-bit numbers.
;;; In:
;;; $a0..$a1  first number
;;; $a2..$a3  second
;;; Out:
;;; $a0..$a   result
;;; flags     c if first >= second
        lda $a0
        sbc $a2
        sta $a0
        lda $a1
        sbc $a3
        sta $a1
        rts

scrollup:
;;; Scrolls the screen up a lines.
        ;; a * 320 + #$2000 in $a2..$a3.
        ;; #$2000 in $a0..$a1.
        sta $a3
        ldy #0
        sty $a2
        sty $a0
        lsr
        ror $a2
        lsr
        ror $a2
        clc
        adc $a3
        adc #$20
        sta $a3
        lda #$20
        sta $a1
        ;; update CURPOS
        lda CURPOS
        sec
        sbc $a2
        sta CURPOS
        lda CURPOS + 1
        sbc $a3
        bcs scrollupcpok
        lda #0
        sta CURPOS
scrollupcpok:
        clc
        adc #$20
        sta CURPOS + 1
        ;; copy bytes until $a2..$a3 > #$3f3f.
scrolluplow:
        lda $a3
        cmp #$3f
        bcs scrolluphi
        ;; y is 0 at this point
scrollupll:
        lda ($a2), y
        sta ($a0), y
        iny
        bne scrollupll
        inc $a1
        inc $a3
        jmp scrolluplow
scrolluphi:
        bne scrollupblank
        clc
scrolluphl:
        tya
        adc $a2
        cmp #$40
        bcs scrollupblank
        lda ($a2), y
        sta ($a0), y
        iny
        jmp scrolluphl
scrollupblank:
        lda $a1
        cmp #$3f
        bcc scrollupmore
        bne scrollupdone
        lda $a0
        bne scrollupdone
        lda #$3e
        sta $a1
        lda #$40
        sta $a0
        ldy #$c0
scrollupmore:
        lda #0
        sta ($a0), y
        iny
        bne scrollupmore
        inc $a1
        jmp scrollupblank
scrollupdone:
        rts

setcur:
;;; Sets the cursor position.
;;; In:
;;; a   Row (0..24).
;;; x   Column (0..39).
        ;; We need 320 * row + 8 * col.
        ;;  = 256 * row + 64 * row + 8 * col.
        ;; Store row in $81.
        sta $81
        ;; Set upper 2 bits of $80 to lower 2 bits of row
        ;; (other bits of $80 are irrelevant at this point).
        lsr
        ror
        ror
        sta $80
        ;; Add $81 to (row >> 2) to get row + (row >> 2).
        rol
        and #$3f
        clc
        adc $81
        sta $81
        ;; Mask out lower 6 bits of $80, then shift right.
        ;; This makes $80 at most #$60.
        lda $80
        and #$c0
        lsr
        sta $80
        ;; Add col << 2.
        txa
        asl
        asl
        ;; At this point, a is at most #$9c.
        ;; Add $80. This makes it at most #$fc, so no carry.
        adc $80
        ;; Shift left one more time. This can carry.
        asl
        sta $80
        ;; Add display base.
        lda #$20
        adc $81
        sta $81
        rts

shchr:
;;; Convert unshifted character code to shifted character code.
;;; For example, 'a' becomes 'A', '1' becomes '!'.
;;; In:
;;; a   character code
;;; Out:
;;; a   shifted character code
;;; Clobbers y.
        cmp #$27
        beq shchrq
        cmp #$3d
        beq shchre
        cmp #$60
        beq shchrb
        cmp #$41
        bcs shchrl
        cmp #$21
        bcs shchry
        rts
shchrq:
        lda #$22
        rts
shchre:
        lda #$2b
        rts
shchrb:
        lda #$7e
        rts
shchrl:
        eor #$20
        rts
shchry:
        and #$0f
        tay
        lda shchrt, y
        rts

show:
;;; Displays a number of characters.
;;; In:
;;; SHOWADDR  address of first character to show (2 bytes).
;;; SHOWCTR   number of characters to show.
showl:
        ldy #0
        lda (SHOWADDR), y
        jsr showchr
        inc SHOWADDR
        bne showok
        inc SHOWADDR + 1
showok:
        dec SHOWCTR
        bne showl
showend:
        rts

showchr:
;;; Displays a character at the current cursor position
;;; and updates the cursor position.
;;; In:
;;; a   Character to display.
        sta SAVEA
        lda $a3
        pha
        lda $a2
        pha
        lda $a1
        pha
        lda $a0
        pha
        tya
        pha
        lda SAVEA
        pha
        jsr adjcur
        pla
        asl
        rol
        rol
        tay
        rol
        and #7
        clc
        adc #>FONTBASE
        sta $a3
        tya
        and #$f8
        sta $a2
        lda CURPOS
        sta $a0
        adc #8
        sta CURPOS
        lda CURPOS + 1
        sta $a1
        adc #0
        sta CURPOS + 1
        jsr copy8
        pla
        tay
        pla
        sta $a0
        pla
        sta $a1
        pla
        sta $a2
        pla
        sta $a3
        rts

showhex:
;;; Print a as hex.
        sta SAVEA
        tya
        pha
        lda SAVEA
        pha
        lsr
        lsr
        lsr
        lsr
        tay
        lda hexits, y
        jsr showchr
        pla
        and #$0f
        tay
        lda hexits, y
        jsr showchr
        pla
        tay
        rts

showint:
;;; In:
;;; $a0..$a1  number to show.
        lda BUFPTR
        pha
        sta SHOWADDR
        lda BUFPTR + 1
        sta SHOWADDR + 1
        jsr putint
        jmp showintd

showuint:
;;; In:
;;; $a0..$a1  number to show.
        lda BUFPTR
        pha
        sta SHOWADDR
        lda BUFPTR + 1
        sta SHOWADDR + 1
        jsr putuint
        lda BUFPTR
        sec
        sbc SHOWADDR
        sta SHOWCTR
        jsr show
showintd:
        pla
        sta BUFPTR
        rts

showspc:
        lda #$20
        jmp showchr

twiack:
;;; Clobbers a.
        lda #$1f
twibit:
        sta SERCR
        ora #$40
        sta SERCR
        and #$bf
        sta SERCR
        rts

twinak:
;;; Clobbers a.
        lda #$9f
        jmp twibit

twigetb:
;;; Receives a byte over TWI. This function does not send an ACK or NAK,
;;; so twiack or twinak must be called to complete the receipt of the
;;; byte.
;;; Out:
;;; a   The byte received.
;;; Clobbers x.
        lda #$9f
        sta SERCR
        ldx #8
twigetbl:
        lda #$df
        sta SERCR
        and #$bf
        sta SERCR
        dex
        bne twigetbl
        lda SERIR
        rts

twiresta:
;;; Sends a TWI restart.
;;; Out:
;;; a   0 if successful. Any other value indicates an error.
        lda #$9f
        sta SERCR
        ora #$40
        nop
        nop
        nop
        sta SERCR
        nop
        nop
        nop
        jmp twistart

twisendb:
;;; Preconditions:
;;; TWI start must have been sent.
;;; In:
;;; a   byte to send.
;;; Out:
;;; a   0 if ACK was received. 1 if NAK was received.
;;;     Any other value indicates an error occurred.
;;; Clobbers x, $60.
        sta $60
        ldx #8
twisbl:
        lda $60
        and #$80
        ora #$1f
        sta SERCR
        ora #$40
        sta SERCR
        asl $60
        and #$bf
        sta SERCR
        dex
        bne twisbl
        ora #$80
        sta SERCR
        ora #$40
        sta SERCR
        lda SERIR
        tax
        lda #$9f
        sta SERCR
        txa
        and #1
        rts

twistart:
;;; Out:
;;; a   0 if successful. Any other value means an error occurred.
        lda #$5f
        sta SERCR
        ;; Drive down the clock signal 8us (15 clock cycles) later.
        ;; It takes 6 cycles to execute the and and sta, so we need
        ;; to delay for 9 cycles - one jmp and 3 nops.
        jmp twistadl
twistadl:
        nop
        nop
        nop
        lda #$1f
        sta SERCR
        lda #0
        rts

twistop:
;;; Clobbers a.
        lda #$1f
        sta SERCR
        ora #$40
        nop
        nop
        nop
        sta SERCR
        ora #$80
        nop
        nop
        nop
        sta SERCR
        ora #$20
        sta SERCR
        rts

waitchr:
        jsr pollchr
        beq waitchr
        rts

waitkey:
        jsr pollkey
        beq waitkey
        rts

_end:
        jmp _end

bytess: .byt "bytes"
cartfls: .byt "Could not load cartridge"
CARTFLL = * - cartfls

hexits: .byt "0123456789abcdef"

kbmap:  .byt $1b,"123456",$00
        .byt "`",$09,"qerty",$00
        .byt $00,"awsdfg",$00
        .byt ",zxcvbn "
        .byt "=",$08,"-0987",$00
        .byt "]",$5c,"[pouh",$00
        .byt "'",$0d,"lkijm",$00
        .byt "./;",$1e,$1c,$1d,$1f,$00

shchrt:
        .byt ")!@#$%",$5e,"&*( :<_>?"

uinttbl: .word 80, 800, 8000, 40000

        .dsb $fffa-*,$ff
nmivec:
        .word NMIHNDLR
rstvec:
        .word _start
irqvec:
        .word IRQHNDLR
