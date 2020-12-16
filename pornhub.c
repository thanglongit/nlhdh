#include <stdio.h>  // printf(), fgets()
#include <string.h> // strtok(), strcmp(), strdup()
#include <stdlib.h> // free()
#include <unistd.h> // fork()
#include <sys/types.h>
#include <sys/wait.h> // waitpid()
#include <sys/stat.h>
#include <fcntl.h> // open(), creat(), close()
#include <errno.h>

#define MAX_LINE_LENGTH 1024
#define MAX_LINE 80
#define REDIR_SIZE 2
#define PIPE_SIZE 3
#define MAX_HISTORY_SIZE 128
#define MAX_COMMAND_NAME_LENGTH 128
#define PROMPT_FORMAT "%F %T "
#define PROMPT_MAX_LENGTH 30
#define TOFILE_DIRECT ">"
#define APPEND_TOFILE_DIRECT ">>"
#define FROMFILE "<"
#define PIPE_OPT "|"
/**
 * @description: Hàm xóa dấu xuống dòng của một chuỗi
 * @param: line là một chuỗi các ký tự
 * @return: trả về một chuỗi đã được xóa dấu xuống dòng '\n'
 */
void remove_end_of_line(char *line) {
    int i = 0;
    while (line[i] != '\n') {
        i++;
    }
    line[i] = '\0';
}

// Readline
/**
 * @description: Hàm đọc chuỗi nhập từ bàn phím 
 * @param: line là một chuỗi các ký tự lưu chuỗi người dùng nhập vào
 * @return: none
 */
void read_line(char *line) {
    char *ret = fgets(line, MAX_LINE_LENGTH, stdin);
    // Định dạng lại chuỗi: xóa ký tự xuống dòng và đánh dấu vị trí '\n' bằng '\0' - kết thúc chuỗi
    remove_end_of_line(line);
    // Nếu so sánh thấy chuỗi đầu vào là "exit" hoặc "quit" hoặc là NULL thì kết thúc chương trình
    if (strcmp(line, "exit") == 0 || ret == NULL || strcmp(line, "quit") == 0) {
        exit(EXIT_SUCCESS);
    }
}
/**
 * @description: Hàm parse chuỗi input từ người dùng ra những argument
 * @param : input_string là chuỗi người dùng nhập vào, argv mảng chuỗi chứa những chuỗi arg, is_background cho biết lệnh có chạy nền hay không?
 * @return: none
 */
void parse_command(char *input_string, char **argv, int *wait) {
    int i = 0;
    while (i < MAX_LINE) {
        argv[i] = NULL;
        i++;
    }
    *wait = (input_string[strlen(input_string) - 1] == '&') ? 0 : 1; // Nếu có & thì wait = 0, ngược lại wait = 1
    input_string[strlen(input_string) - 1] = (*wait == 0) ? input_string[strlen(input_string) - 1] = '\0' : input_string[strlen(input_string) - 1];
    i = 0;
    argv[i] = strtok(input_string, " ");

    if (argv[i] == NULL) return;

    while (argv[i] != NULL) {
        i++;
        argv[i] = strtok(NULL, " ");
    }
    argv[i] = NULL;
}
/**
 * @description: kiểm tra có xuất hiện arg chuyển hướng IO xuất hiện trong mảng chuỗi arg hay không
 * @param  argv mảng chuỗi chứa những chuỗi arg
 * @return vị trí của chuỗi chuyển hướng IO hoặc 0 nếu không có
 */
int is_redirect(char **argv) {
    int i = 0;
    while (argv[i] != NULL) {
        if (strcmp(argv[i], TOFILE_DIRECT) == 0 || strcmp(argv[i], APPEND_TOFILE_DIRECT) == 0 || strcmp(argv[i], FROMFILE) == 0) {
            return i; // Có xuất hiện chuỗi chuyển hướng IO
        }
        i = -~i; // Tăng i lên một đơn vị
    }
    return 0; // Không có sự xuất hiện chuỗi chuyển hướng IO
}
/**
 * @description: kiểm tra có xuất hiện arg chuỗi giao tiếp Pipe xuất hiện trong mảng chuỗi arg hay không
 * @param argv mảng chuỗi chứa những chuỗi arg
 * @return vị trí của chuỗi giao tiếp Pipe hoặc 0 nếu không có
 */
