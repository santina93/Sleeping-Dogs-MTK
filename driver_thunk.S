.text
.global bridge_thunk
.global bridge_dlopen
.global bridge_dlsym
.type bridge_thunk,%function
bridge_thunk:
    stp x29, x30, [sp, #-48]!
    stp x19, x20, [sp, #16]
    mov x29, sp
    mov x19, x0
    mov x20, x1
    adr x0, bridge_library
    mov w1, #2
bridge_dlopen:
    bl .
    cbz x0, bridge_failure
    adr x1, bridge_symbol
bridge_dlsym:
    bl .
    cbz x0, bridge_failure
    mov x16, x0
    mov x0, x19
    mov x1, x20
    ldp x19, x20, [sp, #16]
    ldp x29, x30, [sp], #48
    br x16
bridge_failure:
    mov x0, xzr
    ldp x19, x20, [sp, #16]
    ldp x29, x30, [sp], #48
    ret
bridge_library:
    .asciz "libgpudrv.so"
bridge_symbol:
    .asciz "vkGetInstanceProcAddr"
.size bridge_thunk, .-bridge_thunk
