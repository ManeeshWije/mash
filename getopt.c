#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#ifndef NEEDS_GETOPT
#include <unistd.h>

extern char *optarg;
extern int optind;
extern int optopt;
extern int opterr;
extern int optreset;

#else

/*
 * Adapted from (broken) code provided by Microsoft at:
 * https://github.com/iotivity/iotivity/tree/master/resource/c_common/windows
 */

char* optarg = NULL;
int optind = 1;
int optopt = 0;

int getopt(int argc, char *const argv[], const char *optstring)
{
	if (optarg == NULL) {
		fprintf(stderr, "Using home-brewed getopt()\n");
	}

	/**
	 * if the option index is out of bounds, or arg is not a flag, stop
	 */
	if ((optind >= argc)
			|| (strchr("-/", argv[optind][0]) == NULL)
			|| (argv[optind][0] == 0))
	{
		return -1;
	}

	/* match the flag character againts the options */
	int optopt = argv[optind][1];
	const char *matchingOption = strchr(optstring, optopt);

	/** if no match, we return '?' */
	if (matchingOption == NULL)
	{
		optind++;
		return '?';
	}

	/** if the option string indicates an argument, process it */
	if (matchingOption[1] == ':')
	{
		/**
		 * check whether argument abuts flag in argv, otherwise get next argv
		 */
		if (argv[optind][2] != '\0')
		{
			optarg = &argv[optind][2];
		} else {
			optind++;
			if (optind >= argc)
			{
				return '?';
			}
			optarg = argv[optind];
		}
	}
	/** advance to the next index */
	optind++;
	return optopt;
}
#endif
