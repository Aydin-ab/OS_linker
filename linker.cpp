/**
 * CS 2250_1 Section 001 : Operating Systems
 * Lab 1 : Linker
 * @file linker.cpp
 * @author Aydin Abiar
 * aa9380
 * N16335205
**/


#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <string>
#include <map>
#include <vector>
#include <cmath>
#include <tuple>
#include <stdexcept>
#include <algorithm>


using namespace std;


// _______________________ TOKENIZER _________________________-

/**
 * @brief Get the Tokens object from the input file
 * Format of one line : "Token: LINE:OFFSET : SYMBOL" 
 * with LINE and OFFSET corresponding to the position of the first character of SYMBOL
 * 
 * @param in : the input file to tokenize
 * @param out : the stream to output the tokens
 * @return int : status (0 if success, -1 if encounter a non-alphanumerical character)
 */

int getTokens(istream& in, ostream& out) {

    int line_c = 1; // current read line 
    int pos_c = 1; // position of the current read character
    int last_pos_c = 1; // previous position of the read character. Will be user to get the end of file
    
    char c; // buffer char to read each file character
    string symbol = ""; // buffer to store the current tokenized symbol
    
    while ( in.get( c ) ) {

        // new line
        if ( c == '\n' ) { 
            // We've reached eol without symbol to tokenize
            if ( symbol.length() == 0 ) { line_c++; last_pos_c = pos_c; pos_c = 1; } 
             // We've reached eol with last symbol of line to tokenize
            else { 
                out << "Token: "<< line_c << ":" << pos_c - symbol.length() << " : " << symbol << "\n";
                line_c++; 
                last_pos_c = pos_c; 
                pos_c = 1; 
                symbol = ""; 
            }
            continue;
        }
        // spaces or tab
        if ( c == ' ' || c == '\t') { 
            // ignore spaces and tabs until new symbol
            if ( symbol.length() == 0 ) { pos_c++; } 
            // This means we were reading a symbol and we've reached the end of a symbol 
            else { 
                out << "Token: "<< line_c << ":" << pos_c - symbol.length() << " : " << symbol << "\n";
                pos_c++; 
                symbol = ""; 
            } 
            continue;
        }

        // We dealt with the case of spaces, new lines. Now we assert that c is alpha numerical
        if ( isalnum(c) == 0 ) { 
            out << "A non alpha numeric character has been found at pos "<< line_c << ":" << pos_c;
            return -1; 
        }

        // Now we know that c is a valid alpha numeric character
        // We append c to the symbol buffer and continue until the end of symbol (one of the if conditions above)
        symbol.push_back(c);
        pos_c++;
        
    }

    // We've reached end of file. We check if we don't have a last symbol to tokenize. 
    // Should be handled by the last /n but we never know
    if ( symbol.length() > 0 ) {
        out << "Token: "<< line_c << ":" << pos_c - symbol.length() << " : " << symbol << "\n";
    }

    out<< "Final Spot in File : line=" << line_c - 1 << " offset=" << last_pos_c << "\n";

    return 0;

}



// _______________________________________ LINKER _______________________________

int MACHINE_SIZE = 512;

int parseErrorLine; // line printed when a parse error happens
int parseErrorOffset; // offset printed when a parse error happens

int last_line; // Last line of file (stored at the end of tokenizer)
int last_offset; // Last offset of last line of file (stored in at the end of tokenizer)

int last_moduleID = -1; // last moduleID of the program (used for an edge case of rule 7)


/**
 * @brief Exception structure for the Parse Errors
 * 
 */
struct ParseErrorException : public exception {
    const char* what () const throw () {
        return "hello you";
    }

    int errcode; // code error (see map below)
    string errstr; // Parse Error Message

    map<string,int> errstrMap = {
        {"NUM_EXPECTED", 1}, // Number Expected 
        {"SYM_EXPECTED", 2}, // Symbol Expected
        {"ADDR_EXPECTED", 3}, // Addressing Expected which is A/E/I/R
        {"SYM_TOO_LONG", 4}, // Symbol Name is too long
        {"TOO_MANY_DEF_IN_MODULE", 5}, // > 16
        {"TOO_MANY_USE_IN_MODULE", 6}, // > 16
        {"TOO_MANY_INSTR", 7} // total num_instr exceeds memory size (512)
    };

