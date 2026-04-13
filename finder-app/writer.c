#include <stdio.h>
#include <syslog.h>

int main(int argc, const char * argv[])
{
	openlog(argv[0],LOG_PID|LOG_PERROR|LOG_CONS , LOG_USER);
	if (argc < 2)
	{
		syslog(LOG_ERR,"some parameters were not specified in the command line\n");
		closelog();
		return 1;
	}

	const char* writefile = argv[1];
	const char* writestr  = argv[2];


        FILE* fp = fopen(writefile,"w");
	if (fp == NULL)
	{
		syslog(LOG_ERR, "unable to open file %s for writing\n", writefile);
		closelog();
		return 1;
	}

	syslog(LOG_DEBUG, "writing %s to %s\n", writestr, writefile);

	fprintf(fp,"%s\n", writestr);
	fclose (fp);

	closelog();

	return 0;
}
