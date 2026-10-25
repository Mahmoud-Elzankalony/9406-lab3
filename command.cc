#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <glob.h>
#include <vector>
#include <string>
#include <iostream>
#include "command.h"
#include "tokenizer.h"

volatile sig_atomic_t childSignalReceived = 0;

void handleChildSignal(int)
{
    childSignalReceived = 1;
}

void installSignalHandlers()
{
    struct sigaction action = {};

    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);

    if (sigaction(SIGINT, &action, NULL) == -1)
    {
        perror("sigaction SIGINT");
        exit(1);
    }

    action.sa_handler = handleChildSignal;
    action.sa_flags = SA_NOCLDSTOP | SA_RESTART; // منع قطع std::getline بواسطة SIGCHLD
    sigemptyset(&action.sa_mask);

    if (sigaction(SIGCHLD, &action, NULL) == -1)
    {
        perror("sigaction SIGCHLD");
        exit(1);
    }
}

void logChildExit(pid_t pid, int status)
{
    FILE *file = fopen("myshell.log", "a");

    if (file == NULL)
    {
        perror("myshell.log");
        return;
    }

    time_t now = time(NULL);
    struct tm timeInfo;
    char timeText[30];

    if (localtime_r(&now, &timeInfo) != NULL)
    {
        strftime(timeText, sizeof(timeText),
                 "%Y-%m-%d %H:%M:%S", &timeInfo);
    }
    else
    {
        strcpy(timeText, "unknown time");
    }

    if (WIFEXITED(status))
    {
        fprintf(file, "[%s] PID %d exited with status %d\n",
                timeText, (int)pid, WEXITSTATUS(status));
    }
    else if (WIFSIGNALED(status))
    {
        fprintf(file, "[%s] PID %d terminated by signal %d\n",
                timeText, (int)pid, WTERMSIG(status));
    }

    fclose(file);
}

void reapBackgroundChildren()
{
    childSignalReceived = 0;

    int status;
    pid_t pid;

    while (true)
    {
        pid = waitpid(-1, &status, WNOHANG);

        if (pid > 0)
        {
            logChildExit(pid, status);
        }
        else if (pid == -1 && errno == EINTR)
        {
            continue;
        }
        else
        {
            break;
        }
    }
}


std::vector<std::string> expandWildcard(const char *pattern)
{
    glob_t matches = {};
    std::vector<std::string> result;

    int status = glob(pattern, 0, NULL, &matches);

    if (status == 0 && matches.gl_pathc > 0)
    {
        for (size_t i = 0; i < matches.gl_pathc; i++)
        {
            result.push_back(matches.gl_pathv[i]);
        }
    }
    else
    {
        result.push_back(pattern);
    }

    globfree(&matches);

    return result;
}

bool lexInput(const std::string &input, std::vector<Token> &tokens)
{
    tokens.clear();

    size_t i = 0;
    bool expectingCommand = true;

    while (i < input.size())
    {
        if (input[i] == ' ' || input[i] == '\t' ||
            input[i] == '\n' || input[i] == '\r')
        {
            i++;
            continue;
        }

        Token token;
        bool isOperator = true;

        if (input.compare(i, 3, ">>&") == 0)
        {
            token = {TOKEN_REDIRECT_AND_ERROR, ">>&"};
            i += 3;
        }
        else if (input.compare(i, 2, ">>") == 0)
        {
            token = {TOKEN_APPEND, ">>"};
            i += 2;
        }
        else if (input.compare(i, 2, "2>") == 0)
        {
            token = {TOKEN_ERROR, "2>"};
            i += 2;
        }
        else if (input[i] == '>')
        {
            token = {TOKEN_REDIRECT, ">"};
            i++;
        }
        else if (input[i] == '<')
        {
            token = {TOKEN_INPUT, "<"};
            i++;
        }
        else if (input[i] == '|')
        {
            token = {TOKEN_PIPE, "|"};
            i++;
            expectingCommand = true;
        }
        else if (input[i] == '&')
        {
            token = {TOKEN_BACKGROUND, "&"};
            i++;
        }
        else
        {
            isOperator = false;

            std::string word;
            char quote = '\0';

            while (i < input.size())
            {
                char c = input[i];

                if (quote == '\0' &&
                    (c == ' ' || c == '\t' || c == '\n' ||
                     c == '\r' || c == '|' || c == '&' ||
                     c == '<' || c == '>'))
                {
                    break;
                }

                if (c == '\\' && i + 1 < input.size() &&
                    quote != '\'')
                {
                    word += input[i + 1];
                    i += 2;
                    continue;
                }

                if (quote == '\0' && (c == '"' || c == '\''))
                {
                    quote = c;
                    i++;
                    continue;
                }

                if (quote != '\0' && c == quote)
                {
                    quote = '\0';
                    i++;
                    continue;
                }

                word += c;
                i++;
            }

            if (quote != '\0')
            {
                fprintf(stderr,
                        "Syntax error: Unclosed quotation mark.\n");
                return false;
            }

            if (word.empty())
            {
                fprintf(stderr, "Syntax error: Invalid token.\n");
                return false;
            }

            token.type = expectingCommand ? TOKEN_COMMAND : TOKEN_ARGUMENT;
            token.value = word;
            expectingCommand = false;
        }

        tokens.push_back(token);

        if (isOperator &&
            token.type != TOKEN_PIPE &&
            token.type != TOKEN_BACKGROUND)
        {
            continue;
        }
    }

    tokens.push_back({TOKEN_EOF, ""});
    return true;
}

