.syntax unified
.cpu cortex-m4
.thumb

.extern thread_queue
.extern thread_queue_index

.equ MAX_THREADS, 4
.equ STACK_SIZE, 50

.section .text
.global thread_handler

thread_handler:
