#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
HANDLE backgroundHandles[1024] = {NULL};
#define chdir _chdir
#else
#include <glob.h>
#include <regex.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "os_defs.h"

#include "mash_run.h"
#include "mash_tokenize.h"

/**
 * Print a prompt if the input is coming from a TTY
 */
static void prompt(FILE *pfp, FILE *ifp) {
    if (isatty(fileno(ifp))) {
        fputs(PROMPT_STRING, pfp);
    }
}

void freeFinalTokens(char **finalTokens, int nTokens) {
    if (finalTokens) {
        for (int i = 0; i < nTokens; i++) {
            if (finalTokens[i]) {
                free(finalTokens[i]);
            }
        }
        free(finalTokens);
    }
}

#ifndef _WIN32
// Returns 0 if regex matches, 1 otherwise
int checkRegex(int value) {
    if (value == 0) {
        return 0;
    } else if (value == REG_NOMATCH) {
        return 1;
    } else {
        fprintf(stderr, "ERROR: checkRegex failed\n");
        return 1;
    }
}
#endif

#ifndef _WIN32
int expandGlobs(char ***tokens, int *nTokens) {
    glob_t globResult;
    int newTokenCount = 0;
    char **expandedTokens = NULL;
    int newTokenIdx = 0;

    // Count total number of expanded tokens
    for (int i = 0; i < *nTokens; i++) {
        memset(&globResult, 0, sizeof(glob_t));
        int ret = glob((*tokens)[i], 0, NULL, &globResult);
        if (ret == 0) {
            newTokenCount += globResult.gl_pathc;
        } else if (ret == GLOB_NOMATCH) {
            newTokenCount++;
        } else {
            perror("glob");
            return 1;
        }
        globfree(&globResult);
    }

    // Allocate space for expanded tokens
    expandedTokens = malloc((newTokenCount + 1) * sizeof(char *));
    if (expandedTokens == NULL) {
        perror("malloc");
        return 1;
    }

    // Perform expansion
    for (int i = 0; i < *nTokens; i++) {
        memset(&globResult, 0, sizeof(glob_t));
        int ret = glob((*tokens)[i], 0, NULL, &globResult);
        if (ret == 0) {
            for (size_t j = 0; j < globResult.gl_pathc; j++) {
                expandedTokens[newTokenIdx] = strdup(globResult.gl_pathv[j]);
                if (expandedTokens[newTokenIdx] == NULL) {
                    perror("strdup");
                    globfree(&globResult);
                    for (int k = 0; k < newTokenIdx; k++) {
                        free(expandedTokens[k]);
                    }
                    free(expandedTokens);
                    return 1;
                }
                newTokenIdx++;
            }
        } else if (ret == GLOB_NOMATCH) {
            expandedTokens[newTokenIdx] = strdup((*tokens)[i]);
            if (expandedTokens[newTokenIdx] == NULL) {
                perror("strdup");
                globfree(&globResult);
                for (int k = 0; k < newTokenIdx; k++) {
                    free(expandedTokens[k]);
                }
                free(expandedTokens);
                return 1;
            }
            newTokenIdx++;
        }
        globfree(&globResult);
    }

    expandedTokens[newTokenIdx] = NULL;

    // Update the tokens array and count
    *tokens = expandedTokens;
    *nTokens = newTokenIdx;

    return 0;
}
#endif

