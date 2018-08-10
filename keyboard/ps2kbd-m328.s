;;; PS/2 keyboard interface for Home Micro.

;;; Design:
;;;
;;; Row input pins: PB0..PB5, PB7, PC0.
;;; Row change pin: PC1.
;;; Column output pins: PD0..PD7.
;;; PS/2 clock: PC4
;;; PS/2 data: PC5
;;; Clock: 7.16MHz on XTAL1
;;; Fuses: e:07 h:d9 l:e2
;;; 
;;; We use RAM to store the current state of the keyboard in the
;;; row and column format expected by the Home Micro.
;;; In a loop, we continuously:
;;; 1. Read row number from input pins.
;;; 2. Write column information to output pins.
;;; 3. Read from keyboard.
;;; 4. Update stored information.
;;;
;;; r16 holds the last 8 data bits received.
;;; r17 holds parity and stop bits.
;;; r18 is used to read PINC.
;;; r19 is used to hold status bits:
;;;  0x80: break code (after receiving 0xf0).
;;;  0x40: extended code (after receiving 0xe0).
;;; r24 is used by the interrupt handler.
;;; r25 is used by the interrupt handler.
;;; r26:r27 used as address in interrupt handler.
;;; r28:r29 used as address outside interrupt handler.
;;; r30:r31 used as address outside interrupt handler.
;;;
;;; Clock ticks happen every ~140ns. PS/2 gives us at least
;;; 30us between clock changes. That gives us over 200 clock
;;; cycles between PS/2 clock changes - enough time that the
;;; interrupt handler shouldn't be a problem.
;;;
;;; Use pin change interrupt to detect row changes. We can't use
;;; int0 and int1 because those overlap with PORTD.
;;; Use PCINT9 instead.

        ;; We use in and out to access the DDR*, PIN*, PORT*, and SREG
        ;; registers, so those offsets are reduced by 0x20 compared to
        ;; the values in the datasheet.
        .equ DDRB, 0x04
        .equ DDRC, 0x07
        .equ DDRD, 0x0a    
        .equ MCUSR, 0x54
        .equ PCICR, 0x68
        .equ PCMSK1, 0x6c
        .equ PINB, 0x03
        .equ PINC, 0x06
        .equ PORTC, 0x08
        .equ PORTD, 0x0b
        .equ SREG, 0x3f
        .equ WDTCSR, 0x60

__vectors:
        ;; Interrupt vectors.
        ;; The ATmega 328 has 26 interrupt vectors, each of which occupies
        ;; 2 words. Since we don't use interrupts, we set them all to
        ;; jump to __start.
        jmp __start             ; reset
        jmp __start             ; int0
        jmp __start             ; int1
        jmp __start             ; pcint0
        jmp rowchange           ; pcint1
        jmp __start             ; pcint2
        jmp __start             ; wdt
        jmp __start             ; timer2_compa
        jmp __start             ; timer2_compb
        jmp __start             ; timer2_ovf
        jmp __start             ; timer1_capt
        jmp __start             ; timer1_compa
        jmp __start             ; timer1_combp
        jmp __start             ; timer1_ovf
        jmp __start             ; timer0_compa
        jmp __start             ; timer0_compb
        jmp __start             ; timer0_ovf
        jmp __start             ; spi stc
        jmp __start             ; usart_rx
        jmp __start             ; usart_udre
        jmp __start             ; usart_tx
        jmp __start             ; adc
        jmp __start             ; ee ready
        jmp __start             ; analog comp
        jmp __start             ; twi
        jmp __start             ; spm ready

rowchange:
        ;; Read row address from PINB and PINC, read value from memory,
        ;; write value to PORTD.
        ;; Addresses: 0000 001x x0xx xxxx
        ;; On entry, we will be at most 10 cycles after the row number
        ;; change (up to 3 cycles to finish an instruction, 4 cycles of
        ;; set up, and 3 cycles to execute the jmp in the vector table).
        in r24, SREG            ; 1
        in r26, PINB            ; 2
        in r27, PINC            ; 3
        andi r26, 0xbf          ; 4
        andi r27, 0x01          ; 5
        ori r27, 0x02           ; 6
        ld r25, X               ; 8
        out PORTD, r25          ; 9
        out SREG, r24           ; 10
        reti

.global __start
__start:
        ;; first things first, disable interupts and reset watchdog
        cli
        wdr
        ;; set our outputs high as soon as we can
        ldi r16, 0xff
        out PORTD, r16
        out DDRD, r16
        ;; Ensure pull-up resistors on PINC are off.
        eor r16, r16
        out PORTC, r16
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

        ;; Set the memory locations we use to store keyboard state to 0xff.
        eor r28, r28
        ldi r29, 0x02
        ldi r18, 0xff
