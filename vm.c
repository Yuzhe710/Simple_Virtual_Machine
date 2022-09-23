#include <stdio.h>
#include <stdint.h>
#include <signal.h>
/* unix only */
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>

/*
The registers for LC-3 
10 total registers, each is 16 bits
8 General purpose registers R0 - R7, used for any calculations
1 program counter register PC, unsigned integer which is the address of next instruction in memory to execute
1 conditional flag register COND, has the information of the previous instruction
*/

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
    R_COND,
    R_COUNT
};

/* Opcodes for instructions  */
/* Opcode indicates which kind of task to perform and its parameters 
   16 opcodes in LC-3, each instruction is 16 bits long, with the left 4 bits storing Opcodes 
   the rest storing the parameters */

enum 
{
    OP_BR = 0, /* branch */
    OP_ADD,    /* add */
    OP_LD,     /* load */ 
    OP_ST,     /* store */
    OP_JSR,    /* jump register */ 
    OP_AND,    /* bitwise and */
    OP_LDR,    /* load register */
    OP_STR,    /* store register */ 
    OP_RTI,    /* unused */
    OP_NOT,    /* bitwise not */ 
    OP_LDI,    /* load indirect */
    OP_STI,    /* store indirect */ 
    OP_JMP,    /* jump */ 
    OP_RES,    /* reserved (unused) */ 
    OP_LEA,    /* load effective address */ 
    OP_TRAP    /* execute trap */
};

/* Condition Flags */
/* there are 3 condition flags in LC-3, which indicates the sign of previous calculation */
enum
{
    FL_POS = 1 << 0, /* P */
    FL_ZRO = 1 << 1, /* Z */
    FL_NEG = 1 << 2, /* N */
};

enum
{
    TRAP_GETC = 0x20, /* get character from keyboard, not echoed onto the terminal */
    TRAP_OUT = 0x21, /* output a character */
    TRAP_PUTS = 0x22, /* output a word string */
    TRAP_IN = 0x23, /* get character from keyboard, echoed onto the terminal */
    TRAP_PUTSP = 0x24, /* output a byte string */
    TRAP_HALT = 0x25 /* halt the program */
};

/* Memory Mapped Registers */
enum
{
    MR_KBSR = 0xFE00, /* keyboard status */
    MR_KBDR = 0xFE02  /* keyboard data */
};


#define MEMORY_MAX (1 << 16) // 65536
uint16_t memory[MEMORY_MAX]; // 65536 memory locations, about 128KB
uint16_t reg[R_COUNT];


/* ------------------------- UTILITY ----------------------------------- */

/* UNIX specifc specific */
struct termios original_tio;

