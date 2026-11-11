// Copyright (c) HanSoloBlaster, afiaka87
// One four-word stub per gated hook: stub i jumps to fieldworks_gate_targets[i].

    .section .text.fieldworks_gate_stubs, "ax", %progbits
    .global fieldworks_gate_stubs
    .type fieldworks_gate_stubs, %function
    .balign 16
fieldworks_gate_stubs:
    .set gate_index, 0
    .rept 512
    adrp x17, fieldworks_gate_targets + (gate_index * 8)
    ldr  x17, [x17, #:lo12:fieldworks_gate_targets + (gate_index * 8)]
    br   x17
    nop
    .set gate_index, gate_index + 1
    .endr
    .size fieldworks_gate_stubs, . - fieldworks_gate_stubs

// The off target of a gated inline hook: the callback does nothing.
    .section .text.fieldworks_gate_return, "ax", %progbits
    .global fieldworks_gate_return
    .type fieldworks_gate_return, %function
    .balign 4
fieldworks_gate_return:
    ret
    .size fieldworks_gate_return, . - fieldworks_gate_return
