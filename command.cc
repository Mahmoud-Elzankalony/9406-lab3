
/*
 * 
 *
 * Template file.
 * You will need to add more code here to execute the command table.
 *
 * NOTE: You are responsible for fixing any bugs this code may have!
 *
 */
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
#include <wordexp.h>
#include <vector>
#include <string>
#include <cctype>
#include <iostream>
#include <sstream>
#include "command.h"
#include "tokenizer.h"
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <cstdlib>







void parse(std::vector<Token> &tokens)
{
    if (tokens.empty())
        return;

    Command currentCommand;
    SimpleCommand *currentSimpleCommand = new SimpleCommand();

    for (size_t i = 0; i < tokens.size(); i++)
    {
        Token token = tokens[i];

        switch (token.type)
        {
            case TOKEN_COMMAND:
            case TOKEN_ARGUMENT:
            {
                char *argument = strdup(token.value.c_str());

                if (argument == NULL)
                {
                    fprintf(stderr,
                            "Error: Memory allocation failed for argument.\n");
                    delete currentSimpleCommand;
                    return;
                }

                currentSimpleCommand->insertArgument(argument);
                break;
            }

            case TOKEN_PIPE:
            {
                if (currentSimpleCommand->_numberOfArguments == 0)
                {
                    fprintf(stderr,
                            "Syntax error: Missing command before '|'.\n");
                    delete currentSimpleCommand;
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
                    delete currentSimpleCommand;
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
                            "Syntax error: Invalid or missing filename after redirection '%s'.\n",
                            token.value.c_str());
                    delete currentSimpleCommand;
                    return;
                }

                char *filename = strdup(nextToken.value.c_str());

                if (filename == NULL)
                {
                    fprintf(stderr,
                            "Error: Memory allocation failed for filename.\n");
                    delete currentSimpleCommand;
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
                        free(currentCommand._outFile);
                        free(currentCommand._errFile);

                        currentCommand._outFile = filename;
                        currentCommand._errFile = strdup(filename);
                        currentCommand._append = 1;
                        currentCommand._out_error = 1;
                        break;

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
                            "Syntax error: '&' must appear at the end of the command.\n");
                    delete currentSimpleCommand;
                    return;
                }

                if (currentSimpleCommand->_numberOfArguments == 0 &&
                    currentCommand._numberOfSimpleCommands == 0)
                {
                    fprintf(stderr,
                            "Syntax error: Missing command before '&'.\n");
                    delete currentSimpleCommand;
                    return;
                }

                currentCommand._background = 1;
                break;
            }

            case TOKEN_EOF:
                break;

            default:
                fprintf(stderr, "Syntax error: Unexpected token.\n");
                delete currentSimpleCommand;
                return;
        }
    }

    if (currentSimpleCommand->_numberOfArguments > 0)
    {
        currentCommand.insertSimpleCommand(currentSimpleCommand);
    }
    else
    {
        delete currentSimpleCommand;

        if (currentCommand._numberOfSimpleCommands > 0)
        {
            fprintf(stderr,
                    "Syntax error: Missing command after '|'.\n");
            return;
        }
    }

    if (currentCommand._numberOfSimpleCommands == 0)
    {
        fprintf(stderr, "Syntax error: No command to execute.\n");
        return;
    }

    currentCommand.execute();
}


SimpleCommand::SimpleCommand()
{
    _numberOfAvailableArguments = 5;
    _numberOfArguments = 0;
    _arguments = (char **)malloc(_numberOfAvailableArguments * sizeof(char *));
}

void SimpleCommand::insertArgument(char *argument)
{
    if (_numberOfAvailableArguments == _numberOfArguments + 1)
    {
        _numberOfAvailableArguments *= 2;
        _arguments = (char **)realloc(_arguments,
                                      _numberOfAvailableArguments * sizeof(char *));
    }

    _arguments[_numberOfArguments] = argument;
    _arguments[_numberOfArguments + 1] = NULL;

    _numberOfArguments++;
}

Command::Command()
{
    _numberOfAvailableSimpleCommands = 1;
    _simpleCommands = (SimpleCommand **)
        malloc(_numberOfSimpleCommands * sizeof(SimpleCommand *));

    _numberOfSimpleCommands = 0;
    _outFile = 0;
    _inputFile = 0;
    _errFile = 0;
    _background = 0;
}

void Command::insertSimpleCommand(SimpleCommand *simpleCommand)
{
    if (_numberOfAvailableSimpleCommands == _numberOfSimpleCommands)
    {
        _numberOfAvailableSimpleCommands *= 2;
        _simpleCommands = (SimpleCommand **)realloc(_simpleCommands,
                                                    _numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));
    }

    _simpleCommands[_numberOfSimpleCommands] = simpleCommand;
    _numberOfSimpleCommands++;
}

