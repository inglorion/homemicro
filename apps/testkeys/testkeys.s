#define KBDCOL $d002
#define KBDROW $d003
        cls = $e003
        
* = $0000
        .byt "HM",0,1
        .word _start
        .word 32
        .word _endhdr
        .word _endcode-_start
        .word _start
        .word 0

_endhdr:
        * = $0300
_start:
        jsr cls

        lda #0
        tax
        jsr setcursor
        lda #<title
        sta $8
        lda #>title
        sta $9
        jsr puts

        lda #6
        ldx #4
        jsr setcursor
        lda #$45
        jsr putchar_raw
        lda #$73
        jsr putchar_raw
        lda #$63
        jsr putchar_raw

        lda #10
        ldx #4
        jsr setcursor
        lda #<row1
        sta $8
        lda #>row1
        sta $9
        jsr puts

        lda #12
        ldx #4
        jsr setcursor
        lda #<row2
        sta $8
        lda #>row2
        sta $9
        jsr puts

        lda #14
        ldx #4
        jsr setcursor
        lda #<row3
        sta $8
        lda #>row3
        sta $9
        jsr puts

        lda #16
        ldx #4
        jsr setcursor
        lda #<row4
        sta $8
        lda #>row4
        sta $9
        jsr puts

        lda #18
        ldx #4
        jsr setcursor
        lda #<row5
        sta $8
        lda #>row5
        sta $9
        jsr puts

main_loop:
        ldy #7
        lda #$7f
readkbd:
        sta $40
        sta KBDROW
        nop
        lda KBDCOL
        sta newkbd, y
        lda #$ff
        sta KBDROW
        lda $40
        sec
        ror
        dey
        bpl readkbd

        lda #<tbl
        sta $40
        lda #>tbl
        sta $41
update_keys:
        ldy #0
        lda ($40), y
        tax
        lda prevkbd, x
        eor newkbd, x
        iny
        and ($40), y
        beq no_change
        iny
        lda ($40), y
        tax
        iny
        lda ($40), y
        jsr setcursor
        iny
        lda ($40), y
changed:
        beq no_change
        pha
        jsr invertchar
        pla
        tax
        dex
        txa
        jmp changed
no_change:
        lda $40
        clc
        adc #5
        sta $40
        lda $41
        adc #0
        sta $41
        cmp #>tbl_end
        bcc update_keys
        lda $40
        cmp #<tbl_end
        bcc update_keys

        lda #<prevkbd
        sta $8
        lda #>prevkbd
        sta $9
        lda #<newkbd
        sta $a
        lda #>newkbd
        sta $b
        ldy #8
        jsr memcpy

        lda #200
        jsr wait
        lda #133
        jsr wait

        jmp main_loop

_end:
        jmp _end

putchar_raw:
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
        adc #$e8
        sta $b
        tya
        and #$f8
        sta $a
        lda $80
        sta $8
        adc #8
        sta $80
        lda $81
        sta $9
        adc #0
        sta $81
        jmp cpy8

puts:
        ldy #0
        lda ($8), y
        beq puts_done
        tay
        lda $9
        pha
        lda $8
        pha
        tya
        jsr putchar_raw
        pla
        clc
        adc #1
        sta $8
        pla
        adc #0
        sta $9
        jmp puts
puts_done:
        rts

setcursor:
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
        
cpy8:
;;; Copies 8 bytes from ($a) to ($8).
;;; Clobbers y.
        ldy #8
memcpy:
;;; Copies y bytes from ($a) to ($8).
        dey
        lda ($a), y
        sta ($8), y
        cpy #0
        bne memcpy
        rts

invertchar:
;;; Inverts the character at the current cursor position
;;; and updates the cursor position.
;;; Clobbers y.
        lda $80
        sta $8
        clc
        adc #8
        sta $80
        lda $81
        sta $9
        adc #0
        sta $81
        ldy #8
invert_loop:
        dey
        lda ($8), y
        eor #$ff
        sta ($8), y
        cpy #0
        bne invert_loop
        rts

