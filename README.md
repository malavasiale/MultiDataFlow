# Multi-Data-Flow

### Info
Source code for SOA project that implements a linux driver for Multi-flow device file.

***Author***: Alessio Malavasi (0287437)

### Installation
In the main directory run the  command  `make all`  to compile the module and create de .ko object.

Then run the command  `sudo make mount` to install the module.

### User code execution
In the sub-directory **/user** there is another make file that compile the user.c code using the command `make all`.

After the compilation is complete you can run the code using the command:
`sudo ./user pathname major minor command`

The major number to use can be read using the dmesg command.

### Commands
The parameter ***command*** can be a number between 0 and 5 and it's used to run the program with different behaviours:
- 0 : start n thread for write
- 1 : start n thread for read
- 2 : change priority of the dev
- 3 : change timeout for blocking operations of the dev
- 4 : change blocking / non-blocking dev
- 5 : launch the test routine on the device

### Test routine
With command number 5 a test routine will start and execute the following steps:
1. Spawn a non blocking read thread
2. Spawn 2 write threads
3. Set the device in blocking mode
4. Spawn 3 blocking read threads (2 success - 1 blocked)
5. Launch one write thread. The blocked read one will wake up and read the data
6. Reset the device in non-blocking mode
7. Set priority to low
8. Launch one write on low priority
9. Spawn two read threads non-blocking on low priority (**Could run before or after the write thread!!**)


