/*********************************************************************
**
** Thomas Ames
** ECEA 5305, assignment #2, writer.c
** July 2023
**/

#include <stdlib.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
  const char *writefile;
  const char *writestr;
  int fd;

  /*
   * Call to openlog is optional (called be first syslog if not called
   * explicitly), but allows the facility to be specified once, vs
   * or'ed with leven on syslog call.  But LOG_USER is the default
   * facility, so this call isn't really needed.
   */
  openlog(argv[0], 0, LOG_USER);
  //  syslog(LOG_DEBUG, const char *format, ...);
  //  syslog(LOG_ERR, const char *format, ...);

  /*
   * In bash, $# counts args only, does not include program name, and $1 is
   * filesdir.  In C, argc includes program name, so argc = $#+1
   */
  if (3 != argc) {
    syslog(LOG_ERR, "Usage: %s writefile writestr", argv[0]);
    syslog(LOG_ERR, "Create the file writefile containing text writestr.");
    syslog(LOG_ERR, "Overwrites writefile if already exists; directory must exist.");
    syslog(LOG_ERR, "Exit code 0 on success, 1 if file cannot be written or bad args");\
    exit(1);
  }

  /* Descriptive names for clarity */
  writefile=argv[1];
  writestr=argv[2];

  syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);

  /* Open file, create if new, trunc if not */
  if (-1 == (fd = open(writefile, O_CREAT | O_TRUNC | O_RDWR,
		       S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH))) {
    syslog(LOG_ERR, "open failed, errno = %d - %s", errno, strerror(errno));
    exit(1);
  }

  /* Write file */
  if (strlen(writestr) != write(fd, writestr, strlen(writestr))) {
    syslog(LOG_ERR, "write failed, errno = %d - %s", errno, strerror(errno));
    exit(1);
  }

  /* close file*/
  if (-1 == close(fd)) {
    syslog(LOG_ERR, "write failed, errno = %d - %s", errno, strerror(errno));
    exit(1);
  }

  closelog();			/* Optional, called on exit anyway */
  exit(0);
}
