        cls = $e003
        curpos = $80
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
        ;; Initialization.
        ;; We want to keep the number of instructions we rely on small,
        ;; so that the tests can work even if not all instructions are
        ;; not completely correctly implemented. However, to put the CPU
        ;; in a known state, we execute some instructions here that we
        ;; otherwise do not rely on. If any of these are not implemented
        ;; in an emulator being tested, they can be commented out.
        sei
        cld
        ldx #$ff
        txs
        jsr initoutp

        ;; Test bne instruction.
        ;; Instructions assumed working:
        ;;  - lda immediate must correctly load value, set the z flag
        ;;    if it is 0, and clear the z flag if it is nonzero.
        ;;  - jmp absolute must transfer control to the correct address.
        ;;  - jsr must save address and transfer control to the correct
        ;;    address.
        ;;  - rts must return to the instruction after the most recent jsr.
        ;;  - nop
        ;; The test is surrounded by nop slides that jmp to the failure
        ;; code.
        ;; For correct reporting, the report, ok, and fail procedures
        ;; must also work.
        jmp test_bne
        .dsb 253, $ea
        jmp bne_fail
test_bne:
        lda #$00
        jsr report
        lda #0
        bne bne_fail
        lda #1
        bne bne_ok
bne_fail:
        jsr fail
        jmp halt
bne_ok1:
        jmp bne_done
bne_ok:
        lda #0
        bne bne_fail
        lda #1
        bne bne_ok1
        .dsb 253, $ea
        jmp bne_fail
bne_done:
        jsr ok

test_cmpz:
        ;; Tests that the cmp instruction sets the z flag as expected.
        lda #$01
        jsr report
        lda #0
        cmp #0
        bne cmpz_fail
        cmp #1
        bne cmpz_ok
cmpz_fail:
        jsr fail
        jmp halt
cmpz_ok:
        lda #1
        sta t0
        cmp t0
        bne cmpz_fail
        lda #0
        cmp t0
        bne cmpz_ok1
        jmp cmpz_fail
cmpz_ok1:
        jsr ok

test_flags:
        ;; Test processor status register.
        ;; Layout:
        ;; N V 1 B D I Z C
        ;; | | | | | | | |
        ;; | | | | | | | \-- carry
        ;; | | | | | | \---- zero
        ;; | | | | | \------ interrupt disable
        ;; | | | | \-------- decimal mode
        ;; | | | \---------- break
        ;; | | \------------ always 1
        ;; | \-------------- overflow
        ;; \---------------- negative
        ;; 
        ;; Instructions assumed working:
        ;;  - lda immediate must correctly load value.
        ;;  - jmp absolute must transfer control to the correct address.
        ;;  - jsr must save address and transfer control to the correct
        ;;    address.
        ;;  - rts must return to the point right after jsr.
        ;;  - cmp immediate must set the zero flag when imm is equal to a,
        ;;    and clear the zero flag when they are not equal.
        ;;  - bne must transfer control to the correct location.
        ;;  - php must push processor status.
        ;;  - pla must pull a.
        lda #$02
        jsr report
        lda #2
        cmp #1
        clc
        cli
        php
        sei
        pla
        ;; only b set
        cmp #$30
        bne fail02
        jmp ok02
fail02:
        jsr fail
        jmp halt
ok02:
        lda #2
        cmp #1
        clc
        php
        pla
        ;; only b and i set.
        cmp #$34
        bne fail02
        lda #0
        cmp #0
        clc
        php
        pla
        ;; b, i and z set
        cmp #$36
        bne fail02
        lda #0
        cmp #0
        php
        pla
        ;; b, c, i and z set
        cmp #$37
        bne fail02
        lda #1
        cmp #0
        php
        pla
        ;; b, c, and i set
        cmp #$35
        bne fail02
        lda #0
        cmp #1
        php
        pla
        ;; b, i and n set
        cmp #$b4
        bne fail02
        sec
        php
        pla
        ;; b, c, i, and z set
        cmp #$37
        bne fail02
        jsr ok

        ;; At this point, we have a basic set of useful instructions:
        ;;  - bne jumps to the correct location under the right conditions.
        ;;  - clc, cli, sec, and sei work.
        ;;  - cmp correctly sets the flags.
        ;;  - jmp absolute, jsr, and rts work.
        ;;  - lda correctly loads immediate values and sets the z flag.
        ;;  - php pushes the processor status register
        ;;  - pha pulls a
        ;;  - the flags are in the correct locations in the processor
        ;;    status register.

