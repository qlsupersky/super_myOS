#ifndef PRINT_ONLY
.section .text.entry

.global trap_s
.extern handler_s
.equ reg_size, 0x8
.align 2

trap_s:
    sd sp, -8(sp)
    sd ra, -16(sp)
    sd gp, -24(sp)
    sd tp, -32(sp)
    sd t0, -40(sp)
    sd t1, -48(sp)
    sd t2, -56(sp)
    sd fp, -64(sp)
    sd s1, -72(sp)
    sd a0, -80(sp)
    sd a1, -88(sp)
    sd a2, -96(sp)
    sd a3, -104(sp)
    sd a4, -112(sp)
    sd a5, -120(sp)
    sd a6, -128(sp)
    sd a7, -136(sp)
    sd s2, -144(sp)
    sd s3, -152(sp)
    sd s4, -160(sp)
    sd s5, -168(sp)
    sd s6, -176(sp)
    sd s7, -184(sp)
    sd s8, -192(sp)
    sd s9, -200(sp)
    sd s10, -208(sp)
    sd s11, -216(sp)
    sd t3, -224(sp)
    sd t4, -232(sp)
    sd t5, -240(sp)
    sd t6, -248(sp)
    csrr a1, sepc
    sd a1, -256(sp)
    addi sp, sp, -256

    csrr a0, scause
    call handler_s

    ld t0, 0(sp)
    csrw sepc, t0
    ld t6, 8(sp)
    ld t5, 16(sp)
    ld t4, 24(sp)
    ld t3, 32(sp)
    ld s11, 40(sp)
    ld s10, 48(sp)
    ld s9, 56(sp)
    ld s8, 64(sp)
    ld s7, 72(sp)
    ld s6, 80(sp)
    ld s5, 88(sp)
    ld s4, 96(sp)
    ld s3, 104(sp)
    ld s2, 112(sp)
    ld a7, 120(sp)
    ld a6, 128(sp)
    ld a5, 136(sp)
    ld a4, 144(sp)
    ld a3, 152(sp)
    ld a2, 160(sp)
    ld a1, 168(sp)
    ld a0, 176(sp)
    ld s1, 184(sp)
    ld fp, 192(sp)
    ld t2, 200(sp)
    ld t1, 208(sp)
    ld t0, 216(sp)
    ld tp, 224(sp)
    ld gp, 232(sp)
    ld ra, 240(sp)
    ld sp, 248(sp)

    sret
#endif