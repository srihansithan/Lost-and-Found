/*
 * LOST AND FOUND MANAGEMENT SYSTEM  (version 3)
 * ---------------------------------------------------------------
 * Features
 *  - Alphanumeric item IDs (LF001, LF002 ...) - never confused with menu numbers
 *  - Admin login (default: admin / admin123) to change status, edit, delete
 *  - Photo upload for both lost and found items (image file is copied
 *    into the program folder as photo_<ID>.jpg / .png)
 *  - Phone number must be exactly 10 digits
 *  - Every wrong entry shows "ENTER CORRECTLY" and returns to the same entry
 *  - Review + EDIT screen just before final submission
 *  - Smart matching with SIMILARITY PERCENTAGE
 *  - SMS to both persons when match is 80% or more
 *
 * Compile : gcc lost_and_found.c -o lostfound
 * Run     : ./lostfound        (Windows: lostfound.exe)
 *
 * NOTE: delete old items.dat before first run (data format changed).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---------- SMS settings ----------
 * SMS_REAL 0 : SMS is only simulated (printed on screen + saved in sms_log.txt)
 * SMS_REAL 1 : also sends a real SMS through the Twilio service using curl
 *              (needs a Twilio account, curl installed, internet connection)
 */
#ifndef SMS_REAL
#define SMS_REAL 0
#endif
#define COUNTRY_CODE "+91"
#define TWILIO_SID   "YOUR_ACCOUNT_SID"
#define TWILIO_TOKEN "YOUR_AUTH_TOKEN"
#define TWILIO_FROM  "+10000000000"

#ifdef _WIN32
#define NULL_DEVICE "NUL"
#else
#define NULL_DEVICE "/dev/null"
#endif

#define MAX_ITEMS      500
#define DATA_FILE      "items.dat"
#define PASS_FILE      "admin.txt"
#define SMS_LOG        "sms_log.txt"
#define ADMIN_USER     "admin"
#define DEFAULT_PASS   "admin123"

#define MAX_WORDS      40
#define WORD_LEN       20
#define MATCH_SHOW_MIN 40   /* show a match when similarity >= 40%  */
#define SMS_MIN        80   /* send SMS when similarity >= 80%      */
#define HIGH_MIN       80

enum { LOST = 1, FOUND = 2, CLAIMED = 3 };

typedef struct {
    char id[10];            /* LF001 */
    char name[50];
    char description[100];
    char location[50];
    char date[12];          /* DD-MM-YYYY */
    char phone[11];         /* exactly 10 digits */
    char photo[40];         /* photo_LF001.jpg  (empty = no photo) */
    int  status;            /* LOST, FOUND, CLAIMED */
} Item;

Item items[MAX_ITEMS];
int  itemCount = 0;
char adminPass[30] = DEFAULT_PASS;

/* =====================================================
 *  BASIC HELPERS
 * ===================================================== */

void toUpperStr(char *s) { for (; *s; s++) *s = (char)toupper((unsigned char)*s); }
void toLowerStr(char *s) { for (; *s; s++) *s = (char)tolower((unsigned char)*s); }

void trim(char *s) {
    int start = 0, len;
    while (s[start] && isspace((unsigned char)s[start])) start++;
    if (start) memmove(s, s + start, strlen(s + start) + 1);
    len = (int)strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
}

const char *statusText(int s) {
    switch (s) {
        case LOST:    return "LOST";
        case FOUND:   return "FOUND";
        case CLAIMED: return "CLAIMED";
        default:      return "UNKNOWN";
    }
}

/* reads one line. returns 0 = ok, 1 = input was longer than the buffer */
int readLine(const char *prompt, char *buf, int size) {
    printf("%s", prompt);
    if (fgets(buf, size, stdin) == NULL) {
        printf("\nInput closed. Exiting.\n");
        exit(0);
    }
    char *nl = strchr(buf, '\n');
    if (nl) { *nl = '\0'; return 0; }
    int c = getchar();
    if (c == '\n' || c == EOF) return 0;
    while (c != '\n' && c != EOF) c = getchar();   /* throw away the rest */
    return 1;
}

void pause_screen(void) {
    char tmp[10];
    readLine("\nPress Enter to continue...", tmp, sizeof(tmp));
}

/* =====================================================
 *  VALIDATED INPUT  (wrong entry -> "ENTER CORRECTLY" -> ask again)
 * ===================================================== */

