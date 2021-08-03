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
function strlen(const char *s)
{
    int i = 0;
    while(s[i])
    {
        i+=1;
    }    
    return i;
}

function print(const char *s)
{
    write(1, s, strlen(s) + 1);
}

function main()
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
