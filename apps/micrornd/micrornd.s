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

rnd:
;;; Returns a pseudorandom number in a.
;;; If I've counted correctly, this takes 44 cycles and 29 bytes if
;;; micrornd_state is located on the zero page, 56 cycles and 41 bytes
;;; if micrornd_state is not on the zero page. In both cases excluding
;;; the rts at the end and the sequence to call the procedure. The
;;; PRNG uses 4 bytes of state.
;;; The four instructions at the beginning can be removed to reduce
;;; the size of the state by one byte, the size of the routine by
;;; 8 bytes (12 bytes if state is not on the zero page), and the number
;;; of clock cycles by 14 (18 if not using zero page).
        lda micrornd_state + 1
        eor micrornd_state + 3
        sta micrornd_state + 1
        inc micrornd_state + 3

        lda micrornd_state + 1
        asl
        eor #$d5
        adc micrornd_state + 2
        sta micrornd_state + 1
        lda micrornd_state + 2
        adc #1
        sta micrornd_state + 2
        lda micrornd_state
        adc micrornd_state + 1
        sta micrornd_state
        rts
        
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

micrornd_state:
        .byte 0, 0, 0, 0

_end:
