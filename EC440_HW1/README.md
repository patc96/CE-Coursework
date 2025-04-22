Description:
This project implements a custom shell program in C that supports basic command execution and piping. It can execute external commands using execvp() command, and parses arguments using "|" as the pipe character.

Process:
1) The trim_spaces() function is used to remove leading and trailing spaces from user input and command tokens. The function processes the input by trimming any spaces at the beginning and end, and null-terminating the string to make sure no unwanted characters remain.

2) The split_redirection_symbols() splits any input like <file or >file into separate arguments: the redirection operator and the filename.

3) The handle_redirection() function is responsible for setting up input/output redirection based on these parsed symbols. It redirects stdin and stdout as necessary using freopen() to open files for reading or writing.

4) The execute_command() function forks a new process to execute the parsed commands using execvp(). This ensures that the parent process can continue receiving user input while child processes handle command execution. If the user includes a background process by using the character "&", the parent does not wait for the child process to finish, allowing concurrent command execution.

4) The execute_piped_commands() function handles piping between commands. It forks a child process for each command, connecting their inputs and outputs via pipes. This ensures that the output of one command is passed as input to the next. The parent process waits for each command in the pipeline to finish before proceeding to the next.

5) The parse_and_execute() function handles the overall flow of parsing and executing user input. It checks for background processes and splits the input into separate commands using the pipe symbol |.
Each command is further processed to handle redirection, and the command(s) are executed either as standalone or piped commands, depending on the input.

Problems:
Redirection Without Spaces: A major issue was handling redirection operators (<, >, >>, <<) when they were combined with filenames without spaces (e.g., command<input.txt). This caused errors in redirection, as the symbols were not recognized correctly. The problem was solved by adding the split_redirection_symbols() function, which properly splits these combined symbols.