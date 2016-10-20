# How to execute the shell
Use ./esh to run the shell and -p path to load plugins

## Description of Base Functionality
* jobs:
print a list of current jobs with their job id, status, and command line
Iterate through the list of current running or stopped jobs and print their job id,status and command line one by one

* fg:
put the specified background job or most recent background job into foreground and run it.
Use give_terminal_to and wait_for_pipeline

* bg:
make the specified background job or most recent background job run in background.
Use kill to send SIGCONT signal to the background process

* kill:
kill the specified background job.
Use kill to send SIGKILL signal to the process

* stop:
stop the specified background job.
Use kill to send SIGSTOP signal to the process

* ctrl+z:
send SIGTSTP to the current running job and update job status
Install signal handler

* ctrl+c:
sent SIGINT to the current running job and update job status.

## Description of Extend Functionality
* I/O:
Use open system call to open a file in special mode and connect it to the 0 or 1 file descriptor

* Pipes:
Using two pipes to give the output of the last command as the input of the command following.

* Exclusive Access:
Use give_terminal_to to give the exclusive access to the terminal.

## List of Plugins Implemented

* circalc
Calculates and outputs the area and circumference of the given circle.
