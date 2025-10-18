; Creates infinite loop
loop:
    jmp loop

; Fill with 510 zeros minus the size of the previous code
times 510-($-$$) db 0
; Ensuring bytes 511 and 512 trigger bootable media
dw 0xaa55
