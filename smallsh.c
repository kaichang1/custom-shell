#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define MAXLENGTH 2048  // Max length of user input
#define MAXARGS 512  // Max number of command arguments
#define MAXBGPROCS 100  // Max number of background processes

/**
 * Store the different elements of a command
 */
struct command_line {
    char *command;
    char *args[MAXARGS + 2]; // Extra arguments for command and NULL
    int free_args[MAXARGS];  // Arguments that must be freed
    char *input_file;
    char *output_file;
    int bg;  // 1 if process should run in background, else 0 (Only applicable for non-built-in commands)
};

/** 
 * Perform variable expansion of "$$" -> PID in command arguments.
 *
 * @param string The string to expand
 * @param ptr Pointer to first occurrence of "$$" in the string
 * @return Expanded string allocated on heap memory. This must be freed by caller
 */
char * variable_expansion(char *string, char *ptr) {
    // Get PID and convert to string format
    pid_t pid = getpid();
    char *pid_str;
    int n = snprintf(NULL, 0, "%d", pid);  // Get length of formatted output
    pid_str = malloc((n + 1) * sizeof *pid_str);
    sprintf(pid_str, "%d", pid);  // Send formatted output to pid_str

    // Get front half of string, before "$$"
    size_t front_length = ptr - string;  // Get length from start of string to ptr
    char front[front_length + 1];
    strncpy(front, string, front_length);
    front[front_length] = '\0';
    // Get back half of string, after "$$"
    size_t back_length = &string[strlen(string)] - (ptr + 2);  // Get length from after "$$" to end of string
    char back[back_length + 1];
    strncpy(back, ptr + 2, back_length);
    back[back_length] = '\0';
    // Get combined expanded string
    int size = front_length + strlen(pid_str) + back_length;
    char *combined = calloc(size + 1, sizeof *combined);
    strcpy(combined, front);
    strcpy(combined + front_length, pid_str);
    strcpy(combined + front_length + strlen(pid_str), back);
    combined[size] = '\0';

    free(pid_str);
    return combined;
}

/** 
 * Get user input and store command elements in command_line structure.
 * 
 * Command syntax: command [arg1 arg2 ...] [< input_file] [> output_file] [&].
 * Input redirection may appear before or after output redirection.
 *
 * @param user_input User input
 * @param command The command_line structure to store user input
 */
void get_input(char *user_input, struct command_line *command) {
    // Get user input and store in user_input
    fgets(user_input, MAXLENGTH + 1, stdin);
    if ((strlen(user_input) > 0) && (user_input[strlen(user_input) - 1]) == '\n') {
        user_input[strlen(user_input) - 1] = '\0';  // Remove trailing newline from user input
    }
    // Ignore comments
    if (user_input[0] == '#') {
        free(user_input);
        return;
    }
    // Determine if command should be executed in the background
    if (strlen(user_input) >= 2) {
        char *last_two = &user_input[strlen(user_input) - 2];  // Get last two characters of user input
        if (!strcmp(last_two, " &")) {
            command->bg = 1;
            memset(&user_input[strlen(user_input) - 2], '\0', 2);  // Remove " &" from user input
        }
    }
    // Get command from user input
    int arg_i = 0;
    int free_i = 0;
    char *token = strtok(user_input, " ");
    if (token != NULL) {
        command->command = token;
        command->args[arg_i] = token;  // For use with exec()
        arg_i++;
    }
    // Continue parsing user input
    token = strtok(NULL, " ");
    while (token != NULL) {
        // Handle redirection of standard output
        if (!strcmp(token, ">")) {
            // Next token is output file
            token = strtok(NULL, " ");
            command->output_file = token;
        }
        // Handle redirection of standard input
        else if (!strcmp(token, "<")) {
            // Next token is input file
            token = strtok(NULL, " ");
            command->input_file = token;
        }
        // Handle command arguments
        else {
            char *ptr = strstr(token, "$$");
            // "$$" detected in token, perform variable expansion of $$ -> PID in token
            if (ptr != NULL) {
                command->args[arg_i] = variable_expansion(token, ptr);
                command->free_args[free_i] = arg_i;
                free_i++;
            }
            // "$$" not detected in token
            else {
                command->args[arg_i] = token;
            }
            arg_i++;
        }
        token = strtok(NULL, " ");
    }
}

/** 
 * Redirect stdin and stdout.
 * 
 * @param command The command_line structure to store user input
 */
void IO_redirection(struct command_line *command) {
    // Output redirection
    if (command->output_file != NULL) {
        // Open output file
        int target_fd = open(command->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (target_fd == -1) {
            fprintf(stderr, "Error opening output file\n");
            fflush(stdout);
            exit(1);
        }
        // Redirect stdout to output file
        int ret = dup2(target_fd, 1);
        if (ret == -1) {
            fprintf(stderr, "Error redirecting stdout\n");
            fflush(stdout);
            exit(1);
        }
    }
    // Input redirection
    if (command->input_file != NULL) {
        // Open input file
        int source_fd = open(command->input_file, O_RDONLY);
        if (source_fd == -1) {
            fprintf(stderr, "Error opening input file\n");
            fflush(stdout);
            exit(1);
        }
        // Redirect stdin to input file
        int ret = dup2(source_fd, 0);
        if (ret == -1) {
            fprintf(stderr, "Error redirecting stdin\n");
            fflush(stdout);
            exit(1);
        }
    }
}

/** 
 * Check if an array of non-zero integers is full.
 * 
 * Array elements are specified as non-zero because 0 is used to indicate an empty spot in the array.
 * 
 * @param arr Array to check
 * @param n Number of elements in array
 * @return 1 if array is full, else 0
 */
int space_check_arr(int *arr, int n) {
    for (int i = 0; i < n; i++) {
        if (arr[i] == 0) {
            return 0;
        }
    }
    return 1;
}

/** 
 * Add a child PID to an array of background processes.
 * 
 * @param bg_processes Array of background processes
 * @param pid Child PID to add
 */
void add_bg_process(int *bg_processes, int pid) {
    for (int i = 0; i < MAXBGPROCS; i++) {
        if (bg_processes[i] == 0) {
            bg_processes[i] = pid;
            return;
        }
    }
}

/** 
 * Remove a child PID from an array of background processes.
 * 
 * @param bg_processes Array of background processes
 * @param i Index of child PID to remove
 */
void remove_bg_process(int *bg_processes, int i) {
    bg_processes[i] = 0;
}

/** 
 * Reap potential zombies from an array of background processes.
 * 
 * @param bg_processes Array of background processes
 */
void reap_processes(int *bg_processes) {
    int child_status;
    pid_t child_pid;
    for (int i = 0; i < MAXBGPROCS; i++) {
        if (bg_processes[i] != 0) {
            child_pid = waitpid(bg_processes[i], &child_status, WNOHANG);
            if (child_pid != 0) {
                // Child with PID of bg_processes[i] has completed running
                remove_bg_process(bg_processes, i);
                printf("Background process with PID %d has completed running: exit value %d\n", child_pid, child_status);
                fflush(stdout);
            }
        }
    }
}

/** 
 * Perform clean-up after each prompt cycle.
 * 
 * @param user_input User input
 * @param command The command_line structure to store user input
 */
void clean_up(char *user_input, struct command_line *command) {
    free(user_input);
    for (int i = 0; command->free_args[i]; i++) {
        int free_i = command->free_args[i];
        free(command->args[free_i]);
    }
}

/** 
 * Process built-in commands
 *
 * @param command The command_line structure to store user input
 * @param stop Stop flag to exit program
 * @param fg_status Most recent foreground process status
 * @return 0 if built-in command was run, else 1 (command is not a build-in command)
 */
int process_built_ins(struct command_line *command, int *stop, int *fg_status) {
    // exit
    if (!strcmp(command->command, "exit")) {
        *stop = 1;
        return 0;
    }
    // cd
    else if (!strcmp(command->command, "cd")) {
        // No arguments: cd to home directory
        if (command->args[1] == NULL) {
            if (chdir(getenv("HOME")) != 0) {
                fprintf(stderr, "Unable to cd to home directory\n");
                fflush(stdout);
            }
        }
        // One argument: cd to specified directory
        else {
            if (chdir(command->args[1]) != 0) {
                fprintf(stderr, "Unable to cd to %s\n", command->args[1]);
                fflush(stdout);
            }
        }
        return 0;
    }
    // status
    else if (!strcmp(command->command, "status")) {
        printf("Exit value %d\n", *fg_status);
        fflush(stdout);
        return 0;
    }
    else {
        return 1;
    }
}

/** 
 * Execute non-built-in commands.
 * 
 * @param command The command_line structure to store user input
 * @param bg_processes Array of background processes
 */
void exec_cmd(struct command_line *command, int *bg_processes, int *fg_status) {
    if (command->bg == 1) {
        // Check and return if too many background processes are running
        if (space_check_arr(bg_processes, MAXBGPROCS)) {
            fprintf(stderr, "Error adding background process: too many processes are running\n");
            fflush(stdout);
            return;
        }
    }

    int child_status;
    pid_t child_pid = fork();
    switch(child_pid) {
        case -1:
                fprintf(stderr, "Error forking\n");
                fflush(stdout);
                exit(1);
                break;
        case 0:
                // Redirect background process I/O to /dev/null if unspecified
                if (command->bg == 1) {
                    if (command->input_file == NULL) {
                        command->input_file = "/dev/null";
                    }
                    if (command->output_file == NULL) {
                        command->output_file = "/dev/null";
                    }
                }
                // Child executes command
                IO_redirection(command);
                execvp(command->command, command->args);
                fprintf(stderr, "Error executing command\n");
                fflush(stdout);
                exit(1);
                break;
        default:
                // Process to be run in the background
                if (command->bg == 1) {
                    add_bg_process(bg_processes, child_pid);
                    waitpid(child_pid, &child_status, WNOHANG);
                    printf("Background process with PID %d is running\n", child_pid);
                    fflush(stdout);
                }
                // Process to be run in the foreground
                else {
                    child_pid = waitpid(child_pid, &child_status, 0);
                    if (WIFEXITED(child_status)) {
                        *fg_status = WEXITSTATUS(child_status);
                    }
                }
                break;
    }
}

int main() {
    int stop = 0;
    int fg_status = 0;  // Most recent foreground process status
    int bg_processes[MAXBGPROCS] = {0};  // Background processes that must be reaped

    while (stop == 0) {
        reap_processes(bg_processes);

        // Prompt
        printf(": ");
        fflush(stdout);

        // Get user input
        char *user_input = calloc(MAXLENGTH + 1, sizeof *user_input);
        struct command_line command = {NULL, {NULL}, {0}, NULL, NULL, 0};
        get_input(user_input, &command);
        // Ignore comments and empty commands
        if (command.command == NULL) {
            continue;
        }

        // Process commands
        if (process_built_ins(&command, &stop, &fg_status)) {  // Process built-in commands
            exec_cmd(&command, bg_processes, &fg_status);  // Execute other commands
        }

        clean_up(user_input, &command);
    }
    // TODO: Terminate all processes
    return 0;
}