int getChoice(const char *prompt, int min, int max) {
    char buf[20];
    for (;;) {
        int r = readLine(prompt, buf, sizeof(buf));
        trim(buf);
        if (r == 0 && buf[0]) {
            char *end;
            long v = strtol(buf, &end, 10);
            if (*end == '\0' && v >= min && v <= max) return (int)v;
        }
        printf("ENTER CORRECTLY\n");
    }
}

int getYesNo(const char *prompt) {
    char buf[10];
    for (;;) {
        int r = readLine(prompt, buf, sizeof(buf));
        trim(buf);
        if (r == 0 && strlen(buf) == 1) {
            char c = (char)tolower((unsigned char)buf[0]);
            if (c == 'y') return 1;
            if (c == 'n') return 0;
        }
        printf("ENTER CORRECTLY\n");
    }
}

/* plain text. allowEmpty = 1 means "just press Enter" is accepted */
void getText(const char *prompt, char *buf, int size, int allowEmpty) {
    for (;;) {
        int r = readLine(prompt, buf, size);
        trim(buf);
        if (r == 0 && (buf[0] || allowEmpty)) return;
        printf("ENTER CORRECTLY\n");
        if (r == 1) printf("(Maximum %d characters)\n", size - 1);
    }
}

int validPhone(const char *s) {
    if (strlen(s) != 10) return 0;
    for (int i = 0; i < 10; i++) if (!isdigit((unsigned char)s[i])) return 0;
    return 1;
}

void getPhone(const char *prompt, char *out, int allowEmpty) {
    char buf[30];
    for (;;) {
        int r = readLine(prompt, buf, sizeof(buf));
        trim(buf);
        if (r == 0) {
            if (!buf[0] && allowEmpty) { out[0] = '\0'; return; }
            if (validPhone(buf)) { strcpy(out, buf); return; }
        }
        printf("ENTER CORRECTLY\n(Phone number must be exactly 10 digits, numbers only)\n");
    }
}

int validDate(const char *s) {
    if (strlen(s) != 10 || s[2] != '-' || s[5] != '-') return 0;
    for (int i = 0; i < 10; i++)
        if (i != 2 && i != 5 && !isdigit((unsigned char)s[i])) return 0;
    int d = atoi(s), m = atoi(s + 3), y = atoi(s + 6);
    if (y < 2000 || y > 2100 || m < 1 || m > 12 || d < 1) return 0;
    int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) days[1] = 29;
    return d <= days[m - 1];
}

void getDate(const char *prompt, char *out, int allowEmpty) {
    char buf[30];
    for (;;) {
        int r = readLine(prompt, buf, sizeof(buf));
        trim(buf);
        if (r == 0) {
            if (!buf[0] && allowEmpty) { out[0] = '\0'; return; }
            if (validDate(buf)) { strcpy(out, buf); return; }
        }
        printf("ENTER CORRECTLY\n(Use a real date like 25-11-2026)\n");
    }
}

void getId(const char *prompt, char *out) {
    char buf[30];
    for (;;) {
        int r = readLine(prompt, buf, sizeof(buf));
        trim(buf);
        toUpperStr(buf);
        int len = (int)strlen(buf), ok = (r == 0 && len >= 3 && len <= 9 &&
                                          buf[0] == 'L' && buf[1] == 'F');
        for (int i = 2; ok && i < len; i++) if (!isdigit((unsigned char)buf[i])) ok = 0;
        if (ok) { strcpy(out, buf); return; }
        printf("ENTER CORRECTLY\n(An ID looks like LF001)\n");
    }
}

/* ---------- photo handling ---------- */

int hasImageExt(const char *p) {
    const char *dot = strrchr(p, '.');
    if (!dot) return 0;
    char e[8];
    strncpy(e, dot + 1, 7); e[7] = '\0';
    toLowerStr(e);
    return !strcmp(e, "jpg") || !strcmp(e, "jpeg") || !strcmp(e, "png");
}

