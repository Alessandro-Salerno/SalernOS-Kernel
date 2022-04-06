[bits 64]
kernel_paging_laod:
    mov     rax,    0x000ffffffffff000
    and     rdi,    rax
    mov     cr3,    rdi
    ret


GLOBAL kernel_paging_laod
