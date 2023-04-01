#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define ARRAY_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))

typedef struct {
    char **argv;
    int argc;
} cmd_t;

typedef struct {
    cmd_t *cmds;
    const char **seps;
    size_t cmdNum;
} cmdSequence_t;

static const char *g_seps[] = {
    "&&", "&", "||", "|", ">>", ">"
};

/**
 * @return found separator else NULL
 */
static const char *getSepFromBeginning(const char *str) {
    for (size_t i = 0; i < ARRAY_LEN(g_seps); ++i) {
        // g_seps is static const, no need in strnlen
        if (strncmp(str, g_seps[i], strlen(g_seps[i])) == 0) {
            return g_seps[i];
        }
    }
    return NULL;
}

static size_t getCmdCount(const char *cmdStr) {
    size_t cmdNum = 0;
    for (size_t i = 0; cmdStr[i]; ++i) {
        const char *sep = NULL;
        if ((sep = getSepFromBeginning(&cmdStr[i]))) {
            i += strlen(sep);
            ++cmdNum;
        }
    }
    return cmdNum + 1;
}

static size_t getArgCount(const char *cmdStr) {
    size_t argc = 0;
    bool isSpaceFound = true;
    for (size_t i = 0; cmdStr[i]; ++i) {
        if (isSpaceFound && cmdStr[i] != ' ') {
            ++argc;
            isSpaceFound = false;
        } else if (cmdStr[i] == ' ') {
            isSpaceFound = true;
        }
        
        const char *sep = NULL;
        if ((sep = getSepFromBeginning(&cmdStr[i]))) {
            break;
        }
    }
    return argc;
}

int32_t parseCmdSeq(const char *cmdStr, cmdSequence_t *outSeq) {
    size_t cmdNum = getCmdCount(cmdStr);
    printf("cmdNum %ld\n", cmdNum);
    outSeq->cmds = calloc(cmdNum, sizeof(cmdSequence_t));
    outSeq->seps = calloc(cmdNum - 1, sizeof(char *));
    outSeq->cmdNum = cmdNum;
    bool isSpaceFound = true;
    size_t currentCmdNum = 0, currentArgvNum = 0, currentSep = 0;
    for (size_t i = 0, argStart = 0; i < strlen(cmdStr) + 1; ++i) {
        printf("i %ld char code %d\n", i, cmdStr[i]);
        const char *sep = NULL;
        if ((sep = getSepFromBeginning(&cmdStr[i]))) { // sep
            i += strlen(sep) - 1;
            outSeq->cmds[currentCmdNum].argc = currentArgvNum;
            currentArgvNum = 0;
            ++currentCmdNum;
            outSeq->seps[currentSep] = sep;
            ++currentSep;
            isSpaceFound = true;
        } else if (((cmdStr[i] == ' ' && !isSpaceFound) || (cmdStr[i] == 0)) && (outSeq->cmds[currentCmdNum].argc != 0)) { // argEnd
            outSeq->cmds[currentCmdNum].argv[currentArgvNum] = calloc(i - argStart, sizeof(char));
            memcpy(outSeq->cmds[currentCmdNum].argv[currentArgvNum], &cmdStr[argStart], i - argStart);
            ++currentArgvNum;
            isSpaceFound = true;
        } else if (isSpaceFound && cmdStr[i] != ' ') { // argStart
            if (outSeq->cmds[currentCmdNum].argc == 0) { // cmd start
                outSeq->cmds[currentCmdNum].argc = getArgCount(&cmdStr[i]);
                outSeq->cmds[currentCmdNum].argv = calloc(outSeq->cmds[currentCmdNum].argc, sizeof(char *));
            }
            argStart = i;
            isSpaceFound = false;
        } else if (cmdStr[i] == ' '){
            argStart = i;
            isSpaceFound = true;
        }
    }
    return EXIT_SUCCESS;
}

int main() {
    cmdSequence_t seq = {0};
    printf("Parse start\n");
    parseCmdSeq("test 1&&secondtest 2", &seq);
    printf("Parse end\n");
    printf("cmdNum: %ld\n", seq.cmdNum);
    for (int i = 0; i < seq.cmdNum; ++i) {
        printf("argc %d\n", seq.cmds[i].argc);
        for (int j = 0; j < seq.cmds[i].argc; ++j) {
            printf("argv <%s>\n", seq.cmds[i].argv[j]);
        }
        if (i < seq.cmdNum - 1) {
            printf("sep <%s>\n", seq.seps[i]);
        }

    }
    printf("END\n");
    return EXIT_SUCCESS;
}