# OS linker
Homework for OS course at NYU. Basic linker to replicate OS behavior in C++
Grade : 100/100

## HOW TO USE
Compile the code with the ```make``` command
Execute the program with ```linker input_file```. The output goes to the standard output
Given a list of input files, you can use the ```runit.sh``` script to run the program on each of them and put the outputs in a output directory. Use the runit.sh script inside the input directory like that : ```runit.sh <your_output_dir> linker```


## CONTEXT
This is a two-pass linker. In general, a linker takes individually compiled code/object modules and creates a single executable by resolving external symbol references (e.g. variables and functions) and module relative addressing by assigning global addresses after placing the modules’ object code at global addresses.

Rather than dealing with complex x86 tool chains, we assume a target machine with the following properties: (a) word addressable, (b) addressable memory of 512 words, and (c) each valid word is represented by an integer (<10000).
[ I know that is a really strange machine, but I once saw an UFO too].

The input to the linker is a file containing a sequence of tokens (symbols and integers and instruction type characters). Don’t assume tokens that make up a section to be on one line, don’t make assumptions about how much space separates tokens or that lines are non-empty for that matter or that each input conforms syntactically. Symbols always begin with alpha characters followed by optional alphanumerical characters, i.e.[a-Z][a-Z0-9]\*. Valid symbols can be up to 16 characters. Integers are decimal based. Instruction type characters are (I, A, R, E). Token delimiters are ‘ ‘, ‘\t’ or ‘\n’.

The input file to the linker is structured as a series of “object module” definitions.
Each “object module” definition contains three parts (in fixed order): definition list, use list, and program text.
• definition list consists of a count defcount followed by defcount pairs (S, R) where S is the symbol being defined and R is the relative word address (offset) to which the symbol refers in the module (0-based counting).
• use list consists of a count usecount followed by usecount symbols that are referred to in this module. These could include symbols defined in the definition list of any module (prior or subsequent or not at all).
• program text consists of a count codecount followed by codecount pairs (type, instr), where type is a single character indicating the addressing mode as Relative, External. Immediate or Absolute and instr is the instruction (integer) Note that codecount defines the length of the module.


An instruction is composed of an integer that is comprised of an opcode (op/1000) and an operand (op mod 1000). The opcode always remains unchanged by the linker. For the instruction value read an integer and ensure opcode < 10, see errorcodes below. The operand is modified/retained based on the instruction type in the program text as follows:

(R) operand is a relative address in the module which is relocated by replacing the relative address with the absolute address of that relative address after the module’s global address has been determined (absolute_addr = module_base+relative_addr). (E) operand is an external address which is represented as an index into the uselist. For example, a reference in the program text with operand K represents the Kth symbol in the use list, using 0-based counting, e.g., if the use list is ‘‘2 f g’’, then an instruction ‘‘E 7000’’ refers to f, and an instruction ‘‘E 5001’’ refers to g. You must identify to which global address the symbol is assigned and then replace the operand with that global address.

(I) an immediate operand is unchanged.

(A) operand is an absolute address which will never be changed in pass2; however it can’t be “>=” the machine size (512);

The linker must process the input twice (that is why it is called two-pass). Pass One parses the input and verifies the correct syntax and determines the base address for each module and the absolute address for each defined symbol, storing the latter in a symbol table. The first module has base address zero; the base address for module X+1 is equal to the base address of module X plus the length of module X (defined as the number of instructions in a module). The absolute address for symbol S defined in module M is the base address of M plus the relative address of S within M. After pass one print the symbol table (including errors related to it. Do not store parsed tokens, the only data you should and need to store between passes is the symboltable.

Pass Two again parses the input and uses the base addresses and the symbol table entries created in pass one to generate the actual output by relocating relative addresses and resolving external references.