    ParseErrorException(string _errstr) {
        errstr = _errstr;
        errcode = errstrMap[_errstr];
    }

    void parseError() { 
        cout << "Parse Error line "<< parseErrorLine << " offset " << parseErrorOffset << ": " << errstr << endl; 
    };

};

/**
 * @brief Class for Symbol token. This could be a symbol OR an address mode R E I A. 
 * Pass 2 will specifically take care of differenciating symbols and address modes
 * The confusion between symbols and address mode only occurs in the first pass 
 * because I wanted to handle all parse errors in the first pass, and from the first pass point of view, 
 * we don't care if it's a symbol or an address mode
 * 
 * Difference between a symbol and an address mode is different initialization
 * (a symbol will have a value but not an instruction for example)
 * 
 */
class Symbol {
    public:
        string variable; //  !! Could also mean the address mode !!
        int value;  // for symbols only
        int moduleID; // module where symbol was defined (for warning rule 4)
        int moduleOffset; // offset of the module where it was defined (for E instruction)
        map<int, int> relativeIndexMap; // relative index in module (for E instruction). Module defined by its offset

        int index; // Order by which we will print the symbol in the symbol table (defined when the symbol's value is defined)

        bool errorMultipleTimesDefined; // true if it has been defined more than once (for warning rule 2)
        bool warningDefinedButNotBeenUsed; // true if it has not been used yet (for warning rule 4)
        tuple<int,bool,int> warningTooBigValue; // (high value, true, max value) if it was defined with a too high value (for warning rule 5)
        map<int,bool> warningAppearsUseListButNotUsed; // (module,true) if it appears in the use list of the module but was not actually used (for warning rule 7)
        bool isDuplicate; // true if the value is a duplicate of an already defined value. Won't be present in Symbol Table. This flag is used because some warnings are printed multiple times

        Symbol(string _variable) {
            variable= _variable;
            value = -1;
            moduleID = -1;
            moduleOffset = -1;
            
            index = -1;

            errorMultipleTimesDefined = false;
            warningDefinedButNotBeenUsed = false;
            warningTooBigValue = make_tuple(-1, false, -1);
            isDuplicate = false;
        }

        // To get the order of appearnce in the symbol table
        bool operator< (const Symbol &other) const {
            return index < other.index;
        }
};


/**
 * @brief Class for Instructions. Built in the second pass.
 * For example "E 1000" are first read as a Symbol and an int in the first pass
 * The second pass will then build an Instruction object from the 2 tokens
 * 
 */
class Instruction {
    public:
        string mode; // R E I A
        int instr; // The instruction of the input file
        int moduleID; // the module ID of the instruction
        int moduleOffset; // The offset of the module of the instruction
        int opcode; 
        int operand;
        Symbol symbol = Symbol(string("")); // For instructions E. Will store the corresponding symbol
        int moduleUselistSize; // For instructions E. Will check if the operand doesn't excess the use list size
        int moduleSize; // For instructions R. Will check if the symbol address dosen't excess the module size

        int index; // use for printing
        int memory; // use for printing. The final memory value

        bool errorSymbolNotDefined; // for error rule 3
        bool errorExceedsLengthUselist; // for error rule 6
        bool errorExceedsMachineSize; // for error rule 8
        bool errorExceedsModuleSize; // for error rule 9
        bool errorIllegalImmediateValue; // for error rule 10
        bool errorIllegalOpCode; // for error rule 11

        Instruction(string _mode) {
            mode = _mode;
            instr = -1;
            moduleID = -1;
            moduleOffset = -1;
            opcode = -1;
            operand = -1;
            moduleUselistSize = -1;
            moduleSize = -1;

            index = -1;
            memory = -1;
            errorSymbolNotDefined = false;
            errorExceedsLengthUselist = false;
            errorExceedsMachineSize = false;
            errorExceedsModuleSize = false;
            errorIllegalImmediateValue = false;
            errorIllegalOpCode = false;
        }
};

// ________ symbols vector build by the first pass ____________
vector<Symbol> symbols;
// ____________________________________________________________


/**
 * @brief Given a line from the tokenizer, extract the token and its position (line, offset) in the input file. 
 * The line shouldn't normally be the last line of the tokenizer. 
 * If it happens it means that a parse error is happening (use list size is too short for example)
 * 
 * @param line the tokenizer line to process in the format "Token: LINE:OFFSET : SYMBOL"
 * @return tuple<int,int, string> in the format (LINE, OFFSET, SYMBOL)
 * @throw invalid_argument exception if the input is an eof
 */
tuple<int,int, string> processTokenLine (string& line) {

    // First we check if it's the end of file.. 
    // If it is, this is a parse error because this function shouldn't be called for the last line
    // Line is in this format : "Final Spot in File : line=LL offset=OO"
    if ( line.find("Final Spot in File : ") != string::npos ) {
        size_t last_line_pos_start = line.find("line=") + 5; // start position of where LL is
        size_t last_line_pos_end = line.find(" offset="); // end position of where LL is 
        last_line = stoi( line.substr(last_line_pos_start, last_line_pos_end - last_line_pos_start) ); // extract "LL"
        parseErrorLine = last_line; // update global variable

        size_t last_offset_pos_start = line.find("offset=") + 7; // start position of where OO is
        last_offset = stoi( line.substr(last_offset_pos_start) ); // extract "OO"
        parseErrorOffset = last_offset; // update global variable
        throw invalid_argument("EOF"); // Parent process will know what is the parse error exactly with the context
    }


    // The line is like that : "Token: LL:CC : SYM"
    // We want to retrieve LL, CC, and SYM. We will split by ":" and return the 3 outputs
    stringstream lineStream(line);
    string segment;
    vector<string> seglist;

    while ( getline(lineStream, segment, ':') ) {
        seglist.push_back(segment);
    }

    // Here seglist = ("Token", " LL", "CC ", " SYM"). We just need to delete the first part and the extra spaces
    seglist.at(1).erase(seglist.at(1).begin()); // return LL as string
    seglist.at(2).erase(seglist.at(2).end() - 1); // return CC as string
    seglist.at(3).erase(seglist.at(3).begin()); // return SYM as string

    return make_tuple(stoi(seglist.at(1)), stoi(seglist.at(2)), seglist.at(3));

} 


/**
 * @brief Given a line from the tokenizer containing an interger token, extract the token and output is as an int
 * It also checks that the token is indeed numerical
 * Input are "1", "0", "1000" for example
 * 
 * @param line line from the tokenizer
 * @return int the parsed integer token
 * @throw ParseErrorException if the token isn't a number : NUM_EXPECTED
 */
int readInt(string& line) {

    string sym = "-1"; 

    // Check the special case of the last line of tokenizer. 
    // If it happens it means the input file lacks enough tokens, it's a parse eror
    try {
        tuple<int, int, string> lineOffsetSym = processTokenLine(line); // return (line, offset, symbol)
        parseErrorLine = get<0>(lineOffsetSym);
        parseErrorOffset = get<1>(lineOffsetSym);
        sym = get<2>(lineOffsetSym);
    } catch (exception e) {ParseErrorException("NUM_EXPECTED");}

        // We need to check if the symbol is indeed an int, else we throw a parse error
        string::const_iterator it = sym.begin();
        while ( it != sym.end() && isdigit(*it) ) { ++it; }
        if ( it != sym.end() ) { throw ParseErrorException("NUM_EXPECTED"); }

    return stoi(sym);

}

/**
 * @brief Given a line from the tokenizer containing a symbol/address mode, extract the token and output is as a Symbol instance
 * It also checks that the token is indeed starting with an alphabetical character
 * Input are "x", "R", "akrjeafl", "E" for example
 * 
 * @param line line from the tokenizer
 * @return Symbol(sym) the parsed Symbol token
 * @throw ParseErrorException if the token isn't a number : SYM_EXPECTED
 */