// Allocates new token array that contains the original command including var subs
int substituteVariables(char **tokens, char variableNames[512][512], char variableValues[512][512], int *nameIdx,
                        int nTokens, char ***finalTokens) {
    // Allocate same number of tokens
    *finalTokens = malloc((nTokens + 1) * sizeof(char *));
    if (*finalTokens == NULL) {
        perror("malloc");
        return 1;
    }

    for (int i = 0; i < nTokens; i++) {
        char *token = tokens[i];
        char newToken[1024] = {0};
        char varName[512] = {0};

        // Iterate through the token to find variables and replace them
        for (int k = 0; token[k] != '\0'; k++) {
            if (token[k] == '$' && token[k + 1] == '{') {
                // Found the start of a variable placeholder
                int start = k + 2;
                int end = start;

                // Find the closing brace
                while (token[end] != '}' && token[end] != '\0') {
                    end++;
                }

                if (token[end] == '}') {
                    // Extract the variable name
                    strncpy(varName, token + start, end - start);
                    varName[end - start] = '\0';

                    // Search for the variable name in the list
                    // if NOT found, imitate bash and print nothing
                    for (int l = 0; l < *nameIdx; l++) {
                        if (strcmp(variableNames[l], varName) == 0) {
                            // Replace the variable with its value
                            strcat(newToken, variableValues[l]);
                            break;
                        }
                    }
                    // Move k past the end of the variable placeholder
                    k = end;
                }
            } else {
                // Append characters that are not part of a variable placeholder
                strncat(newToken, &token[k], 1);
            }
        }

        // Allocate space for the new token and store it in the finalTokens array
        (*finalTokens)[i] = malloc(strlen(newToken) + 1);
        if ((*finalTokens)[i] == NULL) {
            perror("malloc");
            return 1;
        }
        strcpy((*finalTokens)[i], newToken);
    }
    // execvp needs last token to be NULL
    (*finalTokens)[nTokens] = NULL;
    return 0;
}

int getExistingVarIndex(char *varToSearchFor, char variableNames[512][512], int *nameIdx) {
    for (int i = 0; i < *nameIdx; i++) {
        if (strcmp(variableNames[i], varToSearchFor) == 0) {
            return i;
        }
    }
    return -1;
}

#ifndef _WIN32
// Returns 0 if successfully saved variable into memory, 1 otherwise
int saveVariable(char **tokens, int val, char variableNames[512][512], char variableValues[512][512], int *nameIdx,
                 int *valIdx) {
    regex_t reegex;
    if (strcmp(tokens[1], "=") == 0) {
        // Regex for user defined variable names
        val = regcomp(&reegex, "[A-Za-z_][A-Za-z0-9_]*", 0);
        // Now compare
        val = regexec(&reegex, tokens[0], 0, NULL, 0);
        regfree(&reegex);
        if (checkRegex(val) == 0) {
            strcpy(variableNames[(*nameIdx)++], tokens[0]);
            // Join all tokens after the equals sign into a single value
            char value[1024] = "";
            for (int i = 2; tokens[i] != NULL; i++) {
                strcat(value, tokens[i]);
                if (tokens[i + 1] != NULL) {
                    // Add space between tokens
                    strcat(value, " ");
                }
            }

            // if variable already exists, overwrite the value, otherwise just store it at next index
            int varExistIdx = getExistingVarIndex(tokens[0], variableNames, nameIdx);
            if (varExistIdx != -1) {
                strcpy(variableValues[varExistIdx], value);
            } else {
                strcpy(variableValues[(*valIdx)++], value);
            }
            return 0;
        } else {
            fprintf(stderr, "ERROR: variable name did not satisfy regex [A-Za-z_][A-Za-z0-9_]*\n");
            return 1;
        }
    } else {
        fprintf(stderr, "ERROR: incorrect variable assignment syntax, expected x=y\n");
        return 1;
    }
}
#else
int isValidVariableName(const char *name) {
    if (!name || !isalpha(name[0]) && name[0] != '_') {
        return 0; // Invalid if it doesn't start with a letter or underscore
    }
    for (int i = 1; name[i] != '\0'; i++) {
        if (!isalnum(name[i]) && name[i] != '_') {
            return 0; // Invalid if it contains non-alphanumeric characters
        }
    }
    return 1; // Valid
}

int saveVariable(char **tokens, int val, char variableNames[512][512], char variableValues[512][512], int *nameIdx,
                 int *valIdx) {
    if (strcmp(tokens[1], "=") == 0) {
        // Check if the variable name is valid
        if (isValidVariableName(tokens[0])) {
            strcpy(variableNames[(*nameIdx)++], tokens[0]);
            // Join all tokens after the equals sign into a single value
            char value[1024] = "";
            for (int i = 2; tokens[i] != NULL; i++) {
                strcat(value, tokens[i]);
                if (tokens[i + 1] != NULL) {
                    // Add space between tokens
                    strcat(value, " ");
                }
            }

            // if variable already exists, overwrite the value, otherwise just store it at next index
            int varExistIdx = getExistingVarIndex(tokens[0], variableNames, nameIdx);
            if (varExistIdx != -1) {
                strcpy(variableValues[varExistIdx], value);
            } else {
                strcpy(variableValues[(*valIdx)++], value);
            }
            return 0;
        } else {
            fprintf(stderr, "ERROR: variable name did not satisfy criteria [A-Za-z_][A-Za-z0-9_]*\n");
            return 1;
        }
    } else {
        fprintf(stderr, "ERROR: incorrect variable assignment syntax, expected x=y\n");
        return 1;
    }
}
#endif

