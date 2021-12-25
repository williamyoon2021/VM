#include <stdint.h> // uint16_t
#include <stdio.h>  // FILE
#include <signal.h> // SIGINT
/* windows only */
#include <Windows.h>
#include <conio.h>  // _kbhit

HANDLE hStdin = INVALID_HANDLE_VALUE;

/* REGISTERS */
enum
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
    R_COND, /* program counter */
    R_COUNT
};

/* OPCODE */
enum
{
    OP_BR = 0, /* branch */
    OP_ADD, /* add */
    OP_LD, /* load */
    OP_ST, /* store */
    OP_JSR, /* jump register */
    OP_AND, /* bitwise and */
    OP_LDR, /* load register */
    OP_STR, /* store register */
    OP_RTI, /* unused */
    OP_NOT, /* bitwise not */
    OP_LDI, /* load indirect */
    OP_STI, /* store indirect */
    OP_JMP, /* jump */
    OP_RES, /* reserved (unused) */
    OP_LEA, /* load effective address */
    OP_TRAP /* execute trap */
};

/* TRAP CODES */
enum
{
    TRAP_GETC = 0x20, /* get charactor from keyboard, not echoed onto terminal */
    TRAP_OUT = 0x21, /* output a character */
    TRAP_PUTS = 0x22, /* output a word string */
    TRAP_IN = 0x23, /* get character from keyboard, echoed onto terminal */
    TRAP_PUTSP = 0x24, /* output a byte string */
    TRAP_HALT = 0x25, /* halt the program */
};

/* CONDITION FLAGS */
enum
{
    FL_POS = 1 << 0, /* P */
    FL_ZRO = 1 << 1, /* Z */
    FL_NEG = 1 << 2 /* N */
};

/* MEMORY MAPPED REFISTERS */
enum
{
    MR_KBSR = 0xFE00, /* if key is pressed */
    MR_KBDR = 0xFE02  /* which key is pressed */
};

/* MEMORY STORAGE*/
uint16_t memory[UINT16_MAX]; /* 65536 locations */

/* REGISTERS STORAGE */
uint16_t reg[R_COUNT];

/* WRITE MEMORY */
void mem_write(uint16_t address, uint16_t val) 
{
    memory[address] = val;
}

