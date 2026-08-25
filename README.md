# 4BIT-RELAY-CPU-SERIALVIEWER
Built and physically debugged a custom 4-bit hybrid relay computer with A/B accumalator registers, 4 bit ripple-carry ALU, and Arduino Uno to act as main control unit and program storage, which provided deterministic timing and sequencing as well as non volatile storage for micro operations. Verified correctness via RegA/RegB+0 sweeps, A+B checks, and mod-16 Fibonacci loops. Implemented UART serial output and a Linux Python (pyserial/pygame) viewer for live graphics, including a raycasting-style demo.

 -HARDWARE: 

      -20x 5v DPDT Relays 
      -1x Arduino UNO 
      -750-800x Jumper Wires
      -12x LEDs
      -1x 74HC595 
      -4x Full Size Breadboards
      -External 5v Power Supply 


1.) register-a-test-sweep.ino (to be uploaded with Arduino IDE) 
         -Sweeps Register A with controlled inputs (Reg A + 0 style) 

2.) fib-sequence-mod16.ino (to be uploaded with Arduino IDE) 
         -Runs Fibonacci, wraps mod 16 to stress repeated execution and verify stability. 

3.) raycast-demo.ino (to be uploaded with Arduino IDE) 
         -Raycasting style demo workload used as an end to end stress test for compute + serial + viewer throughput. 

4.) viewer.py 
         -Python app that reads the serial stream with 'pyserial' and renders graphics using 'pygame'. 


Relay Computer + Visualization Demo: https://youtube.com/shorts/qc_YNZvkO54?is=frjGqjloafG-7UYr
 

