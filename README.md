# SGEA
SGEA is a set of programs developed in C using the Win32 API that allows simulating an airspace management system.

1. run controlGUI
2. run aviao.exe executing command:  aviao.exe [capacity] [speed] [origin] 
3. run passag.exe executing command: passag.exe [origin] [destination] [name] [(waitingTime)]


control commands:
- shutdown: shutdown the system
- ADD AIRPORT: add new airport to the map
- PAUSE: pause new plane acceptance
- UNPAUSE: unpause new plane acceptance
- LIST: list of available commands

aviao commands:
- define destination: set the plane destination
- boarding: board passengers with the same origin and destination airports
- start flight: start the flight
- details: display details about the plane
- help: list of available commands
- clear: clear the console
- cls: clear the console

passag commands:
- details: display passenger details



See an example: https://youtu.be/vlxLFu9PihI 
