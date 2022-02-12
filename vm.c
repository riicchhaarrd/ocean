//basic x86 opcode virtual machine

#include "compile.h"
#include "types.h"
#include <assert.h>
#include <limits.h>
#include <stdarg.h>

#define VERBOSE

typedef int32_t regval_t;

#define COUNT_OF(x) (sizeof(x) / sizeof(x[0]))

static void verbose_printf(const char *format, ...)
{
	va_list va;
	va_start(va, format);
	char buf[16384];
	#ifdef VERBOSE
	vsnprintf(buf, sizeof(buf), format, va);
	printf("%s\n", buf);
	#endif
	va_end(va);
}

typedef struct
{
    regval_t registers[REGISTER_X86_MAX];
	u8 *mem;
	regval_t memsz;
	int nreadonly;
} vm_t;

regval_t* query_register(vm_t *vm, int reg)
{
    return &vm->registers[reg];
}

static u8 *get_reference_to_vm_memory_address(vm_t *vm, regval_t addr)
{
	assert(addr >= 0 && addr < vm->memsz);
	return (u8*)&vm->mem[addr];
}

u8 fetch_opcode(vm_t *vm)
{
	/*
	u8 op;
	scanf("%02hhX", &op);
	return op;
	*/
	u8 op = *get_reference_to_vm_memory_address(vm, vm->registers[EIP]++);
	verbose_printf("0x%02X\n", op);
	return op;
}

int fetch_instruction(vm_t *vm)
{
	//verbose_printf("instruction?\n");
	return fetch_opcode(vm);
}

int fetch_operand(vm_t *vm)
{
	//verbose_printf("operand?\n");
	return fetch_opcode(vm);
}

u8 register_byte_value(regval_t value, int index)
{
	union
	{
		regval_t value;
		u8 b[4];
	} u;
	u.value = value;
	return u.b[index];
}

regval_t fetch_register_value(vm_t *vm)
{
	union
	{
		regval_t value;
		u8 b[4];
	} u;
	u.b[0] = fetch_operand(vm);
	u.b[1] = fetch_operand(vm);
	u.b[2] = fetch_operand(vm);
	u.b[3] = fetch_operand(vm);
	return u.value;
}

enum x86_vm_error_code
{
    VM_OK,
    VM_HALT,
    VM_ERR_UNHANDLED_OPERAND,
	VM_ERR_INVALID_OPCODE,
	VM_ERR_UNHANDLED_SYSCALL
};

static u8 *stack(vm_t *vm)
{
	assert(vm->registers[ESP] >= vm->nreadonly);
	u8 *stack = get_reference_to_vm_memory_address(vm, vm->registers[ESP]);
	return stack;
}

static void push(vm_t *vm, int reg)
{
	vm->registers[ESP] -= sizeof(regval_t);
	*(regval_t*)stack(vm) = vm->registers[reg];
}

static regval_t pop(vm_t *vm, int reg)
{
	vm->registers[reg] = *(regval_t*)stack(vm);
	vm->registers[ESP] += sizeof(regval_t);
	return vm->registers[reg];
}

static void set_memory_value(vm_t *vm, int index, regval_t value)
{
	//make sure it's writable
	assert(index >= vm->nreadonly);
	assert(index >= 0 && index < vm->memsz);
	vm->mem[index] = value;
}

static regval_t get_memory_value(vm_t *vm, int index)
{
	assert(index >= 0 && index < vm->memsz);
	return vm->mem[index];
}

static u8 *get_memory_pointer(vm_t *vm, int index)
{
	assert(index >= 0 && index < vm->memsz);
	return &vm->mem[index];
}

static void set_flags(vm_t *vm, regval_t result)
{
	vm->registers[REGISTER_X86_FLAGS] = 0;
	if(result < 0)
		vm->registers[REGISTER_X86_FLAGS] |= X86_SIGN_FLAG;
	if(result == 0)
		vm->registers[REGISTER_X86_FLAGS] |= X86_ZERO_FLAG;
}

static void dump_vm_state(vm_t *vm)
{
    verbose_printf("flags: %d\n", vm->registers[REGISTER_X86_FLAGS]);
    verbose_printf("registers:\n");
    for(int i = 0; i < 8; ++i)
    {
        verbose_printf("\t%s: %d\n", register_x86_names[i], vm->registers[i]);
    }
}