wait:
;;; Waits a instants. An instant is close to 50 microseconds.
;;; In:
;;; a   number of instants to wait.
        ;; We want to wait a total of 90 cycles per instant.
        ;; On entry, we assume we already burned 8 cycles
        ;; (2 on an lda, and 6 on a jsr). When a is 1 on entry,
        ;; we spend 2 cycles on a sec, 2 on sbc, 3 on pha, 4 on pla,
        ;; 3 on beq, and 6 on rts. All that together gives
        ;; 8 + 2 + 2 + 3 + 4 + 3 + 6 = 28 cycles.
        ;; If a was not 1 on entry, we will avoid the rts and
        ;; the beq will take one cycle less, so we use 7 fewer
        ;; cycles. We add 3 cycles back in with a jmp, and 4
        ;; with 2 nops, getting back to 28 cycles.
        ;; To get to 90 cycles per instant, we need to burn
        ;; 62. We use a loop for this.
        ;; The initialization of the loop takes 2 cycles and
        ;; the last iteration takes 6 cycles, leaving 54 cycles
        ;; to burn. Each iteration other than the last takes 7
        ;; cycles, so we use 7 of those plus the last iteration,
        ;; for a total of 8 iterations.
        ;; That burns 49 + 6 + 2 = 57 of our 62 cycles.
        ;; We burn the remaining 5 using a nop and a jmp.
        sec
        sbc #1
        pha
        lda #8
        nop
        jmp wait_loop
wait_loop:
        sec
        sbc #1
        bne wait_loop
        pla
        beq wait_done
        nop
        nop
        jmp wait
wait_done:
        rts
        
row1:   .byt "` 1 2 3 4 5 6 7 8 9 0 - = BS",0
row2:   .byt "TB Q W E R T Y U I O P [ ] ",$5c,0
row3:   .byt "CTL A S D F G H J K L ; ' RET",0
row4:   .byt " SH  Z X C V B N M , . / SH UP",0
row5:   .byt "          SPACE      CTL LE DN RI",0
tbl:
        .byt 0,1,4,6,3
        .byt 0,2,6,10,2
        .byt 0,4,8,10,2
        .byt 0,8,10,10,2
        .byt 0,16,12,10,2
        .byt 0,32,14,10,2
        .byt 0,64,16,10,2
        
        .byt 1,1,4,10,2
        .byt 1,2,4,12,3
        .byt 1,4,7,12,2
        .byt 1,8,11,12,2
        .byt 1,16,13,12,2
        .byt 1,32,15,12,2
        .byt 1,64,17,12,2

        .byt 2,1,4,14,3
        .byt 2,2,8,14,2
        .byt 2,4,9,12,2
        .byt 2,8,10,14,2
        .byt 2,16,12,14,2
        .byt 2,32,14,14,2
        .byt 2,64,16,14,2
        .byt 2,128,5,16,2
        .byt 2,128,29,16,2

        .byt 3,1,23,16,2
        .byt 3,2,9,16,2
        .byt 3,4,11,16,2
        .byt 3,8,13,16,2
        .byt 3,16,15,16,2
        .byt 3,32,17,16,2
        .byt 3,64,19,16,2
        .byt 3,128,14,18,5

        .byt 4,1,28,10,2
        .byt 4,2,30,10,2
        .byt 4,4,26,10,2
        .byt 4,8,24,10,2
        .byt 4,16,22,10,2
        .byt 4,32,20,10,2
        .byt 4,64,18,10,2

        .byt 5,1,29,12,2
        .byt 5,2,31,12,2
        .byt 5,4,27,12,2
        .byt 5,8,25,12,2
        .byt 5,16,23,12,2
        .byt 5,32,19,12,2
        .byt 5,64,18,14,2

        .byt 6,1,28,14,2
        .byt 6,2,30,14,3
        .byt 6,4,24,14,2
        .byt 6,8,22,14,2
        .byt 6,16,21,12,2
        .byt 6,32,20,14,2
        .byt 6,64,21,16,2

        .byt 7,1,25,16,2
        .byt 7,2,27,16,2
        .byt 7,4,26,14,2
        .byt 7,8,29,18,2
        .byt 7,16,32,16,2
        .byt 7,32,32,18,2
        .byt 7,64,35,18,2
tbl_end:
title:  .byt "Keyboard test",0

_endcode:
        prevkbd = $3f38
        newkbd = $3f28
