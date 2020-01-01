        CURPOS = $80
        BUFPTR = $82
        SHOWADDR = $84
        SHOWCTR = $86
        PUTINTAD = $fa
        PUTINTDG = $fb
        PUTINTPS = $fc
        SAVEA = $fd
        SAVEX = $fe
        SAVEY = $ff
        IRQHNDLR = $0200
        NMIHNDLR = $0204
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
        beq cart_loaded
        dec $c0
        bne try_cart

cart_loaded:
        lda $2a00
        cmp #$48
        bne unloadable_image
        lda $2a01
        cmp #$4d
        bne unloadable_image
        lda $2a02
        bne unloadable_image
        lda $2a03
        cmp #$1
        bne unloadable_image

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
        bne unloadable_image
        lda #$aa
        sta $2007
        jmp ($c0)

unloadable_image:
        lda #$80
        sta $2000
        sta $2001
        sta $2002
        sta $2003
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

_end:
        jmp _end

bytess: .byt "bytes"

hexits: .byt "0123456789abcdef"

kbmap:  .byt $1b,"123456",$00
        .byt "`",$09,"qerty",$00
        .byt $00,"awsdfg",$00
        .byt ",zxcvbn "
        .byt "=",$08,"-0987",$00
        .byt "]",$5c,"[pouh",$00
        .byt "'",$0d,"lkijm",$00
        .byt "./;",$1e,$1c,$1d,$1f,$00

uinttbl: .word 80, 800, 8000, 40000

        .dsb $fffa-*,$ff
nmivec:
        .word NMIHNDLR
rstvec:
        .word _start
irqvec:
        .word IRQHNDLR