// Returns number of pipes
int getPipes(char **tokens, int nTokens, int *pipePositions) {
    int j = 0;
    for (int i = 0; i < nTokens; i++) {
        if (strcmp(tokens[i], "|") == 0) {
            pipePositions[j] = i;
            j++;
        }
    }
    return j;
}

int handleFileRedirection(char **tokens, int nTokens, char variableNames[512][512], char variableValues[512][512],
                          int val, int *nameIdx, int *valIdx
#ifdef _WIN32
                          ,
                          STARTUPINFO *si
#endif
) {
    char *inputFile = NULL;
    char *outputFile = NULL;
    int inputPos = -1;
    int outputPos = -1;

    // First pass: locate redirection operators and their files
    for (int i = 0; i < nTokens - 1; i++) {
        if (tokens[i] != NULL) {
            if (strcmp(tokens[i], "<") == 0 && tokens[i + 1] != NULL) {
                inputFile = tokens[i + 1];
                inputPos = i;
            }
            if (strcmp(tokens[i], ">") == 0 && tokens[i + 1] != NULL) {
                outputFile = tokens[i + 1];
                outputPos = i;
            }
        }
    }

    // If no redirection, return early
    if (!inputFile && !outputFile) {
        return 0;
    }

#ifndef _WIN32
    // Handle input redirection
    if (inputFile) {
        int fd = open(inputFile, O_RDONLY);
        if (fd == -1) {
            perror("open input");
            return 1;
        }
        if (dup2(fd, 0) == -1) {
            perror("dup2 input");
            close(fd);
            return 1;
        }
        close(fd);

        // Remove input redirection tokens
        tokens[inputPos] = NULL;
        tokens[inputPos + 1] = NULL;
    }

    // Handle output redirection
    if (outputFile) {
        int fd = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            perror("open output");
            return 1;
        }
        if (dup2(fd, 1) == -1) {
            perror("dup2 output");
            close(fd);
            return 1;
        }
        close(fd);

        // Remove output redirection tokens
        tokens[outputPos] = NULL;
        tokens[outputPos + 1] = NULL;
    }

    return 0;
#else
    SECURITY_ATTRIBUTES sa = {
        .nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = NULL, .bInheritHandle = TRUE};

    // Initialize STARTUPINFO for redirection
    si->dwFlags |= STARTF_USESTDHANDLES;

    // Get original handles
    HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hStdErr = GetStdHandle(STD_ERROR_HANDLE);

    // Duplicate the error handle to make it inheritable
    HANDLE hNewStdErr;
    if (!DuplicateHandle(GetCurrentProcess(), hStdErr, GetCurrentProcess(), &hNewStdErr, 0, TRUE,
                         DUPLICATE_SAME_ACCESS)) {
        fprintf(stderr, "Failed to duplicate stderr handle: %lu\n", GetLastError());
        return 1;
    }
    si->hStdError = hNewStdErr;

    // Handle input redirection
    if (inputFile) {
        HANDLE hNewStdIn =
            CreateFileA(inputFile, GENERIC_READ, FILE_SHARE_READ, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

        if (hNewStdIn == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "Error opening input file: %lu\n", GetLastError());
            CloseHandle(hNewStdErr);
            return 1;
        }
        si->hStdInput = hNewStdIn;
        tokens[inputPos] = NULL;
        tokens[inputPos + 1] = NULL;
    } else {
        // Duplicate the input handle to make it inheritable
        HANDLE hNewStdIn;
        if (!DuplicateHandle(GetCurrentProcess(), hStdIn, GetCurrentProcess(), &hNewStdIn, 0, TRUE,
                             DUPLICATE_SAME_ACCESS)) {
            fprintf(stderr, "Failed to duplicate stdin handle: %lu\n", GetLastError());
            CloseHandle(hNewStdErr);
            return 1;
        }
        si->hStdInput = hNewStdIn;
    }

    // Handle output redirection
    if (outputFile) {
        HANDLE hNewStdOut = CreateFileA(outputFile, GENERIC_WRITE, 0, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

        if (hNewStdOut == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "Error opening output file: %lu\n", GetLastError());
            CloseHandle(si->hStdInput);
            CloseHandle(hNewStdErr);
            return 1;
        }
        si->hStdOutput = hNewStdOut;
        tokens[outputPos] = NULL;
        tokens[outputPos + 1] = NULL;
    } else {
        // Duplicate the output handle to make it inheritable
        HANDLE hNewStdOut;
        if (!DuplicateHandle(GetCurrentProcess(), hStdOut, GetCurrentProcess(), &hNewStdOut, 0, TRUE,
                             DUPLICATE_SAME_ACCESS)) {
            fprintf(stderr, "Failed to duplicate stdout handle: %lu\n", GetLastError());
            CloseHandle(si->hStdInput);
            CloseHandle(hNewStdErr);
            return 1;
        }
        si->hStdOutput = hNewStdOut;
    }
    return 0;
