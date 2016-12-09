;;; Generates VGA synchronization signals from an ATmega 328.
        
;;; This program is designed to work with a 14.31818 MHz clock
;;; supplied on XTAL1. This requires CKDIV8 to be set to 1
;;; and CKSEL[0:3] to 0000b. This can be accomplished by
;;; setting lfuse to 0xe0.
;;;
;;; The program generates vsync on PC5, hsync on PC4, an image active
;;; signal on PC3, and an address to load video data from
;;; on PB5:PB0:PD7:PD0 (most significant bit in PB5, least
;;; significant bit in PD0). PC2 is set to high after initialization
;;; has been completed.
;;;
;;; It provides the following timings (standard VGA timings
;;; included for reference).
;;; 
;;; Horizontal:
;;; Phase       Clocks  Time        Standard
;;; active      364     25.422us    25.422us      
;;; front porch   9      0.629us     0.636us
;;; hsync low    56      3.911us     3.813us
;;; back porch   27      1.886us     1.907us
;;; line total  456     31.848us    31.778us
;;;
;;; Vertial:
;;; Phase       Lines   Time        Standard
;;; active      480     15.287ms    15.253ms
;;; front porch  10      0.318ms     0.318ms
;;; vsync low     2      0.064ms     0.064ms
;;; back porch   33      1.051ms     1.048ms
;;; frame total 525     16.720ms    16.683ms
;;; refresh rate        59.809Hz    59.940Hz
;;;
;;; We only actually display graphics in a 320x200 area. We repeat every active
;;; line twice, and add 40 inactive lines before and after. Horizontally, we
;;; add 22 inactive pixels before and after the active part. In effect, this
;;; increases the front porch and back porch values.
;;;
;;; Loading pixel data is controlled by a shift register that cycles through
;;; 8 positions, each active for 1 clock cycle. When active is low, the
;;; register is reset to position 0, and the CPU has access to RAM. Once
;;; active goes high, the register starts cycling. On positon 2, we start
;;; reading a byte from screen RAM, and we write that byte to a shift
;;; register on position 3. This repeats every 8 cycles. After accounting
;;; for the increased front and back porch values, and the 3 cycle delay
;;; from active going high to pixel data actually being loaded, the values
;;; we program into the controller become:
;;;
;;; Horizontal:
;;; Phase       Clocks  Time
;;; active      320     22.349us
;;; front porch 34       2.375us
;;; hsync low   56       3.911us
;;; back porch  46       3.213us
;;; line total  456     31.848us
;;;
;;; Vertical:
;;; Phase       Lines   Time
;;; active      400     12.739ms
;;; front porch 50       1.592ms
;;; vsync low   2        0.064ms
;;; back porch  73       2.325ms
;;; frame total 525     16.720ms
;;; refresh rate        59.809Hz

        ;; We use in and out to access the DDR* and PORT* registers,
        ;; so those offsets are reduced by 0x20 compared to the values
        ;; in the datasheet.
        .equ DDRB, 0x04
        .equ DDRC, 0x07
        .equ DDRD, 0x0a    
        .equ MCUSR, 0x54
        .equ PORTB, 0x05
        .equ PORTC, 0x08
        .equ PORTD, 0x0b
        .equ WDTCSR, 0x60
        
__vectors:
        ;; Interrupt vectors.
        ;; The ATmega 328 has 26 interrupt vectors, each of which occupies
        ;; 2 words. Since we don't use interrupts, we set them all to
        ;; jump to __start.
        jmp __start
        jmp __start
        jmp __start
        jmp __start
        jmp __start
        jmp __start
        jmp __start
        jmp __start
        jmp __start
        jmp __start
        jmp __start
        jmp __start
        jmp __start
        jmp __start
        jmp __start
        jmp __start
        jmp __start
        jmp __start
        jmp __start
        jmp __start
        jmp __start
        jmp __start
        jmp __start
        jmp __start
        jmp __start
        jmp __start

