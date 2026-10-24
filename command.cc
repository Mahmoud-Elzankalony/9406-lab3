
/*
 * 
 *
 * Template file.
 * You will need to add more code here to execute the command table.
 *
 * NOTE: You are responsible for fixing any bugs this code may have!
 *
 */

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
                    tokens[i + 1].type == TOKEN_PIPE)
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
                        break;

                    case TOKEN_APPEND:
                        free(currentCommand._outFile);
                        currentCommand._outFile = filename;
                        currentCommand._append = 1;
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
                        currentCommand._out_error = 1;
                        break;

                    default:
                        break;
                }

                break;
            }

            case TOKEN_BACKGROUND:
                currentCommand._background = 1;
                break;

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
    print();
    /*
        Write your code here
    */

    clear();
    prompt();
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
