/*
 * esh - the 'pluggable' shell.
 *
 * Developed by Godmar Back for CS 3214 Fall 2009
 * Virginia Tech.
 */

#include <stdio.h>
#include <readline/readline.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "esh.h"
#include "esh-sys-utils.h"

// The return number for each command in builtin_command function
#define EXIT 1
#define JOBS 2
#define FG 3
#define BG 4
#define KILL 5
#define STOP 6
#define DEFAULT 0

/* List of current pipelines/jobs */
struct list current_pipelines;

// Used to assign job id, Everytime we run a command, this variable will plus one.
// If the current_pipelines is empty this number will comeback to 0.
int pipeline_num;

static void usage(char *progname);

/* Build a prompt by assembling fragments from loaded plugins that
 * implement 'make_prompt.'
 *
 * This function demonstrates how to iterate over all loaded plugins.
 */
static char * build_prompt_from_plugins(void);

// To return a MACRO number for each command
int builtin_command(char *command);

// Return the current pipelines
static struct list* get_jobs(void);

// Return pipeline whose jid equals the given jid
static struct esh_pipeline * get_job_from_jid(int jid);

// Return pipline whose pgrp equals the given pgrp
static struct esh_pipeline * get_job_from_pgrp(pid_t pgrp);

// From the website
static void give_terminal_to(pid_t pgrp, struct termios *pg_tty_state);

// Print the pipeline, such as "sleep 150", no job id, no status
static void print_pipeline(struct esh_pipeline *pipeline);

// Print the status of the pipeline
static void print_pipeline_status(struct esh_pipeline *pipeline);

// If the pipeline is foreground, the shell needs to wait for it and get terminal back
void wait_for_pipeline(struct esh_pipeline *pipeline,struct termios *terminal);

// Every time parent receives a signal, it needs to change status of the child.
static void change_pipeline_status(pid_t pid, int status);

// The handler of the signal sent by ctrl+z which is SIGCHLD
static void child_handler(int sig, siginfo_t *info, void *_ctxt);

// The handler of the signal sent by ctrl+z which is SIGTSTP
static void ctrlz_handler(int sig, siginfo_t *info, void *_ctxt);

// To determine if the command specified by PID is with in the pipeline
static bool is_pipeline_last_command(struct esh_pipeline *pipeline,pid_t pid);

// To determine if the command specified by PID is at the end of the pipeline
static bool is_pipeline_has_command(struct esh_pipeline *pipeline,pid_t pid);

/* The shell object plugins use.
 * Some methods are set to defaults.
 */
struct esh_shell shell =
{
        .get_jobs=get_jobs,
        .get_job_from_jid=get_job_from_jid,
        .get_job_from_pgrp=get_job_from_pgrp,
        .build_prompt = build_prompt_from_plugins,
        .readline = readline, /* GNU readline(3) */
        .parse_command_line = esh_parse_command_line /* Default parser */
};