test_pha:
        ;; Tests the pha instruction.
        lda #$03
        jsr report
        lda #$53
        bne pha_nzok
        jmp pha_fail
pha_nzok:
        cmp #$53
        ;; Save flags after equal comparison.
        php
        pla
        sta t0
        lda #$53
        cmp #$53
        ;; Flags at this point should equal saved flags.
        pha
        ;; pha should not have affected flags.
        php
        pla
        cmp t0
        bne pha_fail
        ;; After pla, z flag should be clear and a
        ;; should be #$53.
        lda #0
        pla
        bne pha_nzok1
        jmp pha_fail
pha_nzok1:
        cmp #$53
        bne pha_fail
        jsr ok
        jmp pha_done
pha_fail:
        jsr fail
pha_done:

test_and:
        lda #$04
        jsr report
        ;; and with #$80 should clear all but the highest bit.
        lda #$ff
        and #$80
        cmp #$80
        bne and_fail
        lda #$80
        and #$7f
        cmp #0
        bne and_fail
        ;; and should set the z flag depending on the result.
        lda #$aa
        and #$55
        bne and_fail
        lda #$ab
        and #$55
        bne andne
        jmp and_fail
andne:
        lda #$e3
        sta t0
        ;; and with 0 should always result in 0.
        and #0
        bne and_fail
        ;; and should work on values in memory.
        lda #$ff
        and t0
        cmp #$e3
        bne and_fail
        lda #$55
        and t0
        cmp #$41
        bne and_fail
        ;; and should set the n flag depending on the result.
        lda #$7f
        and t0
        php
        pla
        and #$80
        bne and_fail
        lda t0
        and #$80
        php
        pla
        and #$80
        cmp #$80
        bne and_fail
        jsr ok
        jmp and_done
and_fail:
        jsr fail
and_done:

test_ora:
        lda #$05
        jsr report
        ;; ora with 0 leaves value unchanged.
        lda #$a0
        sta t0
        lda #0
        ora t0
        cmp #$a0
        bne ora_fail
        ;; ora with some set bits causes those bits
        ;; to become set.
        lda t0
        ora #$15
        cmp #$b5
        bne ora_fail
        ;; ora sets n flag.
        lda #0
        ora t0
        php
        pla
        and #$80
        cmp #$80
        bne ora_fail
        ;; ora clears z flag.
        lda #0
        ora t0
        bne orane
        jmp ora_fail
orane:
        ;; ora sets z flag.
        lda #0
        sta t0
        cmp #1
        ora t0
        bne ora_fail
        jsr ok
        jmp ora_done
ora_fail:
        jsr fail
ora_done:

        ;; At this point, in addition to the basic instructions
        ;; established earlier, we have and, ora, and pha.
        ;; We will use these with the plp instruction to arbitrarily
        ;; manipulate processor flags, and use those flags to test
        ;; branch instructions.
        
test_branches:
        lda #$06
        jsr report
        php
        pla
        ora #2
        pha
        plp
        beq beq_taken
        jmp branches_fail
beq_taken:
        and #$fd
        pha
        plp
        beq branches_fail
        ora #1
        pha
        plp
        bcc branches_fail
        bcs bcs_taken
        jmp branches_fail
bcs_taken:
        and #$fe
        pha
        plp
        bcs branches_fail
        bcc bcc_taken
        jmp branches_fail
bcc_taken:
        ora #$80
        pha
        plp
        bpl branches_fail
        bmi bmi_taken
        jmp branches_fail
bmi_taken:
        and #$7f
        pha
        plp
        bmi branches_fail
        bpl bpl_taken
        jmp branches_fail
bpl_taken:
        ora #$40
        pha
        plp
        bvc branches_fail
        bvs bvs_taken
        jmp branches_fail
