#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>

#define MAXLENGTH 2048
#define MAXARGS 512

/**
 * Store the different elements of a command
 */
struct command_line {
    char *command;
    char *args[MAXARGS + 2]; // Extra arguments for command and NULL
    int free_args[MAXARGS];  // Arguments that must be freed
    char *input_file;
    char *output_file;
    int bg;  // 1 if process should run in background, else 0
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

int main() {
    int stop = 0;

    while (stop == 0) {
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
        clean_up(user_input, &command);
    }
    return 0;
}