Symbol readSym(string& line) {

    string sym;

    // Check the special case of the last line of tokenizer. 
    // If it happens it means the input file lacks enough tokens, it's a parse eror
    try {
        tuple<int, int, string> lineOffsetSym = processTokenLine(line); // return (line, offset, symbol)
        parseErrorLine = get<0>(lineOffsetSym);
        parseErrorOffset = get<1>(lineOffsetSym);
        sym = get<2>(lineOffsetSym);
    } catch (exception e) { throw ParseErrorException("SYM_EXPECTED"); }

    // We need to check if the symbol is valid (must not start with a digit)
    if ( !isalpha(sym[0]) ) { throw ParseErrorException("SYM_EXPECTED"); }

    return Symbol(sym);

}


/**
 * @brief Print the symbol table.
 * Fo through each Symbol stored in the global vector and output its value
 * Also check for errors and output them
 * 
 */
void print_symbol_table() {

    sort(symbols.begin(), symbols.end()); // We defined it by sorting Symbol by the index attribute. = Order of when they were defined

    // Rule 5 warnings are printed before the symbol table
    for ( vector<Symbol>::iterator sym = symbols.begin(); sym != symbols.end(); ++sym ) {
        if (get<1>(sym->warningTooBigValue)) {
            cout << "Warning: Module " << sym->moduleID << ": " << sym->variable << " too big "
                << get<0>(sym->warningTooBigValue) << " (max=" <<get<2>(sym->warningTooBigValue)
                << ") assume zero relative" << endl;
        }
    }

    cout << "Symbol Table" << endl;

    for ( vector<Symbol>::iterator sym = symbols.begin(); sym != symbols.end(); ++sym ) {
        Symbol symbol = *sym;
        if ( symbol.isDuplicate ) {continue;} // A duplicate is when a symbol use a name that was already taken. We don't print it
        if ( symbol.value == -1 ) {continue;} // It means the symbol was not defined, we don't print it
        cout << symbol.variable << "=" << symbol.value;
        if ( symbol.errorMultipleTimesDefined ) {
            cout <<  " Error: This variable is multiple times defined; first value used";
        }
        cout << endl;
    }

}

/**
 * @brief Given a line from the tokenizer containing an address mode, extract the token and output is as an Instruction instance
 * It also checks that the token is indeed starting with an alphabetical character
 * Input are R, E, I, A
 * 
 * @param line line from the tokenizer
 * @return Instruction(mode) the parsed Symbol token
 */
Instruction readInstr(string& line) {

    // The line is like that : "Token: LL:CC : MODE"
    // We need to get the last word, so we split by " : " and delete the first part. What remains is MODE
    string delimiter = " : ";
    size_t pos = line.find(delimiter);
    string mode = line.substr(pos + delimiter.length()); // Return "MODE"

    return Instruction(mode);

}


/**
 * @brief Compute the final memory value of an Instruction object and return a new improved Instruction object
 * Links the instruction to its symbol if it's a mode E etc...
 * 
 * @param instruction virgin instruction
 * @return Instruction complete instruction with its memory value
 */
Instruction createInstr(Instruction& instruction) {
    // Handles warning rule 10 (illegale Immediate value)

    Instruction newInstr = instruction;

    // mode[0] effectively convert the string "R" "E" "I" "A" into the corresponding char (can't use switch with a string)
    switch ( instruction.mode[0] ) {
        case 'R' : {
            // First we check the rule 9 error
            if ( newInstr.operand >= newInstr.moduleSize ) {
                newInstr.errorExceedsModuleSize = true; 
                newInstr.memory = newInstr.instr - newInstr.operand + newInstr.moduleOffset;
            }
            else {newInstr.memory = newInstr.moduleOffset + newInstr.instr;}
            break;
        }
        case 'E' : {
            int useSymIndex = newInstr.instr%1000; // index of used variable we'd like to retrieve
            // First, we check the rule 6 error
            if ( useSymIndex >= newInstr.moduleUselistSize ) {
                newInstr.errorExceedsLengthUselist = true;
                newInstr.memory = newInstr.instr;
                break;
            }
            int useSymValue; // value of the used variable 
            for(std::vector<Symbol>::iterator sym = symbols.begin(); sym != symbols.end(); ++sym) {
                // Is the symbol used in the module ? relativeIndexMap contains the relative position of a used symbol in a module
                if ( sym->relativeIndexMap.find(newInstr.moduleID) != sym->relativeIndexMap.end() 
                    && sym->relativeIndexMap[newInstr.moduleID] == useSymIndex ) {
                    sym->warningAppearsUseListButNotUsed[newInstr.moduleID] = false;
                    sym->warningDefinedButNotBeenUsed = false;
                    newInstr.symbol = *sym;
                    // Check if the value is defined, else it's a rule 3 error
                    if ( newInstr.symbol.value == -1 ) { 
                        useSymValue = 0; 
                        newInstr.errorSymbolNotDefined = true; 
                    }
                    else {useSymValue = newInstr.symbol.value;}
                    break;
                }
            }
            newInstr.memory = instruction.instr - useSymIndex + useSymValue;
            break;
        }
        case 'I' : {
            //  Check rule 10 warning
            if ( instruction.instr >= 10000 ) { 
                newInstr.memory = 9999; 
                newInstr.errorIllegalImmediateValue = true; 
            }
            else {
                newInstr.memory = instruction.instr;
            }
            break;
        }
        case 'A' : {
            // Check error rule 8
            if ( newInstr.operand >= MACHINE_SIZE ) {
                newInstr.errorExceedsMachineSize = true; 
                newInstr.memory = newInstr.instr - newInstr.operand;
            }
            else {newInstr.memory = instruction.instr;};
            break;
        }

    }

    return newInstr;

}


