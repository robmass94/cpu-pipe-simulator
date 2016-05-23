#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
using namespace std;

class Instruction {
public:
	Instruction(string t, string dest_reg, string source_reg1, 
	string source_reg2, char branch_t, int cycles, int i)
	{
		type = t;
		destination_register = dest_reg;
		source_register1 = source_reg1;
		source_register2 = source_reg2;
		branch_taken = branch_t;
		stage = "";
		cycles_needed = cycles;
		cycles_completed = 0;
		id = i;
		stalled = false;
		result_squashed = false;
	}
	string type;
	string destination_register;
	string source_register1;
	string source_register2;
	// current pipeline stage
	string stage;
	// if branch instruction, either T (taken) or F (not taken)
	char branch_taken;
	// needed execution cycles
	int cycles_needed;
	// completed execution cycles
	int cycles_completed;
	// instruction number
	int id;
	// is/is not stalled due to hazard
	bool stalled;
	// result has/has not been squashed to WAW
	bool result_squashed;
};

void getConfig(map<string, int>&, const char*);
void getInstructions(vector<Instruction>&, map<string, int>&);
void executeInstructions(vector<Instruction>&);

int main(int argc, char **argv)
{
	map<string, int> ex_cycles;
	vector<Instruction> instructions;
	getConfig(ex_cycles, "config.txt");
	getInstructions(instructions, ex_cycles);
	executeInstructions(instructions);
	return 0;
}

void getConfig(map<string, int> &config, const char *filename)
{
	// open configuration file
	ifstream in(filename);
	string str;
	// check if file failed to open
	if (!in) {
		fprintf(stderr, "ERROR: could not open config file %s\n", filename);
		exit(EXIT_FAILURE);
	}
	// read needed execution cycles for floating point instructions
	getline(in, str, ' ');
	in >> config["fp_add_sub"];
	getline(in, str, ' ');
	in >> config["fp_mul"];
	getline(in, str, ' ');
	in >> config["fp_div"];
	// print configuration
	printf("Configuration:\n");
	printf("%26s:%3d\n", "fp adds and subs cycles", config["fp_add_sub"]);
	printf("%26s:%3d\n", "fp multiplies cycles", config["fp_mul"]);
	printf("%26s:%3d\n\n\n", "fp divides cycles", config["fp_div"]);
	in.close();
}

void getInstructions(vector<Instruction> &instructions, 
	map<string, int> &config)
{
	istringstream iss;
	string line;
	string instruction;
	string destination_register;
	string label;
	string source_register1;
	string source_register2;
	char branch_taken;
	int displacement;
	int cycles;
	int i = 1;

	// read in and print instructions
	printf("Instructions:\n");
	while (getline(cin, line)) {
		if (i > 100) {
			fprintf(stderr, "ERROR: too many instructions in the trace\n");
			exit(EXIT_FAILURE);
		}
		// reset instruction attributes to be read
		destination_register = "";
		source_register2 = "";
		branch_taken = '\0';
		cycles = 1;
		// initialize input stringstream with line
		iss.str(line);
		// read instruction type
		iss >> instruction;
		// ignore whitespace between type and operands
		iss >> ws;
		if (instruction == "LW" || instruction == "L.S") {
			// if instruction is a load, need destination register and 
			// displacement (in that order)
			getline(iss, destination_register, ',');
			iss >> displacement;
			iss.ignore();
			getline(iss, source_register1, ')');
		} else if (instruction == "SW" || instruction == "S.S") {
			// if instruction is a store, need source register, displacement, and
			// destination register (in that order)
			getline(iss, source_register1, ',');
			iss >> displacement;
			iss.ignore();
			getline(iss, destination_register, ')');
		} else if (instruction == "BEQ" || instruction == "BNE") {
			// if instruction is a branch, need source registers, the label to
			// jump to, and whether or not the branch is taken (in that order)
			getline(iss, source_register1, ',');
			getline(iss, source_register2, ',');
			getline(iss, label, ':');
			iss >> branch_taken;
		} else if (instruction == "MFC1" || instruction == "MOV.S" || 
			instruction == "CVT.S.W" || instruction == "CVT.W.S") {
			// if instruction is data movement (from) or data conversion, need
			// destination register and source register (in that order)
			getline(iss, destination_register, ',');
			iss >> source_register1;
		} else if (instruction == "MTC1") {
			// if instruction is data movement (to), need source register and
			// destination register (in that order)
			getline(iss, source_register1, ',');
			iss >> destination_register;
		} else if (instruction == "DADD" || instruction == "DSUB" || 
			instruction == "AND" || instruction == "OR" || instruction == "XOR" ||
			instruction == "ADD.S" || instruction == "SUB.S" || 
			instruction == "MUL.S" || instruction == "DIV.S") { 
			// r-type instructions, need destination register and source registers
			// (in that order)
			getline(iss, destination_register, ',');
			getline(iss, source_register1, ',');
			iss >> source_register2;
			if (instruction == "ADD.S" || instruction == "SUB.S") {
				cycles = config["fp_add_sub"];
			} else if (instruction == "MUL.S") {
				cycles = config["fp_mul"];
			} else if (instruction == "DIV.S") {
				cycles = config["fp_div"];
			}
		} else {
			fprintf(stderr, "ERROR: invalid instruction\n");
			exit(EXIT_FAILURE);
		}
		// reset stringstream
		iss.str(string());
		// clear any error flags
		iss.clear();
		// construct and push back instruction
		instructions.emplace_back(instruction, destination_register, 
			source_register1, source_register2, branch_taken, cycles, i);
		// print out instruction
		printf("%3d. %s\n", i++, line.c_str());
	}
	printf("\n\n");
}

void executeInstructions(vector<Instruction> &instructions)
{
	// to hold instructions currently in pipeline
	vector<Instruction> pipeline;
	// current CPU cycle
	int current_cycle = 0;
	// used to fetch instruction
	int instruction_ptr = 0;
	// iterators
	int i;
	int j;
	// used to execute correct number of needed stalls
	int current_stall_cycle = 0;
	int needed_stall_cycles = 0;
	// used to determine if next instruction needs to be flushed
	bool branch_taken = false;
	bool last_instruction_fetched = false;

	// counters
	int load_delay_hazard_cycles = 0;
	int structural_hazard_cycles = 0;
	int data_hazard_cycles = 0;
	int total_hazard_cycles;
	int waw_squashes = 0;
	int branch_flushes = 0;
	// hazard statistics
	float load_delay_hazard_percent;
	float structural_hazard_percent;
	float data_hazard_percent;

	printf("%5s %5s %5s %5s %5s %5s %5s %5s %5s %5s\n", "cycle", "IF", "ID", 
		"EX", "MEM", "WB", "FADD", "FMUL", "FDIV", "FWB");
	printf("----- ----- ----- ----- ----- ----- ----- ----- ----- -----\n");

	do {
		++current_cycle;
		// FWB stage
		for (i = 0; i < pipeline.size(); ++i) {
			if (((pipeline[i].stage == "FADD" || pipeline[i].stage == "FMUL" || 
			pipeline[i].stage == "FDIV") && 
			pipeline[i].cycles_completed == pipeline[i].cycles_needed) || 
			(pipeline[i].stage == "MEM" && (pipeline[i].type == "MTC1" || 
			pipeline[i].type == "CVT.S.W" || pipeline[i].type == "CVT.W.S" || 
			pipeline[i].type == "MOV.S" || pipeline[i].type == "L.S"))) {
				// if instruction is ADD.S, SUB.S, MUL.S, or DIV.S and it has 
				// completed its required cycles, or if it is MTC1, CVT.S.W, 
				// CVT.W.S, MOV.S, or L.S and it has completed the MEM stage,
				// write back result
				printf("%59d\r", pipeline[i].id);
				// remove from pipeline
				pipeline.erase(pipeline.begin() + i);
				break;
			}
		}
		// FDIV stage
		for (i = 0; i < pipeline.size(); ++i) {
			if (pipeline[i].stage == "ID" && pipeline[i].type == "DIV.S") {
				if (!pipeline[i].stalled) {
					// transition DIV.S instruction into FDIV stage
					pipeline[i].stage = "FDIV";
					printf("%53d\r", pipeline[i].id);
					pipeline[i].cycles_completed++;
					if (pipeline[i].cycles_completed == pipeline[i].cycles_needed
					&& pipeline[i].result_squashed) {
						// if instruction has completed, but result has been squashed, 
						// remove instuction from pipeline (do not want to write back)
						pipeline.erase(pipeline.begin() + i);
					}
				}
				break;
			} else if (pipeline[i].stage == "FDIV") {
				// DIV.S instruction is already executing, but needs additional 
				// cycle(s) to complete
				if (!pipeline[i].stalled) {
					printf("%53d\r", pipeline[i].id);
					pipeline[i].cycles_completed++;
					if (pipeline[i].cycles_completed == pipeline[i].cycles_needed 
					&& pipeline[i].result_squashed) {
						// if instruction has completed, but result has been squashed, 
						// remove instuction from pipeline (do not want to write back)
						pipeline.erase(pipeline.begin() + i);
					}
				}
				break;
			}
		}
		// FMUL stage
		for (i = 0; i < pipeline.size(); ++i) {
			if (pipeline[i].stage == "ID" && pipeline[i].type == "MUL.S") {
				if (!pipeline[i].stalled) {
					// transition MUL.S instruction into FMUL stage
					pipeline[i].stage = "FMUL";
					printf("%47d\r", pipeline[i].id);
					pipeline[i].cycles_completed++;
					if (pipeline[i].cycles_completed == pipeline[i].cycles_needed 
					&& pipeline[i].result_squashed) {
						// if instruction has completed, but result has been squashed, 
						// remove instuction from pipeline (do not want to write back)
						pipeline.erase(pipeline.begin() + i);
					}
				}
				break;
			} else if (pipeline[i].stage == "FMUL") {
				// MUL.S instruction is already executing, but needs additional 
				// cycle(s) to complete
				if (!pipeline[i].stalled) {
					printf("%47d\r", pipeline[i].id);
					pipeline[i].cycles_completed++;
					if (pipeline[i].cycles_completed == pipeline[i].cycles_needed 
					&& pipeline[i].result_squashed) {
						// if instruction has completed, but result has been squashed, 
						// remove instuction from pipeline (do not want to write back)
						pipeline.erase(pipeline.begin() + i);
					}
				}
				break;
			}
		}
		// FADD stage
		for (i = 0; i < pipeline.size(); ++i) {
			if (pipeline[i].stage == "ID" && (pipeline[i].type == "ADD.S" ||
				pipeline[i].type == "SUB.S")) {
				if (!pipeline[i].stalled) {
					// transition ADD.S or SUB.S instruction into FADD stage
					pipeline[i].stage = "FADD";
					printf("%41d\r", pipeline[i].id);
					pipeline[i].cycles_completed++;
					if (pipeline[i].cycles_completed == pipeline[i].cycles_needed 
					&& pipeline[i].result_squashed) {
						// if instruction has completed, but result has been squashed, 
						// remove instuction from pipeline (do not want to write back)
						pipeline.erase(pipeline.begin() + i);
					}
				}
				break;
			} else if (pipeline[i].stage == "FADD") {
				// ADD.S or SUB.S instruction is already executing, but needs
				// additional cycle(s) to complete
				if (!pipeline[i].stalled) {
					printf("%41d\r", pipeline[i].id);
					pipeline[i].cycles_completed++;
					if (pipeline[i].cycles_completed == pipeline[i].cycles_needed 
					&& pipeline[i].result_squashed) {
						// if instruction has completed, but result has been squashed, 
						// remove instuction from pipeline (do not want to write back)
						pipeline.erase(pipeline.begin() + i);
					}
				}
				break;
			}
		}
		// WB stage
		for (i = 0; i < pipeline.size(); ++i) {
			if (pipeline[i].stage == "MEM") {
				if (!pipeline[i].stalled) {
					// instruction completed, write back result
					printf("%35d\r", pipeline[i].id);
					// remove from pipeline
					pipeline.erase(pipeline.begin() + i);
				}
				break;
			}
		}
		// MEM stage
		for (i = 0; i < pipeline.size(); ++i) {
			if (pipeline[i].stage == "EX") {
				if (!pipeline[i].stalled) {
					// transition instruction from EX stage to MEM
					pipeline[i].stage = "MEM";
					printf("%29d\r", pipeline[i].id);
					if (pipeline[i].type == "SW" || pipeline[i].type == "S.S") {
						// store instructions complete in MEM stage, remove from
						// pipeline
						pipeline.erase(pipeline.begin() + i);
					}
				}
				break;
			}
		}
		// EX stage
		for (i = 0; i < pipeline.size(); ++i) {
			if (pipeline[i].stage == "ID") {
				if (!pipeline[i].stalled) {
					// transition instruction from ID stage to EX
					pipeline[i].stage = "EX";
					printf("%23d\r", pipeline[i].id);
				}
				break;
			}
		}
		// execute needed stalls
		if (current_stall_cycle != needed_stall_cycles) {
			if (!last_instruction_fetched) {
				printf("%5d %5s %5s\n", current_cycle, "stall", "stall");
			} else {
				// last instruction was fetched, only need to stall in ID stage
				printf("%5d %11s\n", current_cycle, "stall");
			}
			if (++current_stall_cycle == needed_stall_cycles) {
				// needed stall(s) have been executed
				// reset stall counters
				current_stall_cycle = 0;
				needed_stall_cycles = 0;
				for (i = 0; i < pipeline.size(); ++i) {
					// unstall the instruction that needed the stall(s)
					if (pipeline[i].stalled) {
						pipeline[i].stalled = false;
					}
				}
			}
			continue;
		}
		// ID stage
		for (i = 0; i < pipeline.size(); ++i) {
			if (pipeline[i].stage == "IF") {
				// transition instruction from IF stage to ID
				pipeline[i].stage = "ID";
				printf("%17d\r", pipeline[i].id);
				if (pipeline[i].id == instructions.size()) {
					// this is the last instruction, so any stalls will happen only
					// in future ID stages
					last_instruction_fetched = true;
				}
				if (pipeline[i].type == "BEQ" || pipeline[i].type == "BNE") {
					if (pipeline[i].branch_taken == 'T') {
						// needed to flush fetched instruction
						branch_taken = true;
					}
					// branch instructions complete in ID stage, remove from pipeline
					pipeline.erase(pipeline.begin() + i);
				}
				for (j = i - 1; j >= 0; --j) {
					// check for WAW (current instruction will write to same FP reg 
					// before an executing instuction)
					if (pipeline[i].destination_register[0] == 'F' 
					&& pipeline[i].destination_register == 
					pipeline[j].destination_register && pipeline[i].cycles_needed 
					< (pipeline[j].cycles_needed - pipeline[j].cycles_completed)) {
						// squash result (prevent it from be written to FP reg)
						pipeline[j].result_squashed = true;
						++waw_squashes;
					}
					// check for a structural hazard (required functional unit is 
					// being used by executing instruction)
					if ((((pipeline[i].type == "ADD.S" || 
					pipeline[i].type == "SUB.S") && pipeline[j].stage == "FADD") ||
					(pipeline[i].type == "MUL.S" && pipeline[j].stage == "FMUL") ||
					(pipeline[i].type == "DIV.S" && pipeline[j].stage == "FDIV")) && 
					pipeline[j].cycles_completed != pipeline[j].cycles_needed) {
						// needed stall cycles is the number of cycles the culprit
						// instruction still needs
						needed_stall_cycles = pipeline[j].cycles_needed - 
						pipeline[j].cycles_completed;
						if (needed_stall_cycles < 0) {
							// if culprit instruction will complete, don't need to
							// stall
							needed_stall_cycles = 0;
							break;
						}
						// stall current instruction
						pipeline[i].stalled = true;
						structural_hazard_cycles += needed_stall_cycles;
						break;
					}
					// check for a load hazard or a data hazard
					if (pipeline[j].destination_register == 
					pipeline[i].destination_register && (pipeline[j].type == "LW"
					|| pipeline[j].type == "L.S") && (pipeline[i].type == "SW" ||
					pipeline[i].type == "S.S")) {
						// load hazard: store instruction needs value of load
						// instruction's destination register to write to memory
						needed_stall_cycles = 1;
						load_delay_hazard_cycles += needed_stall_cycles;
						// stall current instruction
						pipeline[i].stalled = true;
						break;
					}
					if (pipeline[j].destination_register == 
					pipeline[i].source_register1 || 
					pipeline[j].destination_register == 
					pipeline[i].source_register2) {
						if (pipeline[j].type == "LW" || pipeline[j].type == "L.S") {
							if (pipeline[i].type == "SW" || 
							pipeline[i].type == "S.S") {
								// not a load hazard: store instruction does not write 
								// to memory until MEM stage, at which time the value of 
								// the load instruction's destination register will be 
								// available
								continue;
							}
							// load hazard: instruction needs value of load
							// instruction's destination register to execute
							// only need one stall cycle
							needed_stall_cycles = 1;
							load_delay_hazard_cycles += needed_stall_cycles;
							// stall current instruction
							pipeline[i].stalled = true;
						} else {
							// data hazard: instruction needs value of previous 
							// instruction's destination register to execute
							if (pipeline[i].type == "BEQ" || 
							pipeline[i].type == "BNE") {
								// branch instructions are resolved in ID stage, so need
								// to stall for source register value (1 cycle)
								needed_stall_cycles = 1;
								// stall current instruction
								pipeline[i].stalled = true;
							} else if (pipeline[j].type == "ADD.S" || 
							pipeline[j].type == "SUB.S" || 
							pipeline[j].type == "MUL.S" ||
							pipeline[j].type == "DIV.S") {
								// needed stall cycles is the numer of cycles the
								// culprit instruction still needs
								needed_stall_cycles = pipeline[j].cycles_needed - 
								pipeline[j].cycles_completed;
								if (pipeline[i].type == "SW" || 
								pipeline[i].type == "S.S") {
									// needed stall cycles is one less because store 
									// instruction doesn't write to memory until MEM 
									// stage
									needed_stall_cycles -= 1;
									if (needed_stall_cycles < 0) {
										// if instruction will complete, though, don't
										// need to stall
										needed_stall_cycles = 0;
										break;
									}
								}
								// stall current instruction
								pipeline[i].stalled = true;
							}
							data_hazard_cycles += needed_stall_cycles;
						}
						break;
					}
				}
				break;
			}
		}
		// IF stage
		if (instruction_ptr < instructions.size()) {
			// fetch next instruction, place it in pipeline
			pipeline.push_back(instructions[instruction_ptr++]);
			// indicate that it's currently in the IF stage
			pipeline.back().stage = "IF";
			// print its identifier
			printf("%11d\r", pipeline.back().id);
			// if branch is taken, need to flush fetched instruction
			if (branch_taken) {
				pipeline.erase(pipeline.end()-1);
				++branch_flushes;
				// reset flag
				branch_taken = false;
			}
		}
		printf("%5d\n", current_cycle);
	} while (!pipeline.empty());

	// calculate percent of hazard cycles for each type of hazard, provided
	// total_hazard_cycles > 0
	total_hazard_cycles = load_delay_hazard_cycles + structural_hazard_cycles 
	+ data_hazard_cycles;
	if (total_hazard_cycles > 0) {
		load_delay_hazard_percent = (static_cast<float>(load_delay_hazard_cycles) 
		/ total_hazard_cycles) * 100.0;
		structural_hazard_percent = (static_cast<float>(structural_hazard_cycles) 
		/ total_hazard_cycles) * 100.0;
		data_hazard_percent = (static_cast<float>(data_hazard_cycles) 
		/ total_hazard_cycles) * 100.0;
	} else {
		load_delay_hazard_percent = 0.0;
		structural_hazard_percent = 0.0;
		data_hazard_percent = 0.0;
	}

	// print statistics
	printf("\nhazard type  cycles  %% of stalls  %% of total\n");
	printf("-----------  ------  -----------  ----------\n");
	printf("%-11s  %6d  %11.2f  %10.2f\n", "load-delay", 
		load_delay_hazard_cycles, load_delay_hazard_percent, 
		(static_cast<float>(load_delay_hazard_cycles) / current_cycle) * 100.0);
	printf("%-11s  %6d  %11.2f  %10.2f\n", "structural", 
		structural_hazard_cycles, structural_hazard_percent, 
		(static_cast<float>(structural_hazard_cycles) / current_cycle) * 100.0);
	printf("%-11s  %6d  %11.2f  %10.2f\n", "data", data_hazard_cycles, 
		data_hazard_percent, 
		(static_cast<float>(data_hazard_cycles) / current_cycle) * 100.0);
	printf("-----------  ------  -----------  ----------\n");
	printf("%-11s  %6d  %11.2f  %10.2f\n", "total", total_hazard_cycles, 
		load_delay_hazard_percent + structural_hazard_percent + 
		data_hazard_percent, 
		(static_cast<float>(total_hazard_cycles) / current_cycle) * 100.0);

	printf("\nWAW squashes: %d\n", waw_squashes);
	printf("branch flushes: %d\n", branch_flushes);
}