
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
                currentSimpleCommand->insertArgument(argument);
                break;
            }

            case TOKEN_PIPE:
            {
                if (currentSimpleCommand->_numberOfArguments == 0)
                {
                    printf("Syntax error: Empty command before pipe\n");
                    delete currentSimpleCommand;
                    currentCommand.clear();
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
                if (i + 1 >= tokens.size() ||
                    tokens[i + 1].type == TOKEN_EOF ||
                    tokens[i + 1].type == TOKEN_PIPE ||
                    tokens[i + 1].type == TOKEN_BACKGROUND ||
                    tokens[i + 1].type == TOKEN_REDIRECT ||
                    tokens[i + 1].type == TOKEN_APPEND ||
                    tokens[i + 1].type == TOKEN_INPUT ||
                    tokens[i + 1].type == TOKEN_ERROR ||
                    tokens[i + 1].type == TOKEN_REDIRECT_AND_ERROR)
                {
                    printf("Syntax error: Missing redirection filename\n");
                    delete currentSimpleCommand;
                    currentCommand.clear();
                    return;
                }

                char *filename = strdup(tokens[++i].value.c_str());

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
                if (i + 1 < tokens.size() - 1)
                {
                    printf("Syntax error: Unexpected token after '&'\n");
                    delete currentSimpleCommand;
                    currentCommand.clear();
                    return;
                }

                currentCommand._background = 1;
                break;
            }

            case TOKEN_EOF:
                break;
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
            printf("Syntax error: Missing command after pipe\n");
            currentCommand.clear();
            return;
        }
    }

    if (currentCommand._numberOfSimpleCommands > 0)
    {
        currentCommand.execute();
    }
    else
    {
        currentCommand.clear();
    }
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
        clear();
        return;
    }

    int numberOfCommands = _numberOfSimpleCommands;
    int previousPipeRead = -1;
    std::vector<pid_t> childPids;

    for (int i = 0; i < numberOfCommands; i++)
    {
        int pipefd[2] = {-1, -1};

        if (i < numberOfCommands - 1)
        {
            if (pipe(pipefd) == -1)
            {
                perror("pipe");

                if (previousPipeRead != -1)
                    close(previousPipeRead);

                clear();
                return;
            }
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

            if (!_background)
            {
                for (size_t j = 0; j < childPids.size(); j++)
                {
                    while (waitpid(childPids[j], NULL, 0) == -1 &&
                           errno == EINTR)
                    {
                    }
                }
            }

            clear();
            return;
        }

        if (pid == 0)
        {
            if (previousPipeRead != -1)
            {
                if (dup2(previousPipeRead, STDIN_FILENO) == -1)
                {
                    perror("dup2");
                    _exit(1);
                }
            }

            if (pipefd[1] != -1)
            {
                if (dup2(pipefd[1], STDOUT_FILENO) == -1)
                {
                    perror("dup2");
                    _exit(1);
                }
            }

            if (previousPipeRead != -1)
                close(previousPipeRead);

            if (pipefd[0] != -1)
                close(pipefd[0]);

            if (pipefd[1] != -1)
                close(pipefd[1]);

            if (i == 0 && _inputFile != NULL)
            {
                int fd = open(_inputFile, O_RDONLY);

                if (fd == -1)
                {
                    perror(_inputFile);
                    _exit(1);
                }

                if (dup2(fd, STDIN_FILENO) == -1)
                {
                    perror("dup2");
                    close(fd);
                    _exit(1);
                }

                close(fd);
            }

            if (i == numberOfCommands - 1 && _outFile != NULL)
            {
                int flags = O_WRONLY | O_CREAT;

                if (_append)
                    flags |= O_APPEND;
                else
                    flags |= O_TRUNC;

                int fd = open(_outFile, flags, 0666);

                if (fd == -1)
                {
                    perror(_outFile);
                    _exit(1);
                }

                if (dup2(fd, STDOUT_FILENO) == -1)
                {
                    perror("dup2");
                    close(fd);
                    _exit(1);
                }

                if (_out_error)
                {
                    if (dup2(fd, STDERR_FILENO) == -1)
                    {
                        perror("dup2");
                        close(fd);
                        _exit(1);
                    }
                }

                close(fd);
            }

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
                    perror(_errFile);
                    _exit(1);
                }

                if (dup2(fd, STDERR_FILENO) == -1)
                {
                    perror("dup2");
                    close(fd);
                    _exit(1);
                }

                close(fd);
            }

            if (_simpleCommands[i]->_numberOfArguments == 0 ||
                _simpleCommands[i]->_arguments[0] == NULL)
            {
                _exit(1);
            }

            execvp(
                _simpleCommands[i]->_arguments[0],
                _simpleCommands[i]->_arguments
            );

            perror(_simpleCommands[i]->_arguments[0]);
            _exit(127);
        }
        else
        {
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
                perror("waitpid");
        }
    }

    clear();
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
