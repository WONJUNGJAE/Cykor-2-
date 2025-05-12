#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <stdbool.h>

#define max_command 1024
#define max_nums 64
#define max_pipes 10
#define max_history 100
#define DELIMS " \t\r\n"


char history[max_history][max_command];
int history_count = 0;

int token(char* str, char* tokens[]);
void cd(char* args[]);
void pwd();
void pipeline(char* commands[], int cmd_count);
void command(char* tokens[], bool background, int* last_status);
void handle_multi_command(char* line);
void split_commands(char* line, char* commands[], char* operators[]);
void handle_history();

int last_command_status = 0;

int main() {
    char cmd[max_command];

    while (1) {
        
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("\033[1;32m%s\033[0m$ ", cwd);
        }
        else {
            perror("getcwd() error");
            exit(EXIT_FAILURE);
        }

        
        if (!fgets(cmd, sizeof(cmd), stdin)) break;

        
        if (strcmp(cmd, "exit\n") == 0) break;

        
        if (cmd[0] != '\n') {
            if (history_count < max_history) {
                strncpy(history[history_count++], cmd, max_command - 1);
            }
            else {
                
                for (int i = 1; i < max_history; i++)
                    strcpy(history[i - 1], history[i]);
                strncpy(history[max_history - 1], cmd, max_command - 1);
            }
        }

        
        if (strncmp(cmd, "history", 7) == 0 &&
            (cmd[7] == '\n' || cmd[7] == ' ' || cmd[7] == '\0')) {
            handle_history();
            continue;
        }

        
        handle_multi_command(cmd);
    }
    return 0;
}

int token(char* str, char* tokens[]) {
    int count = 0;
    char* token = strtok(str, DELIMS);

    while (token && count < max_nums - 1) {
        tokens[count++] = token;
        token = strtok(NULL, DELIMS);
    }
    tokens[count] = NULL;
    return count;
}

void cd(char* args[]) {
    if (args[1] == NULL) {
        chdir(getenv("HOME"));
    }
    else {
        if (chdir(args[1]) != 0) {
            perror("cd error");
            last_command_status = 1;
        }
        else {
            last_command_status = 0;
        }
    }
}

void pwd() {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
        last_command_status = 0;
    }
    else {
        perror("pwd error");
        last_command_status = 1;
    }
}

void pipeline(char* commands[], int cmd_count) {
    int pipes[max_pipes][2];
    pid_t pids[max_pipes];
    int i, j;

    for (i = 0; i < cmd_count - 1; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe error");
            return;
        }
    }

    for (i = 0; i < cmd_count; i++) {
        pids[i] = fork();
        if (pids[i] == 0) {
            
            if (i > 0) {
                dup2(pipes[i - 1][0], STDIN_FILENO);
                close(pipes[i - 1][0]);
                close(pipes[i - 1][1]);
            }
            if (i < cmd_count - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
                close(pipes[i][0]);
                close(pipes[i][1]);
            }
            
            for (j = 0; j < cmd_count - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            
            char* args[max_nums];
            token(commands[i], args);
            execvp(args[0], args);
            perror("execvp error");
            exit(EXIT_FAILURE);
        }
    }

    
    for (i = 0; i < cmd_count - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    
    for (i = 0; i < cmd_count; i++) {
        waitpid(pids[i], NULL, 0);
    }
}

void command(char* tokens[], bool background, int* last_status) {
    int token_count = 0;
    while (tokens[token_count] != NULL) token_count++;
    if (token_count > 0 && strcmp(tokens[token_count - 1], "&") == 0) {
        tokens[token_count - 1] = NULL;
        background = true;
    }
    if (strcmp(tokens[0], "cd") == 0) {
        cd(tokens);
        return;
    }
    if (strcmp(tokens[0], "pwd") == 0) {
        pwd();
        return;
    }
    if (strcmp(tokens[0], "history") == 0) {
        handle_history();
        return;
    }
    pid_t pid = fork();
    if (pid == 0) {
        execvp(tokens[0], tokens);
        perror("execvp error");
        exit(EXIT_FAILURE);
    }
    else if (pid > 0) {
        if (!background) {
            int status;
            waitpid(pid, &status, 0);
            *last_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        }
        else {
            printf("[백그라운드 PID: %d]\n", pid);
            signal(SIGCHLD, SIG_IGN);
            *last_status = 0;
        }
    }
    else {
        perror("fork error");
        *last_status = 1;
    }
}


void split_commands(char* line, char* commands[], char* operators[]) {
    int i = 0, j = 0, k = 0;
    while (line[i] != '\0') {
        if ((line[i] == '&' && line[i + 1] == '&') ||
            (line[i] == '|' && line[i + 1] == '|')) {
            line[i] = '\0';
            commands[k] = &line[j];
            operators[k] = (line[i + 1] == '&') ? "&&" : "||";
            k++;
            i += 2;
            j = i;
        }
        else if (line[i] == ';') {
            line[i] = '\0';
            commands[k] = &line[j];
            operators[k] = ";";
            k++;
            i++;
            j = i;
        }
        else {
            i++;
        }
    }
    if (j < i) {
        commands[k] = &line[j];
        operators[k] = NULL;
        k++;
    }
    commands[k] = NULL;
    operators[k] = NULL;
}

void handle_multi_command(char* line) {
    char* commands[max_nums];
    char* operators[max_nums];
    split_commands(line, commands, operators);

    for (int i = 0; commands[i] != NULL; i++) {
        
        if (strstr(commands[i], "|") != NULL) {
            char* pipe_commands[max_pipes + 1];
            int pipe_count = 0;
            char* token = strtok(commands[i], "|");
            while (token != NULL && pipe_count < max_pipes) {
                pipe_commands[pipe_count++] = token;
                token = strtok(NULL, "|");
            }
            pipe_commands[pipe_count] = NULL;
            pipeline(pipe_commands, pipe_count);
            last_command_status = 0; 
            continue;
        }
        char* args[max_nums];
        int arg_count = token(commands[i], args);
        if (arg_count == 0) continue;
        if (i == 0 || (operators[i - 1] && strcmp(operators[i - 1], ";") == 0)) {
            command(args, false, &last_command_status);
        }
        else if (operators[i - 1] && strcmp(operators[i - 1], "&&") == 0) {
            if (last_command_status == 0)
                command(args, false, &last_command_status);
        }
        else if (operators[i - 1] && strcmp(operators[i - 1], "||") == 0) {
            if (last_command_status != 0)
                command(args, false, &last_command_status);
        }
    }
}

void handle_history() {
    for (int i = 0; i < history_count; i++) {
        printf("%4d  %s", i + 1, history[i]);
        int len = strlen(history[i]);
        if (len == 0 || history[i][len - 1] != '\n') {
            printf("\n");
        }
    }
}