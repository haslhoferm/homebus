
   .section .text

   .global FlashSum
   .global FlashFillPagePuffer
   .global FlashProgramPagePuffer
   .global FlashPageErase

;-----------------------------------------------------------------------------
;  Summe �ber Flash berechnen
;  UINT16 FlashSum(UINT16 wordAddr, UINT8 numWords)
;
FlashSum:
   ; Z-Register aufsetzen
   movw r30, r24 ; lsb  (r25:r24 -> z, r24 lsb, r25 msb wordaddr)
   ; �bergabeparameter mit 2 multiplizieren
   clc
   rol r30
   rol r31

                  ; numWords wird in r22 �bergeben und wird als loop counter verwendet 
   ldi r24, 0     ; sum lsb
   ldi r25, 0     ; sum msb
   
1:
   lpm r27, z+
   clc
   adc  r24, r27
   lpm r27, z+
   adc  r25, r27

   subi r22, 1 
   brne 1b
   
   ; returnwert in r24/25 zur�ckgeben (sum liegt schon dort)
   ret
;-----------------------------------------------------------------------------
;  Pagepuffer mit Daten f�llen
;  void FlashFillPagePuffer(UINT16 offset, UINT16 *pBuf, UINT8 numWords) {
;               
FlashFillPagePuffer:
   ; offset    r25:r24
   ; pBuf      r23:r22
   ; numWords  r20
   ; transfer data from RAM to Flash page buffer
   ; offset -> Z 
   movw r30, r24
   clc
   rol r30
   rol r31
   
   ; numWords in r20 (als loop counter verwendet)

   ; pBuf -> x
   movw r26, r22

1:
   ld  r0, x+
   ld  r1, x+
   ldi r18, 0x01 ; (1<<SPMEN)
   rcall Do_spm
   adiw r30, 2
   subi r20, 1 ;use subi for PAGESIZEB<=256
   brne 1b
   clr r1    ; r1 muss bei WinAVR wieder auf 0 gesetzt werden
   ret

;-----------------------------------------------------------------------------
;  Pagepuffer in Flash programmieren
;  void FlashProgramPagePuffer(UINT16 pageWordAddr) {
;
FlashProgramPagePuffer:
   ; pageWordAddr    r25:r24
   ; Z-Register aufsetzen
   movw r30, r24
   ; �bergabeparameter mit 2 multiplizieren
   clc
   rol r30
   rol r31

   ; execute page write
   ldi r18, 0x05      ; (1<<PGWRT) | (1<<SPMEN)
   rcall Do_spm

   ; re-enable the RWW section
   ldi r18, 0x11      ; (1<<RWWSRE) | (1<<SPMEN)
   rcall Do_spm

   ret

;-----------------------------------------------------------------------------
;  Page l�schen
;  void FlashPageErase(UINT16 pageWordAddr) {
;
FlashPageErase:
   ; pageWordAddr    r25:r24
    
   ; Z-Register aufsetzen
   movw r30, r24
   ; �bergabeparameter mit 2 multiplizieren
   clc
   rol r30
   rol r31

   ldi r18, 0x03        ; (1<<PGERS) | (1<<SPMEN)
   rcall Do_spm               
    
   ; re-enable the RWW section
   ldi r18, 0x11      ; (1<<RWWSRE) | (1<<SPMEN)
   rcall Do_spm

   ret

;
; Do_spm
; 
Do_spm:
   ; check for previous SPM complete
1:
   lds r23, 0x57  ; SPMCR
   sbrc r23, 0    ; SPMEN
   rjmp 1b
   ; SPM timed sequence
   sts 0x57, r18   ; SPMCR
   spm
   ret
   
