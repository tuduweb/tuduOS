;/*
;* Copyright (c) 2006-2018, RT-Thread Development Team
;*
;* SPDX-License-Identifier: Apache-2.0
;*
; * Change Logs:
; * Date           Author       Notes
; * 2009-01-17     Bernard      first version.
; * 2012-01-01     aozima       support context switch load/store FPU register.
; * 2013-06-18     aozima       add restore MSP feature.
; * 2013-06-23     aozima       support lazy stack optimized.
; * 2018-07-24     aozima       enhancement hard fault exception handler.
; */

;/**
; * @addtogroup cortex-m4
; */
;/*@{*/

SCB_VTOR        EQU     0xE000ED08               ; Vector Table Offset Register
NVIC_INT_CTRL   EQU     0xE000ED04               ; interrupt control state register
NVIC_SYSPRI2    EQU     0xE000ED20               ; system priority register (2)
NVIC_PENDSV_PRI EQU     0x00FF0000               ; PendSV priority value (lowest)
NVIC_PENDSVSET  EQU     0x10000000               ; value to trigger PendSV exception

    AREA |.text|, CODE, READONLY, ALIGN=2
    THUMB
    REQUIRE8
    PRESERVE8

    IMPORT rt_thread_switch_interrupt_flag
    IMPORT rt_interrupt_from_thread
    IMPORT rt_interrupt_to_thread

;/*
; * rt_base_t rt_hw_interrupt_disable();
; */
rt_hw_interrupt_disable    PROC
    EXPORT  rt_hw_interrupt_disable
    MRS     r0, PRIMASK
    CPSID   I
    BX      LR
    ENDP

;/*
; * void rt_hw_interrupt_enable(rt_base_t level);//将变量 level 作为需要恢复的状态，覆盖芯片的全局中断状态。
; */
rt_hw_interrupt_enable    PROC
    EXPORT  rt_hw_interrupt_enable
    MSR     PRIMASK, r0
    BX      LR
    ENDP

;/*
; * void rt_hw_context_switch(rt_uint32 from, rt_uint32 to);
; * r0 --> from
; * r1 --> to
; */
rt_hw_context_switch_interrupt
    EXPORT rt_hw_context_switch_interrupt
rt_hw_context_switch    PROC
    EXPORT rt_hw_context_switch

    ; set rt_thread_switch_interrupt_flag to 1
    LDR     r2, =rt_thread_switch_interrupt_flag
    LDR     r3, [r2]
    CMP     r3, #1
    BEQ     _reswitch
    MOV     r3, #1
    STR     r3, [r2]

    LDR     r2, =rt_interrupt_from_thread   ; set rt_interrupt_from_thread;addr to from_thread->sp
    STR     r0, [r2]

_reswitch
    LDR     r2, =rt_interrupt_to_thread     ; set rt_interrupt_to_thread;addr to to_thread->sp
    STR     r1, [r2]

    LDR     r0, =NVIC_INT_CTRL              ; trigger the PendSV exception (causes context switch)
    LDR     r1, =NVIC_PENDSVSET
    STR     r1, [r0]
    BX      LR
    ENDP

