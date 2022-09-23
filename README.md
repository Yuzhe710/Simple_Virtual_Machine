# Simple_Virtual_Machine
 This project builds a virtual machine which simulates LC-3, a frictional computer for educational purpose

## VM
A VM is a program that acts like a computer, it simulates a CPU along with a few other hardware components, allowing it to perform arithmetic, read and write to memory, and interact with I/O devices, just like a physical computer. Most importantly, it can understand a machine language which you can use to program it. <br />

VM is primarily done to make software development easier. A VM could offer a standard platform which provided portability for a program that ran on multiple computer architectures. Instead of rewriting a program in different dialects of assembly for each CPU architecture, you would only need to write the small VM program in each assembly language. Each program would then be written only once (assembled) in the VM’s assembly language. <br />

Consider compiler? Compiler VS Assembler <br />

A compiler solves a similar problem by compiling a standard high-level language to several CPU architectures. A VM creates one standard CPU architecture which is simulated on various hardware devices. One advantage of a compiler is that it has no runtime overhead while a VM does. Even though compilers do a pretty good job, writing a new one that targets multiple platforms is very difficult, so VMs are still helpful here. In practice, VMs and compilers are mixed at various levels. <br />

## Memory
Memory of LC-3 has 65536 locations (The maximum that is addressable by a 16-bit unsigned integer 2^16). Each stores a 16-bit value, it means a total of 128KB. The memory will be stored in a simple array. <br />

## Registers
LC-3 has total of 10 registers each with 16 bits. 8 General Purpose Registers (R0 - R7), 1 Program Counter (PC) register, 1 conditional flags (COND) register. <br />

The general purpose register can be used to perform any program calculations, The program counter is an unsigned integer which is the address of the next instruction in memory to execute. The condition flags store information about the previous calculation. <br />

## Instruction Set
An instruction is a command which tells the CPU to do some fundamental task, such as add two numbers. Instructions have both an opcode which indicates the kind of task to perform and a set of parameters which provide inputs to the task being performed. <br />

Each opcode represents one task that the CPU “knows” how to do. There are just 16 opcodes in LC-3, Each instruction is 16 bits long, with the left 4 bits storing the opcode. The rest of the bits are used to store the parameters. Everything the computer can calculate is some sequence of these simple instructions. <br />

CISC VS RISC <br />
