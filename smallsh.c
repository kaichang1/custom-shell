#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
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
    char *input_file;
    char *output_file;
    int bg;
};

/** 
 * Get user command input.
 * 
 * Command syntax: command [arg1 arg2 ...] [< input_file] [> output_file] [&].
 * Input redirection may appear before or after output redirection.
 *
 * @param command The command_line structure to store user input
 */
void get_input(struct command_line *command) {
    char *user_input = calloc(MAXLENGTH + 1, sizeof *user_input);
    int arg_i = 0;
    // Get user input and store in user_input
    fgets(user_input, MAXLENGTH + 1, stdin);
    if ( (strlen(user_input) > 0) && (user_input[strlen(user_input) - 1]) == '\n' ) {
        user_input[strlen(user_input) - 1] = '\0';  // Remove trailing newline from user input
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
        if (!strcmp(token, ">")) {  // Next token is output file
            token = strtok(NULL, " ");
            command->output_file = token;
        }
        // Handle redirection of standard input
        else if (!strcmp(token, "<")) {  // Next token is input file
            token = strtok(NULL, " ");
            command->input_file = token;
        }
        // Handle command arguments
        else {
            command->args[arg_i] = token;
            arg_i++;
        }
        token = strtok(NULL, " ");
    }
    free(user_input);
}

int main() {
    while (1) {
        // Prompt
        printf(": ");
        fflush(stdout);
        // Get user input
        struct command_line command = {NULL, {NULL}, NULL, NULL, 0};
        get_input(&command);
    }
    return 0;
}
