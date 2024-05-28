_start:
    LDX #$03
    LDA #$1C
    STA $500
    STX $501
LB1:
    LDA $501
    CMP #$FF
    BNE LB0
    LDA $500
    CMP #$FF
LB0:
    BCS END_FUNC
    LDA $501
    STA $503
    LDA $500
    STA $502
    LDA $500
    LDY #$00
    STA $502,y
    INC $500
    BNE LB1
    INC $501
    JMP LB1
END_FUNC:
    RTS


NMI:
    JMP _start

IRQ:
    JMP _start