/* READ MEMORY */
uint16_t mem_read(uint16_t address)
{
    if (address == MR_KBSR)
    {
        if(check_key())
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


/* READ LC-3 PROGRAM INTO MEM */
void read_image_file(FILE* file)
{
    /* origin tells where in memory to place origin */
    uint16_t origin;
    fread(&origin, sizeof(origin), 1, file);
    origin = swap16(origin);

    /* know maximum file size so onlyn need one fread */
    uint16_t max_read = UINT16_MAX - origin;
    uint16_t* p = memory + origin;
    size_t read = fread(p, sizeof(uint16_t), max_read, file);

    /* swap to little endian */
    while (read-- > 0)
    {
        *p = swap16(*p);
        ++p;
    }
}

/* SWAP BIG-ENDIAN TO LITTLE-ENDIAN */
uint16_t swap16(uint16_t x)
{
    return (x << 8) | (x >> 8);
}

/* READ IMAGE */
int read_image(const char* image_path)
{
    FILE* file = fopen(image_path, "rb");
    if (!file) { return 0; };
    read_image_file(file);
    fclose(file);
    return 1;

}

/* SIGN EXTEND */
uint16_t sign_extend(uint16_t x, int bit_count)
{
    /* checks left-most bit to see if negative  */
    if (( x >> (bit_count - 1)) & 1) {
        /* only adds 1 after MSB */
        x |= (0xFFFF << bit_count);
    }
    return x;
}

/* UPDATE FLAGS */
void update_flags(uint16_t r)
{
    if (reg[r] == 0)
    {
        reg[R_COND] = FL_ZRO;
    }
    else if (reg[r] >> 15) /* a 1 in the left-most bit indicates negtative */
    {
        reg[R_COND] = FL_NEG;
    }
    else
    {
        reg[R_COND] = FL_POS;
    }
}

/* CHECK KEY */
uint16_t check_key()
{
    return WaitForSingleObject(hStdin, 1000) == WAIT_OBJECT_0 && _kbhit();
}

/* INPUT BUFFERING WINDOWS */
DWORD fdwMode, fdwOldMode;

void disable_input_buffering()
{
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hStdin, &fdwOldMode); /* save old mode */
    fdwMode = fdwOldMode
            ^ ENABLE_ECHO_INPUT  /* no input echo */
            ^ ENABLE_LINE_INPUT; /* return when one or
                                    more characters are available */
    SetConsoleMode(hStdin, fdwMode); /* set new mode */
    FlushConsoleInputBuffer(hStdin); /* clear buffer */
}

void restore_input_buffering()
{
    SetConsoleMode(hStdin, fdwOldMode);
}

/* HANDLE INTERRUPT */
void handle_interrupt(int signal)
{
    restore_input_buffering();
    printf("\n");
    exit(-2);
}

/* MAIN LOOP */
int main(int argc, const char* argv[])
{
    /* LOAD ARGUMENTS */
    if (argc < 2)
    {
        /* show usage string */
        printf("lc3 [image-file1] ...\n");
        exit(2);
    }
    
    for (int j = 1; j < argc; ++j)
    {
        if (!read_image(argv[j]))
        {
            printf("failed to load image: %s\n", argv[j]);
            exit(1);
        }
    }

    /* SETUP */
    signal(SIGINT, handle_interrupt);
    disable_input_buffering();
    
    /* set the PC to starting position */
    /* 0x3000 is the default */
    enum { PC_START = 0x3000 };
    reg[R_PC] = PC_START;

    int running = 1;
    while (running)
    {
        /* FETCH */
        uint16_t instr = mem_read(reg[R_PC]++);
        uint16_t op = instr >> 12;

        switch (op)
        {
            case OP_ADD:
                {
                    uint16_t r0 = (instr >> 9) & 0x7;
                    uint16_t r1 = (instr >> 6) & 0x7;
                    uint16_t imm_flag = (instr >> 5) & 0x1;

                    if (imm_flag)
                    {
                        uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                        reg[r0] = reg[r1] + imm5;
                    }
                    else
                    {
                        uint16_t r2 = instr & 0x7;
                        reg[r0] = reg[r1] + reg[r2];
                    }
                    update_flags(r0);
                }
                break;
            case OP_AND:
                {
                    uint16_t r0 = (instr >> 9) & 0x7;
                    uint16_t r1 = (instr >> 6) & 0x7;
                    uint16_t imm_flag = (instr >> 5) & 0x1;

                    if (imm_flag)
                    {
                        uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                        reg[r0] = reg[r1] & imm5;
                    }
                    else
                    {
                        uint16_t r2 = instr & 0x7;
                        reg[r0] = reg[r1] & reg[r2];
                    }
                    update_flags(r0);
                }
                break;
            case OP_NOT:
                {
                    uint16_t r0 = (instr >> 9) & 0x7;
                    uint16_t r1 = (instr >> 6) & 0x7;

                    reg[r0] = ~reg[r1];
                    update_flags(r0);
                }
                break;
            case OP_BR:
                {
                    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                    uint16_t cond_flag = (instr >> 9) & 0x7;
                    if (cond_flag & reg[R_COND])
                    {
                        reg[R_PC] += pc_offset;
                    }
                }
                break;
            case OP_JMP:
                {
                    uint16_t r1 = (instr >> 6) & 0x7;
                    reg[R_PC] = reg[r1];
                }
                break;
            case OP_JSR:
                {
                    uint16_t long_flag = (instr >> 11) & 1;
                    reg[R_R7] = reg[R_PC];
                    if (long_flag)
                    {
                        uint16_t long_pc_offset = sign_extend(instr & 0x7FF, 11);
                        reg[R_PC] += long_pc_offset; /*JSR*/
                    }
                    else 
                    {
                        uint16_t r1 = (instr >> 6) & 0x7;
                        reg[R_PC] = reg[R_R1]; /*JSRR*/
                    }
                    break;
                }
                break;
            case OP_LD:
                {
                    uint16_t r0 = (instr >> 9) & 0x7;
                    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                    reg[r0] = mem_read(reg[R_PC] + pc_offset);
                    update_flags(r0);
                }
                break;
            case OP_LDI:
                {
                    uint16_t r0 = (instr >> 9) & 0x7;
                    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                    reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offset));
                    update_flags(r0);
                }
                break;
            case OP_LDR:
                {
                    uint16_t r0 = (instr >> 9) & 0x7;
                    uint16_t r1 = (instr >> 6) & 0x7;
                    uint16_t offset = sign_extend(instr & 0x3F, 6);
                    reg[r0] = mem_read(reg[r1] + offset);
                    update_flags(r0);
                }
                break;
            case OP_LEA:
                {
                    uint16_t r0 = (instr >> 9) & 0x7;
                    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                    reg[r0] = reg[R_PC] + pc_offset;
                    update_flags(r0);
                }
                break;
            case OP_ST:
                {
                    uint16_t r0 = (instr >> 9) & 0x7;
                    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                    mem_write(reg[R_PC] + pc_offset, reg[r0]);
                }
                break;
            case OP_STI:
                {
                    uint16_t r0 = (instr >> 9) & 0x7;
                    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                    mem_write(mem_read(reg[R_PC] + pc_offset), reg[r0]);
                }
                break;
            case OP_STR:
                {
                    uint16_t r0 = (instr >> 9) & 0x7;
                    uint16_t r1 = (instr >> 6) & 0x7;
                    uint16_t offset = sign_extend(instr & 03F, 6);
                    mem_write(reg[r1] + offset, reg[r0]);
                }
                break;
            case OP_TRAP:
                switch (instr & 0xFF)
                {
                    case TRAP_GETC:
                    {
                        /* read single ASCII char */
                        reg[R_R0] = (uint16_t)getchar();
                    }
                    break;
                    case TRAP_OUT:
                    {
                        putc((char)reg[R_R0], stdout);
                        fflush(stdout);
                    }
                    break;
                    case TRAP_PUTS:
                    {
                        /* one char per word */
                        uint16_t* c = memory + reg[R_R0];
                        while (*c)
                        {
                            putc((char) *c, stdout);
                            ++c;
                        }
                        fflush(stdout);
                    }
                    break;
                    case TRAP_IN:
                    {
                        printf("Enter a character: ");
                        char c = getchar();
                        putc(c, stdout);
                        reg[R_R0] = (uint16_t)c;
                    }
                    break;
                    case TRAP_PUTSP:
                    {
                        /* one char per byte (two bytes per word) */
                        uint16_t* c = memory + reg[R_R0];
                        while (*c)
                        {
                            char char1 = (*c) & 0xFF;
                            putc(char1, stdout);
                            char char2 = (*c) >> 8;
                            if (char2) putc(char2, stdout);
                            ++c;
                        }
                        fflush(stdout);
                    }
                    break;
                    case TRAP_HALT:
                    {
                        puts("HALT");
                        fflush(stdout);
                        running = 0;
                    }
                    break;
                }
            case OP_RES:
            case OP_RTI:
            default:
                abort();
                break;
        }
    }
    /* WINDOWS SHUTDOWN */
    restore_input_buffering();
}