int fileExists(const char *p) {
    FILE *f = fopen(p, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

void stripQuotes(char *s) {
    int n = (int)strlen(s);
    if (n >= 2 && (s[0] == '"' || s[0] == '\'') && s[n - 1] == s[0]) {
        memmove(s, s + 1, n - 2);
        s[n - 2] = '\0';
    }
}

/* asks for a path to an existing .jpg/.jpeg/.png file; Enter = no file */
void getPhoto(const char *prompt, char *buf, int size) {
    for (;;) {
        int r = readLine(prompt, buf, size);
        trim(buf);
        stripQuotes(buf);
        trim(buf);
        if (r == 0) {
            if (!buf[0]) return;
            if (hasImageExt(buf) && fileExists(buf)) return;
        }
        printf("ENTER CORRECTLY\n(Give the path of an existing .jpg, .jpeg or .png file, or press Enter to skip)\n");
    }
}

/* copies the uploaded picture to photo_<ID>.<ext> in the program folder */
int copyPhoto(const char *src, const char *id, char *dest, int destSize) {
    const char *dot = strrchr(src, '.');
    char ext[8];
    if (!dot) return 0;
    strncpy(ext, dot + 1, 7); ext[7] = '\0';
    toLowerStr(ext);
    snprintf(dest, destSize, "photo_%s.%s", id, ext);
    if (strcmp(src, dest) == 0) return 1;

    FILE *in = fopen(src, "rb");
    if (!in) return 0;
    FILE *out = fopen(dest, "wb");
    if (!out) { fclose(in); return 0; }
    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), in)) > 0) fwrite(chunk, 1, n, out);
    fclose(in);
    fclose(out);
    return 1;
}

/* =====================================================
 *  FILE HANDLING
 * ===================================================== */

void loadItems(void) {
    FILE *fp = fopen(DATA_FILE, "rb");
    if (!fp) return;
    itemCount = (int)fread(items, sizeof(Item), MAX_ITEMS, fp);
    fclose(fp);
}

void saveItems(void) {
    FILE *fp = fopen(DATA_FILE, "wb");
    if (!fp) { printf("Error: could not save data!\n"); return; }
    fwrite(items, sizeof(Item), itemCount, fp);
    fclose(fp);
}

void loadPassword(void) {
    FILE *fp = fopen(PASS_FILE, "r");
    if (!fp) return;
    if (fgets(adminPass, sizeof(adminPass), fp))
        adminPass[strcspn(adminPass, "\r\n")] = '\0';
    fclose(fp);
}

void savePassword(void) {
    FILE *fp = fopen(PASS_FILE, "w");
    if (!fp) return;
    fprintf(fp, "%s\n", adminPass);
    fclose(fp);
}

/* =====================================================
 *  IDs AND DISPLAY
 * ===================================================== */

void generateId(char *id) {
    int max = 0, n;
    for (int i = 0; i < itemCount; i++)
        if (sscanf(items[i].id + 2, "%d", &n) == 1 && n > max) max = n;
    sprintf(id, "LF%03d", max + 1);
}

int findById(const char *id) {
    char key[10];
    strncpy(key, id, sizeof(key) - 1);
    key[sizeof(key) - 1] = '\0';
    toUpperStr(key);
    for (int i = 0; i < itemCount; i++)
        if (strcmp(items[i].id, key) == 0) return i;
    return -1;
}

void printItem(const Item *it) {
    printf("\n----------------------------------------\n");
    printf("ID          : %s\n", it->id);
    printf("Status      : %s\n", statusText(it->status));
    printf("Item name   : %s\n", it->name);
    printf("Description : %s\n", it->description);
    printf("Location    : %s\n", it->location);
    printf("Date        : %s\n", it->date);
    printf("Phone       : %s\n", it->phone);
    printf("Photo       : %s\n", it->photo[0] ? it->photo : "Not provided");
    printf("----------------------------------------\n");
}

void listItems(int filter) {
    int shown = 0;
    for (int i = 0; i < itemCount; i++)
        if (filter == 0 || items[i].status == filter) { printItem(&items[i]); shown++; }
    if (!shown) printf("\nNo items to show.\n");
    else printf("\nTotal: %d item(s)\n", shown);
}

void listCompact(void) {
    if (itemCount == 0) { printf("\nNo items yet.\n"); return; }
    printf("\n%-8s %-22s %-10s %-12s\n", "ID", "Item", "Status", "Date");
    printf("-------------------------------------------------------\n");
    for (int i = 0; i < itemCount; i++)
        printf("%-8s %-22.22s %-10s %-12s\n", items[i].id, items[i].name,
               statusText(items[i].status), items[i].date);
}

/* =====================================================
 *  SMS
 * ===================================================== */