int is_pipe(char **argv) {
    int i = 0;
    while (argv[i] != NULL) {
        if (strcmp(argv[i], PIPE_OPT) == 0) {
            return i; // Có xuất hiện chuỗi giao tiếp Pipe
        }
        i = -~i; // Tăng i lên một đơn vị
    }
    return 0; // Không có sự xuất hiện chuỗi giao tiếp Pipe
}
/**
 * @description: Hàm parse chuyển hướng IO từ mảng các chuỗi arg
 * @param: argv mảng chuỗi chứa những chuỗi arg, redirect_argv mảng chuỗi chứa những chuỗi để thực hiện lệnh chuyển hướng IO, redirect_index vị trí chuyển IO opt trong argv
 * @return none
 */
void parse_redirect(char **argv, char **redirect_argv, int redirect_index) {
    redirect_argv[0] = strdup(argv[redirect_index]);
    redirect_argv[1] = strdup(argv[redirect_index + 1]);
    argv[redirect_index] = NULL;
    argv[redirect_index + 1] = NULL;
}
/**
 * @description:  Hàm parse giao tiếp Pipe từ mảng các chuỗi arg
 * @param argv mảng chuỗi chứa những chuỗi arg, child01_argv mảng chuỗi chứa những chuỗi arg child 01, child02_argv mảng chuỗi chứa những chuỗi arg child 02, pipe_index vị trí pipe opt trong ags
 * @return
 */
void parse_pipe(char **argv, char **child01_argv, char **child02_argv, int pipe_index) {
    int i = 0;
    for (i = 0; i < pipe_index; i++) {
        child01_argv[i] = strdup(argv[i]);
    }
    child01_argv[i++] = NULL;

    while (argv[i] != NULL) {
        child02_argv[i - pipe_index - 1] = strdup(argv[i]);
        i++;
    }
    child02_argv[i - pipe_index - 1] = NULL;
}
// Execution
/**
 * @description: Hàm thực hiện lệnh child
 * @param argv mảng chuỗi chứa những chuỗi arg để truyền vào execvp (int execvp(const char *file, char *const argv[]);)
 * @return none
 */
void exec_child(char **argv) {
    if (execvp(argv[0], argv) < 0) {
        fprintf(stderr, "Error: Failed to execte command.\n");
        exit(EXIT_FAILURE);
    }
}
/**
 * @description Hàm thực hiện chuyển hướng đầu vào <
 * @param argv mảng chuỗi chứa những chuỗi arg, dir mảng chuỗi chứa những chuỗi con chứa các args đã parse bằng parse_redirect
 * @return none
 */
void exec_child_overwrite_from_file(char **argv, char **dir) {
    // osh>ls < out.txt
    int fd_in = open(dir[1], O_RDONLY);
    if (fd_in == -1) {
        perror("Error: Redirect input failed");
        exit(EXIT_FAILURE);
    }
    dup2(fd_in, STDIN_FILENO);
    if (close(fd_in) == -1) {
        perror("Error: Closing input failed");
        exit(EXIT_FAILURE);
    }
    exec_child(argv);
}
/**
 * @description Hàm thực hiện chuyển hướng đầu ra >
 * @param argv mảng chuỗi chứa những chuỗi arg, dir mảng chuỗi chứa những chuỗi con chứa các args đã parse bằng parse_redirect
 * @return none
 */
void exec_child_overwrite_to_file(char **argv, char **dir) {
    // osh>ls > out.txt
    int fd_out;
    fd_out = creat(dir[1], S_IRWXU);
    if (fd_out == -1) {
        perror("Error: Redirect output failed");
        exit(EXIT_FAILURE);
    }
    dup2(fd_out, STDOUT_FILENO);
    if (close(fd_out) == -1) {
        perror("Error: Closing output failed");
        exit(EXIT_FAILURE);
    }
    exec_child(argv);
}
/**
 * @description Hàm thực hiện chuyển hướng đầu ra >> (Append) nhưng mà đang lỗi, có lẽ tụi em sẽ update sau
 * @param argv mảng chuỗi chứa những chuỗi arg, dir mảng chuỗi chứa những chuỗi con chứa các args đã parse bằng parse_redirect
 * @return none*/