void Command::clear()
{
    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++)
        {
            free(_simpleCommands[i]->_arguments[j]);
        }

        free(_simpleCommands[i]->_arguments);
        free(_simpleCommands[i]);
    }

    if (_outFile)
    {
        free(_outFile);
    }

    if (_inputFile)
    {
        free(_inputFile);
    }

    if (_errFile)
    {
        free(_errFile);
    }

    _numberOfSimpleCommands = 0;
    _outFile = 0;
    _inputFile = 0;
    _errFile = 0;
    _background = 0;
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
            fprintf(stderr,
                    "Error: Empty command at position %d.\n", i + 1);

            if (previousPipeRead != -1)
                close(previousPipeRead);

            for (size_t j = 0; j < childPids.size(); j++)
                waitpid(childPids[j], NULL, 0);

            return;
        }

        int pipefd[2] = {-1, -1};

        if (i < numberOfCommands - 1)
        {
            if (pipe(pipefd) == -1)
            {
                perror("Error: Failed to create pipe");

                if (previousPipeRead != -1)
                    close(previousPipeRead);

                for (size_t j = 0; j < childPids.size(); j++)
                    waitpid(childPids[j], NULL, 0);

                return;
            }
        }

        pid_t pid = fork();

        if (pid == -1)
        {
            perror("Error: Failed to create child process");

            if (previousPipeRead != -1)
                close(previousPipeRead);

            if (pipefd[0] != -1)
                close(pipefd[0]);

            if (pipefd[1] != -1)
                close(pipefd[1]);

            for (size_t j = 0; j < childPids.size(); j++)
            {
                while (waitpid(childPids[j], NULL, 0) == -1 &&
                       errno == EINTR)
                {
                }
            }

            return;
        }

        if (pid == 0)
        {
            // Connect the previous command to standard input.
            if (previousPipeRead != -1)
            {
                if (dup2(previousPipeRead, STDIN_FILENO) == -1)
                {
                    perror("Error: Failed to redirect pipe input");
                    _exit(1);
                }
            }

            // Connect this command to the next command.
            if (pipefd[1] != -1)
            {
                if (dup2(pipefd[1], STDOUT_FILENO) == -1)
                {
                    perror("Error: Failed to redirect pipe output");
                    _exit(1);
                }
            }

            if (previousPipeRead != -1)
                close(previousPipeRead);

            if (pipefd[0] != -1)
                close(pipefd[0]);

            if (pipefd[1] != -1)
                close(pipefd[1]);

            // Input redirection applies to the first command.
            if (i == 0 && _inputFile != NULL)
            {
                int fd = open(_inputFile, O_RDONLY);

                if (fd == -1)
                {
                    fprintf(stderr, "myshell: Cannot open input file '%s': ",
                            _inputFile);
                    perror(NULL);
                    _exit(1);
                }

                if (dup2(fd, STDIN_FILENO) == -1)
                {
                    perror("Error: Failed to redirect input");
                    close(fd);
                    _exit(1);
                }

                close(fd);
            }

            // Output redirection applies to the last command.
            if (i == numberOfCommands - 1 && _outFile != NULL)
            {
                int flags = O_WRONLY | O_CREAT;

                flags |= (_append ? O_APPEND : O_TRUNC);

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
                    perror("Error: Failed to redirect output");
                    close(fd);
                    _exit(1);
                }

                if (_out_error)
                {
                    if (dup2(fd, STDERR_FILENO) == -1)
                    {
                        perror("Error: Failed to redirect standard error");
                        close(fd);
                        _exit(1);
                    }
                }

                close(fd);
            }

            // Error redirection applies to the last command.
            if (i == numberOfCommands - 1 &&
                _errFile != NULL &&
                !_out_error)
            {
                int fd = open(
                    _errFile,
                    O_WRONLY | O_CREAT | O_TRUNC,
                    0666
                );

                if (fd == -1)
                {
                    fprintf(stderr, "myshell: Cannot open error file '%s': ",
                            _errFile);
                    perror(NULL);
                    _exit(1);
                }

                if (dup2(fd, STDERR_FILENO) == -1)
                {
                    perror("Error: Failed to redirect standard error");
                    close(fd);
                    _exit(1);
                }

                close(fd);
            }

            // Execute the command.
            execvp(
                _simpleCommands[i]->_arguments[0],
                _simpleCommands[i]->_arguments
            );

            int savedErrno = errno;

            fprintf(stderr, "myshell: %s: %s\n",
                    _simpleCommands[i]->_arguments[0],
                    strerror(savedErrno));

            _exit(savedErrno == ENOENT ? 127 : 126);
        }
        else
        {
            // Parent process.
            childPids.push_back(pid);

            if (previousPipeRead != -1)
                close(previousPipeRead);

            if (pipefd[1] != -1)
                close(pipefd[1]);

            previousPipeRead = pipefd[0];
        }
    }

    if (previousPipeRead != -1)
        close(previousPipeRead);

    // Wait for foreground commands only.
    if (!_background)
    {
        for (size_t i = 0; i < childPids.size(); i++)
        {
            pid_t result;

            do
            {
                result = waitpid(childPids[i], NULL, 0);
            }
            while (result == -1 && errno == EINTR);

            if (result == -1)
                perror("Error: waitpid failed");
        }
    }
    else
    {
        fprintf(stderr, "[Background process started: PID %d]\n",
                static_cast<int>(childPids[0]));
    }
}

void Command::prompt()
{
    printf("myshell>");
    fflush(stdout);
    std::string input;
    while (true)
    {
        std::getline(std::cin, input); 
        std::vector<Token> tokens = tokenize(input);
        parse(tokens);
    }
}

Command Command::_currentCommand;
SimpleCommand *Command::_currentSimpleCommand;

int main()
{
    Command::_currentCommand.prompt();
    return 0;
}