void sendSMS(const char *phone, const char *msg) {
    FILE *lg = fopen(SMS_LOG, "a");
    if (lg) { fprintf(lg, "To %s%s : %s\n", COUNTRY_CODE, phone, msg); fclose(lg); }

    printf("\n[SMS %s] To %s%s\n  \"%s\"\n",
           SMS_REAL ? "SENT" : "SIMULATED", COUNTRY_CODE, phone, msg);

#if SMS_REAL
    /* message text goes through a file so odd characters cannot break the command;
       the phone number is already checked to be 10 digits only */
    FILE *b = fopen("sms_body.tmp", "w");
    if (b) {
        fprintf(b, "%s", msg);
        fclose(b);
        char cmd[700];
        snprintf(cmd, sizeof(cmd),
                 "curl -s -X POST https://api.twilio.com/2010-04-01/Accounts/%s/Messages.json "
                 "--data-urlencode \"To=%s%s\" --data-urlencode \"From=%s\" "
                 "--data-urlencode \"Body@sms_body.tmp\" -u %s:%s > %s 2>&1",
                 TWILIO_SID, COUNTRY_CODE, phone, TWILIO_FROM,
                 TWILIO_SID, TWILIO_TOKEN, NULL_DEVICE);
        system(cmd);
        remove("sms_body.tmp");
    }
#endif
}

void notifyPair(const Item *a, const Item *b, int pct) {
    char msg[200];
    snprintf(msg, sizeof(msg),
             "Lost&Found: %d%% match for your item %s. Other item %s (%.20s). Contact: %s",
             pct, a->id, b->id, b->name, b->phone);
    sendSMS(a->phone, msg);
    snprintf(msg, sizeof(msg),
             "Lost&Found: %d%% match for your item %s. Other item %s (%.20s). Contact: %s",
             pct, b->id, a->id, a->name, a->phone);
    sendSMS(b->phone, msg);
}

/* =====================================================
 *  SMART MATCHING WITH PERCENTAGE
 * ===================================================== */

static const char *STOP_WORDS[] = {
    "the", "and", "with", "has", "have", "was", "for", "from", "near", "very",
    "this", "that", "are", "its", "lost", "found", "item", "there", "which",
    "of", "in", "on", "at", "to", "is", "it", "as", "by", "an", "or", NULL
};

int isStopWord(const char *w) {
    for (int i = 0; STOP_WORDS[i]; i++)
        if (strcmp(w, STOP_WORDS[i]) == 0) return 1;
    return 0;
}

void addWord(char w[][WORD_LEN], int *n, char *cur) {
    int len = (int)strlen(cur);
    if (len < 2 || isStopWord(cur)) return;
    if (len > 3 && cur[len - 1] == 's') cur[len - 1] = '\0';   /* keys -> key */
    for (int i = 0; i < *n; i++)
        if (strcmp(w[i], cur) == 0) return;
    if (*n < MAX_WORDS) strcpy(w[(*n)++], cur);
}

int tokenize(const char *text, char w[][WORD_LEN]) {
    int n = 0, len = 0;
    char cur[WORD_LEN];
    for (const char *p = text; ; p++) {
        if (isalnum((unsigned char)*p)) {
            if (len < WORD_LEN - 1) cur[len++] = (char)tolower((unsigned char)*p);
        } else {
            if (len > 0) { cur[len] = '\0'; addWord(w, &n, cur); len = 0; }
            if (*p == '\0') break;
        }
    }
    return n;
}

int sharedWords(char a[][WORD_LEN], int na, char b[][WORD_LEN], int nb) {
    int count = 0;
    for (int i = 0; i < na; i++)
        for (int j = 0; j < nb; j++)
            if (strcmp(a[i], b[j]) == 0) count++;
    return count;
}

/* overlap in percent: shared words / words in the shorter list */
int overlapPercent(char a[][WORD_LEN], int na, char b[][WORD_LEN], int nb) {
    int smaller = na < nb ? na : nb;
    if (smaller == 0) return 0;
    int p = sharedWords(a, na, b, nb) * 100 / smaller;
    return p > 100 ? 100 : p;
}

/*  Similarity % = 60% item name + 30% name&description + 10% location  */
int similarityPercent(const Item *a, const Item *b) {
    char nameA[MAX_WORDS][WORD_LEN], nameB[MAX_WORDS][WORD_LEN];
    char allA[MAX_WORDS][WORD_LEN],  allB[MAX_WORDS][WORD_LEN];
    char locA[MAX_WORDS][WORD_LEN],  locB[MAX_WORDS][WORD_LEN];
    char textA[170], textB[170];

    int nA = tokenize(a->name, nameA), nB = tokenize(b->name, nameB);
    snprintf(textA, sizeof(textA), "%s %s", a->name, a->description);
    snprintf(textB, sizeof(textB), "%s %s", b->name, b->description);
    int tA = tokenize(textA, allA), tB = tokenize(textB, allB);
    int lA = tokenize(a->location, locA), lB = tokenize(b->location, locB);

    int nameP = overlapPercent(nameA, nA, nameB, nB);
    int allP  = overlapPercent(allA, tA, allB, tB);
    int locP  = overlapPercent(locA, lA, locB, lB);

    return (60 * nameP + 30 * allP + 10 * locP) / 100;
}

/* compares one item with all items of the opposite kind.
   sendSms = 1 -> SMS goes to both persons when similarity >= SMS_MIN */
int showMatches(int idx, int sendSms) {
    int want, found = 0;
    if (items[idx].status == LOST)       want = FOUND;
    else if (items[idx].status == FOUND) want = LOST;
    else return 0;

    for (int i = 0; i < itemCount; i++) {
        if (i == idx || items[i].status != want) continue;
        int pct = similarityPercent(&items[idx], &items[i]);
        if (pct < MATCH_SHOW_MIN) continue;

        if (!found) {
            printf("\n*****************************************\n");
            printf("  NOTIFICATION: POSSIBLE MATCH FOUND!\n");
            printf("*****************************************\n");
        }
        printf("\n>>> SIMILARITY: %d%% (%s)", pct, pct >= HIGH_MIN ? "HIGH" : "POSSIBLE");
        printItem(&items[i]);
        if (sendSms && pct >= SMS_MIN) notifyPair(&items[idx], &items[i], pct);
        found++;
    }
    if (found)
        printf("\n%d possible match(es). Contact the person using the phone number shown.\n", found);
    return found;
}

int listAllMatches(int print) {
    int count = 0;
    for (int i = 0; i < itemCount; i++) {
        if (items[i].status != LOST) continue;
        for (int j = 0; j < itemCount; j++) {
            if (items[j].status != FOUND) continue;
            int pct = similarityPercent(&items[i], &items[j]);
            if (pct < MATCH_SHOW_MIN) continue;
            if (print)
                printf("\nLOST %s (%s)  <-->  FOUND %s (%s) : %d%% similar\n",
                       items[i].id, items[i].name, items[j].id, items[j].name, pct);
            count++;
        }
    }
    if (print && !count) printf("\nNo possible matches right now.\n");
    return count;
}

void checkMyMatches(void) {
    char id[10];
    getId("\nEnter your Item ID (e.g. LF001): ", id);
    int idx = findById(id);
    if (idx < 0) { printf("\nNo item with that ID.\n"); return; }
    if (items[idx].status == CLAIMED) { printf("\nThis item is already marked CLAIMED.\n"); return; }
    printf("\nChecking matches for %s (%s)...\n", items[idx].id, items[idx].name);
    if (!showMatches(idx, 0)) printf("\nNo matching items yet. Please check again later.\n");
}

/* =====================================================
 *  REPORTING AN ITEM (with review / edit before submit)
 * ===================================================== */

