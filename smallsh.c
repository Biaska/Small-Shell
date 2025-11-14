#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <termios.h>
#include <linux/limits.h>
#define INPUT_LENGTH 2048
#define MAX_ARGS 512
#define MAX_BG_PC 20

static bool fg_only = false;
int background_processes[MAX_BG_PC] = {0};

struct last_status 
{
    bool exited;
    bool terminated;
    int code; 
};

struct command_line
{
    char *argv[MAX_ARGS + 1];   // argument array
    int argc;                   // argument counts
    char *input_file;           // input redirection
    char *output_file;          // output redirection
    bool is_bg;                 // is background
};

static struct last_status prev_fg_status = {false, false,0 };


/**
 * @brief           Handler for SIGTSTP. Forces the shell into a foreground only mode
 *                  and ignores all & commands to run a background process.
 * 
 * @param signo     The signal number that is passed to the handler
 */
void handle_SIGTSTP(int signo){
    // signal_received = true;
    const char *msg_enter = "\nEntering foreground-only mode (& is now ignored)\n";
    const char *msg_exit  = "\nExiting foreground-only mode\n";
    if (!fg_only) {
        fg_only = 1;
        write(STDOUT_FILENO, msg_enter, strlen(msg_enter));
    } else {
        fg_only = 0;
        write(STDOUT_FILENO, msg_exit, strlen(msg_exit));
    }
    // Flush any pending input from the terminal.
    fflush(stdout);
}

/**
 * @brief           Signal handler for SIGINT. Ignores the signal for parent
 *                  and does not do anything.
 * 
 * @param signo     The signal number passed to the handler.
 */
void handle_SIGINT(int signo){
    // ignore signal for parent
    // this handler will not be passed down to child processes
}

/**
 * @brief           Iterates through the background process global array to 
 *                  check for background proccesses that have ended. If the
 *                  process has ended, the status is printed and the pid 
 *                  removed from the array. 
 */
void check_background_processes() {
    int bgStatus;
    for (int i = 0; i < MAX_BG_PC; i++) {
        if (background_processes[i] != 0) {  // if this slot has a valid PID
            pid_t result = waitpid(background_processes[i], &bgStatus, WNOHANG);
            if (result > 0) { // Process finished
                if (WIFEXITED(bgStatus)) {
                    printf("background pid %d is done: exit value %d\n", 
                           result, WEXITSTATUS(bgStatus));
                } else if (WIFSIGNALED(bgStatus)) {
                    printf("background pid %d is done: terminated by signal %d\n", 
                           result, WTERMSIG(bgStatus));
                }
                fflush(stdout);
                // Remove the PID from the array now that it's been reaped.
                background_processes[i] = 0;
            }
        }
    }
}


/**
 * @brief       Adds a background process pid to the global array. 
 * 
 * @param pid   The pid of the background process.
 */
void add_bg_process(int pid) {
    for (int i = 0; i < MAX_BG_PC; i++) {
        if (background_processes[i] == 0) {
            background_processes[i] = pid;
            return;
        }
    }
}

/**
 * @brief           Gets input from the user and parses it for commands. 
 *                  Creates a command_line struct with data about the command.
 *                  Parses I/O redirection and background process flags.
 * 
 * @return struct command_line* 
 *                  Returns the command_line struct with the data about the 
 *                  command. 
 */
struct command_line *parse_input()
{
    char input[INPUT_LENGTH];
    struct command_line *curr_command = (struct command_line *) calloc(1,sizeof(struct command_line));
    // Get input
    printf(": ");
    fflush(stdout);
    fgets(input, INPUT_LENGTH, stdin);
    // Tokenize the input
    char *token = strtok(input, " \n");
    while(token){
        if (!strcmp(token,"<")){
            curr_command->input_file = strdup(strtok(NULL," \n"));
        } else if(!strcmp(token,">")){
            curr_command->output_file = strdup(strtok(NULL," \n"));
        } else if(!strcmp(token,"&")){
            curr_command->is_bg = true;
        } else{
            curr_command->argv[curr_command->argc++] = strdup(token);
        }
        token=strtok(NULL," \n");
    }
    return curr_command;
}

/**
 * @brief       Frees the command line struct from memory.
 * 
 * @param cmd   Command to be freed.
 */
void free_command(struct command_line *cmd)
{
    // Free the strings in argv[]
    for (int i = 0; i < cmd->argc; i++) {
        free(cmd->argv[i]);
    }

    // Free the input_file and output_file if they were set
    if (cmd->input_file) {
        free(cmd->input_file);
    }

    if (cmd->output_file) {
        free(cmd->output_file);
    }

    // Finally free the struct itself
    free(cmd);
}

/**
 * @brief       Creates a child process in the foreground. Since it is a 
 *              foreground process, the parent process waits for the child
 *              to finish before continuing.
 * 
 *              If the command contains input and output redirection, the
 *              stdin and stdout are redirected. 
 * 
 * @param cmd   The command_line struct that contains the data needed to 
 *              run the command. 
 */
void foreground_process(struct command_line *cmd)
{
    int fgStatus;
  
    // Fork a new process
    pid_t childPid = fork();
  
    switch(childPid){

        case -1:
            perror("fork()\n");
            exit(1);
            break;

        case 0:

            if (cmd->input_file != NULL) {
                // Open the source file
                int sourceFD = open(cmd->input_file, O_RDONLY);
                if (sourceFD == -1) { 
                    printf("cannot open %s for input\n", cmd->input_file);
                    exit(1); 
                }

                // Redirect stdin to source file
                int in_result = dup2(sourceFD, 0);
                if (in_result == -1) { 
                    perror(cmd->input_file); 
                    exit(2); 
                }
            }

            if (cmd->output_file != NULL) {
                // Open target file
                int targetFD = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (targetFD == -1) { 
                    perror(cmd->output_file); 
                    exit(1); 
                }

                // Redirect stdout to target file
                int out_result = dup2(targetFD, 1);
                if (out_result == -1) { 
                    perror(cmd->output_file); 
                    exit(2); 
                }
            }

            execvp(cmd->argv[0], cmd->argv);
            // exec only returns if there is an error
            perror(cmd->argv[0]);
            exit(2);
            break;

        default:
            // Wait for child's termination
            childPid = waitpid(childPid, &fgStatus, 0);
            if (childPid == -1) {
                perror("wait");
            }
            if (WIFSIGNALED(fgStatus)) {
                prev_fg_status.terminated = true;
                prev_fg_status.exited = false;
                prev_fg_status.code = WTERMSIG(fgStatus);
            } else if (WIFEXITED(fgStatus)) {
                prev_fg_status.exited = true;
                prev_fg_status.terminated = false;
                prev_fg_status.code = WEXITSTATUS(fgStatus);
            }
            if (prev_fg_status.terminated) {
                printf("terminated by signal %d\n", prev_fg_status.code);
            }
            break;

    }
}

/**
 * @brief       Creates a child process in the background. Since it is a 
 *              background process, the parent process does not wait for 
 *              the child to finish before continuing.
 * 
 *              If the command contains input and output redirection, the
 *              stdin and stdout are redirected, otherwise /dev/null is
 *              used.
 * 
 * @param cmd   The command_line struct that contains the data needed to 
 *              run the command. 
 */
void background_process(struct command_line *cmd)
{
    int bgStatus;
  
    // Fork a new process
    pid_t childPid = fork();
  
    switch(childPid){

        case -1:
            perror("fork()\n");
            exit(1);
            break;

        case 0:
            char *src_file;
            if (cmd->input_file != NULL) {
                src_file = strdup(cmd->input_file);
            } else {
                src_file = "/dev/null";
            }

            // Open the source file
            int sourceFD = open(src_file, O_RDONLY);
            if (sourceFD == -1) { 
                perror(src_file); 
                exit(1); 
            }

            // Redirect stdin to source file
            int in_result = dup2(sourceFD, 0);
            if (in_result == -1) { 
                perror(src_file); 
                exit(2); 
            }
            
            char *target_file;

            if (cmd->output_file != NULL) {
                target_file = strdup(cmd->output_file);
            } else {
                target_file = "/dev/null";
            }

            // Open target file
            int targetFD = open(target_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (targetFD == -1) { 
                perror(target_file); 
                exit(1); 
            }

            // Redirect stdout to target file
            int out_result = dup2(targetFD, 1);
            if (out_result == -1) { 
                perror(target_file); 
                exit(2); 
            }
            execvp(cmd->argv[0], cmd->argv);
            // exec only returns if there is an error
            perror(cmd->argv[0]);
            exit(2);
            break;

        default:
            add_bg_process(childPid);
            printf("background pid is %d\n", childPid);
            fflush(stdout);
            break;
    }
}

int main()
{

    // Initialize SIGINT_action struct to be empty
    struct sigaction SIGINT_action = {0};

    // Fill out the SIGINT_action struct
    // Register handle_SIGINT as the signal handler
    SIGINT_action.sa_handler = handle_SIGINT;
    // Block all catchable signals while handle_SIGINT is running
    sigfillset(&SIGINT_action.sa_mask);
    // No flags set
    SIGINT_action.sa_flags = SA_RESTART;

    // Install our signal handler
    sigaction(SIGINT, &SIGINT_action, NULL);

    // Initialize SIGINT_action struct to be empty
    struct sigaction SIGTSTP_action = {0};

    // Register signal handler
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    // Block all catchable signals
    sigfillset(&SIGTSTP_action.sa_mask);
    // No flags set
    SIGTSTP_action.sa_flags = 0;

    // Install signal handler
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    struct command_line *curr_command;
    while(true)
    {
        check_background_processes();

        curr_command = parse_input();
        if (curr_command->argc == 0 
                || curr_command->argv[0][0] == '#') {
            // comment line
            // do nothing
        } else if (strcmp(curr_command->argv[0], "exit") == 0) {
            // Terminate all child processes

            // Exit
            exit(0);
        } else if (strcmp(curr_command->argv[0], "cd") == 0) {
            // change path
            if (curr_command->argv[1] != NULL) {
                chdir(curr_command->argv[1]);
            } else {
                chdir(getenv("HOME"));
            }
        } else if (strcmp(curr_command->argv[0], "status") == 0) {
            // print most recent fg process status
            if (prev_fg_status.exited) {
                printf("exit value %d\n", prev_fg_status.code);
            } else if (prev_fg_status.terminated) {
                printf("terminated by signal %d\n", prev_fg_status.code);
            } else {
                printf("exit status 0\n");
            }
            fflush(stdout);
        } else {
            if (curr_command->is_bg && fg_only == false) {
                background_process(curr_command);
            } else {
                foreground_process(curr_command);
            }
        }

        free_command(curr_command);
    }

    return EXIT_SUCCESS;
}
