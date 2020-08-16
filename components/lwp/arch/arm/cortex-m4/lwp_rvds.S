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

    ; set CPU to user-thread mode.thread-unprivilege mode;在本版本中,如果不注释,在引起Pendcv中断时候会报HardFault
    MRS     R2, CONTROL
    ORR     R2, R2, #0x03       ; use PSP, user-thread mode.
    MSR     CONTROL, R2

    POP     {R0-R3,LR}             ; pop app address to R1.;ADD LR
    ; set data address.
    MOV     R9, R2              ;R9 偏址寄存器

    ; run app, only Thumb-mode.
    ORR     R1, R1, #0x01       ;指令模式的最后一位需要处理一下(32位指令集) ;   R1 = text_entry
    BX      R1                  ;执行程序

    ; never reach here!
    ENDP

;/*
; * void SVC_Handler(void);     Syscall Num:R7 / Param:R0,R1,R2,R3,R4,R5
; */
SVC_Handler    PROC
    EXPORT SVC_Handler

    PUSH    {LR}

    ; get user SP.
    TST     LR, #0x4
    ITE     EQ
    MRSEQ   R1, MSP
    MRSNE   R1, PSP
	;   The SVC number is held in 8 bits instead of the 24 bits in ARM state.
    ; get SVC number.
    LDR     R2,[R1,#24]   ;从栈中读取PC值
    LDRB    R2,[R2,#-2]   ;从SVC指令中读取立即数放到R0
    
    ; SWITCH CASE
    TBB     [PC, R2]
Table_start
    DCB     ((SVC_Case0-Table_start)/2)
    DCB     ((SVC_Case1-Table_start)/2)

SVC_Case0
    PUSH    {R1}           ; push app SP.
    mov     R0, R7          ;R7 is filled by APP syscall assembly code

    ; get kernel system API
    BL      lwp_get_sys_api
    
    PUSH    {R0}            ; push api(func)

    ; get kernel SP to R0.
    BL lwt_get_kernel_sp

    POP     {R2}             ; pop api to R2.
    POP     {R1}             ; pop app SP to R1.

    stmfd     r0!, {r1}      ; save app SP to kernel SP

    ;push syscall parm4&5 to kernel SP  ;   syscall函数的参数,syscall最多6个参数R0~R5;其他的参数通过硬件压栈,在exception_stack_frame中;参考https://www.rt-thread.org/document/site/programming-manual/porting/porting/#_6
    STMFD   R0!,  {R4 - R5}
    ; copy R1(app SP) to R0(kernel SP).
    push {r8-r11}
    LDMFD   R1,   {R4 - R11}     ; pop (app) exception_stack_frame to r4 - r11 register
    STMFD   R0!,  {R4 - R11}     ; push (app) exception_stack_frame to server(kernel) SP.
    pop {r8-r11}

    LDR     R3, =svc_exit
    STR     R3, [R0, #20]       ; update LR
    STR     R2, [R0, #24]       ; update api to PC  ;   when exit SVC_Handler by "BX LR",restore Thread-Mode,go PC
    MSR     PSP, R0             ; update SP, API is executed with kernel SP ;更新了之后返回地址就变了

    ; set to thread-privilege mode.
    MRS     R3, CONTROL
    BIC     R3, R3, #0x01
    ORR     R3, R3, #0x02
    MSR     CONTROL, R3

    B       Table_exit


SVC_Case1
    ;B       userApp_entry       ;只会改变PC;简单的程序跳转，跳转到到目标标号处执行。
    BL      userApp_entry       ;带链接程序跳转，也就是要带返回地址。在发生跳转前，将当前PC-4保存到R14中。;也就是带LR
Table_exit
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
    ldr     r3, [r0, #-4]           ; currentSP - 4
    pop {r0}

    ldr     lr, [r3, #20]
    ldr     r1, [r3, #24]           ; load pc <restore? user App PC>
    add     r3, #32                 ; exception_stack_frame size
    MSR     PSP, R3                 ; restore app stack pointer
    ; restore to PSP & thread-unprivilege mode.;进入线程非特权模式,在核心中测试时，切勿进入，否则会发生无法切换线程的错误。
    MRS     R2, CONTROL
    ORR     R2, R2, #0x03
    MSR     CONTROL, R2

    ; return to lwp.<User App>
    ORR     R1, R1, #0x01           ; only Thumb-mode.
    BX      R1                      ; return to user app.

    ENDP


;/*
; * void svc_exit(void);
; */
userApp_entry   PROC
    EXPORT  userApp_entry
    
    B       .
    ENDP

    ALIGN

    END