void addItem(int status) {
    if (itemCount >= MAX_ITEMS) { printf("Database full!\n"); return; }

    char name[50], desc[100], loc[50], date[12], phone[11], photo[200] = "";
    int isLost = (status == LOST);

    printf("\n--- Report %s Item ---\n", isLost ? "Lost" : "Found");
    getText("Item name        : ", name, sizeof(name), 0);
    getText("Description      : ", desc, sizeof(desc), 0);
    getText(isLost ? "Where lost?      : " : "Where found?     : ", loc, sizeof(loc), 0);
    getDate("Date (DD-MM-YYYY): ", date, 0);
    getPhone("Phone (10 digits): ", phone, 0);
    getPhoto("Photo file path (Enter to skip): ", photo, sizeof(photo));

    for (;;) {
        printf("\n=========== REVIEW YOUR DETAILS ===========\n");
        printf("1. Item name   : %s\n", name);
        printf("2. Description : %s\n", desc);
        printf("3. Location    : %s\n", loc);
        printf("4. Date        : %s\n", date);
        printf("5. Phone       : %s\n", phone);
        printf("6. Photo       : %s\n", photo[0] ? photo : "(none)");
        printf("===========================================\n");
        printf("Type 1-6 to EDIT that detail\n7 to SUBMIT\n8 to CANCEL\n");

        int c = getChoice("Your choice: ", 1, 8);
        if (c == 1) getText("New item name        : ", name, sizeof(name), 0);
        else if (c == 2) getText("New description      : ", desc, sizeof(desc), 0);
        else if (c == 3) getText("New location         : ", loc, sizeof(loc), 0);
        else if (c == 4) getDate("New date (DD-MM-YYYY): ", date, 0);
        else if (c == 5) getPhone("New phone (10 digits) : ", phone, 0);
        else if (c == 6) getPhoto("New photo path (Enter to remove photo): ", photo, sizeof(photo));
        else if (c == 8) { printf("\nCancelled. Nothing was saved.\n"); return; }
        else break;   /* 7 = submit */
    }

    Item it;
    memset(&it, 0, sizeof(it));
    generateId(it.id);
    strcpy(it.name, name);
    strcpy(it.description, desc);
    strcpy(it.location, loc);
    strcpy(it.date, date);
    strcpy(it.phone, phone);
    it.status = status;
    if (photo[0] && !copyPhoto(photo, it.id, it.photo, sizeof(it.photo))) {
        it.photo[0] = '\0';
        printf("\nWarning: photo could not be saved. Item saved without photo.\n");
    }

    items[itemCount++] = it;
    saveItems();
    printf("\nItem submitted successfully! Your Item ID is: %s\n", it.id);
    printf("Please note it down to track your item.\n");

    if (!showMatches(itemCount - 1, 1))
        printf("\nNo matching items yet. Use 'Check matches' with your ID later.\n");
}

void searchByKeyword(void) {
    char key[50], hay[260];
    getText("\nEnter keyword (name/description/location): ", key, sizeof(key), 0);
    toLowerStr(key);
    int found = 0;
    for (int i = 0; i < itemCount; i++) {
        snprintf(hay, sizeof(hay), "%s %s %s", items[i].name,
                 items[i].description, items[i].location);
        toLowerStr(hay);
        if (strstr(hay, key)) { printItem(&items[i]); found++; }
    }
    if (!found) printf("\nNo matching items found.\n");
}

void trackById(void) {
    char id[10];
    getId("\nEnter Item ID (e.g. LF001): ", id);
    int idx = findById(id);
    if (idx < 0) printf("\nNo item with that ID.\n");
    else printItem(&items[idx]);
}

/* =====================================================
 *  ADMIN
 * ===================================================== */

int adminLogin(void) {
    char user[30], pass[30];
    for (int tries = 1; tries <= 3; tries++) {
        getText("\nAdmin username: ", user, sizeof(user), 0);
        getText("Admin password: ", pass, sizeof(pass), 0);
        if (strcmp(user, ADMIN_USER) == 0 && strcmp(pass, adminPass) == 0) {
            printf("\nLogin successful. Welcome, Admin!\n");
            int m = listAllMatches(0);
            if (m) printf(">> %d possible lost/found match(es) waiting. See option 6.\n", m);
            return 1;
        }
        printf("Wrong credentials. Attempts left: %d\n", 3 - tries);
    }
    return 0;
}

void changeStatus(void) {
    char id[10];
    listCompact();
    getId("\nEnter Item ID: ", id);
    int idx = findById(id);
    if (idx < 0) { printf("Item not found.\n"); return; }
    printItem(&items[idx]);
    printf("\n1. Mark as LOST\n2. Mark as FOUND\n3. Mark as CLAIMED (returned to owner)\n");
    int s = getChoice("Choose new status (1-3): ", 1, 3);
    items[idx].status = s;
    saveItems();
    printf("Status updated to %s.\n", statusText(s));
}

