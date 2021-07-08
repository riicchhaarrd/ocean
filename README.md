# ocean

Programming language that compiles into a x86 ELF executable.

Example of code (may be subject to change)

```c
function add(a, b)
{
	return a + b;
}

function main()
{
	write(1, "hello\n", 7);
	exit(add(100, 23));
}
```

Which compiles into

```asm
mov eax, 0x804902d
call eax
xor ebx, ebx
xor eax, eax
inc eax
int 0x80
push ebp
mov ebp, esp
sub esp, 0x20
mov eax, dword [ebp + 8]
push eax
mov eax, dword [ebp + 0xc]
mov ecx, eax
pop eax
xor edx, edx
add eax, ecx
mov esp, ebp
pop ebp
ret
mov esp, ebp
pop ebp
ret
push ebp
mov ebp, esp
sub esp, 0x20
mov eax, 1
mov ebx, eax
mov eax, 0x804a000
mov ecx, eax
mov eax, 7
mov edx, eax
mov eax, 4
int 0x80
mov eax, 0x64
push eax
mov eax, 0x17
push eax
call 0xe
add esp, 8
mov bl, al
xor eax, eax
inc eax
int 0x80
mov esp, ebp
pop ebp
ret
```
