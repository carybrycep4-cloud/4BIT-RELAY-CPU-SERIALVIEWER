# 4BIT-RELAY-CPU-SERIALVIEWER
Built and physically debugged a custom 4-bit hybrid relay computer with relay based ripple-carry ALU. Used Arduino Uno to act as main control unit and program storage, which provided deterministic timing and sequencing as well as non volatile storage for micro operations. Verified correctness via RegA/RegB+0 sweeps, A+B checks, and mod-16 Fibonacci loops. Implemented UART serial output and a Linux Python (pyserial/pygame) viewer for live graphics, including a raycasting-style demo.

1.) register-a-test-sweep.ino (to be uploaded with Arduino IDE) 
         -Sweeps Register A with controlled inputs (Reg A + 0 style) 
2.)