BITS 16
CPU 186
ORG 0

; ---- pusha_popa_test.s equivalents ----
pusha                   ; expect 60
popa                    ; expect 61

; ---- push_imm_test.s equivalents ----
push word 0             ; expect 68 00 00
push word 32767         ; expect 68 FF 7F
push word -1            ; expect 68 FF FF
push byte 0             ; expect 6A 00
push byte 127           ; expect 6A 7F
push byte -1            ; expect 6A FF
push byte -128          ; expect 6A 80

; ---- insb_outsb_test.s equivalents ----
insb                    ; expect 6C
outsb                   ; expect 6E

; ---- shift_rotate_imm_count_test.s equivalents ----
rol ax,2                ; expect C1 C0 02
ror cx,3                ; expect C1 C9 03
rcl dx,4                ; expect C1 D2 04
rcr bx,5                ; expect C1 DB 05
shl si,7                ; expect C1 E6 07
shr di,15               ; expect C1 EF 0F
sar bp,31               ; expect C1 FD 1F
shl word [bx],4         ; expect C1 27 04
rol byte [si],5         ; expect C0 04 05

; ---- enter_leave_test.s equivalents ----
enter 16,0              ; expect C8 10 00 00
enter 32,1              ; expect C8 20 00 01
enter 64,3              ; expect C8 40 00 03
leave                   ; expect C9

; ---- bound_test.s equivalents ----
bound ax,[bx]           ; expect 62 07
bound cx,[si+4]         ; expect 62 4C 04
bound dx,[bp+1000]      ; expect 62 96 E8 03

; ---- imul_imm_test.s equivalents ----
imul ax,10              ; expect 6B C0 0A
imul cx,1000            ; expect 69 C9 E8 03
imul dx,-1              ; expect 6B D2 FF
imul cx,ax,4            ; expect 6B C8 04
imul dx,bx,2000         ; expect 69 D3 D0 07
imul cx,ax,200          ; expect 6B C8 C8   (byte truncation of 200)
