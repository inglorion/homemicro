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
        lda micrornd_state + 1  ; 3    1
        eor micrornd_state + 3  ; 6    1
        sta micrornd_state + 1  ; 9    1
        inc micrornd_state + 3  ; 14   1

        lda micrornd_state + 1  ; 17   1
        asl                     ; 19
        eor #$d5                ; 21
        adc micrornd_state + 2  ; 24   1
        sta micrornd_state + 1  ; 27   1
        lda micrornd_state + 2  ; 30   1
        adc #1                  ; 32
        sta micrornd_state + 2  ; 35   1
        lda micrornd_state      ; 38   1
        adc micrornd_state + 1  ; 41   1
        sta micrornd_state      ; 44   1
        rts