; r0 --> switch from thread stack
; r1 --> switch to thread stack
; psr, pc, lr, r12, r3, r2, r1, r0 are pushed into [from] stack
PendSV_Handler   PROC
    EXPORT PendSV_Handler
    IMPORT bin_lwt_mpu_switch
    ; disable interrupt to protect context switch
    MRS     r2, PRIMASK
    CPSID   I

    ; get rt_thread_switch_interrupt_flag
    LDR     r0, =rt_thread_switch_interrupt_flag
    LDR     r1, [r0]
    CBZ     r1, pendsv_exit         ; pendsv already handled

    ; clear rt_thread_switch_interrupt_flag to 0
    MOV     r1, #0x00
    STR     r1, [r0]

    LDR     r0, =rt_interrupt_from_thread
    LDR     r1, [r0]
    CBZ     r1, switch_to_thread    ; skip register save at the first time

    MRS     r1, psp                 ; get from thread stack pointer;psp->r1

    IF      {FPU} != "SoftVFP"
    TST     lr, #0x10               ; if(!EXC_RETURN[4])
    VSTMFDEQ  r1!, {d8 - d15}       ; push FPU register s16~s31
    ENDIF

    STMFD   r1!, {r4 - r11}         ; push r4 - r11 register

    IF      {FPU} != "SoftVFP"
    MOV     r4, #0x00               ; flag = 0

    TST     lr, #0x10               ; if(!EXC_RETURN[4])
    MOVEQ   r4, #0x01               ; flag = 1

    STMFD   r1!, {r4}               ; push flag
    ENDIF

    LDR     r0, [r0]
    STR     r1, [r0]                ; update from thread stack pointer

switch_to_thread
    LDR     r1, =rt_interrupt_to_thread ;R1 point to thread->sp
    LDR     r1, [r1]                ; 取指针 得到 thread->sp 这玩意的地址,相当于thread偏移0x1c的位置(当前版本)
    LDR     r1, [r1]                ; load thread stack pointer ;继续取指针,得到的就是当前栈地址

    IF      {FPU} != "SoftVFP"
    LDMFD   r1!, {r3}               ; pop flag
    ENDIF

    LDMFD   r1!, {r4 - r11}         ; pop r4 - r11 register     ;取栈地址的数据

    IF      {FPU} != "SoftVFP"
    CMP     r3,  #0                 ; if(flag_r3 != 0)
    VLDMFDNE  r1!, {d8 - d15}       ; pop FPU register s16~s31
    ENDIF

    MSR     psp, r1                 ; update stack pointer

    IF      {FPU} != "SoftVFP"
    ORR     lr, lr, #0x10           ; lr |=  (1 << 4), clean FPCA.
    CMP     r3,  #0                 ; if(flag_r3 != 0)
    BICNE   lr, lr, #0x10           ; lr &= ~(1 << 4), set FPCA.
    ENDIF
    ;mpu switch
    PUSH    {R0-R3,R12,LR}
    LDR     r1, =rt_interrupt_from_thread
    LDR     r0, [r1]
    LDR     r2, =rt_interrupt_to_thread
    LDR     r1, [r2]
    BL      bin_lwt_mpu_switch
    POP     {R0-R3,R12,LR}

pendsv_exit
    ; restore interrupt
    MSR     PRIMASK, r2
    MRS     r1, CONTROL
    TST     r1,#1   ;CONTROL[1] 为0表示选择MSP，为1表示选择PSP  ; CONTROL[0]：为0表示特权级的线程模式，为1表示用户级的线程模式; TST 判断该位是否为0 为0则Z=1
                    ;作用,判断现在的运行状态
    BNE     ret_to_user ;Z!=0时执行  即MSP时执行
    
    ORR     lr, lr, #0x04;TO PSP MODE
    BX      lr

ret_to_user ;返回用户线程需要进行signal处理,在这里预留出来。
    ORR     lr, lr, #0x04;TO PSP MODE
    BX      lr
    ENDP

