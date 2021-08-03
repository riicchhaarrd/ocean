# ocean
Programming language that compiles into a x86 ELF executable.

# TODO

- [x] basic features of a programming language, conditionals/loops/variables/types
- [ ] a compiler somewhat resembling C
- [ ] standard library
- [ ] other compile targets e.g https://docs.microsoft.com/en-us/windows/win32/debug/pe-format
- [ ] self-hosting
- [ ] Add new or borrow from other language(s) features ontop of C, deviate away from just C
	
	- [ ] defer
	- [ ] operator overloading
	- [ ] constexpr/consteval from c++ or some #run directive (https://github.com/BSVino/JaiPrimer/blob/master/JaiPrimer.md)
	- [ ] learn from other similar projects (e.g https://news.ycombinator.com/item?id=27890888)
- [ ] target other ISA (instruction set architectures) e.g ARM/x86_64
- [ ] better error handling / memory cleanup
- [ ] LLVM or other IR backend (atm just targeting x86 and it's a great learning exercise)

# Example

Example of code (may be subject to change)

```c
int strlen(const char *s)
{
    int i = 0;
    while(s[i])
    {
        i+=1;
    }    
    return i;
}

void print(const char *s)
{
    write(1, s, strlen(s) + 1);
}

int main()
{
    print("hello, world!\n");
    
    char msg[32];
    msg[0] = 'h';
    msg[1] = 'e';
    msg[2] = 'l';
    msg[3] = 'l';
    msg[4] = 'o';
    msg[5] = ',';
    msg[6] = ' ';
    msg[7] = 'w';
    msg[8] = 'o';
    msg[9] = 'r';
    msg[10] = 'l';
    msg[11] = 'd';
    msg[12] = '!';
    msg[13] = 10;
    msg[14] = 0;

    int i;
    
    for(i = 0; i < 10; i += 1)
    {
        if(i % 2 ==0)
        {
            print(msg);
        }
    }
}
```

Which compiles into (no relocations, when compiling to ELF strings will be relocated and 0xcccccccc would be replaced by the actual location)

```asm
mov eax, 0
call eax
xor ebx, ebx
xor eax, eax
inc eax
int 0x80
push ebp
mov ebp, esp
sub esp, 4
mov eax, 0
push eax
lea ebx, [ebp - 4]
pop eax
mov dword [ebx], eax
mov ebx, dword [ebp + 8]
mov eax, dword [ebp - 4]
add ebx, eax
movzx eax, byte [ebx]
test eax, eax
je 0x47
mov eax, 1
push eax
lea ebx, [ebp - 4]
pop eax
add dword [ebx], eax
jmp 0x23
mov eax, dword [ebp - 4]
mov esp, ebp
pop ebp
ret
mov esp, ebp
pop ebp
ret
push ebp
mov ebp, esp
sub esp, 0
mov eax, 1
push eax
mov eax, dword [ebp + 8]
push eax
mov eax, dword [ebp + 8]
push eax
call 0xe
add esp, 4
push eax
mov eax, 1
mov ecx, eax
pop eax
xor edx, edx
add eax, ecx
mov edx, eax
pop ecx
pop ebx
mov eax, 4
int 0x80
mov esp, ebp
pop ebp
ret
push ebp
mov ebp, esp
sub esp, 0x24
mov eax, 0xcccccccc
push eax
call 0x52
add esp, 4
mov eax, 0x68
push eax
push eax
lea ebx, [ebp - 0x20]
mov eax, 0
add ebx, eax
pop eax
pop eax
mov byte [ebx], al
mov eax, 0x65
push eax
push eax
lea ebx, [ebp - 0x20]
mov eax, 1
add ebx, eax
pop eax
pop eax
mov byte [ebx], al
mov eax, 0x6c
push eax
push eax
lea ebx, [ebp - 0x20]
mov eax, 2
add ebx, eax
pop eax
pop eax
mov byte [ebx], al
mov eax, 0x6c
push eax
push eax
lea ebx, [ebp - 0x20]
mov eax, 3
add ebx, eax
pop eax
pop eax
mov byte [ebx], al
mov eax, 0x6f
push eax
push eax
lea ebx, [ebp - 0x20]
mov eax, 4
add ebx, eax
pop eax
pop eax
mov byte [ebx], al
mov eax, 0x2c
push eax
push eax
lea ebx, [ebp - 0x20]
mov eax, 5
add ebx, eax
pop eax
pop eax
mov byte [ebx], al
mov eax, 0x20
push eax
push eax
lea ebx, [ebp - 0x20]
mov eax, 6
add ebx, eax
pop eax
pop eax
mov byte [ebx], al
mov eax, 0x77
push eax
push eax
lea ebx, [ebp - 0x20]
mov eax, 7
add ebx, eax
pop eax
pop eax
mov byte [ebx], al
mov eax, 0x6f
push eax
push eax
lea ebx, [ebp - 0x20]
mov eax, 8
add ebx, eax
pop eax
pop eax
mov byte [ebx], al
mov eax, 0x72
push eax
push eax
lea ebx, [ebp - 0x20]
mov eax, 9
add ebx, eax
pop eax
pop eax
mov byte [ebx], al
mov eax, 0x6c
push eax
push eax
lea ebx, [ebp - 0x20]
mov eax, 0xa
add ebx, eax
pop eax
pop eax
mov byte [ebx], al
mov eax, 0x64
push eax
push eax
lea ebx, [ebp - 0x20]
mov eax, 0xb
add ebx, eax
pop eax
pop eax
mov byte [ebx], al
mov eax, 0x21
push eax
push eax
lea ebx, [ebp - 0x20]
mov eax, 0xc
add ebx, eax
pop eax
pop eax
mov byte [ebx], al
mov eax, 0xa
push eax
push eax
lea ebx, [ebp - 0x20]
mov eax, 0xd
add ebx, eax
pop eax
pop eax
mov byte [ebx], al
mov eax, 0
push eax
push eax
lea ebx, [ebp - 0x20]
mov eax, 0xe
add ebx, eax
pop eax
pop eax
mov byte [ebx], al
mov eax, 0
push eax
lea ebx, [ebp - 0x24]
pop eax
mov dword [ebx], eax
mov eax, dword [ebp - 0x24]
push eax
mov eax, 0xa
mov ecx, eax
pop eax
xor edx, edx
cmp eax, ecx
jge 0x202
xor eax, eax
inc eax
jmp 0x204
xor eax, eax
test eax, eax
je 0x256
mov eax, dword [ebp - 0x24]
push eax
mov eax, 2
mov ecx, eax
pop eax
xor edx, edx
idiv ecx
mov eax, edx
push eax
mov eax, 0
mov ecx, eax
pop eax
xor edx, edx
cmp eax, ecx
jne 0x232
xor eax, eax
inc eax
jmp 0x234
xor eax, eax
cmp eax, 0
je 0x245
lea eax, [ebp - 0x20]
push eax
call 0x52
add esp, 4
mov eax, 1
push eax
lea ebx, [ebp - 0x24]
pop eax
add dword [ebx], eax
jmp 0x1eb
mov esp, ebp
pop ebp
ret
```