static void cmp(vm_t *vm, regval_t a, regval_t b)
{
	regval_t c = a - b;
	set_flags(vm, c);
	if((a > 0 && b > INT_MAX - a) || (a < 0 && b < INT_MIN - a)) //overflow or underflow
		vm->registers[REGISTER_X86_FLAGS] |= X86_OVERFLOW_FLAG;
}

int execute_vm(vm_t *vm)
{
    int opcode = fetch_instruction(vm);
    switch(opcode)
    {
		//mov byte ptr [ebx], al
		case 0x88:
		{
            int operand = fetch_operand(vm);
			assert(operand == 0x03);
			u8 *ptr = (u8*)get_memory_pointer(vm, vm->registers[EBX]);
			*ptr = register_byte_value(vm->registers[EAX], 0);
			verbose_printf("mov byte ptr [ebx], al\n");
		} break;
		
		case 0xf7:
		{
            int operand = fetch_operand(vm);
			switch(operand)
			{
				//not eax
				case 0xd0:
					verbose_printf("not eax\n");
					vm->registers[EAX] = ~vm->registers[EAX];
				break;
				
				//imul esi
				case 0xee:
					verbose_printf("imul esi\n");
					vm->registers[EAX] *= vm->registers[ESI];
				break;
				
				default:
					return VM_ERR_UNHANDLED_OPERAND;
			}
		} break;
		
		//add r32, r32
        case 0x1:
        {
            int operand = fetch_operand(vm);
			switch(operand)
			{
				//add [ebx],eax
				case 0x03:
				{
					regval_t *ptr = (regval_t*)get_memory_pointer(vm, vm->registers[EBX]);
					*ptr += vm->registers[EAX];
					verbose_printf("add [ebx], eax\n");
				} break;
				
				default:
				{
					if(operand < 0xc0 || operand > 0xff)
						return VM_ERR_INVALID_OPCODE;
					operand -= 0xc0;
					int dstreg = operand % 8;
					int srcreg = (operand - dstreg) / 8;
					vm->registers[dstreg] += vm->registers[srcreg];
					verbose_printf("add %s, %s\n", register_x86_names[dstreg], register_x86_names[srcreg]);
				} break;
			}
        } break;
		
		//xor r32, r32
        case 0x31:
        {
            int operand = fetch_operand(vm);
			if(operand < 0xc0 || operand > 0xff)
				return VM_ERR_INVALID_OPCODE;
			operand -= 0xc0;
			int dstreg = operand % 8;
			int srcreg = (operand - dstreg) / 8;
			vm->registers[dstreg] ^= vm->registers[srcreg];
			verbose_printf("xor %s, %s\n", register_x86_names[dstreg], register_x86_names[srcreg]);
        } break;
		
		//sub r32, r32
        case 0x29:
        {
            int operand = fetch_operand(vm);
			if(operand < 0xc0 || operand > 0xff)
				return VM_ERR_INVALID_OPCODE;
			operand -= 0xc0;
			int dstreg = operand % 8;
			int srcreg = (operand - dstreg) / 8;
			vm->registers[dstreg] -= vm->registers[srcreg];
			verbose_printf("sub %s, %s\n", register_x86_names[dstreg], register_x86_names[srcreg]);
        } break;
		
		case 0x89:
		{
            int operand = fetch_operand(vm);
			switch(operand)
			{
				//mov ebp, esp
				case 0xe5:
				{
					vm->registers[EBP] = vm->registers[ESP];
					verbose_printf("mov ebp, esp\n");
				} break;
				
				case 0xec:
				{
					vm->registers[ESP] = vm->registers[EBP];
					verbose_printf("mov esp, ebp\n");
				} break;
				
				case 0xd8:
				{
					vm->registers[EAX] = vm->registers[EBX];
					verbose_printf("mov eax, ebx\n");
				} break;
				
				case 0xc1:
				{
					vm->registers[ECX] = vm->registers[EAX];
					verbose_printf("mov ecx, eax\n");
				} break;
				
				case 0x03:
				{
					verbose_printf("mov [ebx], eax\n");
					set_memory_value(vm, vm->registers[EBX], vm->registers[EAX]);
				} break;
				
				case 0xc3:
				{
					verbose_printf("mov ebx, eax\n");
					vm->registers[EBX] = vm->registers[EAX];
				} break;
				
				default:
					return VM_ERR_UNHANDLED_OPERAND;
			}
		} break;
		
		case 0x81:
		{
            int operand = fetch_operand(vm);
			switch(operand)
			{
				//add ebx, imm32
				case 0xc3:
				{
					regval_t value = fetch_register_value(vm);
					vm->registers[EBX] += value;
					verbose_printf("add ebx, 0x%x\n", value);
				} break;
				//sub esp, imm32
				case 0xec:
				{
					regval_t value = fetch_register_value(vm);
					vm->registers[ESP] -= value;
					verbose_printf("sub esp, 0x%x\n", value);
				} break;
				
				default:
					return VM_ERR_UNHANDLED_OPERAND;
			}
		} break;
		
		case 0xff:
		{
			int operand = fetch_operand(vm);
			switch(operand)
			{
				//call dword ptr
				case 0x15:
				{
					regval_t addr = fetch_register_value(vm);
					verbose_printf("call dword [0x%x]\n", addr);
					push(vm, EIP); //save our current instruction pointer, restore after ret
					//TODO: FIXME dereference value at operand's location and set EIP to that.
					//vm->registers[EIP] = fetch_register_value(vm);
				} break;
				
				//call eax
				case 0xd0:
					push(vm, EIP);
					vm->registers[EIP] = vm->registers[EAX];
					verbose_printf("call eax\n");
				break;
				
				//inc [r32]
				case 0x00:
				case 0x01:
				case 0x02:
				case 0x03:
				case 0x04:
				case 0x05:
				case 0x06:
				case 0x07:
				{
					regval_t *ptr = (regval_t*)get_memory_pointer(vm, vm->registers[operand]);
					*ptr += 1;
				} break;
				
				default:
					return VM_ERR_INVALID_OPCODE;
			}
		} break;
		
		//mov r32, imm32
		case 0xb8:
		case 0xb9:
		case 0xba:
		case 0xbb:
		case 0xbc:
		case 0xbd:
		case 0xbe:
		case 0xbf:
		{
			int reg = opcode - 0xb8;			
			regval_t value = fetch_register_value(vm);
			verbose_printf("mov %s, 0x%x\n", register_x86_names[reg], value);
			vm->registers[reg] = value;
		} break;
		
		//inc r32
		case 0x40:
		case 0x41:
		case 0x42:
		case 0x43:
		case 0x44:
		case 0x45:
		case 0x46:
		case 0x47:
		{
			int reg = opcode - 0x40;
            verbose_printf("inc %s\n", register_x86_names[reg]);
			vm->registers[reg] += 1;
		} break;

        //push r32
        case 0x50: //eax
        case 0x51: //ecx
        case 0x52: //edx
        case 0x53: //ebx
        case 0x54: //esp
        case 0x55: //ebp
        case 0x56: //esi
        case 0x57: //edi
        {
            int reg = opcode - 0x50;
            verbose_printf("push %s\n", register_x86_names[reg]);
			push(vm, reg);
        } break;
		
		//ret
		case 0xc3:
		{
			verbose_printf("ret\n");
			regval_t addr = pop(vm, EIP);
			vm->registers[EIP] = addr;
		} break;
		
		//call imm32
		case 0xe8:
		{
			regval_t addr = fetch_register_value(vm);
			verbose_printf("call 0x%x\n", addr);
			push(vm, EIP);
			vm->registers[EIP] += addr;
		} break;
		
		case 0x83:
		{
			int operand = fetch_operand(vm);
			switch(operand)
			{
				//cmp eax, rel8
				case 0xf8:
				{					
					int rel8 = fetch_operand(vm);
					verbose_printf("cmp eax, 0x%x\n", rel8);
					cmp(vm, vm->registers[EAX], rel8);
				} break;
				
				//add esp, rel8
				case 0xc4:
				{
					int rel8 = fetch_operand(vm);
					verbose_printf("add esp, 0x%x\n", rel8);
					vm->registers[ESP] += rel8;
				} break;
				
				default:
					return VM_ERR_UNHANDLED_OPERAND;
			}
		} break;
		
		//cmp eax, ecx
		case 0x39:
		{
			int operand = fetch_operand(vm);
			assert(operand == 0xc8);
			verbose_printf("cmp eax, ecx\n");
			int a = vm->registers[EAX];
			int b = vm->registers[ECX];
			cmp(vm, a, b);
		} break;
		
		case 0x8b:
		{
			int operand = fetch_operand(vm);
			switch(operand)
			{
				case 0x1b:
				{
					verbose_printf("mov ebx, [ebx]\n");
					vm->registers[EBX] = get_memory_value(vm, vm->registers[EBX]);
				} break;
				
				case 0x3:
					verbose_printf("mov eax, [ebx]\n");
					vm->registers[EAX] = get_memory_value(vm, vm->registers[EBX]);
				break;
				
				default:
					if(operand < 0x85 || operand > (0x85 + 64))
						return VM_ERR_UNHANDLED_OPERAND;
					int reg = (operand - 0x85) / 8;
					regval_t offset = fetch_register_value(vm);
					verbose_printf("mov %s, [ebp + 0x%x]\n", register_x86_names[reg], offset);
					vm->registers[reg] = get_memory_value(vm, vm->registers[EBP] + offset);
					break;
			}
		} break;
		
		case 0x8d:
		{
            int operand = fetch_operand(vm);
			switch(operand)
			{
				//lea edx,[ebx]
				case 0x13:
				{
					verbose_printf("lea edx, [ebx]\n");
					vm->registers[EDX] = vm->registers[EBX];
				} break;
				
				//lea r32, [ebp + offset]
				default:
				{
					if(operand < 0x85 || operand > (0x85 + 64))
						return VM_ERR_INVALID_OPCODE;
					int reg = (operand - 0x85) / 8;
					regval_t offset = fetch_register_value(vm);
					verbose_printf("lea %s, [ebp + 0x%x]\n", register_x86_names[reg], offset);
					vm->registers[reg] = vm->registers[EBP] + offset;
				} break;
			}
		} break;
		
		//int imm8
		case 0xcd:
		{
            int operand = fetch_operand(vm);
			switch(operand)
			{
				//linux x86 syscall
				case 0x80:
				{
					verbose_printf("int 0x80\n");
					switch(vm->registers[EAX])
					{
						//SYS_exit
						case 0x1:
							return VM_HALT;
						//SYS_write
						case 0x4:
						{
							//verbose_printf("write(%d, %d, %d)\n", vm->registers[EBX], vm->registers[ECX], vm->registers[EDX]);
							int write(int filedes, const void *buf, unsigned int nbyte);
							u8 *ecxbuf = get_memory_pointer(vm, vm->registers[ECX]);
							write(vm->registers[EBX], (void*)ecxbuf, vm->registers[EDX]);
						} break;
						default:
							return VM_ERR_UNHANDLED_SYSCALL;
					}
				} break;
				
				default:
					return VM_ERR_UNHANDLED_OPERAND;
			}
		} break;

        //pop r32
        case 0x58: //eax
        case 0x59: //ecx
        case 0x5a: //edx
        case 0x5b: //ebx
        case 0x5c: //esp
        case 0x5d: //ebp
        case 0x5e: //esi
        case 0x5f: //edi
        {
            int reg = opcode - 0x58;
            verbose_printf("pop %s\n", register_x86_names[reg]);
			pop(vm, reg);
        } break;

		//jmp rel32
		case 0xe9:
		{
			regval_t rel32 = fetch_register_value(vm);
			verbose_printf("jmp %x (%d)\n", rel32 + 5, rel32 + 5);
			vm->registers[EIP] += rel32;
		} break;
		
		//test r32, r32
        case 0x85:
        {
            int operand = fetch_operand(vm);
			if(operand < 0xc0 || operand > 0xff)
				return VM_ERR_INVALID_OPCODE;
			operand -= 0xc0;
			int dstreg = operand % 8;
			int srcreg = (operand - dstreg) / 8;
			regval_t result = vm->registers[dstreg] & vm->registers[srcreg];
			set_flags(vm, result);
			verbose_printf("test %s, %s\n", register_x86_names[dstreg], register_x86_names[srcreg]);
        } break;
		
		case 0x0f:
		{
			int operand = fetch_operand(vm);
			switch(operand)
			{
				default:
					return VM_ERR_UNHANDLED_OPERAND;
					
				case 0xb6:
				{
					operand = fetch_operand(vm);
					switch(operand)
					{
						case 0xc0:
							verbose_printf("movzx eax, al\n");
							vm->registers[EAX] = register_byte_value(vm->registers[EAX], 0);
						break;
						
						case 0x03:
							verbose_printf("movzx eax, byte [ebx]\n");
							vm->registers[EAX] = register_byte_value(get_memory_value(vm, vm->registers[EBX]), 0);
						break;
					}
				} break;
				
				//jz rel32
				case 0x84:
				{
					regval_t rel32 = fetch_register_value(vm);
					verbose_printf("jz %x (%d)\n", rel32 + 6, rel32 + 6);
					if((vm->registers[REGISTER_X86_FLAGS] & X86_ZERO_FLAG) == X86_ZERO_FLAG)
						vm->registers[EIP] += rel32;
				} break;
			}
		} break;
		
		//jge rel8
		case 0x7d:
		{
			int rel8 = fetch_operand(vm);
			verbose_printf("jge %x (%d)\n", rel8 + 2, rel8 + 2);
			int of = (vm->registers[REGISTER_X86_FLAGS] & X86_OVERFLOW_FLAG) == X86_OVERFLOW_FLAG;
			int sf = (vm->registers[REGISTER_X86_FLAGS] & X86_SIGN_FLAG) == X86_SIGN_FLAG;
			if(of == sf)
				vm->registers[EIP] += rel8;
		} break;
		
		//jne rel8
		case 0x75:
		{
			int rel8 = fetch_operand(vm);
			verbose_printf("jne %x (%d)\n", rel8 + 2, rel8 + 2);
			int zf = (vm->registers[REGISTER_X86_FLAGS] & X86_ZERO_FLAG) == X86_ZERO_FLAG;
			if(zf == 0)
				vm->registers[EIP] += rel8;
		} break;
		
		//jmp rel8
		case 0xeb:
		{
			int rel8 = fetch_operand(vm);
			verbose_printf("jmp %x (%d)\n", rel8 + 2, rel8 + 2);
			vm->registers[EIP] += rel8;
		} break;

        //hlt
        case 0xf4:
            return VM_HALT;
		
		//nop
		case 0x90:
			verbose_printf("nop\n");
		break;
		
		default:
			verbose_printf("invalid opcode %d (0x%x)\n", opcode, opcode);
			return VM_ERR_INVALID_OPCODE;
    }
    return VM_OK;
}

