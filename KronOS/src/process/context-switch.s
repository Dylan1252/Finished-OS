global process_context_switch

; struct Context layout (packed):
;   0  : CPURegister.index.edi
;   4  : CPURegister.index.esi
;   8  : CPURegister.stack.esp   (pushad slot)
;   12 : CPURegister.stack.ebp   (pushad slot)
;   16 : CPURegister.general.ebx
;   20 : CPURegister.general.edx
;   24 : CPURegister.general.ecx
;   28 : CPURegister.general.eax
;   32 : CPURegister.segment.gs
;   36 : CPURegister.segment.fs
;   40 : CPURegister.segment.es
;   44 : CPURegister.segment.ds
;   48 : eip
;   52 : eflags
;   56 : user_stack_addr
;   60 : page_directory_virtual_addr
;
; The struct is passed by value, so with cdecl it lives at [esp + 4].

section .text
process_context_switch:
    mov edx, esp                 ; edx -> return address, context at edx + 4

    ; Load user data selector into the data segment registers.
    ; Safe while still in ring 0 and required before returning to ring 3.
    mov ax, 0x23                 ; GDT_USER_DATA_SEGMENT_SELECTOR | RPL 3
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Build the inter-privilege iret frame (pushed high address first)
    push dword 0x23              ; ss
    push dword [edx + 60]        ; user esp
    push dword [edx + 56]        ; eflags
    push dword 0x1B              ; cs = GDT_USER_CODE_SEGMENT_SELECTOR | RPL 3
    push dword [edx + 52]        ; eip

    ; Recreate the pushad stack image so popad restores every register correctly
    push dword [edx + 32]        ; eax
    push dword [edx + 28]        ; ecx
    push dword [edx + 24]        ; edx
    push dword [edx + 20]        ; ebx
    push dword [edx + 16]        ; stack slot (5th pushad register)
    push dword [edx + 12]        ; stack slot (6th pushad register)
    push dword [edx + 8]         ; esi
    push dword [edx + 4]         ; edi

    popad
    iret
