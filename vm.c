//basic x86 opcode virtual machine

#include "compile.h"
#include "types.h"
#include <assert.h>

typedef int32_t regval_t;

#define COUNT_OF(x) (sizeof(x) / sizeof(x[0]))

typedef struct
{
    u8 *instr;
	int numinstr;
    regval_t registers[REGISTER_X86_MAX];
    regval_t *stack;
	int stacksize;
} vm_t;

regval_t* query_register(vm_t *vm, int reg)
{
    return &vm->registers[reg];
}

int fetch_opcode(vm_t *vm)
{
	/*
	u8 op;
	scanf("%02hhX", &op);
	return op;
	*/
	assert(vm->registers[EIP] >= 0 && vm->registers[EIP] < vm->numinstr);
    return vm->instr[vm->registers[EIP]++];
}

int fetch_instruction(vm_t *vm)
{
	//printf("instruction?\n");
	return fetch_opcode(vm);
}

int fetch_operand(vm_t *vm)
{
	//printf("operand?\n");
	return fetch_opcode(vm);
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
	VM_ERR_INVALID_OPCODE
};

static void push(vm_t *vm, int reg)
{
	vm->registers[ESP] -= sizeof(regval_t);
	vm->stack[vm->registers[ESP]] = vm->registers[reg];
}

static regval_t pop(vm_t *vm, int reg)
{	
	vm->registers[reg] = vm->stack[vm->registers[ESP]];
	vm->registers[ESP] += sizeof(regval_t);
	assert(vm->registers[ESP] <= vm->stacksize);
	return vm->registers[reg];
}

static void set_flags(vm_t *vm, regval_t result)
{
	if(result < 0)
		vm->registers[REGISTER_X86_FLAGS] |= X86_SIGN_FLAG;
	else
		vm->registers[REGISTER_X86_FLAGS] &= ~X86_SIGN_FLAG;
	
	if(result == 0)
		vm->registers[REGISTER_X86_FLAGS] &= ~X86_ZERO_FLAG;
	else
		vm->registers[REGISTER_X86_FLAGS] |= X86_ZERO_FLAG;
}

int execute_vm(vm_t *vm)
{
    int opcode = fetch_instruction(vm);
    switch(opcode)
    {
		//add r32, r32
        case 0x1:
        {
            int operand = fetch_operand(vm);
			if(operand < 0xc0 || operand > 0xff)
				return VM_ERR_INVALID_OPCODE;
			operand -= 0xc0;
			int dstreg = operand % 8;
			int srcreg = (operand - dstreg) / 8;
			vm->registers[dstreg] += vm->registers[srcreg];
			printf("add %s, %s\n", register_x86_names[dstreg], register_x86_names[srcreg]);
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
			printf("xor %s, %s\n", register_x86_names[dstreg], register_x86_names[srcreg]);
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
			printf("sub %s, %s\n", register_x86_names[dstreg], register_x86_names[srcreg]);
        } break;
		
		//call dword ptr
		case 0xff:
		{
			int operand = fetch_operand(vm);
			assert(operand == 0x15);
			push(vm, EIP); //save our current instruction pointer, restore after ret
			//TODO: FIXME dereference value at operand's location and set EIP to that.
			//vm->registers[EIP] = fetch_register_value(vm);
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
			printf("mov %s, 0x%x\n", register_x86_names[reg], value);
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
			vm->registers[reg] += 1;
		} break;

        //push r32
        case 0x50: //eax
        case 0x51: //ecx
        case 0x52: //edx
        case 0x53: //ebx
        case 0x54: //esp
        case 0x55: //ebp
        case 0x56: //edi
        {
            int reg = opcode - 0x50;
            printf("push %s\n", register_x86_names[reg]);
			push(vm, reg);
        } break;

        //pop r32
        case 0x58: //eax
        case 0x59: //ecx
        case 0x60: //edx
        case 0x61: //ebx
        case 0x62: //esp
        case 0x63: //ebp
        case 0x64: //edi
        {
            int reg = opcode - 0x58;
            printf("pop %s\n", register_x86_names[reg]);
			pop(vm, reg);
        } break;

		//jmp rel32
		case 0xe9:
		{
			regval_t rel32 = fetch_register_value(vm);
			printf("jmp %x (%d)\n", rel32 + 5, rel32 + 5);
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
			printf("test %s, %s\n", register_x86_names[dstreg], register_x86_names[srcreg]);
        } break;
		
		//jz rel32
		case 0x0f:
		{
			int operand = fetch_operand(vm);
			assert(operand == 0x84);
			regval_t rel32 = fetch_register_value(vm);
			printf("jz %x (%d)\n", rel32 + 6, rel32 + 6);
			if((vm->registers[REGISTER_X86_FLAGS] & X86_ZERO_FLAG) == X86_ZERO_FLAG)
				vm->registers[EIP] += rel32;
		} break;

        //hlt
        case 0xf4:
            return VM_HALT;
		
		//nop
		case 0x90:
			printf("nop\n");
		break;
		
		default:
			printf("invalid\n");
			return VM_ERR_INVALID_OPCODE;
    }
    return VM_OK;
}

static void dump_vm_state(vm_t *vm)
{
    printf("flags: %d\n", vm->registers[REGISTER_X86_FLAGS]);
    printf("instructions: %d\n", vm->numinstr);
    printf("registers:\n");
    for(int i = 0; i < 8; ++i)
    {
        printf("\t%s: %d\n", register_x86_names[i], vm->registers[i]);
    }
}

int dec(int c)
{
	if(c >= '0' && c <= '9')
		return c - '0';
	if(c >= 'a' && c <= 'f')
		return c - 'a' + 10;
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
	int ninstr = 0;
	u8 instr[4096];
	for(int i = 1; i < argc; i++)
	{
		int d = hex2dec(argv[i]);
		instr[ninstr++] = d;
	}
	instr[ninstr++] = 0xf4; //hlt
	
    vm_t vm;
    memset(&vm, 0, sizeof(vm));

    regval_t vm_stack[4096];
	vm.stacksize = COUNT_OF(vm_stack);
	memset(vm_stack, 0, sizeof(vm_stack));

    //set our stack
    vm.stack = vm_stack;

    //u8 instr[] = {0x50,0x58,0xf4};
    vm.instr = instr;
	vm.numinstr = ninstr;

    vm.registers[EIP] = 0; //set instruction pointer to location where we are in instr

    //set esp to the end of the stack
    vm.registers[ESP] = vm.stacksize;
    while(1)
    {
        if(execute_vm(&vm))
            break;
        dump_vm_state(&vm);
    }
    return 0;
}