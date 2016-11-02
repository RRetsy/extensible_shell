# How to execute the shell
Use ./esh to run the shell and -p path to load plugins

## Description of Base Functionality
* jobs:
print a list of current jobs with their job id, status, and command line

* fg:
put the specified background job or most recent background job into foreground and run it.

* bg:
make the specified background job or most recent background job run in background.

* kill:
kill the specified background job.

* stop:
stop the specified background job.

* ctrl+z:
send SIGTSTP to the current running job and update job status

* ctrl+c:
sent SIGINT to the current running job and update job status.

## Description of Extend Functionality
* I/O:
Use open system call to open a file in special mode and connect it to the 0 or 1 file descriptor

* Pipes:
Using two pipes to give the output of the last command as the input of the command following.

* Exclusive Access:
Give terminal to application like VIM

## List of Plugins Implemented

* circalc
Calculates and outputs the area and circumference of the given circle.
