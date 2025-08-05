#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <errno.h>

#define MAX_INPUT 1000
#define MAX_ARGS 100
#define MAX_JOBS 100

typedef struct Job {
    int index;
    pid_t pid;
    char command[MAX_INPUT];
} Job;

Job jobs[MAX_JOBS];
int job_count = 0;
pid_t shell_pgid;
struct termios shell_tmodes;
pid_t foreground_pid = 0;

void print_prompt() {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("[nyush %s]$ ", strrchr(cwd, '/') ? strrchr(cwd, '/') + 1 : "/");
        fflush(stdout);
    } else {
        fprintf(stderr, "Error: getcwd failed\n");
    }
}

void add_job(pid_t pid, const char *command) {
    if (job_count < MAX_JOBS) {
        jobs[job_count].index = job_count + 1;
        jobs[job_count].pid = pid;
        strncpy(jobs[job_count].command, command, MAX_INPUT);
        job_count++;
    }
}

void remove_job(int index) {
    for (int i = index; i < job_count - 1; i++) {
        jobs[i] = jobs[i + 1];
        jobs[i].index = i + 1;
    }
    job_count--;
}

void handle_jobs() {
    if (job_count == 0) {
        return;
    }
    for (int i = 0; i < job_count; i++) {
        printf("[%d] %s\n", jobs[i].index, jobs[i].command);
    }
}

void handle_fg(int job_index) {
    pid_t pid = jobs[job_index - 1].pid;
    char command[MAX_INPUT];
    strncpy(command, jobs[job_index - 1].command, MAX_INPUT);
    remove_job(job_index - 1);

    tcsetpgrp(STDIN_FILENO, pid);
    kill(-pid, SIGCONT);

    int status;
    foreground_pid = pid;
    waitpid(pid, &status, WUNTRACED);

    tcsetpgrp(STDIN_FILENO, shell_pgid);
    foreground_pid = 0;

    if (WIFSTOPPED(status)) {
        add_job(pid, command);
    }
}

void sigint_handler(int sig) {
    if (foreground_pid != 0) {
        kill(-foreground_pid, SIGINT);
    }
}

void sigtstp_handler(int sig) {
    if (foreground_pid != 0) {
        kill(-foreground_pid, SIGTSTP);
    }
}

void sigchld_handler(int sig) {
    pid_t pid;
    int status;
    (void)sig;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < job_count; i++) {
            if (jobs[i].pid == pid) {
                remove_job(i);
                break;
            }
        }
    }
}

void setup_signal_handlers() {
    struct sigaction sa_int, sa_tstp, sa_chld;

    sa_int.sa_handler = sigint_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa_int, NULL);

    sa_tstp.sa_handler = sigtstp_handler;
    sigemptyset(&sa_tstp.sa_mask);
    sa_tstp.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &sa_tstp, NULL);

    sa_chld.sa_handler = sigchld_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa_chld, NULL);
}

int is_builtin_command(char *cmd) {
    return strcmp(cmd, "cd") == 0 || strcmp(cmd, "exit") == 0 ||
           strcmp(cmd, "jobs") == 0 || strcmp(cmd, "fg") == 0;
}