int dec(int c)
{
	if(c >= '0' && c <= '9')
		return c - '0';
	if(c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if(c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return 0;
}

int hex2dec(const char *str)
{
	int sum = 0;
	const char *p = str;
	if(strchr(p, 'x'))
		p = strchr(p, 'x') + 1;
	size_t l = strlen(p);
	for(size_t i = 0; i < l; ++i)
		sum += dec(p[i]) << (4 * (l - i - 1));
	return sum;
}

int main(int argc, char **argv)
{
    vm_t vm;
    memset(&vm, 0, sizeof(vm));

#define MEMSZ (0xffff * 2)
    u8 vm_memory[MEMSZ];
	memset(vm_memory, 0, sizeof(vm_memory));
	vm.mem = vm_memory;
	vm.memsz = MEMSZ;
	
	int memidx = 0;
	//copy the code segment/data segment into vm memory
	for(int i = 1; i < argc; i++)
	{
		int d = hex2dec(argv[i]);
		vm_memory[memidx++] = d;
	}
    vm.registers[EIP] = 0; //set instruction pointer to location where we are in instr
	vm.nreadonly = memidx;
    vm.registers[ESP] = 0xffff;
    while(1)
    {
        int err = execute_vm(&vm);
		if(err != 0)
		{
			if(err != VM_HALT)
			{
				printf("Error: %d\n", err);
			}
			break;
		}
		#ifdef VERBOSE
        //dump_vm_state(&vm);
		getchar();
		#endif
    }
    return 0;
}