void exec_child_append_to_file(char **argv, char **dir) {
    // osh>ls >> out.txt
    int fd_out;
    if (access(dir[0], F_OK) != -1) {
        fd_out = open(dir[0], O_WRONLY | O_APPEND);
    }
    if (fd_out == -1) {
        perror("Error: Redirect output failed");
        exit(EXIT_FAILURE);
    }
    dup2(fd_out, STDOUT_FILENO);
    if (close(fd_out) == -1) {
        perror("Error: Closing output failed");
        exit(EXIT_FAILURE);
    }
    exec_child(argv);
}
/**
 * @description Hàm thực hiện giao tiếp hai lệnh thông qua Pipe
 * @param argv_in mảng các args của child 01, argv_out mảng các args của child 02
 * @return none
 */
void exec_child_pipe(char **argv_in, char **argv_out) {
    int fd[2];
    // p[0]: read end
    // p[1]: write end
    if (pipe(fd) == -1) {
        perror("Error: Pipe failed");
        exit(EXIT_FAILURE);
    }
    //child 1 exec input from main process
    //write to child 2
    if (fork() == 0) {
        dup2(fd[1], STDOUT_FILENO);
        close(fd[0]);
        close(fd[1]);
        exec_child(argv_in);
        exit(EXIT_SUCCESS);
    }
    //child 2 exec output from child 1
    //read from child 1
    if (fork() == 0) {
        dup2(fd[0], STDIN_FILENO);
        close(fd[1]);
        close(fd[0]);
        exec_child(argv_out);
        exit(EXIT_SUCCESS);
    }
    close(fd[0]);
    close(fd[1]);
    wait(0);
    wait(0);    
}
void set_prev_command(char *history, char *line) {
    strcpy(history, line);
}
/**
 * @description Hàm lấy lệnh trước đó
 * @param history chuỗi history
 * @return none
 */
char *get_prev_command(char *history) {
    if (history[0] == '\0') {
        fprintf(stderr, "No commands in history\n");
        return NULL;
    }
    return history;
}
// Built-in: Implement builtin functions để thực hiện vài lệnh cơ bản như cd (change directory), demo custome help command
/*
  Function Declarations for builtin shell commands:
 */
int simple_shell_cd(char **args);
void exec_command(char **args, char **redir_argv, int wait, int res);

// List of builtin commands
char *builtin_str[] = {
    "cd",
    "help",
    "exit"
};
// Corresponding functions.
int (*builtin_func[])(char **) = {
    &simple_shell_cd,
};
int simple_shell_num_builtins() {
    return sizeof(builtin_str) / sizeof(char *);
}
// Implement - Cài đặt
/**
 * @description Hàm cd (change directory) bằng cách gọi hàm chdir()
 * @param argv mảng chuỗi chứa những chuỗi arg để thực hiện lệnh
 * @return 0 nếu thất bại, 1 nếu thành công
 */
int simple_shell_cd(char **argv) {
    if (argv[1] == NULL) {
        fprintf(stderr, "Error: Expected argument to \"cd\"\n");
    } else {
        // Change the process's working directory to PATH.
        if (chdir(argv[1]) != 0) {
            perror("Error: Error when change the process's working directory to PATH.");
        }
    }
    return 1;
}
/**
 * @description Hàm thoát
 * @param args mảng chuỗi chứa những chuỗi arg để thực hiện lệnh
 * @return
 */
int simple_shell_history(char *history, char **redir_args) {
    char *cur_args[MAX_LINE];
    char cur_command[MAX_LINE_LENGTH];
    int t_wait;
    if (history[0] == '\0') {
        fprintf(stderr, "No commands in history\n");
        return 1;
    }
    strcpy(cur_command, history);
    printf("%s\n", cur_command);
    parse_command(cur_command, cur_args, &t_wait);
    int res = 0;
    exec_command(cur_args, redir_args, t_wait, res);
    return res;
}
/*
 * @description Hàm thực thi chuyển hướng IO
 * @param args mảng chuỗi chứa những chuỗi arg để thực hiện lệnh, redir_argv mảng chuỗi chứa những chuỗi arg để thực hiện lệnh chuyển hướng IO
 * @return 0 nếu không thực hiện chuyển tiếp IO, 1 nếu đã thực hiện chuyển tiếp IO
 */
