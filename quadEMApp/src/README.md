T4U Parameter Generation
========================

Firmware changes to the T4U hardware may necessitate regenerating code 
and database entries from a template list.  The generated files are 
committed to the repository, so in ordinary use the regeneration is not 
needed.

To regenerate the T4U code and database:


    python3 t4u_gen_code.py T4U_param_list.txt
    mv gc_t4u.db ../Db


