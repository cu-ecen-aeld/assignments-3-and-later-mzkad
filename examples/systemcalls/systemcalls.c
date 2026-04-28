#include "systemcalls.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>

#include <fcntl.h>

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
#include <fcntl.h>

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{

/*
 *  Call the system() function with the command set in the cmd
 *   and return a boolean true if the system() call completed with success
 *   or false() if it returned a failure
*/
	if (cmd == NULL)
	{
		return false;
	}
	
	int result = system(cmd);
	
	if(result == -1)
	{
		perror("do_system");
		return false;
	}
	
	if (WIFSIGNALED(result) &&
	    (WTERMSIG(result) == SIGINT || WTERMSIG(result) == SIGQUIT))
	{
		return false;
    }

    return true;
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool execAndWait(pid_t child_pid, const char *path, char *const argv[])
{
	bool result = true;
    if (child_pid == 0) //child
    { 
		execv(path, argv);
		abort();
    } 
    
	int wait_pid;
	int status = 0xBAAD;
	do {
		wait_pid = waitpid(child_pid, &status, 0);
		if (wait_pid == -1) {

			perror("waitpid");
			exit(EXIT_FAILURE);
		}

		if (WIFEXITED(status)) 
		{  
			// child terminated ok
			result = true;
			int commandExitStatus = WEXITSTATUS(status);
			printf("child exited normally, status=%d\n", commandExitStatus);
			if(commandExitStatus > 0)
			{
				result = false;
			}
		} 
		
		if (WIFSIGNALED(status)) {
			printf("child killed (signal %d)\n", WTERMSIG(status));
			result = false;
		} 
		
		if (WIFSTOPPED(status)) {
			printf("child stopped (signal %d)\n", WSTOPSIG(status));

#ifdef WIFCONTINUED     /* Not all implementations support this */
		} 
		
		if (WIFCONTINUED(status)) {
			printf("child continued\n");
#endif
		} 
	} while (!WIFEXITED(status) && !WIFSIGNALED(status));
	
	return result;
}

bool do_exec(int count, ...)
{
	bool result = true;
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

/*
 * TODO:
 *   Execute a system command by calling fork, execv(),
 *   and wait instead of system (see LSP page 161).
 *   Use the command[0] as the full path to the command to execute
 *   (first argument to execv), and use the remaining arguments
 *   as second argument to the execv() command.
 *
*/
    
    pid_t child_pid = fork();
    
    if (child_pid == -1) //Error. No child created
    {
    	return false;
    }
   
    result = execAndWait(child_pid, command[0], &command[0]);

    va_end(args);

    return result;
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/

bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

/*
 * TODO
 *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
 *   redirect standard out to a file specified by outputfile.
 *   The rest of the behaviour is same as do_exec()
 *
*/   
    pid_t child_pid = fork();
    
    if (child_pid == -1) //Error. No child created
    {
    	return false;
    }
   
    if (child_pid == 0) //child
    {      
    	int fd = open(outputfile, O_CREAT | O_WRONLY | O_TRUNC	, 0777);
    	if (fd < 0)
    	{
    		perror("open");
    		abort();
    	}

		int dupFD = dup2(fd, STDOUT_FILENO);
		if (dupFD < 0)
		{
			perror("dup2");
			close(fd);
			abort();
		}
		//no longer needed
		close(fd);
    }
    
    bool result = execAndWait(child_pid, command[0], &command[0]);
    
    va_end(args);
    return result;
}