void parse(std::vector<Token> &tokens)
{
    if (tokens.empty())
        return;

    Command currentCommand;
    SimpleCommand *currentSimpleCommand = new SimpleCommand();

    auto cleanup = [&]()
    {
        if (currentSimpleCommand != NULL)
        {
            for (int j = 0;
                 j < currentSimpleCommand->_numberOfArguments; j++)
            {
                free(currentSimpleCommand->_arguments[j]);
            }

            free(currentSimpleCommand->_arguments);
            delete currentSimpleCommand;
            currentSimpleCommand = NULL;
        }

        currentCommand.clear();
    };

    for (size_t i = 0; i < tokens.size(); i++)
    {
        Token token = tokens[i];

        switch (token.type)
        {
            case TOKEN_COMMAND:
            case TOKEN_ARGUMENT:
            {
                std::vector<std::string> arguments =
                    expandWildcard(token.value.c_str());

                for (const std::string &arg : arguments)
                {
                    char *argument = strdup(arg.c_str());

                    if (argument == NULL)
                    {
                        perror("strdup");
                        cleanup();
                        return;
                    }

                    currentSimpleCommand->insertArgument(argument);
                }

                break;
            }

            case TOKEN_PIPE:
            {
                if (currentSimpleCommand->_numberOfArguments == 0)
                {
                    fprintf(stderr,
                            "Syntax error: Missing command before '|'.\n");
                    cleanup();
                    return;
                }

                currentCommand.insertSimpleCommand(currentSimpleCommand);
                currentSimpleCommand = new SimpleCommand();
                break;
            }

            case TOKEN_REDIRECT:
            case TOKEN_APPEND:
            case TOKEN_INPUT:
            case TOKEN_ERROR:
            case TOKEN_REDIRECT_AND_ERROR:
            {
                if (i + 1 >= tokens.size())
                {
                    fprintf(stderr,
                            "Syntax error: Missing filename after redirection.\n");
                    cleanup();
                    return;
                }

                Token nextToken = tokens[i + 1];

                if (nextToken.type == TOKEN_EOF ||
                    nextToken.type == TOKEN_PIPE ||
                    nextToken.type == TOKEN_BACKGROUND ||
                    nextToken.type == TOKEN_REDIRECT ||
                    nextToken.type == TOKEN_APPEND ||
                    nextToken.type == TOKEN_INPUT ||
                    nextToken.type == TOKEN_ERROR ||
                    nextToken.type == TOKEN_REDIRECT_AND_ERROR)
                {
                    fprintf(stderr,
                            "Syntax error: Invalid or missing filename after '%s'.\n",
                            token.value.c_str());
                    cleanup();
                    return;
                }

                char *filename = strdup(nextToken.value.c_str());

                if (filename == NULL)
                {
                    perror("strdup");
                    cleanup();
                    return;
                }

                i++;

                switch (token.type)
                {
                    case TOKEN_REDIRECT:
                        free(currentCommand._outFile);
                        currentCommand._outFile = filename;
                        currentCommand._append = 0;
                        currentCommand._out_error = 0;
                        break;

                    case TOKEN_APPEND:
                        free(currentCommand._outFile);
                        currentCommand._outFile = filename;
                        currentCommand._append = 1;
                        currentCommand._out_error = 0;
                        break;

                    case TOKEN_INPUT:
                        free(currentCommand._inputFile);
                        currentCommand._inputFile = filename;
                        break;

                    case TOKEN_ERROR:
                        free(currentCommand._errFile);
                        currentCommand._errFile = filename;
                        break;

                    case TOKEN_REDIRECT_AND_ERROR:
                    {
                        char *errorFilename = strdup(filename);

                        if (errorFilename == NULL)
                        {
                            perror("strdup");
                            free(filename);
                            cleanup();
                            return;
                        }

                        free(currentCommand._outFile);
                        free(currentCommand._errFile);

                        currentCommand._outFile = filename;
                        currentCommand._errFile = errorFilename;
                        currentCommand._append = 1;
                        currentCommand._out_error = 1;
                        break;
                    }

                    default:
                        free(filename);
                        break;
                }

                break;
            }

            case TOKEN_BACKGROUND:
            {
                if (i + 1 < tokens.size() &&
                    tokens[i + 1].type != TOKEN_EOF)
                {
                    fprintf(stderr,
                            "Syntax error: '&' must appear at the end.\n");
                    cleanup();
                    return;
                }

                if (currentSimpleCommand->_numberOfArguments == 0 &&
                    currentCommand._numberOfSimpleCommands == 0)
                {
                    fprintf(stderr,
                            "Syntax error: Missing command before '&'.\n");
                    cleanup();
                    return;
                }

                currentCommand._background = 1;
                break;
            }

            case TOKEN_EOF:
                break;

            default:
                fprintf(stderr, "Syntax error: Unexpected token.\n");
                cleanup();
                return;
        }
    }

    if (currentSimpleCommand->_numberOfArguments > 0)
    {
        currentCommand.insertSimpleCommand(currentSimpleCommand);
        currentSimpleCommand = NULL;
    }
    else
    {
        if (currentSimpleCommand != NULL)
        {
            free(currentSimpleCommand->_arguments);
            delete currentSimpleCommand;
            currentSimpleCommand = NULL;
        }

        if (currentCommand._numberOfSimpleCommands > 0)
        {
            fprintf(stderr,
                    "Syntax error: Missing command after '|'.\n");
            cleanup();
            return;
        }
    }

    if (currentCommand._numberOfSimpleCommands == 0)
    {
        currentCommand.clear();
        return;
    }

    SimpleCommand *cmd = currentCommand._simpleCommands[0];

    bool hasRedirection =
        currentCommand._outFile != NULL ||
        currentCommand._inputFile != NULL ||
        currentCommand._errFile != NULL;

    if (currentCommand._numberOfSimpleCommands == 1 &&
        !hasRedirection &&
        !currentCommand._background)
    {
        char *name = cmd->_arguments[0];

        if (strcmp(name, "cd") == 0)
        {
            if (cmd->_numberOfArguments > 2)
            {
                fprintf(stderr, "cd: too many arguments\n");
            }
            else
            {
                const char *path =
                    cmd->_numberOfArguments == 2
                    ? cmd->_arguments[1]
                    : getenv("HOME");

                if (path == NULL)
                {
                    fprintf(stderr, "cd: HOME is not set\n");
                }
                else if (chdir(path) == -1)
                {
                    perror("cd");
                }
            }

            currentCommand.clear();
            return;
        }

        if (strcmp(name, "exit") == 0)
        {
            if (cmd->_numberOfArguments > 1)
            {
                fprintf(stderr, "exit: too many arguments\n");
                currentCommand.clear();
                return;
            }

            currentCommand.clear();
            printf("Good bye!!\n");
            exit(0);
        }
    }

    currentCommand.print();
    currentCommand.execute();
    currentCommand.clear();
}

