#ifndef __MASH_RUN_SCRIPT_HEADER__
#define __MASH_RUN_SCRIPT_HEADER__

int runScript(FILE *ofp, FILE *pfp, FILE *ifp, const char *filename, int verbosity, char variableNames[512][512], char variableValues[512][512], int *nameIdx, int *valIdx);
int runScriptFile(FILE *ofp, FILE *pfp, const char *filename, int verbosity, char variableNames[512][512], char variableValues[512][512], int *nameIdx, int *valIdx);

#define	LINEBUFFERSIZE	(1024 * 2)
#define	MAXTOKENS		(1024)

#define	MAXCOMMANDSINPIPE	8

#define	PROMPT_STRING	"(mash) % "

#endif /* __MASH_RUN_SCRIPT_HEADER__ */
