        CURPOS = $80
        FONTBASE = $e800
        IOBASE = $d000        
        IRQHNDLR = $0200
        NMIHNDLR = $0204
        RAMTOP = $021e
        ROMBASE = $e000
        SERIR = $d004
        SERCR = $d005

* = ROMBASE
        .byt $01, $00, $00
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
        
        ;; Show memcheck result in the bottom right corner of the screen.
        jsr cls
        lda #$55
        sta $3f3d
        lda RAMTOP
        sta $3f3e
        lda RAMTOP + 1
        sta $3f3f

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

loadcart:
;;; Loads data from a cartridge.
;;; In:
;;; $a0..$a3  Location of first byte to load.
;;; $a4..$a5  Number of bytes to load.
;;; $a6..$a7  Address to write first byte to.
;;; Out:
;;; a       0 if successful. Any other value indicates an error.
;;; Clobbers x.
        jsr twi_start
        jsr cart_set_location
        cmp #0
        bne loadcart_done
        jsr twi_restart
        cmp #0
        bne loadcart_done
        lda #$a1
        jsr twi_send_byte
        cmp #0
        bne loadcart_done
        dec $a4
        bne loadcart_loop
        lda $a5
        beq loadcart_last
loadcart_loop:
        jsr twi_recv_byte
        ldx #0
        sta ($a6,x)
        jsr twi_ack
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
        jsr twi_recv_byte
        ldx #0
        sta ($a6,x)
        jsr twi_nak
        lda #0

loadcart_done:
        tax
        jsr twi_stop
        txa
        rts

cart_set_location:
;;; Sets the location for the next cartridge operation.
;;; Preconditions:
;;;  - twi_start must have been called.
;;; In:
;;; $a0..$a3  Location to set.
;;; Out:
;;; a       0 if successful. Any other value indicates an error.
;;; Clobbers x.
        lda $a2
        asl
        ora #$a0
        jsr twi_send_byte
        cmp #0
        bne cart_set_location_done
        lda $a1
        jsr twi_send_byte
        cmp #0
        bne cart_set_location_done
        lda $a0
        jsr twi_send_byte
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

twi_ack:
;;; Clobbers a.
        lda #$1f
twi_send_bit:
        sta SERCR
        ora #$40
        sta SERCR
        and #$bf
        sta SERCR
        rts
        
twi_nak:
;;; Clobbers a.
        lda #$9f
        jmp twi_send_bit
        
twi_recv_byte:
;;; Receives a byte over TWI. This function does not send an ACK or NAK,
;;; so twi_ack or twi_nak must be called to complete the receipt of the
;;; byte.
;;; Out:
;;; a   The byte received.
;;; Clobbers x.
        lda #$9f
        sta SERCR
        ldx #8
twi_recv_byte_loop:
        lda #$df
        sta SERCR
        and #$bf
        sta SERCR
        dex
        bne twi_recv_byte_loop
        lda SERIR
        rts

twi_restart:
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
        jmp twi_start

twi_send_byte:
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
twi_send_byte_loop:
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
        bne twi_send_byte_loop
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

twi_start:
;;; Out:
;;; a   0 if successful. Any other value means an error occurred.
        lda #$5f
        sta SERCR
        ;; Drive down the clock signal 8us (15 clock cycles) later.
        ;; It takes 6 cycles to execute the and and sta, so we need
        ;; to delay for 9 cycles - one jmp and 3 nops.
        jmp twi_start_delay
twi_start_delay:
        nop
        nop
        nop
        lda #$1f
        sta SERCR
        lda #0
        rts

twi_stop:
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

        .dsb $fffa-*,$ff
nmivec: 
        .word NMIHNDLR
rstvec: 
        .word _start
irqvec: 
        .word IRQHNDLR