.global __start
__start:
        ;; first things first, disable interupts and reset watchdog
        cli
        wdr
        ;; set our outputs low as soon as we can
        eor r16, r16
        out PORTB, r16
        out PORTC, r16
        out PORTD, r16
        ;; configure PORTB pins 0-5, PORTC pins 0-5, and PORTD pins 0-7 as
        ;; outputs.
        ldi r16, 0x3f
        out DDRB, r16
        out DDRC, r16
        ldi r16, 0xff
        out DDRD, r16
        ;; disable the watchdog so we won't suddenly reset
        ;; we can only clear WDE after we clear WDRF, so do that first
        lds r16, MCUSR
        andi r16, 0xf7
        sts MCUSR, r16
        ;; enable changing the WDTCR register
        lds r16, WDTCSR
        ori r16, 0x18
        sts WDTCSR, r16
        ;; finally, actually disable the watchdog
        ;; this sets WDTIE and WDE to 0, which disables the watchdog
        ;; it also sets WDTIF and WDCE to 0 (clearing the flag and disabling further changes),
        ;; and sets WDP to 7 (which shouldn't matter, since we disabled the watchdog anyway).
        ldi r16, 0x07
        sts WDTCSR, r16
        ;; watchdog disabled, enable interrupts again
        sei

        ;; r16 will be used to track value of PC
        ;; bit 5: vsync (set mask: 0x20, clear mask 0xdf)
        ;; bit 4: hsync (set mask: 0x10, clear mask 0xef)
        ;; bit 3: video (set mask: 0x80, clear mask 0xf7)
        ;; we will use r17 to compute a new value for r16
        ;; r29:r28 will be used to track the address. This starts at 0.
        eor r28, r28
        eor r29, r29
        ;; r27:r26 will be set to the number of scan lines per frame (525)
        ldi r26, 13
        ldi r27, 2
        ;; r21:r20 will be set to the number of video lines (400)
        ldi r20, 144
        ldi r21, 1
        ;; vsync pulse starts at scan line 450, which is 256 + 194
        ldi r18, 194
        ;; vsync pulse ends at scan line 452, which is 256 + 196
        ldi r19, 196
        ;; r25:r24 will be used to keep track of the line number
        ;; we initialize them so that we start with vsync low
        mov r24, r18
        mov r25, r21
        ;; at hsync_low, hsync is expected to be low and video disabled.
        ;; we're also starting with vsync low. Set r17 so that at the start
        ;; of the first active video, we will set bits 2 and 4 high.
        ldi r17, 0x14

hsync_low:
        ;; because we execute an rjmp to get here, we start our cycle count at 2
        ;; If r25:r24 < r21:r18, we are before vsync.
        cp r25, r21             ; cycle 2
        brne no_vsync_msb       ; cycle 3
        cp r24, r18             ; cycle 4
        brlo before_vsync       ; cycle 5
        ;; If r24 > r19, we are after vsync.
        cp r19, r24             ; cycle 6
        brlo after_vsync        ; cycle 7
        ;; Vsync active.
        andi r17, 0xdf          ; cycle 8
        rjmp vsync_computed     ; cycle 9
no_vsync_msb:
        nop                     ; cycle 5
        nop                     ; cycle 6
before_vsync:
        nop                     ; cycle 7
        nop                     ; cycle 8
after_vsync:
        ori r17, 0x20           ; cycle 9
        nop                     ; cycle 10
vsync_computed:
        ;; If r25:r24 < r21:r20, we are on an active scan line.
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
        ori r17, 0x08           ; cycle 17
        rjmp active_computed    ; cycle 18
after_active:
        nop                     ; cycle 15
        nop                     ; cycle 16
        nop                     ; cycle 17
not_active:
        andi r17, 0xf7          ; cycle 18
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
        eor r28, r28            ; cycle 28
        eor r29, r29            ; cycle 29
        rjmp line_ok            ; cycle 30
line_msb_ok:
        nop                     ; cycle 25
        nop                     ; cycle 26
line_lsb_ok:
        nop                     ; cycle 27
        nop                     ; cycle 28
        nop                     ; cycle 29
        nop                     ; cycle 30
        nop                     ; cycle 31
line_ok:
        ;; Compute new address.
        ;; If scanline is a multiple of 16, clear lowest 3 bits of address.
        ;; E.g. address 327 becomes 320.
        ;; Else, subtract 320 from scanline. E.g. 321 becomes 1.
        ;; Then, if scanline is even, add 1 to address.
        ;; E.g. 1 becomes 2.
        mov r22, r24            ; cycle 32
        andi r22, 15            ; cycle 33
        breq line_multiple16    ; cycle 34
        subi r28, 64            ; cycle 35
        sbci r29, 1             ; cycle 36
        andi r22, 1             ; cycle 37
        breq incr_addr          ; cycle 38
        nop                     ; cycle 39
        rjmp address_adjusted   ; cycle 40

line_multiple16:
        andi r28, 0xf8          ; cycle 36
        nop                     ; cycle 37
        nop                     ; cycle 38
        nop                     ; cycle 39
        rjmp address_adjusted   ; cycle 40
        
incr_addr:
        adiw r28, 1             ; cycle 40
        
address_adjusted:
        ;; wait until cycle 56, then set hsync high
        ;; that's 14 cycles from now. we need a cycle to set r16,
        ;; leaving 13 cycles. That's 4 iterations of a wait loop,
        ;; plus one nop.
        ldi r22, 4              ; cycle 42
keep_hsync_low:
        subi r22, 1
        brne keep_hsync_low
        nop                     ; cycle 54
        ori r16, 0x10           ; cycle 55
        out PORTC, r16          ; cycle 56
        ;; wait 46 cycles, then set new values for active and vsync
        ;; we need 2 cycles to set the output pins, so that leaves
        ;; 44 cycles. We use 13 iterations of the wait loop, leaving
        ;; 5 cycles after the loop to set output pins.
        ldi r22, 13              ; cycle 57
back_porch:
        subi r22, 1
        brne back_porch
        mov r16, r17            ; cycle 96
        nop                     ; cycle 97
        nop                     ; cycle 98
        nop                     ; cycle 99
        out PORTD, r28          ; cycle 100 (8n+4)
        out PORTB, r29          ; cycle 101 (8n+5)
        out PORTC, r16          ; cycle 102
        nop                     ; cycle 103
        nop                     ; cycle 104
        ;; count for 320 cycles, incrementing the address every
        ;; 8 cycles. after that, set active low. we need 2 cycles
        ;; to set active low, so that leaves 318 cycles. A wait
        ;; loop takes 3 cycles. In our case, we need 4 additional
        ;; cycles to increment the address and write it to the
        ;; output pins. With the insertion of one nop, that gives
        ;; us 8 cycles per iteration of the loop. That means we
        ;; need 39 iterations of the loop, plus the last one,
        ;; where we don't update the loop counter but do pull
        ;; active low.
        ldi r22, 39             ; cycle 105
active:
        adiw r28, 8
        nop
        out PORTD, r28          ; cycle 109, 117, 125, ... (8n+5)
        out PORTB, r29          ; cycle 110, 118, 126, ... (8n+6)
        subi r22, 1
        brne active
        
        adiw r28, 8             ; cycle 106 + 39 * 8 = 418
        nop                     ; cycle 420
        andi r16, 0xf7          ; cycle 421
        out PORTC, r16          ; cycle 422
        ;; wait 34 cycles, then set hsync low
        ;; two cycles to set the pins, leaving 32 cycles to wait.
        ;; that's 10 iterations of a wait loop plus 2 nops.
        ldi r22, 10             ; cycle 423
front_porch:
        subi r22, 1
        brne front_porch
        nop                     ; cycle 453
        andi r16, 0xef          ; cycle 454
        out PORTC, r16          ; cycle 455
        nop                     ; cycle 456
        ;; next scan line
        rjmp hsync_low          ; cycle 457, also cycle 0