#endif
}

#ifndef _WIN32
void checkFinishedBackgroundJobs(int *numBackgroundJobs, int backgroundPIDs[1024]) {
    int status;
    for (int i = 0; i < *numBackgroundJobs; i++) {
        int pid = waitpid(backgroundPIDs[i], &status, WNOHANG); // Check if the process has finished
        if (pid > 0) {
            if (WIFEXITED(status)) {
                int exitStatus = WEXITSTATUS(status);
                printf("Background job [%d] %d finished with status: %d\n", i + 1, pid, exitStatus);
            } else {
                printf("Background job [%d] %d finished unexpectedly\n", i + 1, pid);
            }

            // Shift remaining background jobs forward
            for (int j = i; j < *numBackgroundJobs - 1; j++) {
                backgroundPIDs[j] = backgroundPIDs[j + 1];
            }
            (*numBackgroundJobs)--;
            i--; // Decrement to check new shifted job at this position
        }
    }
}
#else
void checkFinishedBackgroundJobs(int *numBackgroundJobs, int backgroundPIDs[1024]) {
    for (int i = 0; i < *numBackgroundJobs; i++) {
        // Skip if handle is NULL (shouldn't happen, but safety check)
        if (backgroundHandles[i] == NULL) {
            continue;
        }

        // Check if the process has finished with a 0 timeout (non-blocking)
        DWORD waitResult = WaitForSingleObject(backgroundHandles[i], 0);

        if (waitResult == WAIT_OBJECT_0) {
            // Process has finished
            DWORD exitCode;
            if (GetExitCodeProcess(backgroundHandles[i], &exitCode)) {
                printf("Background job [%d] %d finished with status: %lu\n", i + 1, backgroundPIDs[i], exitCode);
            } else {
                printf("Background job [%d] %d finished unexpectedly\n", i + 1, backgroundPIDs[i]);
            }

            // Close the process handle
            CloseHandle(backgroundHandles[i]);
            backgroundHandles[i] = NULL;

            // Shift remaining background jobs forward
            for (int j = i; j < *numBackgroundJobs - 1; j++) {
                backgroundPIDs[j] = backgroundPIDs[j + 1];
                backgroundHandles[j] = backgroundHandles[j + 1];
            }

            // Clear the last handle that was shifted
            backgroundHandles[*numBackgroundJobs - 1] = NULL;

            (*numBackgroundJobs)--;
            i--; // Decrement to check new shifted job at this position
        }
        // If WAIT_TIMEOUT, process is still running, so we continue to next process
        // If WAIT_FAILED, something went wrong but we'll continue checking other processes
    }
}

// Helper function to store process handle when creating background process
void storeBackgroundProcessHandle(HANDLE hProcess, int *numBackgroundJobs) {
    if (*numBackgroundJobs < 1024) {
        backgroundHandles[*numBackgroundJobs] = hProcess;
    }
}
#endif

int runSingleCommand(char **tokens, int nTokens, char variableNames[512][512], char variableValues[512][512], int val,
                     int *nameIdx, int *valIdx, int *numBackgroundJobs, int backgroundPIDs[1024]) {
    int foundEquals = 0;
    int runInBackground = 0;

    // Check if background processes are completed before running the next command
    checkFinishedBackgroundJobs(numBackgroundJobs, backgroundPIDs);

    // Check if last token is "&" for background execution
    if (strcmp(tokens[nTokens - 1], "&") == 0) {
        runInBackground = 1;
        tokens[nTokens - 1] = NULL; // Remove "&" from tokens
        nTokens--;
    }

    // Handle 'cd' command
    if (strcmp(tokens[0], "cd") == 0) {
        if (tokens[1] == NULL) {
            fprintf(stderr, "cd: expected argument to \"cd\"\n");
        } else {
            if (chdir(tokens[1]) != 0) {
                perror("cd");
            }
        }
        return 0;
    }

    // Detect if we need assignment
    for (int i = 0; i < nTokens; i++) {
        if (strcmp(tokens[i], "=") == 0) {
            foundEquals = 1;
            break;
        }
    }

    if (foundEquals) {
        if (saveVariable(tokens, val, variableNames, variableValues, nameIdx, valIdx) == 0) {
            return 0;
        }
        return 1;
    }

#ifndef _WIN32
    // Unix/Linux specific process handling
    pid_t pid;
    int status;

    // Fork a new process
    pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        if (handleFileRedirection(tokens, nTokens, variableNames, variableValues, val, nameIdx, valIdx) != 0) {
            exit(EXIT_FAILURE);
        }
        // Child process
        execvp(tokens[0], tokens);

        // If execvp returns, there was an error
        perror("execvp");
        exit(EXIT_FAILURE);
    } else {
        // Parent process
        if (!runInBackground) {
            // Wait for foreground process to finish
            if (waitpid(pid, &status, 0) < 0) {
                perror("waitpid");
                return -1;
            }

            int exitStatus = WEXITSTATUS(status);
            if (WIFEXITED(status)) {
                if (exitStatus != 0) {
                    fprintf(stderr, "Child(%d) exited -- failure (%d)\n", pid, exitStatus);
                } else {
                    fprintf(stderr, "Child(%d) exited -- success (%d)\n", pid, exitStatus);
                }
            } else {
                fprintf(stderr, "Child(%d) exited -- crashed? (%d)\n", pid, exitStatus);
            }
        } else {
            // Store the background process PID and notify user
            backgroundPIDs[*numBackgroundJobs] = pid;
            printf("[%d] %d\n", (*numBackgroundJobs) + 1, pid);
            (*numBackgroundJobs)++;
        }
    }
