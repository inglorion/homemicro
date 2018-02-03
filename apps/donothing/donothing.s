* = $0000
        .byt "HM",0,1
        .word _start
        .word 32
        .word _endhdr
        .word _endcode-_start
        .word _start
        .word 0

_endhdr:
        ;; * = $0300
        * = $2140
_start:
        lda #$a0
        sta $2000
        lda #$05
        sta $2001
_end:
        jmp _end

        ;; .dsb  $0320-*,$ea
        .dsb $2160-*,$ea
        .byte $ff,$ff,$ff,$ff,$ff,$ff,$ff,$ff
        .byte 0,0,0,0,0,0,0,0
        .byte $00,$30,$cc,$fc,$cc,$cc,$cc,$00
        
_endcode:
