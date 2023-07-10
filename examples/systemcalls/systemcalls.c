#include "systemcalls.h"

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
 * TODO  add your code here
 *  Call the system() function with the command set in the cmd
 *   and return a boolean true if the system() call completed with success
 *   or false() if it returned a failure
*/
  if (cmd && !system(cmd))
    return true;		/* cmd != NULL and return == 0 */
  else
    return false;
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

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    pid_t child_pid;
    int wstatus;

    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    //command[count] = command[count];

/*
 * TODO:
 *   Execute a system command by calling fork, execv(),
 *   and wait instead of system (see LSP page 161).
 *   Use the command[0] as the full path to the command to execute
 *   (first argument to execv), and use the remaining arguments
 *   as second argument to the execv() command.
 *
*/
    /* On failure, fork returns -1 */
    if (-1 == (child_pid = fork())) {
      perror("fork");
      va_end(args);
      return false;
    }

    /* If fork succeeds, child_pid is non-zero in parent, 0 in child */
    if (child_pid) {
      /* in parent - check retval of wait */
      if (-1 == wait(&wstatus)) {
	perror("wait");
	va_end(args);
	return false;
      }
      /* Check for non-zero exit status of cmd */
      if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus)) {
	va_end(args);
	return false;
      }
    } else {
      /* in child - run command[0], entire array is argv */
      if (-1 == execv(command[0],command)) {
	perror("execv");
	exit(EXIT_FAILURE);
      }
      /* no else, execv doesn't return if sucessful */
    }
    va_end(args);

    return true;
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
    pid_t child_pid;
    int wstatus;
    int new_stdout;

    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    //command[count] = command[count];


/*
 * TODO
 *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
 *   redirect standard out to a file specified by outputfile.
 *   The rest of the behaviour is same as do_exec()
 *
*/
    /* On failure, fork returns -1 */
    if (-1 == (child_pid = fork())) {
      perror("fork");
      va_end(args);
      return false;
    }

    /* If fork succeeds, child_pid is non-zero in parent, 0 in child */
    if (child_pid) {
      /* in parent - check retval of wait */
      if (-1 == wait(&wstatus)) {
	perror("wait");
	va_end(args);
	return false;
      }
      /* Check for non-zero exit status of cmd */
      if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus)) {
	va_end(args);
	return false;
      }
    } else {
      /* in child - open outputfile for writing */
      if (-1 == (new_stdout = open(outputfile, O_CREAT | O_TRUNC | O_RDWR,
				   S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH))) {
	perror("open outputfile");
	exit(EXIT_FAILURE);
      }

      /* dup2 to stdout */
      if (-1 == dup2(new_stdout, 1)) {
	perror("dup2");
	exit(EXIT_FAILURE);
      }

      close(new_stdout);	/* Cleanup - close original fd */

      /* run command[0], entire array is argv */
      if (-1 == execv(command[0],command)) {
	perror("execv");
	exit(EXIT_FAILURE);
      }
      /* no else, execv doesn't return if sucessful */
    }
    va_end(args);

    return true;
}
