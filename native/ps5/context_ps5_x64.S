/* Cooperative context switch for the PS5's x86-64 System V ABI. */
    .text
    .p2align 4
    .globl khdays_context_jump
    .type khdays_context_jump,@function
khdays_context_jump:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $8, %rsp
    stmxcsr (%rsp)
    fnstcw 4(%rsp)
    movq %rsp, (%rdi)
    movq %rsi, %rsp
    ldmxcsr (%rsp)
    fldcw 4(%rsp)
    addq $8, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
    .size khdays_context_jump, .-khdays_context_jump

    .p2align 4
    .globl khdays_context_trampoline
    .type khdays_context_trampoline,@function
khdays_context_trampoline:
    movq %rbx, %rdi
    subq $8, %rsp
    call khdays_context_started
    ud2
    .size khdays_context_trampoline, .-khdays_context_trampoline

    .section .note.GNU-stack,"",@progbits