int validate_command(char **args, int argc) {
    if (argc == 0) {
        return 1;
    }
    if (strcmp(args[0], "|") == 0 || strcmp(args[argc - 1], "|") == 0) {
        fprintf(stderr, "Error: invalid command\n");
        return 0;
    }
    for (int i = 0; i < argc - 1; i++) {
        if (strcmp(args[i], "|") == 0 && strcmp(args[i + 1], "|") == 0) {
            fprintf(stderr, "Error: invalid command\n");
            return 0;
        }
    }
    int cmd_start = 0;
    for (int i = 0; i <= argc; i++) {
        if (i == argc || strcmp(args[i], "|") == 0) {
            if (cmd_start == i) {
                fprintf(stderr, "Error: invalid command\n");
                return 0;
            }
            int redirect_in = 0;
            int redirect_out = 0;
            for (int j = cmd_start; j < i; j++) {
                if (strcmp(args[j], "<") == 0 || strcmp(args[j], ">") == 0 || strcmp(args[j], ">>") == 0) {
                    if (j + 1 >= i || strcmp(args[j + 1], "|") == 0 ||
                        strcmp(args[j + 1], "<") == 0 || strcmp(args[j + 1], ">") == 0 ||
                        strcmp(args[j + 1], ">>") == 0) {
                        fprintf(stderr, "Error: invalid command\n");
                        return 0;
                    }
                    if (strcmp(args[j], "<") == 0) {
                        redirect_in++;
                        if (redirect_in > 1 || cmd_start != 0) {
                            fprintf(stderr, "Error: invalid command\n");
                            return 0;
                        }
                    } else {
                        redirect_out++;
                        if (redirect_out > 1 || i != argc) {
                            fprintf(stderr, "Error: invalid command\n");
                            return 0;
                        }
                    }
                }
            }
            if (is_builtin_command(args[cmd_start])) {
                if (i != argc || i - cmd_start > 2) {
                    fprintf(stderr, "Error: invalid command\n");
                    return 0;
                }
                for (int j = cmd_start + 1; j < i; j++) {
                    if (strcmp(args[j], "<") == 0 || strcmp(args[j], ">") == 0 ||
                        strcmp(args[j], ">>") == 0 || strcmp(args[j], "|") == 0) {
                        fprintf(stderr, "Error: invalid command\n");
                        return 0;
                    }
                }
                int builtin_argc = i - cmd_start;
                if (strcmp(args[cmd_start], "cd") == 0) {
                    if (builtin_argc != 2) {
                        fprintf(stderr, "Error: invalid command\n");
                        return 0;
                    }
                } else if (strcmp(args[cmd_start], "exit") == 0) {
                    if (builtin_argc != 1) {
                        fprintf(stderr, "Error: invalid command\n");
                        return 0;
                    }
                } else if (strcmp(args[cmd_start], "jobs") == 0) {
                    if (builtin_argc != 1) {
                        fprintf(stderr, "Error: invalid command\n");
                        return 0;
                    }
                } else if (strcmp(args[cmd_start], "fg") == 0) {
                    if (builtin_argc != 2) {
                        fprintf(stderr, "Error: invalid command\n");
                        return 0;
                    }
                }
            }
            cmd_start = i + 1;
        }
    }
    return 1;
}