int main(int ac, char *av[])
{
        // Initialize the list of plugins
        list_init(&esh_plugin_list);

        // Parse the option of user input
        int opt;
        /* Process command-line arguments. See getopt(3) */
        while ((opt = getopt(ac, av, "hp:")) > 0) {
                switch (opt) {
                case 'h':
                        usage(av[0]);
                        break;

                case 'p':
                        //Load plugins from directory
                        esh_plugin_load_from_directory(optarg);

                        break;
                }
        }

        // Initialize the shell by any plugin
        esh_plugin_initialize(&shell);

        // Initialize the pipeline list
        list_init(&current_pipelines);

        // We now have zero pipelines
        pipeline_num=0;

        // The main process is the parent of its own
        setpgid(0,0);

        // Initialize the termianal state and give it to the main process
        struct termios *terminal=esh_sys_tty_init();
        give_terminal_to(getpgrp(),terminal);

        // Install handler for SIGCHLD and SIGTSTP
        esh_signal_sethandler(SIGCHLD,child_handler);
        esh_signal_sethandler(SIGTSTP,ctrlz_handler);

        // Read/eval loop
        for (;; ) {
                char * prompt = isatty(0) ? shell.build_prompt() : NULL;
                char * cmdline = shell.readline(prompt);

                // To check if any plugin wants to change command line
                struct list_elem *e;
                for(e=list_begin(&esh_plugin_list); e!=list_end(&esh_plugin_list); e=list_next(e)) {
                        struct esh_plugin * plugin=list_entry(e,struct esh_plugin,elem);
                        if(plugin->process_raw_cmdline) {
                                plugin->process_raw_cmdline(&cmdline);
                        }
                }

                free (prompt);

                if (cmdline == NULL) /* User typed EOF */
                        break;

                struct esh_command_line * cline = shell.parse_command_line(cmdline);
                free (cmdline);
                if (cline == NULL) /* Error in command line */
                        continue;

                if (list_empty(&cline->pipes)) { /* User hit enter */
                        esh_command_line_free(cline);
                        continue;
                }

                // Load the first pipiline from the command line
                struct esh_pipeline *pipeline=list_entry(list_begin(&cline->pipes),struct esh_pipeline,elem);

                // To check if any plugin wants to change pipeline
                for(e=list_begin(&esh_plugin_list); e!=list_end(&esh_plugin_list); e=list_next(e)) {
                        struct esh_plugin * plugin=list_entry(e,struct esh_plugin,elem);
                        if(plugin->process_pipeline) {
                                plugin->process_pipeline(pipeline);
                        }
                }

                // Load the first command from the pipeline
                struct esh_command *command=list_entry(list_begin(&pipeline->commands),struct esh_command,elem);

                // Parse the command
                int command_num = builtin_command(command->argv[0]);

                // Check if the command is defined by pluggins, if it is, run it
                // and change command_num so that our shell won't run it
                for(e=list_begin(&esh_plugin_list); e!=list_end(&esh_plugin_list); e=list_next(e)) {
                        struct esh_plugin *plugin=list_entry(e,struct esh_plugin,elem);
                        if(plugin->process_builtin) {
                                if(plugin->process_builtin(command)) {
                                        command_num=-1;
                                }
                        }
                }

                // exit
                if(command_num==EXIT) {
                        exit(0);
                }

                // jobs/pipelines
                if(command_num==JOBS) {
                        if(!list_empty(&current_pipelines)) {
                                struct list_elem *e;
                                for(e=list_begin(&current_pipelines); e!=list_end(&current_pipelines); e=list_next(e)) {
                                        struct esh_pipeline * pipeline=list_entry(e,struct esh_pipeline,elem);
                                        print_pipeline_status(pipeline);
                                        printf("(");
                                        print_pipeline(pipeline);
                                        printf(")\n");
                                }
                        }
                }

                if(command_num==FG||command_num==BG||command_num==KILL||command_num==STOP) {
                        // The current pipelines must be unempty.
                        struct esh_pipeline *specified_pipeline;

                        int job_id=-1;

                        // This part is for getting pid of the pipeline for the operation.
                        // If the command desn't specify a job_id, we will operate on the most recent pipeline.
                        if(command->argv[1]==NULL) {
                                struct list_elem *e=list_back(&current_pipelines);
                                struct esh_pipeline *pipeline=list_entry(e,struct esh_pipeline,elem);
                                job_id=pipeline->jid;
                        }
                        else{
                                // if the argv has % we need to use the number for jid
                                if(strncmp(command->argv[1],"%",1)==0) {
                                        job_id=atoi(command->argv[1]+1);
                                }else{
                                        job_id=atoi(command->argv[1]);
                                }
                        }

                        // Get the pipeline according to the job_id
                        // or prompt no such job
                        if(!list_empty(&current_pipelines)) {
                                specified_pipeline=get_job_from_jid(job_id);
                                if(specified_pipeline==NULL) {
                                        printf("No job with job id %d found",job_id);
                                        //printf("%s: %s: no such job\n",command->argv[0],command->argv[1]);
                                        continue;
                                }
                        }else{
                                printf("No job with job id %d found",job_id);
                                //printf("%s: %s: no such job\n",command->argv[0],command->argv[1]);
                                continue;
                        }

                        //fg command
                        if(command_num==FG) {
                                specified_pipeline->status=FOREGROUND;
                                printf("(");
                                print_pipeline(specified_pipeline);
                                printf(")\n");

                                // Send SIGCONT no matter if the job is running or stopped
                                if(kill(-specified_pipeline->pgrp,SIGCONT)<0) {
                                        esh_sys_fatal_error("SIGCONT error");
                                }

                                // The pipeline is now foreground.
                                give_terminal_to(specified_pipeline->pgrp,terminal);
                                wait_for_pipeline(specified_pipeline,terminal);

                                // Remember to give terminal back to main process
                                give_terminal_to(getpgrp(),terminal);
                        }

                        //bg command
                        if(command_num==BG) {
                                specified_pipeline->status=BACKGROUND;

                                // Send SIGCONT no matter if the job is running or stopped
                                if(kill(-specified_pipeline->pgrp,SIGCONT)<0) {
                                        esh_sys_fatal_error("SIGCONT error");
                                }
                                printf("[%d] ",specified_pipeline->jid);
                                printf("(");
                                print_pipeline(specified_pipeline);
                                printf(")\n");
                        }

                        //kill command
                        if(command_num==KILL) {

                                if(kill(-specified_pipeline->pgrp,SIGTERM)<0) {
                                        esh_sys_fatal_error("SIGKILL error");
                                }
                        }

                        // stop command
                        if(command_num==STOP) {
                                if(kill(-specified_pipeline->pgrp,SIGSTOP)<0) {
                                        esh_sys_fatal_error("SIGSTOP error");
                                }

                        }
                } // End of fg bg kill stop

                if(command_num==DEFAULT) {

                        esh_signal_block(SIGCHLD);
                        pipeline_num++;
                        pipeline->jid=pipeline_num;
                        pipeline->pgrp=-1;
                        pid_t pid;

                        int inputPipe[2],outputPipe[2];

                        struct list_elem *e;
                        for(e=list_begin(&pipeline->commands); e!=list_end(&pipeline->commands); e=list_next(e)) {
                                struct esh_command *command=list_entry(e,struct esh_command,elem);

                                if(list_size(&pipeline->commands)>1 && e!=list_back(&pipeline->commands)) {
                                        pipe(outputPipe);
                                }

                                pid=fork();
                                if (pid < 0) {
                                        esh_sys_fatal_error("Fork Error ");
                                }// Child
                                else if(pid==0) {
                                        pid_t pid=getpid();
                                        command->pid=pid;

                                        if(pipeline->pgrp==-1) {
                                                pipeline->pgrp=pid;
                                        }

                                        // Set the pgrp of the every command process as the pgrp of the pipeline
                                        if(setpgid(pid,pipeline->pgrp)<0) {
                                                esh_sys_fatal_error("setpgid error");
                                        }

                                        if(command->iored_input!=NULL) {
                                                int fd_in=open(command->iored_input,O_RDONLY);
                                                if (dup2(fd_in, 0) < 0) {
                                                        esh_sys_fatal_error("dup2 error");
                                                }
                                                close(fd_in);
                                        }

                                        if(command->iored_output!=NULL) {
                                                int fd_out;
                                                if(command->append_to_output) {
                                                        //fd_out = open(command->iored_output, O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
                                                        fd_out = open(command->iored_output, O_WRONLY | O_APPEND | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
                                                }else{
                                                        //fd_out = open(command->iored_output, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
                                                        fd_out = open(command->iored_output, O_WRONLY | O_TRUNC  | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
                                                }
                                                if((dup2(fd_out,1))<0) {
                                                        esh_sys_fatal_error("dup2 error");
                                                };
                                                close(fd_out);
                                        }
                                        // IO direction
                                        if(list_size(&pipeline->commands)>1) {
                                                // Commands that are not the head
                                                if(e!=list_begin(&pipeline->commands)) {
                                                        dup2(inputPipe[0],0);
                                                        close(inputPipe[0]);
                                                        close(inputPipe[1]);
                                                }

                                                // Commands that are not the back
                                                if(e!=list_back(&pipeline->commands)) {
                                                        dup2(outputPipe[1],1);
                                                        close(outputPipe[1]);
                                                        close(outputPipe[0]);
                                                }



                                        }// End of pipeline size > 1

                                        esh_signal_unblock(SIGCHLD);
                                        if(execvp(command->argv[0],command->argv)<0) {
                                                esh_sys_fatal_error("");
                                        }
                                } // End of Child
                                  // Parent
                                else {
                                        // Make the pid of the first command in the command list the pgrp of the pipeline
                                        if(pipeline->pgrp==-1) {
                                                pipeline->pgrp=pid;
                                        }
                                        command->pid=pid;
                                        // Set the pgrp of the every command process as the pgrp of the pipeline
                                        if(setpgid(pid,pipeline->pgrp)<0) {
                                                esh_sys_fatal_error("setpgid error");
                                        }
                                        if(list_size(&pipeline->commands)>1) {

                                                if(e!=list_begin(&pipeline->commands)) {
                                                        close(inputPipe[0]);
                                                        close(inputPipe[1]);
                                                }

                                                if(e!=list_back(&pipeline->commands)) {
                                                        inputPipe[0]=outputPipe[0];
                                                        inputPipe[1]=outputPipe[1];
                                                }

                                                if(e==list_back(&pipeline->commands)) {
                                                        close(inputPipe[0]);
                                                        close(inputPipe[1]);
                                                        close(outputPipe[0]);
                                                        close(outputPipe[1]);
                                                }
                                        }


                                }
                        } // End of iteration through commands

                        // To check if any plugin wants to change pipeline
                        for(e=list_begin(&esh_plugin_list); e!=list_end(&esh_plugin_list); e=list_next(e)) {
                                struct esh_plugin * plugin=list_entry(e,struct esh_plugin,elem);
                                if(plugin->pipeline_forked) {
                                        plugin->pipeline_forked(pipeline);
                                }
                        }

                        e = list_pop_front(&cline->pipes);
                        list_push_back(&current_pipelines, e);
                        // Change pipeline status and give terminal
                        if(pipeline->bg_job) {
                                pipeline->status=BACKGROUND;
                                printf("[%d] %d\n",pipeline->jid,pid);
                        }else{
                                pipeline->status=FOREGROUND;
                                give_terminal_to(pipeline->pgrp,terminal);

                                int i=list_size(&pipeline->commands);
                                for(; i>0; i--) {
                                        wait_for_pipeline(pipeline,terminal);
                                }

                                give_terminal_to(getpgrp(),terminal);
                        }

                } // End of command_num == DEFAULT
                  //esh_command_line_print(cline);
                esh_signal_unblock(SIGCHLD);
                esh_command_line_free(cline);
        }// end of the wholie cline
        return 0;
}

static void usage(char *progname)
{
        printf("Usage: %s -h\n"
               " -h            print this help\n"
               " -p  plugindir directory from which to load plug-ins\n",
               progname);

        exit(EXIT_SUCCESS);
}

static char * build_prompt_from_plugins(void)
{
        char *prompt = NULL;
        struct list_elem * e = list_begin(&esh_plugin_list);

        for (; e != list_end(&esh_plugin_list); e = list_next(e)) {
                struct esh_plugin *plugin = list_entry(e, struct esh_plugin, elem);

                if (plugin->make_prompt == NULL)
                        continue;

                /* append prompt fragment created by plug-in */
                char * p = plugin->make_prompt();
                if (prompt == NULL) {
                        prompt = p;
                } else {
                        prompt = realloc(prompt, strlen(prompt) + strlen(p) + 1);
                        strcat(prompt, p);
                        free(p);
                }
        }

        /* default prompt */
        if (prompt == NULL)
                prompt = strdup("esh> ");

        return prompt;
}

int builtin_command(char *command){
        if (!strcmp(command, "exit")) {
                return EXIT;
        }

        else if (!strcmp(command, "jobs")) {
                return JOBS;
        }

        else if (!strcmp(command, "fg")) {
                return FG;
        }

        else if (!strcmp(command, "bg")) {
                return BG;
        }

        else if (!strcmp(command, "kill")) {
                return KILL;
        }

        else if (!strcmp(command, "stop")) {
                return STOP;
        }
        return DEFAULT;
}

/* Return the current pipelines */
static struct list* get_jobs(void){
        return &current_pipelines;
}

/* Return pipeline whose jid equals the given jid */
static struct esh_pipeline * get_job_from_jid(int jid){
        struct list_elem *e;
        for(e=list_begin(&current_pipelines); e!=list_end(&current_pipelines); e=list_next(e)) {
                struct esh_pipeline *pipeline=list_entry(e,struct esh_pipeline,elem);
                if(pipeline->jid==jid) {
                        return pipeline;
                }
        }
        return NULL;
}

/* Return pipline whose pgrp equals the given pgrp */
static struct esh_pipeline * get_job_from_pgrp(pid_t pgrp){
        struct list_elem *e;
        for(e=list_begin(&current_pipelines); e!=list_end(&current_pipelines); e=list_next(e)) {
                struct esh_pipeline *pipeline=list_entry(e,struct esh_pipeline,elem);
                if(pipeline->pgrp==pgrp) {
                        return pipeline;
                }
        }
        return NULL;
}

// From the website.
static void give_terminal_to(pid_t pgrp, struct termios *pg_tty_state)
{
        esh_signal_block(SIGTTOU);
        int rc = tcsetpgrp(esh_sys_tty_getfd(), pgrp);
        if (rc == -1)
                esh_sys_fatal_error("tcsetpgrp: ");

        if (pg_tty_state)
                esh_sys_tty_restore(pg_tty_state);
        esh_signal_unblock(SIGTTOU);
}

static void print_pipeline_status(struct esh_pipeline *pipeline){
        char *jobs_status[] ={"Running","Running","Stopped", "Done"};
        printf("[%d]   %s         ",pipeline->jid,jobs_status[pipeline->status]);
}

static void print_pipeline(struct esh_pipeline *pipeline){
        struct list_elem *e;
        for(e=list_begin(&pipeline->commands); e!=list_end(&pipeline->commands); e=list_next(e)) {
                struct esh_command *command=list_entry(e,struct esh_command,elem);
                char **argv=command->argv;
                while(*argv) {
                        if(*(argv+1)) {
                                printf("%s ",*argv);
                        }else{
                                printf("%s",*argv);
                        }
                        fflush(stdout);
                        argv++;
                }
                if(list_size(&pipeline->commands)>=2 && list_next(e)!=list_tail(&pipeline->commands)) {
                        printf("|");
                }
        }
}

void wait_for_pipeline(struct esh_pipeline *pipeline,struct termios *terminal)
{
        int status;
        pid_t pid;
        if((pid=waitpid(-pipeline->pgrp, &status, WUNTRACED)) > 0) {
                //give_terminal_to(getpgrp(), terminal);
                change_pipeline_status(pid, status);
        }
}

static void change_pipeline_status(pid_t pid, int status){
        if(pid<=0) {
                esh_sys_fatal_error("Wait error");
        }else{
                struct list_elem* e;

                for(e=list_begin(&current_pipelines); e!=list_end(&current_pipelines); e=list_next(e)) {
                        struct esh_pipeline * pipeline=list_entry(e,struct esh_pipeline,elem);
                        if(is_pipeline_has_command(pipeline,pid)) {

                                // Find the the command according to the pid
                                struct esh_command *command;
                                struct list_elem *command_elem;
                                for(command_elem=list_begin(&pipeline->commands); command_elem!=list_end(&pipeline->commands); command_elem=list_next(command_elem)) {
                                        command=list_entry(command_elem,struct esh_command,elem);
                                        if(command->pid==pid) {
                                                break;
                                        }
                                }

                                // To check if any plugin wants to know about the command status change
                                struct list_elem *plugin_elem;
                                for(plugin_elem=list_begin(&esh_plugin_list); plugin_elem!=list_end(&esh_plugin_list); plugin_elem=list_next(plugin_elem)) {
                                        struct esh_plugin * plugin=list_entry(plugin_elem,struct esh_plugin,elem);
                                        if(plugin->command_status_change) {
                                                plugin->command_status_change(command,status);
                                                return;
                                        }
                                }

                                // Child being stopped
                                if (WIFSTOPPED(status)) {
                                        pipeline->bg_job=true;
                                        pipeline->status = STOPPED;

                                        if(WSTOPSIG(status)==SIGTSTP) {
                                                printf("\n");
                                                print_pipeline_status(pipeline);
                                                printf("(");
                                                print_pipeline(pipeline);
                                                printf(")\n");
                                        }
                                }

                                // Being Killed
                                if (WIFSIGNALED(status)) {
                                        pipeline->status=NEEDSTERMINAL;
                                        //printf("[%d]   %s: %d          ",pipeline->jid,"Terminated",WTERMSIG(status));
                                        //printf("(");
                                        //print_pipeline(pipeline);
                                        //printf(")\n");
                                        list_remove(e);
                                }
                        }

                        if(is_pipeline_last_command(pipeline,pid)) {
                                // Child terminated nomailly
                                if(WIFEXITED(status)) {
                                        if(pipeline->bg_job) {
                                                //Try to output the Done message!
                                                pipeline->status=NEEDSTERMINAL;
                                                print_pipeline_status(pipeline);
                                                printf("(");
                                                print_pipeline(pipeline);
                                                printf(")\n");
                                        }
                                        list_remove(e);
                                }
                                // Child being continued by a SIGCONT
                                if (WIFCONTINUED(status)) {
                                        if(pipeline->bg_job) {
                                                pipeline->status=BACKGROUND;
                                        }else{
                                                pipeline->status=FOREGROUND;
                                        }
                                }

                                if(list_empty(&current_pipelines)) {
                                        pipeline_num=0;
                                }
                        }
                }
        }
}

static void child_handler(int sig, siginfo_t *info, void *_ctxt){
        pid_t pid;
        int status;
        assert(sig==SIGCHLD);
        while ((pid = waitpid(-1, &status,WUNTRACED|WCONTINUED|WNOHANG)) > 0) {
                change_pipeline_status(pid, status);
        }
}

static void ctrlz_handler(int sig, siginfo_t *info, void *_ctxt){
        printf("\b\b  \b\b");
}

static bool is_pipeline_last_command(struct esh_pipeline *pipeline,pid_t pid){
        struct list_elem *e=list_back(&pipeline->commands);
        struct esh_command *command=list_entry(e,struct esh_command,elem);
        if(command->pid==pid) {
                return true;
        }else{
                return false;

        }
}

static bool is_pipeline_has_command(struct esh_pipeline *pipeline,pid_t pid){
        struct list_elem *e;
        for(e=list_begin(&pipeline->commands); e!=list_end(&pipeline->commands); e=list_next(e)) {
                struct esh_command *command=list_entry(e,struct esh_command,elem);
                if(command->pid==pid) {
                        return true;
                }
        }
        return false;
}
