/*
 * Circle calculator
 * invocation: circalc < raduis > 
 * calculates the circumference and area.
 * Authors: (hanghu + abdul94)
 */
 
#include <stdbool.h>
#include <stdio.h>
#include "../esh.h"
#include "../esh-sys-utils.h"
#define PI 3.1416

static bool 
init_plugin(struct esh_shell *shell)
{
    printf("Plugin 'circalc' initialized...\n");
    return true;
}

/* Implement the calculations 
 * Returns true if handled correctly, false otherwise. */
static bool
circalc(struct esh_command *cmd)
{
    if (strcmp(cmd->argv[0], "circalc"))
        return false;

    int r;
    char *argument = cmd->argv[1];
    // if no argument is given doesn't work
    if (argument == NULL) {
		esh_sys_error("You have to provide the raduis as an argument.\n");
		return true;		
    } else if (atoi(cmd->argv[1]) < 100000 && atoi(cmd->argv[1]) >= 0) {
        r = atoi(cmd->argv[1]);
        printf("area = %.2f , circumference = %.2f \n", (r * PI * r) , (2 * PI * r));
		return true;
    }
    else {
		esh_sys_error("Invalid raduis. Use a number betweent 0 - 100000\n");
		return true;
    }

	return true;
}

struct esh_plugin esh_module = {
  .rank = 1,
  .init = init_plugin,
  .process_builtin = circalc
};