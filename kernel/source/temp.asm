update_sleeping_threads:
    mov r0, #0

    ldr r1, =thread_queue_len
    ldr r1, [r1]

    ldr r2, =thread_queue

sleeping_threads_loop:
    cmp r0, r1
    beq exit_loop

    ldr r3, [r2]
    add r3, 4
    ldr r12, [r3]

    cmp r12, #2
    bne next_iter

    add r3, 4
    ldr r12, [r3]
    sub r12, r12, 1
    str r12, [r3]

    cmp r12, 0
    bne next_iter
    sub r3, 4
    mov r12, 0
    str r12, [r3]

next_iter:

    add r2, #4
    add r0, r0, #1
    b sleeping_threads_loop



exit_loop:
    b return_from_update