ff_loop:
        st Y+, r18
        cpi r29, 0x04
        brne ff_loop

        ;; Set up PC1 to trigger rowchange.
        ldi r16, 0x02
        sts PCMSK1, r16
        sts PCICR, r16

        ;; We will use r17 and r16 to hold incoming bits.
        ;; We receive bits 11 at a time (start, 8 data, parity, stop).
        ;; We shift r17 and r16 right, then store the new bit in r17's
        ;; bit 1. This way, after 11 shifts, the data bits will be in
        ;; r16.
main_loop:
        ;; Load 10 ones into r16 and r17. Since the start bit is 0,
        ;; we can detect we received a byte once the carry shifted
        ;; out is 0.
        ldi r17, 0x03
        ser r16
read_loop:
        ;; Wait for r18 bit 4 to become low.
        in r18, PINC
        sbrc r18, 4
        rjmp read_loop
        ;; Clock is low, read data bit.
        lsr r17
        ror r16
        ;; TODO: Remove this.
        out PORTD, r16
        sbrc r18, 5
        sbr r17, 0x02
read_loop_wait_high:
        ;; Wait for r18 bit 4 to become high.
        in r18, PINC
        sbrs r18, 4
        rjmp read_loop_wait_high
        brcs read_loop

        cpi r16, 0xf0
        brne not_f0
        sbr r19, 0x80
        rjmp main_loop
not_f0:
        cpi r16, 0xe0
        brne not_e0
        sbr r19, 0x40
        rjmp main_loop
not_e0:
        mov r30, r16
        ldi r31, hi8(scantable)
        lpm r18, Z
        cpi r18, 0xff
        breq ignore_key
        mov r30, r18
        andi r30, 0x07
        ori r30, lo8(masktable)
        ldi r31, hi8(masktable)
        lpm r20, Z
        mov r30, r18
        swap r30
        andi r30, 0x07
        ori r30, lo8(masktable)
        lpm r30, Z
        mov r31, r30
        lsl r31
        andi r31, 0x80
        andi r30, 0x3f
        or r30, r31
        rol r31
        andi r31, 0x01
        ori r31, 0x02
        ld r18, Z
        cpi r19, 0x80
        brlo key_pressed
        com r20
        or r18, r20
        rjmp update_key_bit
key_pressed:
        and r18, r20
update_key_bit:
        st Z, r18
        ;; After updating the keyboard state, output the column information
        ;; for the currently selected row. This ensures that if an event
        ;; affected the current row's state, this is reflected on the
        ;; output pins.
        ;; This code can safely be interrupted by row number changes,
        ;; because the ISR will cause r25 to have the correct value.
        ld r25, X
        out PORTD, r25
ignore_key:
        andi r19, 0x3f
        rjmp main_loop

        .align 3, 0xff
masktable:
        ;; Table that translates bit numbers to bitmasks.
        .byte 0xfe, 0xfd, 0xfb, 0xf7, 0xef, 0xdf, 0xbf, 0x7f
        
        .align 8, 0xff
scantable:
        ;; PS/2 scan code to Home Micro scan code mapping. Use the PS/2
        ;; scan code as an offset into this table, then find a byte where
        ;; the top nybble indicates the row and the bottom nybble indicates
        ;; the column. A value of 0xff indicates an unmapped (ignored) code.
        ;; For extended PS/2 scan codes (0xe0 received), add 0x80 to the
        ;; offset in the table. For example, if the PS/2 scan code is
        ;; 0xe0, 0x75, the offset in the table is 0xf5. At that offset,
        ;; find byte 0x74. Column 7, row 4 (up key).
        .byte 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
        .byte 0xff, 0xff, 0xff, 0xff, 0xff, 0x11, 0x10, 0xff
        .byte 0xff, 0xff, 0x27, 0xff, 0x20, 0x12, 0x01, 0xff
        .byte 0xff, 0xff, 0x31, 0x23, 0x21, 0x22, 0x02, 0xff
        .byte 0xff, 0x33, 0x32, 0x24, 0x13, 0x04, 0x03, 0xff
        .byte 0xff, 0x37, 0x34, 0x25, 0x15, 0x14, 0x05, 0xff
        .byte 0xff, 0x36, 0x35, 0x56, 0x26, 0x16, 0x06, 0xff
        .byte 0xff, 0xff, 0x66, 0x65, 0x55, 0x46, 0x45, 0xff
        .byte 0xff, 0x30, 0x63, 0x64, 0x54, 0x43, 0x44, 0xff
        .byte 0xff, 0x70, 0x71, 0x62, 0x72, 0x53, 0x42, 0xff
        .byte 0xff, 0xff, 0x60, 0xff, 0x52, 0x40, 0xff, 0xff
        .byte 0xff, 0x27, 0x61, 0x50, 0xff, 0x51, 0xff, 0xff
        .byte 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x41, 0xff
        .byte 0xff, 0xff, 0xff, 0x73, 0xff, 0xff, 0xff, 0xff
        .byte 0xff, 0xff, 0x75, 0xff, 0x76, 0x74, 0x00, 0xff
        .byte 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