/**
 * @brief Given a vector of Instruction object, print the memory map and the associated warnings/errors.
 * The second pass builds a vector of Instruction. This vector is given to the function as an input
 * All warnings and errors are stored in the flags of the Instruction class
 * 
 * @param instructions 
 */
void print_memory_map(vector<Instruction> instructions) {
    
    cout << "\n" << "Memory Map" << endl;

    int moduleIDTracker = 1; // To know in which module we are. Used for rule 7 warning
    for(vector<Instruction>::iterator instr = instructions.begin(); instr != instructions.end(); ++instr) {

        Instruction instruction = *instr;

        // Check if we changed module
        if ( instruction.moduleID > moduleIDTracker ) {
            // We entered a new module, so we have to check the warning rule 7 of previous modules
            // EDGE CASE : Maybe we jump from module M to M+K because some modules have no insruction.
            //              Therefore, we have to check rule 7 for these inbetween modules also
            for ( int modulePrev = moduleIDTracker; modulePrev < instruction.moduleID; ++modulePrev ) {
                for ( vector<Symbol>::iterator sym = symbols.begin(); sym != symbols.end(); ++sym ) {
                    if ( sym->warningAppearsUseListButNotUsed[modulePrev] ) {
                        // Care, sym->moduleID = module where it was defined =/= current module of instruction
                        cout << "Warning: Module " << modulePrev << ": " 
                            << sym->variable << " appeared in the uselist but was not actually used" << endl;
                    }
                }
            }
            moduleIDTracker = instruction.moduleID;
        }

        cout << setfill('0') << setw(3) << instruction.index << ": " << setfill('0') << setw(4) << instruction.memory;
        
        if ( instruction.errorSymbolNotDefined ) {
            cout << " Error: " << instruction.symbol.variable << " is not defined; zero used";
        }
        if ( instruction.errorExceedsLengthUselist ) {
            cout << " Error: External address exceeds length of uselist; treated as immediate";
        }
        if ( instruction.errorExceedsMachineSize ) {
            cout << " Error: Absolute address exceeds machine size; zero used";
        }
        if ( instruction.errorExceedsModuleSize ) {
            cout << " Error: Relative address exceeds module size; zero used";
        }
        if ( instruction.errorIllegalImmediateValue ) {
            cout << " Error: Illegal immediate value; treated as 9999";
        }
        if (instruction.errorIllegalOpCode ) {
            cout << " Error: Illegal opcode; treated as 9999";
        }

        cout << endl;

    }
    // We check the warning rule 7 of last modules
    for ( int modulePrev = moduleIDTracker; modulePrev <= last_moduleID; ++modulePrev ) {
        for ( vector<Symbol>::iterator sym = symbols.begin(); sym != symbols.end(); ++sym ) {
            if ( sym->warningAppearsUseListButNotUsed[modulePrev] ) 
            {
                cout << "Warning: Module " << modulePrev << ": " 
                        << sym->variable << " appeared in the uselist but was not actually used" << endl;
            }
        }
    }

}


