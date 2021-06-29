global _start
BITS 32
section .data
newline_string:  db 10, 0
section .text

strlen:
    push ebp
    mov ebp, esp

    xor eax,eax
    mov ecx,[ebp+8]

.loop:
    xor dx,dx
    mov dl,byte [ecx+eax]
    inc eax
    cmp dl,0x0
    jne .loop
.end:
    mov esp,ebp
    pop ebp
    ret
    
print_string:
    
    push ebp
    mov ebp, esp
    
    mov ebx,1                   ; use stdout
    mov ecx,[ebp + 8]           ; first arg pushed as string

    push ecx
    call strlen
    mov edx,eax
    mov eax,4
    int 0x80
    mov esp,ebp
    pop ebp
    ret

print_dec:

    push ebp
    mov ebp, esp

    mov eax,[ebp + 8]           ;byte arg
    
    sub esp, 32
    mov esi, 0

    lea ecx, [ebp]
    mov byte [ecx], 0
    
l1:
    ;; cdq
    xor edx,edx
    mov ebx,10
    idiv ebx

    ;; remainder is in edx
    
    add dl,'0'
    lea ecx, [ebp - 1]
    sub ecx, esi
    mov byte [ecx], dl
    inc esi
    
    test eax,eax
    jnz l1

    lea ecx, [ebp]
    sub ecx, esi
    push ecx
    call print_string
    add esp, 4
    
    mov esp,ebp
    pop ebp
    ret
_start:

    push ebp
    mov ebp, esp
    xor esi,esi

    %include 'generated.asm'
    
    push eax
    call print_dec
    add esp, 4

    push newline_string
    call print_string
    add esp, 4
    
    ;; store result of eax into ebx for the syscall arg
    mov ebx,eax
    mov esp,ebp
    pop ebp
    
    xor eax,eax
    mov al,1
    int 0x80