SimpleCommand::SimpleCommand()
{
    _numberOfAvailableArguments = 5;
    _numberOfArguments = 0;
    _arguments = (char **)malloc(
        _numberOfAvailableArguments * sizeof(char *));

    if (_arguments == NULL)
    {
        perror("malloc");
        exit(1);
    }

    _arguments[0] = NULL;
}

void SimpleCommand::insertArgument(char *argument)
{
    if (_numberOfAvailableArguments <= _numberOfArguments + 1)
    {
        int newCapacity = _numberOfAvailableArguments * 2;

        char **newArguments = (char **)realloc(
            _arguments, newCapacity * sizeof(char *));

        if (newArguments == NULL)
        {
            perror("realloc");
            free(argument);
            return;
        }

        _arguments = newArguments;
        _numberOfAvailableArguments = newCapacity;
    }

    _arguments[_numberOfArguments] = argument;
    _numberOfArguments++;
    _arguments[_numberOfArguments] = NULL;
}

Command::Command()
{
    _numberOfAvailableSimpleCommands = 1;
    _numberOfSimpleCommands = 0;

    _simpleCommands = (SimpleCommand **)malloc(
        _numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));

    if (_simpleCommands == NULL)
    {
        perror("malloc");
        exit(1);
    }

    _outFile = NULL;
    _inputFile = NULL;
    _errFile = NULL;
    _background = 0;
    _append = 0;
    _out_error = 0;
}

void Command::insertSimpleCommand(SimpleCommand *simpleCommand)
{
    if (_numberOfSimpleCommands >= _numberOfAvailableSimpleCommands)
    {
        int newCapacity =
            _numberOfAvailableSimpleCommands == 0
            ? 1
            : _numberOfAvailableSimpleCommands * 2;

        SimpleCommand **newCommands = (SimpleCommand **)realloc(
            _simpleCommands,
            newCapacity * sizeof(SimpleCommand *));

        if (newCommands == NULL)
        {
            perror("realloc");
            return;
        }

        _simpleCommands = newCommands;
        _numberOfAvailableSimpleCommands = newCapacity;
    }

    _simpleCommands[_numberOfSimpleCommands] = simpleCommand;
    _numberOfSimpleCommands++;
}

void Command::clear()
{
    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        if (_simpleCommands[i] == NULL)
            continue;

        for (int j = 0;
             j < _simpleCommands[i]->_numberOfArguments; j++)
        {
            free(_simpleCommands[i]->_arguments[j]);
        }

        free(_simpleCommands[i]->_arguments);
        delete _simpleCommands[i];
    }

    free(_simpleCommands);
    _simpleCommands = NULL;

    free(_outFile);
    free(_inputFile);
    free(_errFile);

    _numberOfAvailableSimpleCommands = 0;
    _numberOfSimpleCommands = 0;

    _outFile = NULL;
    _inputFile = NULL;
    _errFile = NULL;
    _background = 0;
    _append = 0;
    _out_error = 0;
}

void Command::print()
{
    printf("\n\n");
    printf("              COMMAND TABLE                \n");
    printf("\n");
    printf("  #   Simple Commands\n");
    printf("  --- ----------------------------------------------------------\n");

    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        printf("  %-3d ", i);
        for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++)
        {
            printf("\"%s\" \t", _simpleCommands[i]->_arguments[j]);
        }
    }

    printf("\n\n");
    printf("  Output       Input        Error        Err&Out       Background\n");
    printf("  ------------ ------------ ------------ ------------ ------------\n");
    printf("  %-12s %-12s %-12s %-12s %-12s\n", _outFile ? _outFile : "default",
           _inputFile ? _inputFile : "default", _errFile ? _errFile : "default", _out_error == 1 ? _errFile : "default"
           ,_background ? "YES" : "NO");
    printf("\n\n");
}


void Command::execute()
{
    if (_numberOfSimpleCommands == 0)
    {
        fprintf(stderr, "Error: No command to execute.\n");
        return;
    }

    int numberOfCommands = _numberOfSimpleCommands;
    int previousPipeRead = -1;
    std::vector<pid_t> childPids;

    for (int i = 0; i < numberOfCommands; i++)
    {
        if (_simpleCommands[i] == NULL ||
            _simpleCommands[i]->_numberOfArguments == 0 ||
            _simpleCommands[i]->_arguments[0] == NULL)
        {
            fprintf(stderr, "Error: Empty command at position %d.\n", i + 1);

            if (previousPipeRead != -1)
                close(previousPipeRead);

            for (pid_t child : childPids)
            {
                while (waitpid(child, NULL, 0) == -1 && errno == EINTR)
                {
                }
            }

            return;
        }

        int pipefd[2] = {-1, -1};

        if (i < numberOfCommands - 1 && pipe(pipefd) == -1)
        {
            perror("pipe");

            if (previousPipeRead != -1)
                close(previousPipeRead);

            for (pid_t child : childPids)
            {
                while (waitpid(child, NULL, 0) == -1 && errno == EINTR)
                {
                }
            }

            return;
        }

        pid_t pid = fork();

        if (pid == -1)
        {
            perror("fork");

            if (previousPipeRead != -1)
                close(previousPipeRead);

            if (pipefd[0] != -1)
                close(pipefd[0]);

            if (pipefd[1] != -1)
                close(pipefd[1]);

            for (pid_t child : childPids)
            {
                int status;
                pid_t result;

                do
                {
                    result = waitpid(child, &status, 0);
                }
                while (result == -1 && errno == EINTR);

                if (result > 0)
                    logChildExit(result, status);
            }

            return;
        }

        if (pid == 0)
        {
            signal(SIGINT, SIG_DFL);
            signal(SIGCHLD, SIG_DFL);

            if (previousPipeRead != -1 &&
                dup2(previousPipeRead, STDIN_FILENO) == -1)
            {
                perror("dup2 pipe input");
                _exit(1);
            }

            if (pipefd[1] != -1 &&
                dup2(pipefd[1], STDOUT_FILENO) == -1)
            {
                perror("dup2 pipe output");
                _exit(1);
            }

            if (previousPipeRead != -1)
                close(previousPipeRead);

            if (pipefd[0] != -1)
                close(pipefd[0]);

            if (pipefd[1] != -1)
                close(pipefd[1]);

            if (i == 0)
            {
                int fd = -1;
                if (_inputFile != NULL)
                {
                    fd = open(_inputFile, O_RDONLY);
                    if (fd == -1)
                    {
                        fprintf(stderr, "myshell: Cannot open input file '%s': ",
                                _inputFile);
                        perror(NULL);
                        _exit(1);
                    }
                }
                else if (_background)
                {
                    fd = open("/dev/null", O_RDONLY);
                    if (fd == -1)
                    {
                        perror("open /dev/null");
                        _exit(1);
                    }
                }

                if (fd != -1)
                {
                    if (dup2(fd, STDIN_FILENO) == -1)
                    {
                        perror("dup2 input");
                        close(fd);
                        _exit(1);
                    }
                    close(fd);
                }
            }

            if (i == numberOfCommands - 1 && _outFile != NULL)
            {
                int flags = O_WRONLY | O_CREAT;
                flags |= _append ? O_APPEND : O_TRUNC;

                int fd = open(_outFile, flags, 0666);

                if (fd == -1)
                {
                    fprintf(stderr, "myshell: Cannot open output file '%s': ",
                            _outFile);
                    perror(NULL);
                    _exit(1);
                }

                if (dup2(fd, STDOUT_FILENO) == -1)
                {
                    perror("dup2 output");
                    close(fd);
                    _exit(1);
                }

                if (_out_error && dup2(fd, STDERR_FILENO) == -1)
                {
                    perror("dup2 stderr");
                    close(fd);
                    _exit(1);
                }

                close(fd);
            }

            if (i == numberOfCommands - 1 &&
                _errFile != NULL && !_out_error)
            {
                int fd = open(_errFile,
                              O_WRONLY | O_CREAT | O_TRUNC, 0666);

                if (fd == -1)
                {
                    fprintf(stderr, "myshell: Cannot open error file '%s': ",
                            _errFile);
                    perror(NULL);
                    _exit(1);
                }

                if (dup2(fd, STDERR_FILENO) == -1)
                {
                    perror("dup2 stderr");
                    close(fd);
                    _exit(1);
                }

                close(fd);
            }

            execvp(_simpleCommands[i]->_arguments[0],
                   _simpleCommands[i]->_arguments);

            int savedErrno = errno;

            fprintf(stderr, "myshell: %s: %s\n",
                    _simpleCommands[i]->_arguments[0],
                    strerror(savedErrno));

            _exit(savedErrno == ENOENT ? 127 : 126);
        }

        childPids.push_back(pid);

        if (previousPipeRead != -1)
            close(previousPipeRead);

        if (pipefd[1] != -1)
            close(pipefd[1]);

        previousPipeRead = pipefd[0];
    }

    if (previousPipeRead != -1)
        close(previousPipeRead);

    if (!_background)
    {
        for (pid_t child : childPids)
        {
            int status;
            pid_t result;

            do
            {
                result = waitpid(child, &status, 0);
            }
            while (result == -1 && errno == EINTR);

            if (result == -1)
            {
                perror("waitpid");
            }
            else
            {
                logChildExit(result, status);
            }
        }
    }
    else if (!childPids.empty())
    {
        fprintf(stderr, "[Background process started: PID %d]\n",
                (int)childPids[0]);
    }
}

void Command::prompt()
{
    std::string input;

    while (true)
    {
        reapBackgroundChildren();

        printf("myshell>");
        fflush(stdout);

        if (!std::getline(std::cin, input))
        {
            if (std::cin.eof())
                break;

            std::cin.clear();
            continue;
        }

        std::vector<Token> tokens;

        if (!lexInput(input, tokens))
            continue;

        parse(tokens);
    }

    reapBackgroundChildren();
}

Command Command::_currentCommand;
SimpleCommand *Command::_currentSimpleCommand = NULL;

int main()
{
    installSignalHandlers();
    Command::_currentCommand.prompt();
    return 0;
}