/**
 * @brief First pass of the linker. Builds the global vector of Symbols. Checks for all the Parse Errors.
 * 
 * @param the_file 
 * @return int 
 */
int first_pass(istream& the_file){
    // Handles : Warning        rule 4, 5
    //           Error          rule 2
    //           Parse Error    NUM_EXPECTED, SYM_EXPECTED, ADDR_EXPECTED, TOO_MANY_DEF_IN_MODULE, TOO_MANY_USE_IN_MODULE, TOO_MANY_INSTR, SYM_TOO_LONG


    string line; // Current processed line
    int moduleID = 0; // To know in which module we are
    int moduleOffset = 0; // module offset of current module
    int defCount; // buffer for defcount of current processed module
    int useCount; // buffer for useCount of current processed module
    int instCount; // buffer for instCount of current processed module

    int symbIncr = 0; // Keep track of order of print of the symbols. We print them in the order of their definition


    // 1 iteration = 1 module unless we encounter a parse error
    while ( getline(the_file, line) ) {

        // We handle the last tokenizer line case : We extract the last line and last offset of the file :
        // Last tokenizer line is like that : "Final Sport in File : line=LINE offset= OFFSET"
        if ( line.find("Final Spot in File : ") != string::npos ) {
            size_t last_line_pos_start = line.find("line=") + 5; // start position of where the last line is
            size_t last_line_pos_end = line.find(" offset="); // end position of where the last line is 
            last_line = stoi(line.substr(last_line_pos_start, last_line_pos_end - last_line_pos_start)); // update the global variable

            size_t last_offset_pos_start = line.find("offset=") + 7; // start position of where the last line is
            last_offset = stoi(line.substr(last_offset_pos_start)); // update the global variable

            break; // It's last line anyway so we can get out of the while loop
        }

        moduleID++;
        if ( moduleID > last_moduleID ) { last_moduleID = moduleID; } // update global variable

        // Definition list of module :
        defCount = readInt(line); // example "Token: 1:1 : 3"
        if ( defCount > 16 ) { throw ParseErrorException("TOO_MANY_DEF_IN_MODULE"); } // parse error
        for (int i = 0; i < defCount; i++) {
            getline(the_file, line); // example "Token: 1:3 : xy"
            Symbol symbol = readSym(line);
            if ( symbol.variable.length() > 16 ) { throw ParseErrorException("SYM_TOO_LONG"); } // parse error
            getline(the_file, line); // example "Token: 1:6 : 12"
            int value = readInt(line) + moduleOffset;

            // Search if symbol is already defined in the global vector
            string variable_name = symbol.variable;
            auto it = find_if(symbols.begin(), symbols.end(), [&variable_name](const Symbol& obj) {return obj.variable == variable_name;});
            if ( it == symbols.end() ) { 
                // If not defined yet, we set it
                symbol.value = value;
                symbol.moduleID = moduleID;
                symbol.moduleOffset = moduleOffset;
                symbol.index = symbIncr;
                symbol.warningDefinedButNotBeenUsed = true;
                symbols.push_back(symbol);
            }
            else { 
                // Else, we check if it was already defined before (error rule 2)
                if ( it->value == -1 ) {
                    // It wasn't defined before (was present in a previous use list for example). So we define it
                    it->value = value; 
                    it->moduleID = moduleID; 
                    it->moduleOffset = moduleOffset; 
                    it->index = symbIncr;
                    it->warningDefinedButNotBeenUsed = true;
                }
                else {
                    // It was already defined before so we raise the error flag and create a duplicate
                    // A duplicate is created because some errors/warnings are printed for each duplicate (rule 5 for example)
                    it->errorMultipleTimesDefined = true ;
                    Symbol duplicate = Symbol(it->variable);
                    duplicate.value = it->value;
                    duplicate.moduleID = moduleID;
                    duplicate.isDuplicate = true; // prevent the duplicate to be printed in the symbol table
                    duplicate.index = symbIncr;
                    symbols.push_back(duplicate);
                }
            } 
            symbIncr++;
        }

        
        
        // Use list of module :
        getline(the_file, line); // example "Token: 1:1 : 5"
        useCount = readInt(line); 
        if ( useCount > 16 ) { throw ParseErrorException("TOO_MANY_USE_IN_MODULE"); }
        for (int i = 0; i < useCount; i++) {
            getline(the_file, line); // example "Token: 1:3 : abc"
            Symbol symbol = readSym(line); 

            // Search symbol in our defined symbols vector
            string variable_name = symbol.variable;
            auto it = find_if(symbols.begin(), symbols.end(), [&variable_name](const Symbol& obj) {return obj.variable == variable_name;});
            if ( it == symbols.end() ) { 
                // If not found, we add it to the vector but without a value yet
                symbol.relativeIndexMap[moduleID] = i; // used to retrieve the symbol when an E instruction uses this variable
                symbol.warningAppearsUseListButNotUsed[moduleID] = true; // will be updated if the variable is used
                symbols.push_back(symbol);
            }
            else { 
                // If found, we check if it is the first time we've seen the variable in THIS CURRENT use list 
                // (or else we'll miss instructions E that refers to the symbol but at the different offsets of its duplicate)
                // We use warningAppearsUseListButNotUsed to check if we have already seen this variable in this use list
                if ( it->warningAppearsUseListButNotUsed[moduleID] ) {
                    // It's not the first time this variable appears.
                    // A duplicate is created. They will handle E instruction that refers to them instead of the original first symbol
                    Symbol duplicate = Symbol(it->variable);
                    duplicate.isDuplicate = true; // ensures that it will not be printed again in the Symbol Table
                    duplicate.relativeIndexMap[moduleID] = i; // offset of the duplicate in the use list
                    duplicate.value = it->value;
                    symbols.push_back(duplicate);

                } else {
                    // It's the first time this variable appears in the use list
                    it->warningAppearsUseListButNotUsed[moduleID] = true; // we set the flag until an E instruction refers to it
                    it->relativeIndexMap[moduleID] = i;
                }
            }
        }


        // Instruction list of module :
        getline(the_file, line); // example "Token: 1:1 : 8"
        instCount = readInt(line);
        if ( instCount + moduleOffset > MACHINE_SIZE ) { throw ParseErrorException("TOO_MANY_INSTR"); }
        for (int i = 0; i < instCount; i++) {
            getline(the_file, line); // example "Token: 1:1 : E"
            try {
                Symbol sym = readSym(line); // Won't use it but it will check if it's indeed an address mode
            } catch (ParseErrorException e) { throw ParseErrorException("ADDR_EXPECTED"); }

            getline(the_file, line); // example "Token: 1:1 : 1003"
            int instr = readInt(line); // Won't use it but it will check if it's indeed an int
        }
        

        // Now that we know the size of module (instCount)
        // We can check if the defined variable of the module are too big or not (rule 5). Maximum is instCount - 1
        for ( vector<Symbol>::iterator sym = symbols.begin(); sym != symbols.end(); ++sym ) {
            // We check if the symbol was defined in this module
            if ( sym->moduleID == moduleID ) {
                int relativeAddress = sym->value - moduleOffset;
                if ( relativeAddress >= instCount ) {
                    // For duplicates, the initial symbol was already processed, so we set it directly
                    if ( sym->isDuplicate ) { relativeAddress = 0; } 
                    sym->warningTooBigValue = make_tuple(relativeAddress, true, instCount-1);
                    sym->value = moduleOffset;
                }
            } 
        }

        moduleOffset += instCount; // Update next module offset

    }

    // Print symbol table
    print_symbol_table();

    return 0;

}