#else
    // Windows-specific process creation
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(STARTUPINFO));
    memset(&pi, 0, sizeof(PROCESS_INFORMATION));
    si.cb = sizeof(STARTUPINFO);

    if (handleFileRedirection(tokens, nTokens, variableNames, variableValues, val, nameIdx, valIdx, &si) != 0) {
        exit(EXIT_FAILURE);
    }

    // Create command line from tokens
    char commandLine[1024] = "";
    int cmdLen = 0;
    for (int i = 0; i < nTokens && tokens[i] != NULL; i++) {
        if (cmdLen > 0 && cmdLen < 1023) {
            commandLine[cmdLen++] = ' ';
        }
        int result = snprintf(commandLine + cmdLen, 1024 - cmdLen, "%s", tokens[i]);
        if (result < 0 || result >= 1024 - cmdLen) {
            if (si.dwFlags & STARTF_USESTDHANDLES) {
                CloseHandle(si.hStdInput);
                CloseHandle(si.hStdOutput);
                CloseHandle(si.hStdError);
            }
            fprintf(stderr, "Command line too long\n");
            return -1;
        }
        cmdLen += result;
    }

    // Create the child process
    if (!CreateProcess(NULL,               // No module name (use command line)
                       (LPSTR)commandLine, // Command line
                       NULL,               // Process handle not inheritable
                       NULL,               // Thread handle not inheritable
                       TRUE,               // Set handle inheritance to TRUE
                       0,                  // No creation flags
                       NULL,               // Use parent's environment block
                       NULL,               // Use parent's starting directory
                       &si,                // Pointer to STARTUPINFO structure
                       &pi))               // Pointer to PROCESS_INFORMATION structure
    {
        fprintf(stderr, "CreateProcess failed (%d)\n", GetLastError());
        return -1;
    }

    // Close redirected handles
    if (si.dwFlags & STARTF_USESTDHANDLES) {
        CloseHandle(si.hStdInput);
        CloseHandle(si.hStdOutput);
        CloseHandle(si.hStdError);
    }

    // Wait until child process exits
    WaitForSingleObject(pi.hProcess, INFINITE);

    // Get the exit code
    DWORD exitStatus;
    if (GetExitCodeProcess(pi.hProcess, &exitStatus)) {
        if (exitStatus != 0) {
            fprintf(stderr, "Child(%d) exited -- failure (%lu)\n", pi.dwProcessId, exitStatus);
        } else {
            fprintf(stderr, "Child(%d) exited -- success (%lu)\n", pi.dwProcessId, exitStatus);
        }
    } else {
        fprintf(stderr, "Child(%d) exited -- crashed? (%d)\n", pi.dwProcessId, exitStatus);
    }

    if (runInBackground) {
        backgroundPIDs[*numBackgroundJobs] = pi.dwProcessId;
        storeBackgroundProcessHandle(pi.hProcess, numBackgroundJobs);
        printf("[%d] %d\n", (*numBackgroundJobs) + 1, pi.dwProcessId);
        (*numBackgroundJobs)++;
        // Don't close the process handle here since we need it for background monitoring
    } else {
        // Close handles as before for foreground processes
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
#endif
    return 0;
}

