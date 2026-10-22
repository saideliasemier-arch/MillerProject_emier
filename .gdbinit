set pagination off
set confirm off
set architecture riscv:rv32
file software/RIGSoC/software/C/ibex_c_library/firmware.elf
target extended-remote localhost:3333
monitor reset halt
thbreak main
continue