int simple_shell_redirect(char **args, char **redir_argv) {
    // printf("%s", "Executing redirect\n");
    int redir_op_index = is_redirect(args);
    // printf("%d", redir_op_index);
    if (redir_op_index != 0) {
        parse_redirect(args, redir_argv, redir_op_index);
        if (strcmp(redir_argv[0], ">") == 0) {
            exec_child_overwrite_to_file(args, redir_argv);
        } else if (strcmp(redir_argv[0], "<") == 0) {
            exec_child_overwrite_from_file(args, redir_argv);
        } else if (strcmp(redir_argv[0], ">>") == 0) {
            exec_child_append_to_file(args, redir_argv);
        }
        return 1;
    }
    return 0;
}
/**
 * @description Hàm thực thi pipe
 * @param  args mảng chuỗi chứa những chuỗi arg để thực hiện lệnh
 * @return 0 nếu không thực hiện giao tiếp pipe, 1 nếu thực hiện giao tiếp pipe
 */
int simple_shell_pipe(char **args) {
    int pipe_op_index = is_pipe(args);
    if (pipe_op_index != 0) {  
        // printf("%s", "Exec Pipe");
        char *child01_arg[PIPE_SIZE];
        char *child02_arg[PIPE_SIZE];   
        parse_pipe(args, child01_arg, child02_arg, pipe_op_index);
        exec_child_pipe(child01_arg, child02_arg);
        return 1;
    }
    return 0;
}
/**
 * @description Hàm thực thi lệnh
 * @param 
 * @return
 */
void exec_command(char **args, char **redir_argv, int wait, int res) {
    // Kiểm tra có trùng với lệnh nào trong mảng builtin command không, có thì thực thi, không thì xuống tiếp dưới
    for (int i = 0; i < simple_shell_num_builtins(); i++) {
        if (strcmp(args[0], builtin_str[i]) == 0) {
            (*builtin_func[i])(args);
            res = 1;
        }
    }
    // Chưa thực thi builtin commands
    if (res == 0) {
        int status;
        // Tạo tiến trình con
        pid_t pid = fork();
        if (pid == 0) {
            // Child process
            if (res == 0) res = simple_shell_redirect(args, redir_argv);
            if (res == 0) res = simple_shell_pipe(args);
            if (res == 0) execvp(args[0], args);
            exit(EXIT_SUCCESS);
        } else if (pid < 0) { // Khi mà việc tạo tiến trình con bị lỗi
            perror("Error: Error forking");
            exit(EXIT_FAILURE);
        } else { // Thực thi chạy nền
            // Parent process
            // printf("[LOGGING] Parent pid = <%d> spawned a child pid = <%d>.\n", getpid(), pid);
            if (wait == 1) {
                waitpid(pid, &status, WUNTRACED); // 
            }
        }
    }
}
/**
 * @description Hàm main :))
 * @param void không có gì
 * @return 0 nếu hết chương trình
 */
int main(void) {
    // Mảng chưa các agrs
    char *args[MAX_LINE];
    // Chuỗi line
    char line[MAX_LINE_LENGTH];
    // Chuỗi sao chép từ line
    char t_line[MAX_LINE_LENGTH];
    // Chuỗi lưu trữ lịch sử
    char history[MAX_LINE_LENGTH] = "No commands in history";
    // Mảng chứa agrs để thực tthi chuyển hướng IO
    char *redir_argv[REDIR_SIZE];
    // Check xem có chạy nền không
    int wait;
    // Khởi tạo banner shell
    int res = 0;
    int run = 1;
    // Khởi tạo một vòng lặp vô hạn
    while (run) {
        printf("tuanlm>");
        fflush(stdout);
        // Đọc chuuỗi nhận vào từ người dùng
        read_line(line);
        // Sao chép chuỗi
        strcpy(t_line, line);
        // Parser chuỗi input
        parse_command(line, args, &wait);
        // Thực thi lệnh
        if (strcmp(args[0], "!!") == 0) {
            res = simple_shell_history(history, redir_argv);
        } else {
            set_prev_command(history, t_line);
            exec_command(args, redir_argv, wait, res);
        }
        res = 0;
    }
    return 0;
}