int main() {
    setenv("PATH", "", 1);

    shell_pgid = getpid();
    setpgid(shell_pgid, shell_pgid);
    tcsetpgrp(STDIN_FILENO, shell_pgid);
    tcgetattr(STDIN_FILENO, &shell_tmodes);

    setup_signal_handlers();

    while (1) {
        print_prompt();

        char input[MAX_INPUT];
        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("\n");
            break;
        }

        input[strcspn(input, "\n")] = 0;
        if (strlen(input) == 0) {
            continue;
        }

        char input_copy[MAX_INPUT];
        strcpy(input_copy, input);

        char *args[MAX_ARGS];
        int argc = 0;
        char *token = strtok(input, " ");
        while (token != NULL && argc < MAX_ARGS - 1) {
            args[argc++] = token;
            token = strtok(NULL, " ");
        }
        args[argc] = NULL;

        int background = 0;
        if (argc > 0 && strcmp(args[argc - 1], "&") == 0) {
            background = 1;
            args[--argc] = NULL;
        }

        if (!validate_command(args, argc)) {
            continue;
        }

        if (argc > 0 && is_builtin_command(args[0])) {
            if (strcmp(args[0], "exit") == 0) {
                if (argc != 1) {
                    fprintf(stderr, "Error: invalid command\n");
                    continue;
                }
                if (job_count > 0) {
                    fprintf(stderr, "Error: there are suspended jobs\n");
                    continue;
                }
                break;
            } else if (strcmp(args[0], "cd") == 0) {
                if (argc != 2) {
                    fprintf(stderr, "Error: invalid command\n");
                } else if (chdir(args[1]) != 0) {
                    fprintf(stderr, "Error: invalid directory\n");
                }
            } else if (strcmp(args[0], "jobs") == 0) {
                if (argc != 1) {
                    fprintf(stderr, "Error: invalid command\n");
                } else {
                    handle_jobs();
                }
            } else if (strcmp(args[0], "fg") == 0) {
                if (argc != 2) {
                    fprintf(stderr, "Error: invalid command\n");
                } else {
                    char *endptr;
                    long job_index = strtol(args[1], &endptr, 10);
                    if (*endptr != '\0' || job_index <= 0 || job_index > job_count) {
                        fprintf(stderr, "Error: invalid job\n");
                    } else {
                        handle_fg((int)job_index);
                    }
                }
            }
            continue;
        }

        int num_pipes = 0;
        for (int i = 0; i < argc; i++) {
            if (strcmp(args[i], "|") == 0) {
                num_pipes++;
            }
        }

        int pipefds[2 * num_pipes];
        for (int i = 0; i < num_pipes; i++) {
            if (pipe(pipefds + i * 2) < 0) {
                fprintf(stderr, "Error: pipe failed\n");
                exit(EXIT_FAILURE);
            }
        }

        int cmd_start = 0;
        int pipe_index = 0;
        pid_t pid;
        int status;
        int cmd_count = num_pipes + 1;
        pid_t pids[cmd_count];

        for (int i = 0; i <= argc; i++) {
            if (i == argc || strcmp(args[i], "|") == 0) {
                int cmd_end = i;
                args[cmd_end] = NULL;

                pid = fork();
                if (pid == 0) {
                    setpgid(0, 0);

                    if (!background) {
                        tcsetpgrp(STDIN_FILENO, getpid());
                    }

                    for (int k = cmd_start; k < cmd_end; k++) {
                        if (args[k] == NULL) continue;
                        if (strcmp(args[k], "<") == 0) {
                            if (k + 1 >= cmd_end) {
                                fprintf(stderr, "Error: invalid command\n");
                                exit(1);
                            }
                            int input_fd = open(args[k + 1], O_RDONLY);
                            if (input_fd < 0) {
                                fprintf(stderr, "Error: invalid file\n");
                                exit(1);
                            }
                            dup2(input_fd, STDIN_FILENO);
                            close(input_fd);
                            args[k] = NULL;
                            args[k + 1] = NULL;
                        }
                    }

                    for (int k = cmd_start; k < cmd_end; k++) {
                        if (args[k] == NULL) continue;
                        if (strcmp(args[k], ">") == 0 || strcmp(args[k], ">>") == 0) {
                            if (k + 1 >= cmd_end) {
                                fprintf(stderr, "Error: invalid command\n");
                                exit(1);
                            }
                            int flags = O_WRONLY | O_CREAT;
                            if (strcmp(args[k], ">") == 0) {
                                flags |= O_TRUNC;
                            } else {
                                flags |= O_APPEND;
                            }
                            int output_fd = open(args[k + 1], flags, 0644);
                            if (output_fd < 0) {
                                fprintf(stderr, "Error: invalid file\n");
                                exit(1);
                            }
                            dup2(output_fd, STDOUT_FILENO);
                            close(output_fd);
                            args[k] = NULL;
                            args[k + 1] = NULL;
                        }
                    }

                    if (pipe_index != 0) {
                        dup2(pipefds[(pipe_index - 1) * 2], STDIN_FILENO);
                    }
                    if (pipe_index < num_pipes) {
                        dup2(pipefds[pipe_index * 2 + 1], STDOUT_FILENO);
                    }

                    for (int j = 0; j < 2 * num_pipes; j++) {
                        close(pipefds[j]);
                    }

                    int arg_idx = 0;
                    char *cmd_args[MAX_ARGS];
                    for (int k = cmd_start; k < cmd_end; k++) {
                        if (args[k] != NULL) {
                            cmd_args[arg_idx++] = args[k];
                        }
                    }
                    cmd_args[arg_idx] = NULL;

                    if (cmd_args[0] == NULL) {
                        fprintf(stderr, "Error: invalid command\n");
                        exit(1);
                    }

                    if (strchr(cmd_args[0], '/') == NULL) {
                        char full_path[MAX_INPUT];
                        snprintf(full_path, sizeof(full_path), "/usr/bin/%s", cmd_args[0]);
                        execv(full_path, cmd_args);
                    } else {
                        execv(cmd_args[0], cmd_args);
                    }
                    fprintf(stderr, "Error: invalid program\n");
                    exit(1);
                } else if (pid > 0) {
                    setpgid(pid, pid);
                    pids[pipe_index] = pid;
                    if (!background && pipe_index == 0) {
                        foreground_pid = pid;
                    }
                } else {
                    fprintf(stderr, "Error: fork failed\n");
                    exit(1);
                }

                cmd_start = i + 1;
                pipe_index++;
            }
        }

        for (int j = 0; j < 2 * num_pipes; j++) {
            close(pipefds[j]);
        }

        if (!background) {
            for (int i = 0; i < cmd_count; i++) {
                waitpid(pids[i], &status, WUNTRACED);
            }
            tcsetpgrp(STDIN_FILENO, shell_pgid);
            foreground_pid = 0;
            if (WIFSTOPPED(status)) {
                add_job(pids[0], input_copy);
            }
        } else {
            printf("[Process id %d]\n", pids[0]);
            add_job(pids[0], input_copy);
        }
    }
    return 0;
}