void editItem(void) {
    char id[10], buf[200];
    listCompact();
    getId("\nEnter Item ID to edit: ", id);
    int idx = findById(id);
    if (idx < 0) { printf("Item not found.\n"); return; }
    Item *it = &items[idx];
    printItem(it);
    printf("\n(Press Enter to keep the current value)\n");

    getText("New name        : ", buf, sizeof(it->name), 1);
    if (buf[0]) strcpy(it->name, buf);
    getText("New description : ", buf, sizeof(it->description), 1);
    if (buf[0]) strcpy(it->description, buf);
    getText("New location    : ", buf, sizeof(it->location), 1);
    if (buf[0]) strcpy(it->location, buf);
    getDate("New date        : ", buf, 1);
    if (buf[0]) strcpy(it->date, buf);
    getPhone("New phone       : ", buf, 1);
    if (buf[0]) strcpy(it->phone, buf);

    getPhoto("New photo path  : ", buf, sizeof(buf));
    if (buf[0]) {
        char newName[40];
        if (copyPhoto(buf, it->id, newName, sizeof(newName))) {
            if (it->photo[0] && strcmp(it->photo, newName) != 0) remove(it->photo);
            strcpy(it->photo, newName);
        } else printf("Photo could not be saved.\n");
    }

    saveItems();
    printf("Item updated.\n");
}

void deleteItem(void) {
    char id[10];
    listCompact();
    getId("\nEnter Item ID to delete: ", id);
    int idx = findById(id);
    if (idx < 0) { printf("Item not found.\n"); return; }
    printItem(&items[idx]);
    if (!getYesNo("Delete this item? (y/n): ")) { printf("Cancelled.\n"); return; }
    if (items[idx].photo[0]) remove(items[idx].photo);
    for (int i = idx; i < itemCount - 1; i++) items[i] = items[i + 1];
    itemCount--;
    saveItems();
    printf("Item deleted.\n");
}

void changePassword(void) {
    char oldp[30], newp[30], again[30];
    getText("\nCurrent password: ", oldp, sizeof(oldp), 0);
    if (strcmp(oldp, adminPass) != 0) { printf("Wrong password.\n"); return; }
    for (;;) {
        getText("New password    : ", newp, sizeof(newp), 0);
        getText("Confirm password: ", again, sizeof(again), 0);
        if (strcmp(newp, again) == 0) break;
        printf("ENTER CORRECTLY\n(Both passwords must be the same)\n");
    }
    strcpy(adminPass, newp);
    savePassword();
    printf("Password changed.\n");
}

void adminMenu(void) {
    int ch;
    do {
        printf("\n========== ADMIN PANEL ==========\n");
        printf("1. View all items\n");
        printf("2. Change item status (lost/found/claimed)\n");
        printf("3. Edit item details\n");
        printf("4. Delete an item\n");
        printf("5. Add a found item\n");
        printf("6. View all possible matches (with %%)\n");
        printf("7. Change admin password\n");
        printf("8. Logout\n");
        ch = getChoice("Enter choice: ", 1, 8);
        switch (ch) {
            case 1: listItems(0); pause_screen(); break;
            case 2: changeStatus(); pause_screen(); break;
            case 3: editItem(); pause_screen(); break;
            case 4: deleteItem(); pause_screen(); break;
            case 5: addItem(FOUND); pause_screen(); break;
            case 6: listAllMatches(1); pause_screen(); break;
            case 7: changePassword(); pause_screen(); break;
            case 8: printf("Logged out.\n"); break;
        }
    } while (ch != 8);
}

/* =====================================================
 *  MAIN MENU
 * ===================================================== */

int main(void) {
    int ch;
    loadItems();
    loadPassword();

    do {
        printf("\n=====================================\n");
        printf("   LOST AND FOUND MANAGEMENT SYSTEM\n");
        printf("=====================================\n");
        printf("1. Report a LOST item\n");
        printf("2. Report a FOUND item\n");
        printf("3. View all LOST items\n");
        printf("4. View all FOUND items\n");
        printf("5. Search items by keyword\n");
        printf("6. Track an item by ID\n");
        printf("7. Check matches for my item\n");
        printf("8. Admin login\n");
        printf("9. Exit\n");
        ch = getChoice("Enter choice: ", 1, 9);

        switch (ch) {
            case 1: addItem(LOST); pause_screen(); break;
            case 2: addItem(FOUND); pause_screen(); break;
            case 3: listItems(LOST); pause_screen(); break;
            case 4: listItems(FOUND); pause_screen(); break;
            case 5: searchByKeyword(); pause_screen(); break;
            case 6: trackById(); pause_screen(); break;
            case 7: checkMyMatches(); pause_screen(); break;
            case 8:
                if (adminLogin()) adminMenu();
                else printf("Too many failed attempts.\n");
                break;
            case 9: printf("\nThank you! Goodbye.\n"); break;
        }
    } while (ch != 9);

    return 0;
}