/**
 * @brief Second pass of the linker. Prints the Memory Map. Handles the errors/warnings of the memory map
 *  We assume that all parse errors were handled by the first pass so we assume that all symbols/instructions are valid
 * @param the_file the tokenizer stream output
 * @return int status of the second pass
 */
int second_pass(istream& the_file){

    // Handles : Warning  7
    //           Error    3, 6, 8, 9, 10, 11

    string line; // Current processed line
    int moduleID = 0; // Current processed module
    int moduleOffset = 0; // Current processed module offset
    int defCount; // buffer for defcount of current processed module
    int useCount; // buffer for useCount of current processed module
    int instCount; // buffer for instCount of current processed module

    int instIncrement = 0; // Increment to index the reallocated instructions (for the "OOO:", "OO1:" etc)

    vector<Instruction> newInstructions; // Vector containing the instructions to print

    // We process each module
    while ( getline(the_file, line) ) {

        // We handle the last line case
        if ( line.find("Final Spot in File : ") != string::npos ) {
            break; // Nothing to do on last line, just ignore it
        }

        moduleID++;

        // Definition list of module :
        defCount = readInt(line);
        for (int i = 0; i < defCount; i++) {
            getline(the_file, line); // symbol
            getline(the_file, line); // value
        }
        
        // Use list of module :
        getline(the_file, line);
        useCount = readInt(line);
        for (int i = 0; i < useCount; i++) {
            getline(the_file, line); // symbol
        }

        // Program Text of module :
        getline(the_file, line);
        instCount = readInt(line);
        for (int i = 0; i < instCount; i++) {
            getline(the_file, line); // adressing mode symbol R E I A
            Instruction instruction = readInstr(line);
            getline(the_file, line); // instruction integer
            instruction.instr = readInt(line);

            instruction.opcode = (int) instruction.instr/1000;
            instruction.operand = instruction.instr % 1000;
            instruction.moduleUselistSize = useCount;
            instruction.moduleSize = instCount;
            instruction.moduleID = moduleID;
            instruction.moduleOffset = moduleOffset;
            instruction.index = instIncrement; // order in which the instructions are pritned
            
            // Illegal opcode ? rule 11 warning
            if ( instruction.opcode >= 10 && instruction.mode[0] != 'I' ) { 
                // We're done, just need to give 9999 as memory and go to next instruction
                instruction.memory = 9999; 
                instruction.errorIllegalOpCode = true; 
                newInstructions.push_back(instruction);
                instIncrement++;
                continue;
            }
            Instruction newInstr = createInstr(instruction); // New Reallocated instruction. Handles warning of rule 12
            newInstructions.push_back(newInstr);
            instIncrement++;
        }

        moduleOffset += instCount; // Update next module offset
    }

    // Print memory map and its errors/warnings
    print_memory_map(newInstructions);

    return 0;

}


int main(int argc, char *argv[]) {

    if (argc == 1) {
        cout << "You must provide one input file to link ! \n";
        return -1;
    }
    if (argc >= 3) {
        cout << "You can provide only one file \n";
        return -1;
    }

    // Now argc is 2, we assume argv[1] is a filename to open
    ifstream the_file ( argv[1] );
    stringstream ss; // serve as output of the tokenizer

    // Check if file opening succeeded
    if ( !the_file.is_open() ) {
        cout<< "Could not open the file \n";
        return -1;
    }

    int tokenStatus = getTokens(the_file, ss); // tokenizer
    if ( tokenStatus != 0 ) { cout << "Error with the Tokenizer"; return -1; }

    // Here's the plan : 
    // 1. First pass outputs the Symbol table //  DONE
    //      It must also retrieves the last line and last offset of the file, which are located at the end of the token file // DONE
    // 2. Second pass outputs the Memory Map // DONE

    int statusFirstPass = -1;
    // First pass handles all parse error, so we try it
    try {
        statusFirstPass = first_pass(ss); // 0 is success, else it's the parse error code
    } catch (ParseErrorException e) {e.parseError(); return e.errcode;}
    if ( statusFirstPass != 0 ) { return statusFirstPass; }

    ss.seekg(0, ss.beg); // Reset file pointer to beginning

    int statusSecondPass = second_pass(ss); // 0 is success
    if ( statusSecondPass != 0 ) { return statusSecondPass; }


    // Check for Rule 4 : defined symbols but never used
    cout<<"\n";
    for(std::vector<Symbol>::iterator sym = symbols.begin(); sym != symbols.end(); ++sym) {
        Symbol symbol = *sym;
        if ( symbol.warningDefinedButNotBeenUsed ) {
            std::cout << "Warning: Module " << symbol.moduleID << ": " << symbol.variable << " was defined but never used" << endl;
        }
    }
    cout << "\n";

    return 0;

}