# nesem
Nes Emulator in Progress (collab with RenDev)

Currently only the CPU, BUS and CARTRIDGE (mapper 000 only) are done, left are the PPU and actual screen + controllers

Currently all rights are reserved but when the project reaches beta I will release it under a very permissive license
(0BSD, 0MIT, unlicense, something like that, might go with std BSD or MIT)

## How to help:
### (CPU)
Download nestest.nes from [here](https://www.nesdev.org/wiki/Emulator_tests)

Compile with debug (`make debug`)

Run with nestest.nes into a logfile (`./nesem nestest.nes > logfile`)

Compare logfile with provided sampleoutput file that contains expected NES 6502 behaviour, fix any errors that cause branching


### (PPU)
Start work on PPU

### (Cartridge)
Add mappers
