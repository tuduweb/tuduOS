;/*
; * Copyright (c) 2006-2018, RT-Thread Development Team
; *
; * SPDX-License-Identifier: Apache-2.0
; *
; * Change Logs:
; * Date           Author       Notes
; * 2018-10-30     heyuanjie    first version
; */

    AREA |.text|, CODE, READONLY, ALIGN=2
    THUMB
    REQUIRE8
    PRESERVE8

;/*
; * void* lwp_get_sys_api(rt_uint32_t number);
; */
    IMPORT lwp_get_sys_api
    IMPORT lwt_get_kernel_sp
    IMPORT lwt_set_kernel_sp

;/*
; * void lwp_user_entry(args, text, data);
; */
lwp_user_entry    PROC
    EXPORT  lwp_user_entry

    PUSH    {R0-R3,LR}             ; push text&data addr.; ADD LR

    MOV     R0, SP              ; v1 = SP
    BL      lwt_set_kernel_sp   ; lwp_set_kernel_sp(v1) ;sp寄存器指向stack,保存当前(kernel)SP的值

    ; set CPU to user-thread mode.;在本版本中,如果不注释,在引起Pendcv中断时候会报HardFault
    ;MRS     R2, CONTROL
    ;ORR     R2, R2, #0x03       ; use PSP, user-thread mode.
    ;MSR     CONTROL, R2

    POP     {R0-R3,LR}             ; pop app address to R1.;ADD LR
    ; set data address.
    MOV     R9, R2              ;R9 偏址寄存器

    ; run app, only Thumb-mode.
    ORR     R1, R1, #0x01       ;指令模式的最后一位需要处理一下(32位指令集) ;   R1 = text_entry
    BX      R1                  ;执行程序

    ; never reach here!
    ENDP

;/*
; * void SVC_Handler(void);
; */
SVC_Handler    PROC
    EXPORT SVC_Handler

    PUSH    {LR}

    ; get user SP.
    TST     LR, #0x4
    ITE     EQ
    MRSEQ   R1, MSP
    MRSNE   R1, PSP
    PUSH    {R1}           ; push app SP.
	;   The SVC number is held in 8 bits instead of the 24 bits in ARM state.
    ; get SVC number.
    LDR     R1,[R1,#24]   ;从栈中读取PC值
    LDRB    R1,[R1,#-2]   ;从SVC指令中读取立即数放到R0

    mov     R0, R7          ;R7 is filled by APP syscall assembly code

    ; get kernel system API
    BL      lwp_get_sys_api
    
    PUSH    {R0}            ; push api

    ; get kernel SP to R0.
    BL lwt_get_kernel_sp

    POP     {R2}             ; pop api to R2.
    POP     {R1}             ; pop app SP to R1.

    stmfd     r0!, {r1}      ; save app SP to kernel SP

    ;push app parm5~6 to kernel SP
    STMFD   R0!,  {R4 - R5}
    ; copy R1(app SP) to R0(kernel SP).
    push {r8-r11}
    LDMFD   R1,   {R4 - R11}     ; pop exception_stack_frame to r4 - r11 register
    STMFD   R0!,  {R4 - R11}     ; push exception_stack_frame to server SP.
    pop {r8-r11}

    LDR     R3, =svc_exit
    STR     R3, [R0, #20]       ; update LR
    STR     R2, [R0, #24]       ; update api to PC  ;   R2
    MSR     PSP, R0             ; update SP, API is executed with kernel SP

    ; set to thread-privilege mode.
    ;MRS     R3, CONTROL
    ;BIC     R3, R3, #0x01
    ;ORR     R3, R3, #0x02
    ;MSR     CONTROL, R3

    POP     {LR}                ; 0xFFFFFFED
    ORR     LR, LR, #0x10       ;Thumb模式?
    BX      LR

    ENDP

;/*
; * void svc_exit(void);
; */
svc_exit     PROC
    EXPORT svc_exit

    ; get user SP.
    PUSH    {R0}                    ; push result to SP.
    BL      lwt_get_kernel_sp
    ldr     r3, [r0, #-4]
    pop {r0}

    ldr     lr, [r3, #20]
    ldr     r1, [r3, #24]           ; load pc
    add     r3, #32                 ; exception_stack_frame size
    MSR     PSP, R3                 ; restore app stack pointer
    ; restore to PSP & thread-unprivilege mode.;进入线程非特权模式,在核心中测试时，切勿进入，否则会发生无法切换线程的错误。
    ;MRS     R2, CONTROL
    ;ORR     R2, R2, #0x03
    ;MSR     CONTROL, R2

    ; return to lwp.<User App>
    ORR     R1, R1, #0x01           ; only Thumb-mode.
    BX      R1                      ; return to user app.

    ENDP

    ALIGN

    END
