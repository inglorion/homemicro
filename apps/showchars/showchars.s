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
        sta $80
        sta $82
        lda #$20
        sta $81
        lda #$e8
        sta $83
main_loop:
        jsr cpy8
        clc
        lda #8
        adc $82
        sta $82
        lda #0
        adc $83
        cmp #$ec
        beq _end
        sta $83

        clc
        lda #8
        adc $80
        sta $80
        lda #0
        adc $81
        sta $81

        lda $82
        and #$7f
        bne main_loop
        clc
        lda #$c0
        adc $80
        sta $80
        lda #0
        adc $81
        sta $81
        jmp main_loop
        
_end:
        jmp _end


cpy8:
;;; Copies 8 bytes from ($82) to ($80).
;;; Clobbers y.
        ldy #0
cpy8_loop:
        lda ($82), y
        sta ($80), y
        iny
        cpy #8
        bcc cpy8_loop
        rts
        
_endcode:
