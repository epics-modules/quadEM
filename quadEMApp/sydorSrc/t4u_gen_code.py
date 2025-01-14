import sys

if len(sys.argv) != 2:          # Wrong number of arguments
    print('Usage: python3 t4u_gen_code.py <Parameter List Name>')
    sys.exit(1)

db_filename = 'gc_t4u.db'       # The generated database file
hdr_string_filename = 'gc_t4u_hdr_string.h' # The generated parameter strings
hdr_member_filename = 'gc_t4u_hdr_member.h' # The generated members
cpp_param_filename = 'gc_t4u_cpp_params.cpp'  # The generated C++ code

db_file = open(db_filename, 'w')
hdr_string_file = open(hdr_string_filename, 'w')
hdr_member_file = open(hdr_member_filename, 'w')
cpp_param_file = open(cpp_param_filename, 'w')

template_file = open(sys.argv[1], 'r')

# Iterate over all lines in the file
for curr_line in template_file:
    stripped_line = curr_line.strip();
    if (len(stripped_line) == 0) or (stripped_line[0] == '#'): # Blank or comment
        continue                                               # Move to the next line

    # Split into components
    split_line = stripped_line.split(',') # Comma-separated
    pv_suffix = split_line[0].strip()
    data_type = split_line[1].strip()
    param_string = split_line[2].strip()
    param_string_name = split_line[3].strip()
    param_var = split_line[4].strip()
    pv_min = split_line[5].strip()
    pv_max = split_line[6].strip()
    reg_min = split_line[7].strip()
    reg_max = split_line[8].strip()
    reg_num = split_line[9].strip()
    
    # First create the database
    # First comes the ao record
    ao_name_string = 'record(ao, "$(P)$(R){}")\n'.format(pv_suffix);
    db_file.write(ao_name_string)
    db_file.write('{\n');
    db_file.write('field(DTYP, "asynFloat64")\n');
    ao_out_string = 'field(OUT, "@asyn($(PORT) 0){}")\n'.format(param_string);
    db_file.write(ao_out_string);
    db_file.write('}\n\n');

    # Next is the ai readback record
    ai_name_string = 'record(ai, "$(P)$(R){}_RBV")\n'.format(pv_suffix);
    db_file.write(ai_name_string)
    db_file.write('{\n');
    db_file.write('field(DTYP, "asynFloat64")\n');
    ai_inp_string = 'field(INP, "@asyn($(PORT) 0){}")\n'.format(param_string);
    db_file.write(ai_inp_string);
    db_file.write('field(SCAN, "I/O Intr")\n')
    db_file.write('}\n\n');

    # Time to write the header file string defines
    hdr_string_file.write('#define {} "{}"\n'.format(param_string_name, param_string))

    # Now the parameter members
    hdr_member_file.write('int {};\n'.format(param_var));

    # Now the C++ code to create the asyn paramter
    param_type = '';
    if (data_type == 'i32'):
        param_type = 'asynParamInt32';
    elif (data_type == 'f64'):
        param_type = 'asynParamFloat64';
    else:
        print('Invalid data type: "{}"\nin line {}\n'.format(data_type, curr_line))
        sys.exit(1)

    # Create the parameter in asyn
    cpp_param_file.write('createParam({}, {}, &{});\n'.format(param_string_name, param_type, param_var));

    # Now populate the register for the list
    cpp_param_file.write('curr_reg.reg_num = {};\n'.format(reg_num));
    cpp_param_file.write('curr_reg.asyn_num = {};\n'.format(param_var));
    cpp_param_file.write('curr_reg.pv_min = {};\n'.format(pv_min));
    cpp_param_file.write('curr_reg.pv_max = {};\n'.format(pv_max));
    cpp_param_file.write('curr_reg.reg_min = {};\n'.format(reg_min));
    cpp_param_file.write('curr_reg.reg_max = {};\n'.format(reg_max));

    # Push into the list
    cpp_param_file.write('pidRegData_.push_front(curr_reg);\n\n');
    
    # Now we're done

# Close the files
db_file.close()
hdr_string_file.close()
hdr_member_file.close()
cpp_param_file.close();