#ifndef _WIN32
int runPipeCommand(char **tokens, int nTokens, int *pipePositions, int numPipes, char variableNames[512][512],
                   char variableValues[512][512], int *nameIdx, int *valIdx) {
    // Array to hold pipe file descriptors
    int pipefds[2 * numPipes];
    // To hold PIDs of child processes
    pid_t pids[numPipes + 1];

    // Create all pipes first
    for (int i = 0; i < numPipes; i++) {
        if (pipe(pipefds + i * 2) < 0) {
            perror("pipe");
            exit(1);
        }
    }

    int commandStart = 0;
    for (int i = 0; i <= numPipes; i++) {
        // Fork a process for each command
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork");
            exit(1);
        }

        // Child
        if (pids[i] == 0) {
            // If not the last command, redirect stdout to the next pipe
            if (i < numPipes) {
                if (dup2(pipefds[i * 2 + 1], STDOUT_FILENO) < 0) {
                    perror("dup2");
                    exit(1);
                }
            }

            // If not the first command, redirect stdin to the previous pipe
            if (i > 0) {
                if (dup2(pipefds[(i - 1) * 2], STDIN_FILENO) < 0) {
                    perror("dup2");
                    exit(1);
                }
            }

            if (i == numPipes) {
                if (handleFileRedirection(&tokens[commandStart], nTokens - commandStart, variableNames, variableValues,
                                          0, nameIdx, valIdx) != 0) {
                    exit(EXIT_FAILURE);
                }
            }

            // Close all pipe file descriptors
            for (int j = 0; j < 2 * numPipes; j++) {
                close(pipefds[j]);
            }

            // Prepare the arguments for the current command
            // Set the pipe position to NULL to split arguments
            tokens[pipePositions[i]] = NULL;
            char **commandArgs = &tokens[commandStart];
            execvp(commandArgs[0], commandArgs);

            perror("execvp");
            exit(1);
        }

        // Parent process: update commandStart to the next command
        commandStart = pipePositions[i] + 1;
    }

    // Close all pipe file descriptors in the parent
    for (int i = 0; i < 2 * numPipes; i++) {
        close(pipefds[i]);
    }

    // Wait for all child processes
    for (int i = 0; i <= numPipes; i++) {
        if (waitpid(pids[i], NULL, 0) < 0) {
            perror("waitpid");
            exit(1);
        }
    }

    return 0;
}
#else
int runPipeCommand(char **tokens, int nTokens, int *pipePositions, int numPipes) {
    HANDLE *pipefds = (HANDLE *)malloc(2 * numPipes * sizeof(HANDLE));
    PROCESS_INFORMATION *procInfo = (PROCESS_INFORMATION *)malloc((numPipes + 1) * sizeof(PROCESS_INFORMATION));
    STARTUPINFO *startupInfo = (STARTUPINFO *)malloc((numPipes + 1) * sizeof(STARTUPINFO));
    SECURITY_ATTRIBUTES saAttr;

    int commandStart = 0;

    // Set the security attributes for the pipes (ensuring the pipe handles are inherited)
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    // Loop to create pipes and processes
    for (int i = 0; i <= numPipes; i++) {
        // Set up STARTUPINFO and PROCESS_INFORMATION structures
        memset(&procInfo[i], 0, sizeof(PROCESS_INFORMATION));
        memset(&startupInfo[i], 0, sizeof(STARTUPINFO));
        startupInfo[i].cb = sizeof(STARTUPINFO);
        startupInfo[i].dwFlags |= STARTF_USESTDHANDLES;

        // Create a pipe with inheritance enabled
        SECURITY_ATTRIBUTES saAttr;
        saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
        saAttr.bInheritHandle = TRUE;
        saAttr.lpSecurityDescriptor = NULL;

        // Create pipes for input and output redirection
        if (i < numPipes) {
            if (!CreatePipe(&pipefds[i * 2], &pipefds[i * 2 + 1], &saAttr, 0)) {
                fprintf(stderr, "CreatePipe failed (%d)\n", GetLastError());
                return 1;
            }
            // Ensure the write end is inheritable
            SetHandleInformation(pipefds[i * 2 + 1], HANDLE_FLAG_INHERIT, 1);
        }

        // Set up hStdInput and hStdOutput
        if (i > 0) {
            // Not the first command: assign the read end of the previous pipe to hStdInput
            startupInfo[i].hStdInput = pipefds[(i - 1) * 2];
        }

        if (i < numPipes) {
            // Not the last command: assign the write end of the current pipe to hStdOutput
            startupInfo[i].hStdOutput = pipefds[i * 2 + 1];
        } else {
            // Last command: direct hStdOutput to the standard output (console)
            startupInfo[i].hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        }

        // Command line preparation
        tokens[pipePositions[i]] = NULL;
        char cmdLine[1024] = "";
        for (int j = commandStart; tokens[j] != NULL; j++) {
            strcat(cmdLine, tokens[j]);
            strcat(cmdLine, " ");
        }

        // Create the child process
        if (!CreateProcess(NULL, (LPSTR)cmdLine, NULL, NULL, TRUE, 0, NULL, NULL, &startupInfo[i], &procInfo[i])) {
            fprintf(stderr, "CreateProcess failed (%d)\n", GetLastError());
            return 1;
        }

        // Close handles in the parent process as needed
        if (i < numPipes) {
            CloseHandle(pipefds[i * 2 + 1]);
        }

        // Update for next command
        commandStart = pipePositions[i] + 1;
    }

    // Close all pipe read ends in the parent
    for (int i = 0; i < numPipes; i++) {
        CloseHandle(pipefds[i * 2]);
    }

    // Wait for all child processes to finish
    for (int i = 0; i <= numPipes; i++) {
        WaitForSingleObject(procInfo[i].hProcess, INFINITE);
        CloseHandle(procInfo[i].hProcess);
        CloseHandle(procInfo[i].hThread);
    }

    return 0;
}
#endif

/**
 * Actually do the work
 */