bvs_taken:
        and #$bf
        pha
        plp
        bvs branches_fail
        bvc bvc_taken
        jmp branches_fail
bvc_taken:
        jsr ok
        jmp branches_done
branches_fail:
        jsr fail
        jmp halt
branches_done:

test_xy:
        ;; Tests that we can load, store, and compare x and y.
        lda #$08
        jsr report
        ldx #$8f
        beq xy_fail
        bpl xy_fail
        cpx #$90
        beq xy_fail
        bcs xy_fail
        bpl xy_fail
        cpx #$7f
        beq xy_fail
        bcc xy_fail
        bmi xy_fail
        cpx #$8f
        bne xy_fail
        bcc xy_fail
        bmi_xy_fail
        stx t0
        ldx #0
        bne xy_fail
        bmi xy_fail
        ldx t0
        cpx #$8f
        bne xy_fail
        ldy #$7f
        beq xy_fail
        bmi xy_fail
        cpy #$80
        beq xy_fail
        bcs xy_fail
        bpl xy_fail
        cpy #$7e
        beq xy_fail
        bcc xy_fail
        bmi xy_fail
        cpy #$7f
        bne xy_fail
        bcc xy_fail
        bmi xy_fail
        ;; Test indexed loads.
        lda #1
        sta t0
        lda #2
        sta t1
        ldy #0
        ldx t0, y
        cpx #1
        bne xy_fail
        ldy #1
        ldx t0, y
        cpx t1
        bne xy_fail
        ldx #1
        ldy t0, x
        cpy #2
        bne xy_fail
        ldx #0
        ldy t0, x
        cpy t0
        bne xy_fail
        jsr ok
        jmp xy_done
xy_fail:
        jsr fail
xy_done:

test_wrap:
        ;; Tests that zero page indexed loads wrap around.
        lda #$09
        jsr report
        ;; zp, y wraps around.
        lda #3
        sta zp0
        ldy #$ff
        ldx zp0 + 1, y
        cpx #3
        bne wrap_fail
        lda #4
        sta zp0
        ldx zp0 + 1, y
        cpx #4
        bne wrap_fail
        ;; zp, x wraps around.
        lda #3
        ldx #$ff
        ldy zp0 + 1, x
        cpy #4
        bne wrap_fail
        sta zp0
        ldy zp0 + 1, x
        cpy #3
        bne wrap_fail
        jsr ok
        jmp wrap_done
wrap_fail:
        jsr fail
wrap_done:

test_adc:
        lda #$0a
        jsr report
        lda #1
        sta t0
        ;; no carry in
        lda #0
        clc
        adc #$2a
        bcs adc_fail
        bvs adc_fail
        bmi adc_fail
        cmp #$2a
        bne adc_fail
        ;; carry in
        sec
        adc #0
        bcs adc_fail
        bvs adc_fail
        bmi adc_fail
        cmp #$2b
        bne adc_fail
        ;; no overflow, positive
        lda #$7e
        clc
        adc #1
        bcs adc_fail
        bvs adc_fail
        bmi adc_fail
        cmp #$7f
        bne adc_fail
        ;; overflow, negatize
        clc
        adc t0
        bcs adc_fail
        bvc adc_fail
        bpl adc_fail
        cmp #$80
        bne adc_fail
        ;; overflow, carry, positive
        lda #$ff
        clc
        adc t0
        bcc adc_fail
        bvc adc_fail
        bmi adc_fail
        cmp #$7f
        bne adc_fail
        jsr ok
        jmp adc_done
adc_fail:
        jsr fail
adc_done:

test_asl:


        
halt:
        jmp halt

initoutp:
;;; Initialize output.
;;; This clears the screen and sets the cursor to the
;;; beginning of the screen.
        jsr cls
        lda #$20
        sta curpos + 1
        lda #0
        sta curpos
        rts

ok:
;;; Report success. Displays a minus sign.
        lda #$2d
        jmp showchr

fail:
;;; Report failure. Displays an exclamation mark.
        lda #$21
        jmp showchr

report:
;;; Report what we're currently working on.
;;; For simplicity, this just outputs a in hexadecimal.
;;; Clobbers y.
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
_end:
