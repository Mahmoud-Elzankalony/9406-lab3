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

void sigchld_handler(int)
{
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        FILE *log = fopen("myshell.log", "a");
        if (log)
        {
            time_t now = time(NULL);
            char timeStr[30];
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", localtime(&now));
            if (WIFEXITED(status))
                fprintf(log, "[%s] PID %d exited with status %d\n", timeStr, pid, WEXITSTATUS(status));
            fclose(log);
        }
    }
}

std::vector<std::string> expandWildcard(const char *pattern)
{
    glob_t results;
    std::vector<std::string> list;
    if (glob(pattern, 0, NULL, &results) == 0)
    {
        for (size_t i = 0; i < results.gl_pathc; i++)
            list.push_back(results.gl_pathv[i]);
    }
    else
    {
        list.push_back(pattern);
    }
    globfree(&results);
    return list;
}

void parse(std::vector<Token> &tokens)
{
    if (tokens.empty() || tokens[0].type == TOKEN_EOF)
        return;

    std::vector<Token> fixedTokens;
    for (size_t i = 0; i < tokens.size(); i++)
    {
        Token t = tokens[i];
        std::string val = t.value;

        if (!val.empty() && (val[0] == '"' || val[0] == '\'') && (val.back() != val[0] || val.size() == 1))
        {
            char quote = val[0];
            std::string combined = val;
            while (i + 1 < tokens.size())
            {
                i++;
                combined += " " + tokens[i].value;
                if (!tokens[i].value.empty() && tokens[i].value.back() == quote)
                    break;
            }
            t.value = combined;
        }
        fixedTokens.push_back(t);
    }

    Command currentCommand;
    SimpleCommand *currentSimpleCommand = new SimpleCommand();

    for (size_t i = 0; i < fixedTokens.size(); i++)
    {
        Token token = fixedTokens[i];

        if (token.type == TOKEN_COMMAND || token.type == TOKEN_ARGUMENT)
        {
            std::string val = token.value;
            if (!(val.front() == '"' && val.back() == '"') && !(val.front() == '\'' && val.back() == '\''))
            {
                std::vector<std::string> expanded = expandWildcard(val.c_str());
                for (const std::string &arg : expanded)
                {
                    currentSimpleCommand->insertArgument(strdup(arg.c_str()));
                }
            }
            else
            {
                if (val.size() >= 2) val = val.substr(1, val.size() - 2);
                currentSimpleCommand->insertArgument(strdup(val.c_str()));
            }
        }
        else if (token.type == TOKEN_PIPE)
        {
            currentCommand.insertSimpleCommand(currentSimpleCommand);
            currentSimpleCommand = new SimpleCommand();
        }
        else if (token.type == TOKEN_REDIRECT || token.type == TOKEN_APPEND || 
                 token.type == TOKEN_INPUT || token.type == TOKEN_ERROR || 
                 token.type == TOKEN_REDIRECT_AND_ERROR)
        {
            if (i + 1 < fixedTokens.size())
            {
                char *filename = strdup(fixedTokens[i + 1].value.c_str());
                i++;
                if (token.type == TOKEN_REDIRECT) 
                { currentCommand._outFile = filename; currentCommand._append = 0; }
                else if (token.type == TOKEN_APPEND) 
                { currentCommand._outFile = filename; currentCommand._append = 1; }
                else if (token.type == TOKEN_INPUT) 
                { currentCommand._inputFile = filename; }
                else if (token.type == TOKEN_ERROR) 
                { currentCommand._errFile = filename; }
                else if (token.type == TOKEN_REDIRECT_AND_ERROR) {
                    currentCommand._outFile = filename;
                    currentCommand._errFile = strdup(filename);
                    currentCommand._append = 1;
                    currentCommand._out_error = 1;
                }
            }
        }
        else if (token.type == TOKEN_BACKGROUND)
        {
            currentCommand._background = 1;
        }
    }

    if (currentSimpleCommand->_numberOfArguments > 0)
    {
        currentCommand.insertSimpleCommand(currentSimpleCommand);
    }
    else
    {
        delete currentSimpleCommand;
    }

    if (currentCommand._numberOfSimpleCommands > 0)
    {
        SimpleCommand *firstCmd = currentCommand._simpleCommands[0];
        if (currentCommand._numberOfSimpleCommands == 1 && !currentCommand._outFile && !currentCommand._inputFile && !currentCommand._background)
        {
            if (strcmp(firstCmd->_arguments[0], "exit") == 0)
            {
                printf("Good bye!!\n");
                exit(0);
            }
            if (strcmp(firstCmd->_arguments[0], "cd") == 0)
            {
                const char *dir = (firstCmd->_numberOfArguments > 1) ? firstCmd->_arguments[1] : getenv("HOME");
                if (chdir(dir) < 0) 
                perror("cd");
                currentCommand.clear();
                return;
            }
        }

        currentCommand.execute();
    }
    currentCommand.clear();
}

SimpleCommand::SimpleCommand()
{
    _numberOfAvailableArguments = 5;
    _numberOfArguments = 0;
    _arguments = (char **)malloc(_numberOfAvailableArguments * sizeof(char *));
    _arguments[0] = NULL;
}

void SimpleCommand::insertArgument(char *argument)
{
    if (_numberOfAvailableArguments == _numberOfArguments + 1)
    {
        _numberOfAvailableArguments *= 2;
        _arguments = (char **)realloc(_arguments, _numberOfAvailableArguments * sizeof(char *));
    }
    _arguments[_numberOfArguments] = argument;
    _arguments[_numberOfArguments + 1] = NULL;
    _numberOfArguments++;
}

Command::Command()
{
    _numberOfAvailableSimpleCommands = 1;
    _simpleCommands = (SimpleCommand **)malloc(_numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));
    _numberOfSimpleCommands = 0;
    _outFile = 0;
    _inputFile = 0;
    _errFile = 0;
    _background = 0;
    _append = 0;
    _out_error = 0;
}

void Command::insertSimpleCommand(SimpleCommand *simpleCommand)
{
    if (_numberOfAvailableSimpleCommands == _numberOfSimpleCommands)
    {
        _numberOfAvailableSimpleCommands *= 2;
        _simpleCommands = (SimpleCommand **)realloc(_simpleCommands, _numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));
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
    free(_outFile);
    if (_inputFile) 
    free(_inputFile);
    if (_errFile) 
    free(_errFile);

    _numberOfSimpleCommands = 0;
    _outFile = 0;
    _inputFile = 0;
    _errFile = 0;
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
    print(); 

    int numcmds = _numberOfSimpleCommands;
    int fds[2];
    int prevPipe = 0;
    pid_t lastPid = 0;

    int infd = 0;
    if (_inputFile)
    {
        infd = open(_inputFile, O_RDONLY);
        if (infd < 0) 
        { perror("open input"); 
            return; }
    }

    for (int i = 0; i < numcmds; i++)
    {
        if (i < numcmds - 1)
        {
            if (pipe(fds) < 0) { 
                perror("pipe"); 
                return; }
        }

        pid_t pid = fork();
        if (pid == 0)
        {
            if (i == 0 && _inputFile)
            {
                dup2(infd, 0);
                close(infd);
            }
            else if (i > 0)
            {
                dup2(prevPipe, 0);
                close(prevPipe);
            }

            if (i < numcmds - 1)
            {
                dup2(fds[1], 1);
                close(fds[1]);
                close(fds[0]);
            }
            else
            {
                if (_outFile)
                {
                    int flags = O_WRONLY | O_CREAT | (_append ? O_APPEND : O_TRUNC);
                    int outfd = open(_outFile, flags, 0666);
                    dup2(outfd, 1);
                    if (_out_error) 
                    dup2(outfd, 2);
                    close(outfd);
                }
                if (_errFile && !_out_error)
                {
                    int errfd = open(_errFile, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                    dup2(errfd, 2);
                    close(errfd);
                }
            }

            execvp(_simpleCommands[i]->_arguments[0], _simpleCommands[i]->_arguments);
            perror("execvp");
            exit(1);
        }
        else if (pid < 0)
        {
            perror("fork");
            return;
        }

        if (i > 0) 
        close(prevPipe);
        if (i < numcmds - 1)
        {
            prevPipe = fds[0];
            close(fds[1]);
        }
        lastPid = pid;
    }

    if (_inputFile) 
    close(infd);

    if (!_background)
    {
        int status;
        pid_t waited_pid = waitpid(lastPid, &status, 0);
        if (waited_pid > 0)
        {
            FILE *log = fopen("myshell.log", "a");
            if (log)
            {
                time_t now = time(NULL);
                char timeStr[30];
                strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", localtime(&now));
                if (WIFEXITED(status))
                    fprintf(log, "[%s] PID %d exited with status %d\n", timeStr, waited_pid, WEXITSTATUS(status));
                fclose(log);
            }
        }
    }
}

void Command::prompt()
{
    signal(SIGINT, SIG_IGN);
    signal(SIGCHLD, sigchld_handler);

    std::string input;
    while (true)
    {
        printf("myshell>");
        fflush(stdout);
        if (!std::getline(std::cin, input)) break;
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