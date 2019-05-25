        curpos = $80
        vidbase = $2000
        kbdcol = $d002
        kbdrow = $d003
        cls = $e003
        fontbase = $e800

        ;; Locations at which to store temporary values.
        ;; These must be consecutive, with no page boundary in between.
        t0 = $8
        t1 = $9

        ;; A location on the zero page. May be the same or different
        ;; from the t locations.
        zp0 = t0

        * = $0000
        .byt "HM",0,1
        .word _start
        .word 32
        .word _endhdr
        .word _end-_start
        .word _start
        .word 0

_endhdr:
        * = $0400
_start:
        jsr cls
        lda #0
        tax
        jsr setcur
        ldx #0
main_loop:
        jsr rnd
        jsr showhex
        lda #$20
        jsr showchr
        lda #$20
        jsr showchr
        dex
        bne main_loop

halt:
        jmp halt

#include "micrornd_code.inc"
        
setcur:
;;; Sets the cursor position.
;;; In:
;;; a   Row (0..24).
;;; x   Column (0..39).
        ;; We need 320 * row + 8 * col.
        ;;  = 256 * row + 64 * row + 8 * col.
        ;; Save row in curpos + 1.
        sta curpos + 1
        ;; Set upper 2 bits of curpos to lower 2 bits of row
        ;; (other bits of curpos are irrelevant at this point).
        lsr
        ror
        ror
        sta curpos
        ;; Add curpos + 1 to (row >> 2) to get row + (row >> 2).
        rol
        and #$3f
        clc
        adc curpos + 1
        sta curpos + 1
        ;; Mask out lower 6 bits of curpos, then shift right.
        ;; This makes curpos at most #$60.
        lda curpos
        and #$c0
        lsr
        sta curpos
        ;; Add col << 2.
        txa
        asl
        asl
        ;; At this point, a is at most #$9c.
        ;; Add curpos. This makes it at most #$fc, so no carry.
        adc curpos
        ;; Shift left one more time. This can carry.
        asl
        sta curpos
        ;; Add video memory base.
        lda #>vidbase
        adc curpos + 1
        sta curpos + 1
        rts

showhex:
        pha
        lsr
        lsr
        lsr
        lsr
        tay
        lda hexits, y
        jsr showchr
        pla
        and #$f
        tay
        lda hexits, y
        jmp showchr

showchr:
;;; Displays a character at the current cursor position
;;; and updates the cursor position.
;;; In:
;;; a   Character to display.
;;; Clobbers y.
        asl
        rol
        rol
        tay
        rol
        and #7
        clc
        adc #>fontbase
        sta $a1
        tya
        and #$f8
        sta $a0
        ldy #7
showchr_loop:
        lda ($a0), y
        sta (curpos), y
        dey
        bpl showchr_loop
        lda curpos
        ;; Assume carry is clear. Last instr affecting it was
        ;; adc #>fontbase, which should not have resulted in carry.
        adc #8
        sta curpos
        lda curpos + 1
        adc #0
        sta curpos + 1
        rts

hexits:
        .byte "0123456789abcdef"

#include "micrornd_data.inc"

_end:
