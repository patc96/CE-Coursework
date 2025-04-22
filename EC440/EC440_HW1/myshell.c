#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

// Function to trim both leading and trailing spaces from a string
void trim_spaces(char *str) {
    char *end;
    end = str + strlen(str) - 1;
    while (end > str && *end == ' ') end--;  // Trim trailing spaces
    *(end + 1) = '\0';  // Null-terminate the string
}

// Function to split redirection symbols (e.g., '<file' -> '<' and 'file')
void split_redirection_symbols(char *command, char **args, int *arg_count) {
    char *redir_symbols[] = {"<", ">", ">>", "<<"};
    int num_symbols = sizeof(redir_symbols) / sizeof(redir_symbols[0]); //Finds the number of possible redirection symbols

    char *token = strtok(command, " ");
    while (token != NULL) {
        int handled = 0;
        for (int i = 0; i < num_symbols; i++) {
            // If the symbol is in the start of the token
            char *symbol = redir_symbols[i];
            char *found = strstr(token, symbol);
            if (found != NULL && found == token) {
                args[(*arg_count)++] = symbol;
                if (strlen(token) > strlen(symbol)) {
                    args[(*arg_count)++] = token + strlen(symbol);
                }
                handled = 1;
                break;
            } else if (found != NULL) {
                // If the symbol is in the middle of the token
                *found = '\0';
                args[(*arg_count)++] = token;
                args[(*arg_count)++] = symbol;
                args[(*arg_count)++] = found + strlen(symbol);
                handled = 1;
                break;
            }
        }
        if (!handled) {
            args[(*arg_count)++] = token;
        }
        token = strtok(NULL, " ");
    }
}

// Function to handle input/output redirection using stdin and stdout
void handle_redirection(char **args) {
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], ">") == 0) {  // Output redirection (overwrite)
            freopen(args[i + 1], "w", stdout);  // Redirect stdout to file (overwrite)
            args[i] = NULL;  // Remove the redirection symbol and filename
        } else if (strcmp(args[i], ">>") == 0) {  // Output redirection (append)
            freopen(args[i + 1], "a", stdout);  // Redirect stdout to file (append mode)
            args[i] = NULL;
        } else if (strcmp(args[i], "<") == 0) {  // Input redirection
            freopen(args[i + 1], "r", stdin);  // Redirect stdin from file
            args[i] = NULL;
        }
    }
}

// Execute a single command
void execute_command(char **args, int background) {
    pid_t pid = fork();
    if (pid == 0) {  // Child process
        handle_redirection(args);  // Handle redirection
        execvp(args[0], args);  // Execute the command
        perror("execvp failed");  // Handle errors in execvp
        exit(1);
    } else if (pid > 0) {  // Parent process
        if (!background) {
            wait(NULL);  // Wait for the child process if not in background
        }
    } else {
        perror("fork failed");
        exit(1);
    }
}

// Function to handle piping between commands
void execute_piped_commands(char **commands, int num_cmds) {
    int pipefds[2], input_fd = 0;  // file descriptor for pipes

    for (int i = 0; i < num_cmds; i++) {
        pipe(pipefds);  // Create a pipe

        pid_t pid = fork();
        if (pid == 0) {  // Child process
            dup2(input_fd, STDIN_FILENO);  // Get input from previous command
            if (i < num_cmds - 1) {
                dup2(pipefds[1], STDOUT_FILENO);  // Output to the next command
            }
            close(pipefds[0]);  // Close unused read end of the pipe
            char *args[32];
            int j = 0;
            split_redirection_symbols(commands[i], args, &j);
            args[j] = NULL;  // Null-terminate the args array
            handle_redirection(args);  // Handle redirection if needed
            execvp(args[0], args);  // Execute the command
            perror("execvp failed");
            exit(1);
        } else if (pid > 0) {  // Parent process
            wait(NULL);  // Parent waits for child
            close(pipefds[1]);  // Close write end of the pipe in parent
            input_fd = pipefds[0];  // Pass the read end to the next command
        } else {
            perror("fork failed");
            exit(1);
        }
    }
}

// Parse and execute the input, handling pipes and background processes
void parse_and_execute(char *input) {
    char *commands[10];
    int num_cmds = 0;
    int background = 0;

    trim_spaces(input);  // Trim the input string

    // Check if the command is a background process
    if (strchr(input, '&')) {
        background = 1;
        input[strlen(input) - 1] = '\0';  // Remove the '&' from the input
    }

    // Split the input by pipes into individual commands
    commands[num_cmds] = strtok(input, "|");
    while (commands[num_cmds] != NULL) {
        trim_spaces(commands[num_cmds]);  // Trim spaces around each command
        num_cmds++;
        commands[num_cmds] = strtok(NULL, "|");
    }

    // If there's only one command (no pipes), handle it normally
    if (num_cmds == 1) {
        char *args[32];
        int i = 0;
        split_redirection_symbols(commands[0], args, &i);
        args[i] = NULL;  // Null-terminate the args array
        execute_command(args, background);  // Execute single command
    } else {
        // Handle piped commands
        execute_piped_commands(commands, num_cmds);
    }
}

int main(int argc, char *argv[]) {
    char input[512];
    int suppress_prompt = 0;

    // Check if the '-n' argument is provided to suppress the shell prompt
    if (argc > 1 && strcmp(argv[1], "-n") == 0) {
        suppress_prompt = 1;
    }

    while (1) {
        if (!suppress_prompt) {
            printf("my_shell$ ");  // Print the shell prompt
        }
        if (fgets(input, 512, stdin) == NULL || strcmp(input, "exit\n") == 0) {
            break;  // Exit the shell on "exit" command
        }
        input[strcspn(input, "\n")] = '\0';  // Remove the newline character
        parse_and_execute(input);  // Parse and execute the command
    }

    return 0;
}
