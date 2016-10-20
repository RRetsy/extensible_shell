#!/usr/bin/python
#
# Block header comment
#
#
import sys, imp, atexit
sys.path.append("/home/courses/cs3214/software/pexpect-dpty/");
import pexpect, shellio, signal, time, os, re, proc_check

#Ensure the shell process is terminated
def force_shell_termination(shell_process):
	c.close(force=True)

#pulling in the regular expression and other definitions
definitions_scriptname = sys.argv[1]
plugin_dir = sys.argv[2]
def_module = imp.load_source('', definitions_scriptname)
logfile = None
if hasattr(def_module, 'logfile'):
    logfile = def_module.logfile

#spawn an instance of the shell
c = pexpect.spawn(def_module.shell + plugin_dir, drainpty=True, logfile=logfile)

atexit.register(force_shell_termination, shell_process=c)



#############################################################################
# Our tests: 


# no arguments
c.sendline("circalc")
assert c.expect("You have to provide the raduis as an argument.") == 0, \
	"Error: expected output \"You have to provide the raduis as an argument.\" "
# invalid argument
c.sendline("circalc -3")
assert c.expect("Invalid raduis. Use a number betweent 0 - 100000") == 0, \
"Error: expected output \"Invalid raduis. Use a number betweent 0 - 100000\" "

# valid argument
c.sendline("circalc 1")
assert c.expect("area = 3.14 , circumference = 6.28") == 0, \
	"Error: expected output \"You have to provide the raduis as an argument.\" "



shellio.success()