int execFullCommandLine(FILE *ofp, char **const tokens, int nTokens, int verbosity, char variableNames[512][512],
                        char variableValues[512][512], int *nameIdx, int *valIdx, int *numBackgroundJobs,
                        int backgroundPIDs[1024]) {
    if (verbosity > 0) {
        fprintf(stderr, " + ");
        fprintfTokens(stderr, tokens, 1);
    }

    int pipePositions[512];
    int val = 0;
    int numPipes = getPipes(tokens, nTokens, pipePositions);

    char **finalTokens = NULL;
    char **modifiableTokens = tokens;
    int foundVariableSub = 0;
    int expandedTokenCount = nTokens;
    int freeModifiableTokens = 0;

    // Check for variable substitution
    for (int i = 0; i < nTokens; i++) {
        if (strstr(modifiableTokens[i], "${") != NULL) {
            foundVariableSub = 1;
            break;
        }
    }

    // Expand any wildcard patterns
#ifndef _WIN32
    if (expandGlobs(&modifiableTokens, &expandedTokenCount) != 0) {
        fprintf(stderr, "Error expanding globs\n");
        return 1;
    }
    freeModifiableTokens = 1;

    // Recalculate pipe positions after glob expansion
    numPipes = getPipes(modifiableTokens, expandedTokenCount, pipePositions);
#else
    numPipes = getPipes(modifiableTokens, nTokens, pipePositions);
#endif

    // If there are variable substitutions, substitute them
    if (foundVariableSub) {
        if (substituteVariables(modifiableTokens, variableNames, variableValues, nameIdx, nTokens, &finalTokens) != 0) {
            return 1;
        }
        modifiableTokens = finalTokens;
    }

    if (numPipes > 0) {
#ifndef _WIN32
        runPipeCommand(modifiableTokens, nTokens, pipePositions, numPipes, variableNames, variableValues, nameIdx,
                       valIdx);
#else
        runPipeCommand(modifiableTokens, nTokens, pipePositions, numPipes);
#endif
    } else {
        runSingleCommand(modifiableTokens, nTokens, variableNames, variableValues, val, nameIdx, valIdx,
                         numBackgroundJobs, backgroundPIDs);
    }

    // TODO: Revisit this freeing later as im leaking when doing var assignment -> echo var -> ls *.c | grep squash
    if (finalTokens) {
        freeFinalTokens(finalTokens, nTokens);
        freeModifiableTokens = 0; // We've freed the memory, so don't free it again
    }

    if (freeModifiableTokens && modifiableTokens != tokens) {
        freeFinalTokens(modifiableTokens, expandedTokenCount);
    }

    return 0;
}

/**
 * Load each line and perform the work for it
 */
int runScript(FILE *ofp, FILE *pfp, FILE *ifp, const char *filename, int verbosity, char variableNames[512][512],
              char variableValues[512][512], int *nameIdx, int *valIdx) {
    char linebuf[LINEBUFFERSIZE];
    char *tokens[MAXTOKENS];
    int lineNo = 1;
    int nTokens, executeStatus = 0;
    int backgroundPIDs[1024];
    int numBackgroundJobs = 0;

    fprintf(stderr, "SHELL PID %ld\n", (long)getpid());

    prompt(pfp, ifp);
    while ((nTokens = parseLine(ifp, tokens, MAXTOKENS, linebuf, LINEBUFFERSIZE, verbosity - 3)) > 0) {
        lineNo++;

        if (nTokens > 0) {

            if (strcmp(tokens[0], "exit") == 0) {
                exit(EXIT_SUCCESS);
            }

            executeStatus = execFullCommandLine(ofp, tokens, nTokens, verbosity, variableNames, variableValues, nameIdx,
                                                valIdx, &numBackgroundJobs, backgroundPIDs);

            if (executeStatus < 0) {
                fprintf(stderr, "Failure executing '%s' line %d:\n    ", filename, lineNo);
                fprintfTokens(stderr, tokens, 1);
                return executeStatus;
            }
        }
        prompt(pfp, ifp);
    }

    return (0);
}

/**
 * Open a file and run it as a script
 */
int runScriptFile(FILE *ofp, FILE *pfp, const char *filename, int verbosity, char variableNames[512][512],
                  char variableValues[512][512], int *nameIdx, int *valIdx) {
    FILE *ifp;
    int status;

    ifp = fopen(filename, "r");
    if (ifp == NULL) {
        fprintf(stderr, "Cannot open input script '%s' : %s\n", filename, strerror(errno));
        return -1;
    }

    status = runScript(ofp, pfp, ifp, filename, verbosity, variableNames, variableValues, nameIdx, valIdx);
    fclose(ifp);
    return status;
}