;/*
; * void rt_hw_context_switch_to(rt_uint32 to);
; * r0 --> to
; * this fucntion is used to perform the first thread switch
; */
rt_hw_context_switch_to    PROC
    EXPORT rt_hw_context_switch_to
    ; set to thread
    LDR     r1, =rt_interrupt_to_thread
    STR     r0, [r1]

    IF      {FPU} != "SoftVFP"
    ; CLEAR CONTROL.FPCA
    MRS     r2, CONTROL             ; read
    BIC     r2, #0x04               ; modify
    MSR     CONTROL, r2             ; write-back
    ENDIF

    ; set from thread to 0
    LDR     r1, =rt_interrupt_from_thread
    MOV     r0, #0x0
    STR     r0, [r1]

    ; set interrupt flag to 1
    LDR     r1, =rt_thread_switch_interrupt_flag
    MOV     r0, #1
    STR     r0, [r1]

    ; set the PendSV exception priority
    LDR     r0, =NVIC_SYSPRI2
    LDR     r1, =NVIC_PENDSV_PRI
    LDR.W   r2, [r0,#0x00]       ; read
    ORR     r1,r1,r2             ; modify
    STR     r1, [r0]             ; write-back

    ; trigger the PendSV exception (causes context switch)
    LDR     r0, =NVIC_INT_CTRL
    LDR     r1, =NVIC_PENDSVSET
    STR     r1, [r0]

    ; restore MSP
    LDR     r0, =SCB_VTOR
    LDR     r0, [r0]
    LDR     r0, [r0]
    MSR     msp, r0

    ; enable interrupts at processor level
    CPSIE   F
    CPSIE   I

    ; never reach here!
    ENDP

; compatible with old version
rt_hw_interrupt_thread_switch PROC
    EXPORT rt_hw_interrupt_thread_switch
    BX      lr
    ENDP

    IMPORT rt_hw_hard_fault_exception
    EXPORT HardFault_Handler
HardFault_Handler    PROC

    IMPORT lwp_get_sys_api
    IMPORT lwt_get_kernel_sp
    IMPORT rt_thread_exit

    ; get current context
    TST     lr, #0x04               ; if(!EXC_RETURN[2]);根据这一bit的数据判断进入中断前的模式
    ITE     EQ
    MRSEQ   r0, msp                 ; [2]=0 ==> Z=1, get fault context from handler.
    MRSNE   r0, psp                 ; [2]=1 ==> Z=0, get fault context from thread.

    STMFD   r0!, {r4 - r11}         ; push r4 - r11 register
    IF      {FPU} != "SoftVFP"
    STMFD   r0!, {lr}               ; push dummy for flag
    ENDIF
    STMFD   r0!, {lr}               ; push exec_return register

    TST     lr, #0x04               ; if(!EXC_RETURN[2])
    ITE     EQ
    MSREQ   msp, r0                 ; [2]=0 ==> Z=1, update stack pointer to MSP.
    MSRNE   psp, r0                 ; [2]=1 ==> Z=0, update stack pointer to PSP.

    PUSH    {lr}
    BL      rt_hw_hard_fault_exception

    B       .;下面的函数未完成..本来就有while,这里继续while 

    ;GET SYSCALL Num,THEN GET SYSCALL API entry
    MOV     R0, #1      ;syscall func = sys_exit
    BL      lwp_get_sys_api ;(BL will change LR and other)
    MOV     R2, R0
    
    ;GET kernel SP(Thread SP)
    BL      lwt_get_kernel_sp

    ; get user SP.
    TST     LR, #0x4
    ITE     EQ
    MRSEQ   R1, MSP
    MRSNE   R1, PSP

    STMFD   R0!,    {R2}   ;save app SP to kernel SP
    STMFD   R0!,    {R4 - R5}  ;占位

    SUB     R0, #0x20

    
    STR     R2, [R0, #24]   ;update API to PC;sys_exit

    LDR     R2, =rt_thread_exit
    STR     R2, [R0, #20]   ;update LR = sys_exit 占位

    MOV     R1, #1
    STR     R1, [R0, #0]    ;update R0 = #1
    MOV     R1, #0x01000000     ;PSR 0x01000000
    STR     R1, [R0, #28]
    ;MOV     R1, #0
    ;STR     R1, [R0, #28]


    MSR     PSP, R0         ;更新SP

    ; set to thread-privilege mode.
    MRS     R3, CONTROL
    BIC     R3, R3, #0x01
    ORR     R3, R3, #0x02
    MSR     CONTROL, R3

    POP     {lr}

    ORR     lr, lr, #0x10
    BX      lr
    ENDP

    ALIGN   4

    END
