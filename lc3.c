#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>

#define MAX_MEMORY (1 << 16)
uint16_t memory[MAX_MEMORY];

enum Register
{
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC,
    R_COND,
    R_COUNT,
};
uint16_t reg[R_COUNT];

enum ConditionFlags
{
    FL_POS = 1 << 0,
    FL_ZRO = 1 << 1,
    FL_NEG = 1 << 2,
};

enum OpCodes
{
    OP_BR = 0, // branch
    OP_ADD,    // add
    OP_LD,     // load
    OP_ST,     // store
    OP_JSR,    // jump register
    OP_AND,    // bitwise and
    OP_LDR,    // load register
    OP_STR,    // store register
    OP_RTI,    // unused
    OP_NOT,    // bitwise not
    OP_LDI,    // load indirect
    OP_STI,    // store indirect
    OP_JMP,    // jump
    OP_RES,    // reserved (unused)
    OP_LEA,    // load effective address
    OP_TRAP,   // execute trap
};

enum TrapCodes
{
    TRAP_GETC = 0x20,
    TRAP_OUT = 0x21,
    TRAP_PUTS = 0x22,
    TRAP_IN = 0x23,
    TRAP_PUTSP = 0x24,
    TRAP_HALT= 0x25,
};

enum MemoryMappedRegisters
{
    MR_KBSR = 0xFE00,
    MR_KBDR = 0xFE02,
};

uint16_t check_key()
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    return select(1, &readfds, NULL, NULL, &timeout) != 0;
}

void mem_write(uint16_t address, uint16_t value)
{
    memory[address] = value;
}

uint16_t mem_read(uint16_t address)
{
    if (address == MR_KBSR)
    {
        if (check_key())
        {
            memory[MR_KBSR] = (1 << 15);
            memory[MR_KBDR] = getchar();
        }
        else
        {
            memory[MR_KBSR] = 0;
        }
    }
    return memory[address];
}

uint16_t sign_extend(uint16_t x, int bit_count)
{
    if ((x >> (bit_count - 1)) & 1 == 1)
    {
        x |= (0xFFFF << bit_count);
    }
    return x;
}

void update_condition_flags(uint16_t r)
{
    if (reg[r] == 0)
    {
        reg[R_COND] = FL_ZRO;
    }
    else if (reg[r] >> 15)
    {
        reg[R_COND] = FL_NEG;
    }
    else
    {
        reg[R_COND] = FL_POS;
    }
}

void op_add(uint16_t instr)
{
    uint16_t dr = ((instr >> 9) & 0x7);
    uint16_t sr1 = ((instr >> 6) & 0x7);
    uint16_t imm_flag = (instr >> 5) & 0x1;

    if (imm_flag)
    {
        uint16_t imm5 = sign_extend((instr & 0x1F), 5);
        reg[dr] = reg[sr1] + imm5;
    }
    else
    {
        uint16_t sr2 = (instr & 0x7);
        reg[dr] = reg[sr1] + reg[sr2];
    }

    update_condition_flags(dr);
}

void op_and(uint16_t instr)
{
    uint16_t dr = (instr >> 9) & 0x7;
    uint16_t sr1 = (instr >> 6) & 0x7;
    uint16_t imm_flag = (instr >> 5) & 0x1;

    if (imm_flag)
    {
        uint16_t imm5 = sign_extend(instr & 0x1F, 5);
        reg[dr] = reg[sr1] & imm5;
    }
    else
    {
        uint16_t sr2 = instr & 0x7;
        reg[dr] = reg[sr1] & reg[sr2];
    }

    update_condition_flags(dr);
}

void op_not(uint16_t instr)
{
    uint16_t dr = (instr >> 9) & 0x7;
    uint16_t sr = (instr >> 6) & 0x7;
    reg[dr] = ~reg[sr];
    update_condition_flags(dr);
}

void op_br(uint16_t instr)
{
    uint16_t cond_flag = (instr >> 9) & 0x7;
    uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);
    if (cond_flag & reg[R_COND])
    {
        reg[R_PC] += pc_offset9;
    }
}

void op_jmp(uint16_t instr)
{
    uint16_t base = (instr >> 6) & 0x7;
    reg[R_PC] = reg[base];
}

void op_jsr(uint16_t instr)
{
    reg[R_R7] = reg[R_PC];
    if ((instr >> 11) & 0x1)
    {
        uint16_t pc_offset11 = sign_extend(instr & 0x7FF, 11);
        reg[R_PC] += pc_offset11;
    }
    else
    {
        uint16_t base = (instr >> 6) & 0x7;
        reg[R_PC] = reg[base];
    }
}

void op_ld(uint16_t instr)
{
    uint16_t dr = (instr >> 9) & 0x7;
    uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);
    reg[dr] = mem_read(reg[R_PC] + pc_offset9);
    update_condition_flags(dr);
}

void op_ldi(uint16_t instr)
{
    uint16_t dr = ((instr >> 9) & 0x7);
    uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9); 
    reg[dr] = mem_read(mem_read(reg[R_PC] + pc_offset9));
    update_condition_flags(dr);
}

void op_ldr(uint16_t instr)
{
    uint16_t dr = (instr >> 9) & 0x7;
    uint16_t base = (instr >> 6) & 0x7;
    uint16_t offset6 = sign_extend(instr & 0x3F, 6);
    reg[dr] = mem_read(reg[base] + offset6);
    update_condition_flags(dr);
}

