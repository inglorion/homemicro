;;; Generates VGA synchronization signals from an ATtiny.

;;; This program is designed to work with a 12.5 MHz clock.
;;; It generates hsync on B0, vsync on B1, and an image
;;; active signal on B2.
;;;
;;; It provides the following timings (standard VGA timings
;;; included for reference).
;;; 
;;; Horizontal:
;;; Phase       Clocks  Time        Standard
;;; active      320     25.6us      25.422us
;;; front porch 8        0.64us      0.636us
;;; hsync low   47       3.76us      3.813us
;;; back porch  23       1.84us      1.907us
;;; line total  398     31.84us     31.778us
;;;
;;; Vertial:
;;; Phase       Lines   Time        Standard
;;; active      480     15.283ms    15.253ms
;;; front porch 10       0.318ms     0.318ms
;;; vsync low   2        0.064ms     0.064ms
;;; back porch  33       1.051ms     1.048ms
;;; frame total 525     16.716ms    16.683ms
;;; refresh rate        59.822Hz    59.940Hz
.equ DDRB, 0x17
.equ MCUSR, 0x34
.equ PORTB, 0x18
.equ WDTCR, 0x21
        
__vectors:
        ;; interrupt table
        rjmp __start
        rjmp __start
        rjmp __start
        rjmp __start
        rjmp __start
        rjmp __start
        rjmp __start
        rjmp __start
        rjmp __start
        rjmp __start

.global __start
__start:
        ;; first things first, disable interupts and reset watchdog
        cli
        wdr
        ;; set our outputs low as soon as we can
        eor r16, r16
        out PORTB, r16
        ;; we will use PORTB pins 0, 1, and 2 as outputs.
        ldi r16, 0x07
        out DDRB, r16
        ;; disable the watchdog so we won't suddenly reset
        ;; we can only clear WDE after we clear WDRF, so do that first
        in r16, MCUSR
        andi r16, 0xf7
        out MCUSR, r16
        ;; enable changing the WDTCR register
        in r16, WDTCR
        ori r16, 0x18
        out WDTCR, r16
        ;; finally, actually disable the watchdog
        ;; this sets WDTIE and WDE to 0, which disables the watchdog
        ;; it also sets WDTIF and WDCE to 0 (clearing the flag and disabling further changes),
        ;; and sets WDP to 7 (which shouldn't matter, since we disabled the watchdog anyway).
        ldi r16, 0x07
        out WDTCR, r16
        ;; watchdog disabled, enable interrupts again
        sei
        ;; r16 will be used to track value of PB
        ;; bit 0: vsync
        ;; bit 1: hsync
        ;; bit 2: video
        ;; we will use r17 to compute a new value for r16
        ;; r27:r26 will be set to the number of scan lines per frame (525)
        ldi r26, 13
        ldi r27, 2
        ;; r21:r20 will be set to the number of video lines (480)
        ldi r20, 224
        ldi r21, 1
        ;; vsync pulse starts at scan line 490, which is 256 + 234
        ldi r18, 234
        ;; vsync pulse ends at scan line 492, which is 256 + 236
        ldi r19, 236
        ;; r25:r24 will be used to keep track of the line number
        ;; we initialize them so that we start with vsync low
        mov r24, r18
        mov r25, r21
        ;; at hsync_low, hsync is expected to be low and video disabled.
        ;; we're also starting with vsync low. we've already set the
        ;; PORTB pins at the biginning of the program, so just set
        ;; r16 to the expected value.
        eor r16, r16
        ldi r17, 1
hsync_low:
        ;; because we execute an rjmp to get here, we start our cycle count at 2
        cp r25, r21             ; cycle 2
        brne no_vsync_msb       ; cycle 3
        cp r24, r18             ; cycle 4
        brlo before_vsync       ; cycle 5
        cp r19, r24             ; cycle 6
        brlo after_vsync        ; cycle 7
        andi r17, 253           ; cycle 8
        rjmp vsync_computed     ; cycle 9
no_vsync_msb:
        nop                     ; cycle 5
        nop                     ; cycle 6
before_vsync:
        nop                     ; cycle 7
        nop                     ; cycle 8
after_vsync:
        ori r17, 2              ; cycle 9
        nop                     ; cycle 10
vsync_computed:
        cp r25, r21             ; cycle 11
        brlo active_msb         ; cycle 12
        brne after_active       ; cycle 13
        cp r24, r20             ; cycle 14
        brlo active_lsb         ; cycle 15
        rjmp not_active         ; cycle 16
active_msb:
        nop                     ; cycle 14
        nop                     ; cycle 15
        nop                     ; cycle 16
active_lsb:
        ori r17, 4              ; cycle 17
        rjmp active_computed    ; cycle 18
after_active:
        nop                     ; cycle 15
        nop                     ; cycle 16
        nop                     ; cycle 17
not_active:
        andi r17, 251           ; cycle 18
        nop                     ; cycle 19
active_computed:
        ;; compute new scan line number
        adiw r24, 1             ; cycle 20
        ;; wrap around to 0 if we exceeded the number of scan lines
        cp r25, r27             ; cycle 22
        brlo line_msb_ok        ; cycle 23
        cp r24, r26             ; cycle 24
        brlo line_lsb_ok        ; cycle 25
        eor r24, r24            ; cycle 26
        eor r25, r25            ; cycle 27
        rjmp line_ok            ; cycle 28
line_msb_ok:
        nop                     ; cycle 25
        nop                     ; cycle 26
line_lsb_ok:
        nop                     ; cycle 27
        nop                     ; cycle 28
        nop                     ; cycle 29
line_ok:
        ;; wait until cycle 47, then set hsync high
        ;; that's 17 cycles from now. we need two cycles to set hsync,
        ;; leaving 15 cycles. we will use a wait loop. the loop
        ;; uses 3 cycles per iteration, except the last iteration,
        ;; which takes only 2 cycles. initialization takes 1 cycle,
        ;; so, in effect, we can count it as 3 cycles per iteration.
        ;; since we have 15 cycles to wait, that's 5 iterations.
        ldi r28, 5              ; cycle 30
keep_hsync_low:
        subi r28, 1
        brne keep_hsync_low
        ori r16, 1              ; cycle 45
        out PORTB, r16          ; cycle 46
        ;; wait 23 cycles, then set new values for active and vsync
        ;; we need 2 cycles to set the output pins, so that leaves
        ;; 21 cycles, which equals 7 iterations of a wait loop.
        ldi r28, 7              ; cycle 47
back_porch:
        subi r28, 1
        brne back_porch
        mov r16, r17            ; cycle 68
        out PORTB, r16          ; cycle 69
        ;; wait 320 cycles, then set active low. we need 2 cycles
        ;; to set the output pins, so that leaves 318 cycles, or
        ;; 106 iterations of a wait loop.
        ldi r28, 106            ; cycle 70
active:
        subi r28, 1
        brne active
        andi r16, 251           ; cycle 388
        out PORTB, r16          ; cycle 389
        ;; wait 8 cycles, then set hsync low
        ;; two cycles to set the pins, leaving 6 cycles to wait.
        nop                     ; cycle 390
        nop                     ; cycle 391
        nop                     ; cycle 392
        nop                     ; cycle 393
        nop                     ; cycle 394
        nop                     ; cycle 395
        andi r16, 254           ; cycle 396
        out PORTB, r16          ; cycle 397
        ;; next scan line
        rjmp hsync_low          ; cycle 398, also cycle 0