void disable_input_buffering()
{
    tcgetattr(STDIN_FILENO, &original_tio);
    struct termios new_tio = original_tio;
    new_tio.c_lflag &= ~ICANON & ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

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

void handle_interrupt(int signal)
{
    restore_input_buffering();
    printf("\n");
    exit(-2);
}

/* Sign extending for 5 bits adder to be extended to 16 bits */
/* if x is positive, simply fill 0's before the bits, 
   if x is negative, need to fill 1's before the bits */
uint16_t sign_extend(uint16_t x, int bit_count) { // what is bit count ????

    if ((x >> (bit_count - 1)) & 1) {
        x |= (0xFFFF << bit_count); // means it is negative
    }
    return x; // Sign extension corrects this problem by filling in 0’s for positive numbers and 1’s for negative numbers, so that original values are preserved.
}

/* update the condition flags whenever a value is written to a register */
/* The flag is used to indicate its sign */
void update_flags(uint16_t r) {
    if (reg[r] == 0) {
        reg[R_COND] = FL_ZRO;
    } else if (reg[r] >> 15) { /* a 1 in the left-most bit indicates negative */
        reg[R_COND] = FL_NEG;
    } else {
        reg[R_COND] = FL_POS;
    }
}

/* swap a 16 bits representation from big-edian to little-edian (LC-3 is big-edian, most modern computers are little-edian) */
uint16_t swap16(uint16_t x) {
    return (x << 8) | (x >> 8);
}

/* load the program from file containing array of instructions and data into memory */ 
/* The first 16 bits of the program file specify the address in memory where the memory should start (origin)*/
/* It must be read first, after which the rest of the data can be read from the file into memory starting at the origin address. */
void read_image_file(FILE* file)
{
    uint16_t origin;
    fread(&origin, sizeof(origin), 1, file);
    origin = swap16(origin);

    uint16_t max_read = MEMORY_MAX - origin;
    uint16_t* p = memory + origin;
    size_t read = fread(p, sizeof(uint16_t), max_read, file);

    /* swap to little endian */
    while (read-- > 0) {
        *p = swap16(*p);
        ++p;
    }
}

/* The convenience function for read_image_file */
int read_image(const char* image_path)
{
    FILE* file = fopen(image_path, "rb");
    if (!file) {return 0;};
    read_image_file(file);
    fclose(file);
    return 1;
}

/* --------------- Memory Access --------------- */
/* For memory mapped registers, just read and write into their memory locations */
/* Getter and Setter functions should be called for memory mapped registers instead of reading and writing directly */
void mem_write(uint16_t address, uint16_t val)
{
    memory[address] = val;
}

uint16_t mem_read(uint16_t address) {
    if (address == MR_KBSR) {
        if (check_key()) {
            memory[MR_KBSR] = (1 << 15);
            memory[MR_KBDR] = getchar();
        } else {
            memory[MR_KBSR] = 0;
        }
    }
    return memory[address];
}

/* The main loop */
/* The main loop will do the procedures: */
/* 
1. Load one instruction from memory at the address of the PC register
2. Increment the PC register
3. Look at the opcode to determine which type of instruction it should perform
4. Perform the instruction using the parameters in the instruction
5. Go back to step 1
 */
int main(int argc, const char* argv[]) {


    //------------------------------------------
    /* Load Arguments */
    if (argc < 2) {
        /* show usage string */
        printf("lc3 [image-file1] ...\n");
        exit(2); 
    }

    for (int j = 1; j < argc; j ++) { // argv[0] is the program name to be executed 
        if (!read_image(argv[j])) { // to be implemented later 
            printf("failed to load image: %s\n", argv[j]);
            exit(1);
        }
    }


    //------------------------------------------
    /* Setup */

    /*  */
    signal(SIGINT, handle_interrupt);
    disable_input_buffering();

    /* Since exactly one condition flag should be set at any given time, set the Z flag */
    reg[R_COND] = FL_ZRO;

    /* Set the PC to starting position */
    /* 0x3000 is the default, which is 12288 */
    enum { PC_START = 0X3000 };
    reg[R_PC] = PC_START;

    int running = 1;
    while (running) {

        /* FETCH */
        uint16_t instr = mem_read(reg[R_PC]++); // will implement later
        uint16_t op = instr >> 12;

        switch(op) {
            case OP_ADD:
                {
                    // ADD logic

                    /* destination register (DR) */
                    uint16_t r0 = (instr >> 9) & 0x7; // get the bit from instr 9-11
                    /* first operand to add (SR1) */
                    uint16_t r1 = (instr >> 6) & 0x7; // get the bit from instr 6-8
                    /* whether we are in immediate mode */
                    uint16_t imm_flag = (instr >> 5) & 0x1; // get the bit 5

                    if (imm_flag) {
                        uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                        reg[r0] = reg[r1] + imm5;
                    } else {
                        uint16_t r2 = instr & 0x7; // register mode, second operand in instr bit 0-2
                        reg[r0] = reg[r1] + reg[r2];
                    }

                    update_flags(r0); // Any time an instruction modifies a register, the condition flags need to be updated 

                }
            
                break;
            case OP_AND:
                // AND logic
                {
                    uint16_t r0 = (instr >> 9) & 0x7;
                    uint16_t r1 = (instr >> 6) & 0x7;
                    uint16_t imm_flag = (instr >> 5) & 0x1;

                    if (imm_flag) {
                        uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                        reg[r0] = reg[r1] & imm5;
                    } else {
                        uint16_t r2 = instr & 0x7;
                        reg[r0] = reg[r1] & reg[r2];
                    }
                    update_flags(r0);
                }
                break;
            case OP_NOT:
                // NOT logic 
                {
                    uint16_t r0 = (instr >> 9) & 0x7;
                    uint16_t r1 = (instr >> 6) & 0x7;

                    reg[r0] = ~reg[r1];
                    update_flags(r0);
                }
                break;
            case OP_BR:
                // BR logic
                {
                    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                    uint16_t cond_flag = (instr >> 9) & 0x7;
                    if (cond_flag & reg[R_COND]) {
                        reg[R_PC] += pc_offset;
                    }
                }
                
                break;
            case OP_JMP:
                // JMP logic
                {
                    uint16_t r1 = (instr >> 6) & 0x7;
                    reg[R_PC] = reg[r1];
                }
                break;
            case OP_JSR:
                // JSR logic
                {
                    reg[R_R7] = reg[R_PC];
                    uint16_t long_flag = (instr >> 11) & 0x1;

                    if (long_flag) {
                        uint16_t pc_offset = sign_extend(instr & 0x7FF, 11);
                        reg[R_PC] += pc_offset; /* JSR */
                    } else {
                        uint16_t r1 = (instr >> 6) & 0x7;
                        reg[R_PC] = reg[r1];   /* JSRR */
                    }
                }
                break;
            case OP_LD:
                // LD logic
                {
                    uint16_t r0 = (instr >> 9) & 0x7;
                    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                    reg[r0] = mem_read(reg[R_PC] + pc_offset);
                    update_flags(r0);
                }
                break;
            case OP_LDI:
                // LDI logic
                {
                    /* Destination register */
                    uint16_t r0 = (instr >> 9) & 0x7;
                    /* PCoffset 9 */
                    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                    /* add pc_offset to the current PC, look at that memory address to get the final address */
                    reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offset));
                    update_flags(r0);
                }
                break;
            case OP_LDR:
                // LDR logic
                {
                    uint16_t r0 = (instr >> 9) & 0x7;
                    uint16_t r1 = (instr >> 6) & 0x7;
                    uint16_t pc_offset = sign_extend(instr & 0x3F, 6);
                    reg[r0] = mem_read(reg[r1] + pc_offset);
                    update_flags(r0);
                }
                break;
            case OP_LEA:
                // LEA logic
                {
                    uint16_t r0 = (instr >> 9) & 0x7;
                    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                    reg[r0] = reg[R_PC] + pc_offset;
                    update_flags(r0);
                }
                break;
            case OP_ST:
                // store logic
                {
                    uint16_t r0 = (instr >> 9) & 0x7;
                    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                    mem_write(reg[R_PC] + pc_offset, reg[r0]); // mem_write to be implemented
                }
                break;
            case OP_STI:
                // STI logic
                {
                    uint16_t r0 = (instr >> 9) & 0x7;
                    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                    mem_write(mem_read(reg[R_PC] + pc_offset), reg[r0]);
                }
                
                break;
            case OP_STR:
                // STR logic
                {
                    uint16_t r0 = (instr >> 9) & 0x7;
                    uint16_t r1 = (instr >> 6) & 0x7;
                    uint16_t pc_offset = sign_extend(instr & 0x3F, 6);
                    mem_write(reg[r1] + pc_offset, reg[r0]);
                }
                break;
            case OP_TRAP:
                // TRAP logic
                reg[R_R7] = reg[R_PC];
                switch (instr & 0xFF) {
                
                    case TRAP_GETC:
                        /* get char from keyboard, not echoed onto the terminal */
                        reg[R_R0] = (uint16_t)getchar();
                        update_flags(R_R0);
                        break;
                    case TRAP_OUT:
                        /* output a character */
                        putc((char)reg[R_R0], stdout);
                        fflush(stdout);
                        break;
                    case TRAP_PUTS:
                        /* output one char per word */
                        {
                            uint16_t* c = memory + reg[R_R0];
                            while (*c) {
                                putc((char)*c, stdout);
                                ++c;
                            }
                            fflush(stdout);
                        }
                        break;
                    case TRAP_IN:
                        /* get character from keyboard, echoed onto the terminal */
                        {
                            printf("Enter a character: ");
                            char c = getchar();
                            putc(c, stdout);
                            fflush(stdout);
                            reg[R_R0] = (uint16_t)c;
                            update_flags(R_R0);
                        }
                        break;
                    case TRAP_PUTSP:
                        /* output a byte string */
                        {
                            uint16_t* c = memory + reg[R_R0];
                            while (*c)
                            {
                                // upper 8 bits
                                char char1 = (*c) & 0xFF; 
                                putc(char1, stdout);
                                // lower 8 bits
                                char char2 = (*c) >> 8;
                                if (char2) putc(char2, stdout);
                                ++c;
                            }
                            fflush(stdout);
                        }
                        break;
                    case TRAP_HALT:
                        /* halt the program */
                        puts("HALT");
                        fflush(stdout);
                        running = 0;
                        break;
                }
                break;
            case OP_RES:
            case OP_RTI: 
            default:
                // Bad OPcode
                abort();
                break;

        }
    }
    restore_input_buffering();
    
    //------------------------------------------
    /* Shutdown logic */
}