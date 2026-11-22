
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define MAX_WORDS 40
#define WORD_LEN 20

static const char *STOP_WORDS[] = {
    "the", "and", "with", "has", "have", "was", "for",
    "from", "near", "very", "this", "that", "are", "its",
    "lost", "found", "item", "there", "which", "of", "in",
    "on", "at", "to", "is", "it", "as", "by", "an", "or", NULL
};

static int isStopWord(const char *w) {
    for (int i = 0; STOP_WORDS[i]; i++)
        if (strcmp(w, STOP_WORDS[i]) == 0) return 1;
    return 0;
}

static void addWord(char words[][WORD_LEN], int *count, char *word) {
    int len = (int)strlen(word);

    if (len < 2 || isStopWord(word)) return;

    if (len > 3 && word[len - 1] == 's')
        word[len - 1] = '\0';

    for (int i = 0; i < *count; i++)
        if (strcmp(words[i], word) == 0) return;

    if (*count < MAX_WORDS)
        strcpy(words[(*count)++], word);
}

static int tokenize(const char *text, char words[][WORD_LEN]) {
    int count = 0, len = 0;
    char current[WORD_LEN];

    for (const char *p = text; ; p++) {
        if (isalnum((unsigned char)*p)) {
            if (len < WORD_LEN - 1)
                current[len++] = (char)tolower((unsigned char)*p);
        } else {
            if (len > 0) {
                current[len] = '\0';
                addWord(words, &count, current);
                len = 0;
            }
            if (*p == '\0') break;
        }
    }

    return count;
}

static int sharedWords(char a[][WORD_LEN], int na,
                       char b[][WORD_LEN], int nb) {
    int count = 0;

    for (int i = 0; i < na; i++)
        for (int j = 0; j < nb; j++)
            if (strcmp(a[i], b[j]) == 0) count++;

    return count;
}

static int overlapPercent(char a[][WORD_LEN], int na,
                          char b[][WORD_LEN], int nb) {
    int smaller = na < nb ? na : nb;

    if (smaller == 0) return 0;

    int percent = sharedWords(a, na, b, nb) * 100 / smaller;

    return percent > 100 ? 100 : percent;
}

/* Called by JavaScript in the website */
int lf_similarity(
    const char *nameA,
    const char *descriptionA,
    const char *locationA,
    const char *nameB,
    const char *descriptionB,
    const char *locationB
) {
    char nameWordsA[MAX_WORDS][WORD_LEN] = {{0}};
    char nameWordsB[MAX_WORDS][WORD_LEN] = {{0}};
    char allWordsA[MAX_WORDS][WORD_LEN] = {{0}};
    char allWordsB[MAX_WORDS][WORD_LEN] = {{0}};
    char locWordsA[MAX_WORDS][WORD_LEN] = {{0}};
    char locWordsB[MAX_WORDS][WORD_LEN] = {{0}};

    char textA[170], textB[170];

    int nA = tokenize(nameA, nameWordsA);
    int nB = tokenize(nameB, nameWordsB);

    snprintf(textA, sizeof(textA), "%s %s",
             nameA, descriptionA);
    snprintf(textB, sizeof(textB), "%s %s",
             nameB, descriptionB);

    int tA = tokenize(textA, allWordsA);
    int tB = tokenize(textB, allWordsB);

    int lA = tokenize(locationA, locWordsA);
    int lB = tokenize(locationB, locWordsB);

    int nameP = overlapPercent(nameWordsA, nA, nameWordsB, nB);
    int allP  = overlapPercent(allWordsA, tA, allWordsB, tB);
    int locP  = overlapPercent(locWordsA, lA, locWordsB, lB);

    return (60 * nameP + 30 * allP + 10 * locP) / 100;
}