void op_lea(uint16_t instr)
{
    uint16_t dr = (instr >> 9) & 0x7;
    uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);
    reg[dr] = reg[R_PC] + pc_offset9;
    update_condition_flags(dr);
}

void op_st(uint16_t instr)
{
    uint16_t sr = (instr >> 9) & 0x7;
    uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);
    mem_write(reg[R_PC] + pc_offset9, reg[sr]);
}

void op_sti(uint16_t instr)
{
    uint16_t sr = (instr >> 9) & 0x7;
    uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);
    mem_write(mem_read(reg[R_PC] + pc_offset9), reg[sr]);
}

void op_str(uint16_t instr)
{
    uint16_t sr = (instr >> 9) & 0x7;
    uint16_t base = (instr >> 6) & 0x7;
    uint16_t offset6 = sign_extend(instr & 0x3F, 6);
    mem_write(reg[base] + offset6, reg[sr]);
}

void trap_getc()
{
    uint16_t c = (uint16_t)getchar();
    reg[R_R0] = c;
}

void trap_out()
{
    putc(reg[R_R0], stdout);
    fflush(stdout);
}

void trap_puts()
{
    uint16_t* c = memory + reg[R_R0];
    while (*c)
    {
        putc((char)*c, stdout);
        ++c;
    }
    fflush(stdout);
}

void trap_in()
{
    printf("Enter a character: ");
    uint16_t c = (uint16_t)getchar();
    putchar(c);
    fflush(stdout);
    reg[R_R0] = c;
}

void trap_putsp()
{
    uint16_t* c = memory + reg[R_R0];
    while(*c)
    {
        char fc = (*c) & 0xFF;
        char sc = (*c) >> 8;
        putc(fc, stdout);
        if (sc) putc(sc, stdout);
        ++c;
    }
    fflush(stdout);
}

int running = 1;

void trap_halt()
{
    puts("HALT");
    fflush(stdout);
    running = 0;
}

void op_trap(uint16_t instr)
{
    reg[R_R7] = reg[R_PC];
    uint16_t trapvect8 = instr & 0xFF;

    switch (trapvect8)
    {
    case TRAP_GETC:
        trap_getc();
        break;
    case TRAP_OUT:
        trap_out();
        break;
    case TRAP_PUTS:
        trap_puts();
        break;
    case TRAP_IN:
        trap_in();
        break;
    case TRAP_PUTSP:
        trap_putsp();
        break;
    case TRAP_HALT:
        trap_halt();
        break;
    }
}

uint16_t swap16(uint16_t x)
{
    return (x << 8) | (x >> 8);
}

void read_image_file(FILE* file)
{
    uint16_t origin;
    fread(&origin, sizeof(uint16_t), 1, file);
    origin = swap16(origin);

    uint16_t max_read = MAX_MEMORY - origin;
    uint16_t* p = memory + origin;
    size_t read = fread(p, sizeof(uint16_t), max_read, file);
    
    while (read > 0)
    {
        *p = swap16(*p);
        ++p;
        --read;
    }
}

int read_image(const char* file_path)
{
    FILE* file = fopen(file_path, "rb");
    if (file == NULL)
    {
        return 0;
    }
    read_image_file(file);
    fclose(file);
    return 1;
}

struct termios orig_tio;

void disable_input_buffering()
{
    tcgetattr(STDIN_FILENO, &orig_tio);
    struct termios new_tio = orig_tio;
    new_tio.c_lflag &= ~ICANON & ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_tio);
}

void handle_interrupt(int signal)
{
    restore_input_buffering();
    printf("\n");
    exit(-2);
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printf("lc3 [image-file-1] ...\n");
        exit(2);
    }

    for (int i = 1; i < argc; i++)
    {
        if (!read_image(argv[i]))
        {
            printf("failed to load image: %s\n", argv[i]);
            exit(1);
        }
    }

    signal(SIGINT, handle_interrupt);
    disable_input_buffering();

    reg[R_COND] = FL_ZRO;

    enum { PC_START = 0x3000 };
    reg[R_PC] = PC_START;

    while (running)
    {
        uint16_t instruction = mem_read(reg[R_PC]);
        reg[R_PC]++;
        uint16_t opcode = instruction >> 12;

        switch(opcode)
        {
        case OP_ADD:
            op_add(instruction);
            break;
        case OP_AND:
            op_and(instruction);
            break;
        case OP_NOT:
            op_not(instruction);
            break;
        case OP_BR:
            op_br(instruction);
            break;
        case OP_JMP:
            op_jmp(instruction);
            break;
        case OP_JSR:
            op_jsr(instruction);
            break;
        case OP_LD:
            op_ld(instruction);
            break;
        case OP_LDI:
            op_ldi(instruction);
            break;
        case OP_LDR:
            op_ldr(instruction);
            break;
        case OP_LEA:
            op_lea(instruction);
            break;
        case OP_ST:
            op_st(instruction);
            break;
        case OP_STI:
            op_sti(instruction);
            break;
        case OP_STR:
            op_str(instruction);
            break;
        case OP_TRAP:
            op_trap(instruction);
            break;
        // bad opcode
        case OP_RES:
        case OP_RTI:
        default:
            abort();
            break;
        }
    }

    restore_input_buffering();
}