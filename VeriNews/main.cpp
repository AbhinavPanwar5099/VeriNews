// =====================================================================
//  VeriNews : News Credibility and Verification System
//  Team ByteForce (DSCPP-III-2026-T224), Graphic Era University
//
//  A user types or pastes a news headline, even one published a few hours
//  ago. The program searches Google News for the latest reports about it,
//  saves them, and compares the headline with those reports and with our
//  own stored news (verified and fake). It then gives a score from 0 to 100
//  and one of three answers: LIKELY VERIFIED, NEEDS FURTHER VERIFICATION or
//  POTENTIALLY SUSPICIOUS.
//
//  Score = 40 x keyword match + 40 x trusted publishers + 20 x source trust
//
//  Rules that can change the answer (checked in this order):
//    1. Fewer than 2 useful words              -> NEEDS FURTHER VERIFICATION
//    2. Contains "not"/"fake"... and matches    -> NEEDS FURTHER VERIFICATION
//    3. Matches a news proved FALSE             -> POTENTIALLY SUSPICIOUS
//    4. Trusted reports say the opposite
//       ("lose" when reports say "win")         -> POTENTIALLY SUSPICIOUS
//    5. A fact-check article matches better than
//       any trusted report, or fewer than 2
//       trusted publishers report the story     -> POTENTIALLY SUSPICIOUS
//    6. Only smaller, unrated publishers report
//       it                                      -> NEEDS FURTHER VERIFICATION
//    7. The online search worked, but NO
//       publisher reports the story             -> POTENTIALLY SUSPICIOUS
//    8. No supporting news (offline)            -> SUSPICIOUS if the source is bad,
//                                                  otherwise NEEDS FURTHER VERIFICATION
//    9. Low score but only a partial match      -> NEEDS FURTHER VERIFICATION
//
//  Data structures used:
//    Linked list  - stores all news records
//    Hash table   - finds news containing a word quickly
//                   (an array of linked lists)
//    Stack        - history of checks, newest on top
//    Queue        - checks many headlines in the order typed
//    BST          - shows news sorted by date
//    Sorting      - insertion sort ranks the matching news
//    Searching    - linear search for sources and stop words
//
//  Live news: the program runs the "curl" command (built into Windows 10/11,
//  Linux and macOS) to download the Google News RSS feed for the headline.
//  Without internet it still works with the stored news only.
//
//  Compile:  g++ -std=c++11 main.cpp -o verinews
//  Run from the folder that contains the "data" folder.
// =====================================================================
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
using namespace std;

const int MAX_KEYWORDS = 30;   // most keywords we keep from one headline
const int MAX_ID = 5000;       // news IDs must be smaller than this
const int MAX_MATCHES = 400;   // most matching news we look at for one claim

// ---------------------------------------------------------------------
// PART 1: TEXT HELPERS (cleaning a headline into keywords)
// ---------------------------------------------------------------------

// Common words that carry no meaning. They are removed from headlines.
const string STOP_WORDS[] = {
    "a", "an", "the", "and", "or", "but", "of", "in", "on", "at", "to", "for",
    "from", "by", "with", "about", "as", "into", "after", "is", "are", "was",
    "were", "be", "been", "has", "have", "had", "do", "does", "did", "will",
    "would", "can", "could", "this", "that", "it", "its", "they", "their",
    "he", "she", "his", "her", "we", "you", "i", "who", "what", "when",
    "where", "how", "all", "any", "says", "said", "news", "viral", "post",
    "report", "reports", "claims", "claim", "breaking", "latest", "today", "up", "down"};
const int STOP_WORD_COUNT = sizeof(STOP_WORDS) / sizeof(STOP_WORDS[0]);

string toLowerCase(string text) {
    for (int i = 0; i < (int)text.length(); i++) {
        if (text[i] >= 'A' && text[i] <= 'Z') text[i] = text[i] - 'A' + 'a';
    }
    return text;
}

// Linear search in the stop-word list.
bool isStopWord(const string& word) {
    for (int i = 0; i < STOP_WORD_COUNT; i++) {
        if (STOP_WORDS[i] == word) return true;
    }
    return false;
}

// Very simple stemming: "notes" -> "note", "lands" -> "land".
// We do not cut "ss" (class) or "us" (virus).
string removePlural(string word) {
    int n = word.length();
    if (n > 3 && word[n - 1] == 's' && word[n - 2] != 's' && word[n - 2] != 'u') {
        return word.substr(0, n - 1);
    }
    return word;
}

bool hasDigit(const string& word) {
    for (int i = 0; i < (int)word.length(); i++) {
        if (word[i] >= '0' && word[i] <= '9') return true;
    }
    return false;
}

// Turns a headline into a list of keywords. Returns how many were found.
// Example: "ISRO's Chandrayaan-3 lands near the Moon!"
//       -> isro, chandrayaan, 3, land, near, moon
int extractKeywords(const string& text, string keywords[], int maxWords) {
    // Step 1: lowercase, keep letters and digits, everything else becomes a space.
    string cleaned = "";
    for (int i = 0; i < (int)text.length(); i++) {
        char c = text[i];
        if (c == '\'' && i + 1 < (int)text.length() && (text[i + 1] == 's' || text[i + 1] == 'S')) {
            i++;                       // skip "'s" (ISRO's -> ISRO)
            continue;
        }
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) cleaned += c;
        else if (c >= 'A' && c <= 'Z') cleaned += (char)(c - 'A' + 'a');
        else cleaned += ' ';
    }

    // Step 2: split into words and keep only the useful ones.
    stringstream words(cleaned);
    string word;
    int count = 0;
    while (words >> word && count < maxWords) {
        if (isStopWord(word)) continue;                    // remove stop words
        if (word.length() < 2 && !hasDigit(word)) continue; // remove single letters
        word = removePlural(word);

        bool alreadyThere = false;                          // no duplicates
        for (int k = 0; k < count; k++) {
            if (keywords[k] == word) alreadyThere = true;
        }
        if (!alreadyThere) {
            keywords[count] = word;
            count++;
        }
    }
    return count;
}

// True if the headline contains a word that denies something ("not", "fake", ...).
// Example: "5G towers do NOT spread coronavirus" has the same keywords as the
// hoax "5G towers spread coronavirus", so word matching alone would be fooled.
bool hasDenialWord(const string& claim) {
    const string DENIAL[] = {"not", "no", "never", "fake", "false", "hoax", "myth", "rumour", "rumor", "deny", "denied"};
    string keywords[MAX_KEYWORDS];
    int n = extractKeywords(claim, keywords, MAX_KEYWORDS);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < 11; j++) {
            if (keywords[i] == DENIAL[j]) return true;
        }
    }
    return false;
}

// Splits "a|b|c" into parts. Returns the number of parts.
int splitLine(const string& line, string parts[], int maxParts) {
    stringstream ss(line);
    string part;
    int count = 0;
    while (count < maxParts && getline(ss, part, '|')) {
        parts[count] = part;
        count++;
    }
    return count;
}

// Checks the form YYYY-MM-DD (digits and dashes in the right places).
bool isValidDate(const string& d) {
    if (d.length() != 10 || d[4] != '-' || d[7] != '-') return false;
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) continue;
        if (d[i] < '0' || d[i] > '9') return false;
    }
    int month = (d[5] - '0') * 10 + (d[6] - '0');
    int day = (d[8] - '0') * 10 + (d[9] - '0');
    return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

// Removes spaces, tabs and line-break characters from both ends.
// Text pasted from a website often has an extra space at the end ("BBC "),
// which would stop the source from being found.
string trim(const string& text) {
    int start = 0, end = (int)text.length() - 1;
    while (start <= end && (text[start] == ' ' || text[start] == '\t' || text[start] == '\r')) start++;
    while (end >= start && (text[end] == ' ' || text[end] == '\t' || text[end] == '\r')) end--;
    return text.substr(start, end - start + 1);
}

// Reads one line from the keyboard. Stops the program if input ends.
string readLine(const string& prompt) {
    cout << prompt;
    string line;
    if (!getline(cin, line)) {
        cout << "\nInput closed. Goodbye.\n";
        exit(0);
    }
    return trim(line);
}

// Keeps asking until the user types a whole number between low and high.
int readNumber(const string& prompt, int low, int high) {
    while (true) {
        string text = readLine(prompt);
        bool ok = !text.empty() && text.length() < 9;
        for (int i = 0; i < (int)text.length(); i++) {
            if (text[i] < '0' || text[i] > '9') ok = false;
        }
        if (ok) {
            int value = stoi(text);
            if (value >= low && value <= high) return value;
        }
        cout << "  Please type a number from " << low << " to " << high << ".\n";
    }
}

// True if "text" contains "part" (both compared in lowercase).
bool containsText(const string& text, const string& part) {
    return toLowerCase(text).find(toLowerCase(part)) != string::npos;
}

// Makes text safe to put inside a web address: letters and digits stay,
// a space becomes "+", every other byte becomes %XX (its hexadecimal code).
// Example: "Durga Puja 19" -> "Durga+Puja+19"
string urlEncode(const string& text) {
    const char HEX[] = "0123456789ABCDEF";
    string out = "";
    for (int i = 0; i < (int)text.length(); i++) {
        unsigned char c = text[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) out += c;
        else if (c == ' ') out += '+';
        else {
            out += '%';
            out += HEX[c / 16];
            out += HEX[c % 16];
        }
    }
    return out;
}

// News feeds write some characters as codes: &amp; means &, &quot; means " ...
string decodeEntities(string text) {
    const string CODES[] = {"&amp;", "&quot;", "&#39;", "&apos;", "&lt;", "&gt;", "&nbsp;"};
    const string CHARS[] = {"&", "\"", "'", "'", "<", ">", " "};
    for (int k = 0; k < 7; k++) {
        size_t pos = text.find(CODES[k]);
        while (pos != string::npos) {
            text.replace(pos, CODES[k].length(), CHARS[k]);
            pos = text.find(CODES[k], pos + 1);
        }
    }
    return text;
}

// Turns a date into a day number (days since 1 January 1970), so two dates
// can be subtracted. This is a standard calendar formula.
long daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

// "Oct" -> 10. Returns 0 if not a month name.
int monthNumber(const string& name) {
    const string MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    for (int i = 0; i < 12; i++) {
        if (MONTHS[i] == name) return i + 1;
    }
    return 0;
}

// Reads a feed date such as "Sat, 17 Oct 2026 15:04:00 GMT".
// Gives back "2026-10-17" and how many hours ago it was published.
bool parseFeedDate(const string& text, string& isoDate, double& hoursAgo) {
    stringstream ss(text);
    string dayName, monthName, timeText;
    int day = 0, year = 0;
    if (!(ss >> dayName >> day >> monthName >> year >> timeText)) return false;
    int month = monthNumber(monthName);
    if (month == 0 || timeText.length() < 8) return false;
    int hour = stoi(timeText.substr(0, 2)), minute = stoi(timeText.substr(3, 2));

    char buffer[11];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", year, month, day);
    isoDate = buffer;

    double published = daysFromCivil(year, month, day) * 86400.0 + hour * 3600.0 + minute * 60.0;
    double now = (double)time(NULL);           // seconds since 1 January 1970 (UTC)
    hoursAgo = (now - published) / 3600.0;
    if (hoursAgo < 0) hoursAgo = 0;
    return true;
}

// ---------------------------------------------------------------------
// PART 2: ONE NEWS RECORD
// ---------------------------------------------------------------------
class News {
public:
    int id;
    string headline;
    string source;
    string category;
    string date;      // YYYY-MM-DD
    string status;    // VERIFIED, DEBUNKED, UNVERIFIED or FACTCHECK
    double hoursAgo;  // for news downloaded now: hours since it was published, else -1
    int seenInCheck;  // number of the check in which the online search returned this news
    string keywords[MAX_KEYWORDS];
    int keywordCount;

    News() : id(0), hoursAgo(-1), seenInCheck(0), keywordCount(0) {}

    void print() const {
        cout << "  #" << id << " [" << status << "] " << date << " | " << source;
        if (hoursAgo >= 0) cout << " | " << (int)hoursAgo << " hours ago";
        cout << "\n      " << headline << "\n";
    }
};

// ---------------------------------------------------------------------
// PART 3: LINKED LIST of all news records
// ---------------------------------------------------------------------
struct NewsNode {
    News data;
    NewsNode* next;
};

class NewsList {
private:
    NewsNode* head;
    NewsNode* tail;
    int count;

public:
    NewsList() : head(NULL), tail(NULL), count(0) {}

    ~NewsList() {                          // free every node
        NewsNode* current = head;
        while (current != NULL) {
            NewsNode* nextNode = current->next;
            delete current;
            current = nextNode;
        }
    }

    // Adds at the end. Returns a pointer to the stored copy.
    News* addAtEnd(const News& news) {
        NewsNode* node = new NewsNode;
        node->data = news;
        node->next = NULL;
        if (head == NULL) {               // list was empty
            head = node;
        } else {
            tail->next = node;            // old last node points to new node
        }
        tail = node;
        count++;
        return &node->data;
    }

    News* findById(int id) {              // walk the list until we find the ID
        for (NewsNode* p = head; p != NULL; p = p->next) {
            if (p->data.id == id) return &p->data;
        }
        return NULL;
    }

    News* findByHeadline(const string& headline) {   // used to avoid saving a news twice
        string wanted = toLowerCase(headline);
        for (NewsNode* p = head; p != NULL; p = p->next) {
            if (toLowerCase(p->data.headline) == wanted) return &p->data;
        }
        return NULL;
    }

    void printAll() const {
        for (NewsNode* p = head; p != NULL; p = p->next) p->data.print();
        cout << "  Total: " << count << " news records.\n";
    }

    int size() const { return count; }
};

// ---------------------------------------------------------------------
// PART 4: HASH TABLE  word -> news IDs   (an array of linked lists)
// ---------------------------------------------------------------------
struct WordNode {
    string word;
    int newsId;
    WordNode* next;
};

class HashTable {
private:
    static const int SIZE = 101;          // number of buckets (a prime number)
    WordNode* buckets[SIZE];

    // Hash function: add the ASCII values of all letters, then take % SIZE.
    // Example: "moon" = 109+111+111+110 = 441, 441 % 101 = 37 -> bucket 37.
    int hashFunction(const string& word) const {
        int sum = 0;
        for (int i = 0; i < (int)word.length(); i++) sum += word[i];
        return sum % SIZE;
    }

public:
    HashTable() {
        for (int i = 0; i < SIZE; i++) buckets[i] = NULL;
    }

    ~HashTable() {
        for (int i = 0; i < SIZE; i++) {
            WordNode* p = buckets[i];
            while (p != NULL) {
                WordNode* nextNode = p->next;
                delete p;
                p = nextNode;
            }
        }
    }

    // Stores the pair (word, newsId) at the front of its bucket's list.
    void insert(const string& word, int newsId) {
        int index = hashFunction(word);
        WordNode* node = new WordNode;
        node->word = word;
        node->newsId = newsId;
        node->next = buckets[index];
        buckets[index] = node;
    }

    // Finds all news IDs stored for this word. Only ONE bucket is searched.
    // Different words can land in the same bucket (a "collision"),
    // so we still compare the word itself.
    int search(const string& word, int ids[], int maxIds) const {
        int index = hashFunction(word);
        int found = 0;
        for (WordNode* p = buckets[index]; p != NULL && found < maxIds; p = p->next) {
            if (p->word == word) {
                ids[found] = p->newsId;
                found++;
            }
        }
        return found;
    }
};

// ---------------------------------------------------------------------
// PART 5: BINARY SEARCH TREE of news, ordered by date
// ---------------------------------------------------------------------
struct TreeNode {
    News* news;
    TreeNode* left;
    TreeNode* right;
};

class DateTree {
private:
    TreeNode* root;

    // Smaller dates go left, other dates go right.
    TreeNode* insertNode(TreeNode* node, News* news) {
        if (node == NULL) {
            TreeNode* newNode = new TreeNode;
            newNode->news = news;
            newNode->left = NULL;
            newNode->right = NULL;
            return newNode;
        }
        // "2016-11-08" < "2023-08-23" also as text, because the format is YYYY-MM-DD
        if (news->date < node->news->date) node->left = insertNode(node->left, news);
        else node->right = insertNode(node->right, news);
        return node;
    }

    // In-order traversal: left, node, right  -> dates in increasing order.
    void inorder(TreeNode* node) const {
        if (node == NULL) return;
        inorder(node->left);
        node->news->print();
        inorder(node->right);
    }

    void destroy(TreeNode* node) {
        if (node == NULL) return;
        destroy(node->left);
        destroy(node->right);
        delete node;
    }

public:
    DateTree() : root(NULL) {}
    ~DateTree() { destroy(root); }
    void insert(News* news) { root = insertNode(root, news); }
    void printSorted() const { inorder(root); }
};

// ---------------------------------------------------------------------
// PART 6: STACK of recent checks (array based, newest on top)
// ---------------------------------------------------------------------
struct CheckResult {
    string claim;
    string verdict;
    double score;
};

class HistoryStack {
private:
    static const int CAPACITY = 100;
    CheckResult items[CAPACITY];
    int top;                                  // index of the top item, -1 when empty

public:
    HistoryStack() : top(-1) {}
    bool isEmpty() const { return top == -1; }
    bool isFull() const { return top == CAPACITY - 1; }

    void push(const CheckResult& r) {
        if (isFull()) {                       // forget the oldest to make space
            for (int i = 0; i < top; i++) items[i] = items[i + 1];
            top--;
        }
        top++;
        items[top] = r;
    }

    // Prints from top to bottom, so the most recent check comes first.
    void printNewestFirst() const {
        if (isEmpty()) {
            cout << "  No checks yet.\n";
            return;
        }
        for (int i = top; i >= 0; i--) {
            cout << "  " << items[i].verdict << "  (" << items[i].score << ")  " << items[i].claim << "\n";
        }
    }
};

// ---------------------------------------------------------------------
// PART 7: CIRCULAR QUEUE of headlines for batch checking
// ---------------------------------------------------------------------
class ClaimQueue {
private:
    static const int CAPACITY = 20;
    string items[CAPACITY];
    int front, rear, count;

public:
    ClaimQueue() : front(0), rear(-1), count(0) {}
    bool isEmpty() const { return count == 0; }
    bool isFull() const { return count == CAPACITY; }

    bool enqueue(const string& claim) {
        if (isFull()) return false;
        rear = (rear + 1) % CAPACITY;         // wrap around to 0 after the last slot
        items[rear] = claim;
        count++;
        return true;
    }

    string dequeue() {
        string claim = items[front];
        front = (front + 1) % CAPACITY;
        count--;
        return claim;
    }

    int size() const { return count; }
};

// ---------------------------------------------------------------------
// PART 8: SOURCES and how much we trust them (0 to 100)
// ---------------------------------------------------------------------
class SourceList {
private:
    static const int CAPACITY = 100;
    string names[CAPACITY];
    int ratings[CAPACITY];
    int count;

public:
    SourceList() : count(0) {}

    bool load(const string& fileName) {
        ifstream file(fileName.c_str());
        if (!file) return false;
        string line, parts[2];
        while (getline(file, line) && count < CAPACITY) {
            if (line.empty() || line[0] == '#') continue;
            if (splitLine(line, parts, 2) != 2) continue;
            names[count] = parts[0];
            ratings[count] = stoi(parts[1]);
            count++;
        }
        return true;
    }

    // Makes names comparable: "The Hindu" = "the hindu" = "Hindu", "Moneycontrol.com" = "moneycontrol".
    static string simpleName(const string& name) {
        string n = toLowerCase(trim(name));
        if (n.find("the ") == 0) n = n.substr(4);
        const string ENDINGS[] = {".com", ".co.in", ".in", ".org", ".net"};
        for (int k = 0; k < 5; k++) {
            int len = ENDINGS[k].length();
            if ((int)n.length() > len && n.substr(n.length() - len) == ENDINGS[k]) n = n.substr(0, n.length() - len);
        }
        return n;
    }

    // Linear search. Returns the rating, or -1 if the source is unknown.
    int findRating(const string& name) const {
        if (trim(name).empty()) return -1;
        for (int i = 0; i < count; i++) {
            if (simpleName(names[i]) == simpleName(name)) return ratings[i];
        }
        return -1;
    }
};

// Fact-checking websites. Their articles usually explain why a claim is FALSE.
bool isFactChecker(const string& source) {
    const string CHECKERS[] = {"alt news", "boom", "factly", "newschecker", "vishvas news", "fact crescendo",
                               "pib fact check", "afp fact check", "snopes", "politifact", "logically facts"};
    for (int i = 0; i < 11; i++) {
        if (toLowerCase(source) == CHECKERS[i]) return true;
    }
    return false;
}

// Words that show an article is saying something is false.
bool hasFalseWords(const string& title) {
    const string WORDS[] = {"fact check", "fact-check", "factcheck", "fake", "false", "misleading",
                            "hoax", "debunk", "not true", "misinformation", "rumour", "rumor"};
    for (int i = 0; i < 12; i++) {
        if (containsText(title, WORDS[i])) return true;
    }
    return false;
}

// Pairs of word groups with opposite meanings (words as they look after removePlural).
// Used to catch fakes that reuse the words of a real story but flip its meaning,
// e.g. "India LOSE final" when every trusted report says "India WIN final".
const int PAIRS = 7;
const string SIDE_A[PAIRS] = {"lose lost loss",
                              "dead died death killed",
                              "arrested arrest",
                              "approve approved passed passe",
                              "rise hike increase",
                              "open opened",
                              "ban banned"};
const string SIDE_B[PAIRS] = {"win won beat beating defeat defeated clinch clinched",
                              "alive survived survive rescued safe",
                              "released freed acquitted bail",
                              "reject rejected failed",
                              "cut fall drop decrease reduce slashed",
                              "close closed shut",
                              "allow allowed lifted lift"};

// True if "word" is one of the space-separated words in "group".
bool inGroup(const string& word, const string& group) {
    stringstream ss(group);
    string w;
    while (ss >> w) {
        if (w == word) return true;
    }
    return false;
}

// True if any keyword of "news" belongs to "group".
bool newsHasGroup(const News* news, const string& group) {
    for (int k = 0; k < news->keywordCount; k++) {
        if (inGroup(news->keywords[k], group)) return true;
    }
    return false;
}

// ---------------------------------------------------------------------
// PART 9: LIVE NEWS from the internet (Google News RSS feed)
// ---------------------------------------------------------------------
// One report found online.
struct LiveItem {
    string title;      // headline without " - Publisher" at the end
    string source;     // publisher, e.g. "The Hindu"
    string date;       // YYYY-MM-DD
    double hoursAgo;   // how long ago it was published
};

class LiveNews {
private:
    static const int MAX_FILE_SIZE = 2000000;   // 2 MB is more than any feed needs

    // Downloads a web page into a file using the "curl" command.
    // The address only contains letters, digits and %XX codes (see urlEncode),
    // so the command cannot be tricked by special characters in a headline.
    bool download(const string& url, const string& fileName) const {
        remove(fileName.c_str());                     // delete an old copy first
        string command = "curl -s -L --max-time 20 -A \"Mozilla/5.0\" -o \"" + fileName + "\" \"" + url + "\"";
        int status = system(command.c_str());
        ifstream test(fileName.c_str());
        return status == 0 && test.good();
    }

    // Returns the text between <tag ...> and </tag> in "block", or "".
    static string between(const string& block, const string& tag) {
        size_t open = block.find("<" + tag);
        if (open == string::npos) return "";
        size_t start = block.find('>', open);
        size_t close = block.find("</" + tag + ">", start);
        if (start == string::npos || close == string::npos) return "";
        string value = block.substr(start + 1, close - start - 1);
        if (value.find("<![CDATA[") == 0) value = value.substr(9, value.length() - 12);   // remove CDATA wrapper
        return decodeEntities(trim(value));
    }

public:
    // Searches Google News for "query". Fills "items" and returns how many were found,
    // or -1 if there was no internet connection.
    int search(const string& query, LiveItem items[], int maxItems) const {
        string url = "https://news.google.com/rss/search?q=" + urlEncode(query) + "&hl=en-IN&gl=IN&ceid=IN:en";
        string fileName = "data/feed.xml";
        if (!download(url, fileName)) return -1;

        ifstream file(fileName.c_str());
        string xml = "", line;
        while (getline(file, line) && (int)xml.length() < MAX_FILE_SIZE) xml += line + "\n";

        // The feed is a list of <item> ... </item> blocks, one per news report.
        int count = 0;
        size_t pos = xml.find("<item>");
        while (pos != string::npos && count < maxItems) {
            size_t end = xml.find("</item>", pos);
            if (end == string::npos) break;
            string block = xml.substr(pos, end - pos);

            LiveItem item;
            item.title = between(block, "title");
            item.source = between(block, "source");
            item.hoursAgo = -1;
            item.date = "";
            parseFeedDate(between(block, "pubDate"), item.date, item.hoursAgo);

            // Google writes titles as "Headline - Publisher"; remove the publisher part.
            size_t dash = item.title.rfind(" - ");
            if (dash != string::npos && item.title.substr(dash + 3) == item.source) item.title = item.title.substr(0, dash);

            if (item.title.length() >= 10 && !item.source.empty() && item.date.length() == 10) {
                items[count] = item;
                count++;
            }
            pos = xml.find("<item>", end);
        }
        return count;
    }
};

// ---------------------------------------------------------------------
// PART 10: THE VERIFIER (the main logic)
// ---------------------------------------------------------------------
struct Match {
    News* news;
    double similarity;   // 0 = nothing in common, 1 = same keywords
};

// Insertion sort: highest similarity first.
void sortMatches(Match matches[], int n) {
    for (int i = 1; i < n; i++) {
        Match current = matches[i];
        int j = i - 1;
        while (j >= 0 && matches[j].similarity < current.similarity) {
            matches[j + 1] = matches[j];      // shift smaller ones to the right
            j--;
        }
        matches[j + 1] = current;
    }
}

// Adds one news record to the linked list, the hash table and the tree.
News* storeNews(News news, NewsList& list, HashTable& index, DateTree& tree) {
    news.keywordCount = extractKeywords(news.headline, news.keywords, MAX_KEYWORDS);
    News* stored = list.addAtEnd(news);
    for (int k = 0; k < stored->keywordCount; k++) index.insert(stored->keywords[k], stored->id);
    tree.insert(stored);
    return stored;
}

// Weights of the three factors (they add up to 100) and the limits.
const double W_MATCH = 40, W_SUPPORT = 40, W_SOURCE = 20;
const double FULL_MATCH = 0.6;   // different publishers word a story differently, so 60% similar = full match
const double STRONG = 0.5;       // similarity that means "clearly the same story"
const double SAME_STORY = 0.35;  // a report this similar counts as reporting the same story
const double RELEVANT = 0.3;     // below this, a stored news item is not evidence
const int TRUSTED = 70;          // publishers rated 70 or more count as trusted

class Verifier {
private:
    NewsList& list;
    HashTable& index;
    DateTree& tree;
    SourceList& sources;
    int& nextId;
    LiveNews live;
    int checkNumber;          // counts the checks, to know which news the latest search returned

    // Downloads the latest reports about the claim and adds the new ones to our news.
    // Returns how many reports were found, or -1 without internet.
    int addLiveReports(const string words[], int wordCount) {
        string query = "", shortQuery = "";
        for (int w = 0; w < wordCount && w < 8; w++) query += words[w] + " ";       // the first 8 keywords
        for (int w = 0; w < wordCount && w < 4; w++) shortQuery += words[w] + " ";  // the first 4 keywords

        const int MAX_ITEMS = 60;
        LiveItem items[MAX_ITEMS];
        int total = 0;
        // Round 0: normal news. Round 1: a shorter search if round 0 found very little
        // (long headlines can be too specific). Round 2: fact-check articles.
        for (int round = 0; round < 3; round++) {
            if (round == 1 && total >= 5) continue;
            string q = round == 0 ? query : (round == 1 ? shortQuery : query + "fact check");
            int found = live.search(q, items, MAX_ITEMS);
            if (found < 0) return -1;
            total += found;
            for (int i = 0; i < found; i++) {
                News* existing = list.findByHeadline(items[i].title);
                if (existing != NULL) {                 // already saved earlier: mark it as found again
                    existing->hoursAgo = items[i].hoursAgo;
                    existing->seenInCheck = checkNumber;
                    continue;
                }
                if (nextId >= MAX_ID) break;            // our storage is full
                News news;
                news.id = nextId;
                news.headline = items[i].title;
                news.source = items[i].source;
                news.category = "live";
                news.date = items[i].date;
                news.hoursAgo = items[i].hoursAgo;
                news.seenInCheck = checkNumber;
                if (isFactChecker(news.source) || hasFalseWords(news.headline)) news.status = "FACTCHECK";
                else if (sources.findRating(news.source) >= TRUSTED) news.status = "VERIFIED";
                else news.status = "UNVERIFIED";
                for (int c = 0; c < (int)news.headline.length(); c++) {
                    if (news.headline[c] == '|') news.headline[c] = '-';   // '|' separates fields in our file
                }
                storeNews(news, list, index, tree);
                nextId++;
                ofstream cache("data/live_news.txt", ios::app);   // keep it for next time (works offline)
                cache << news.id << "|" << news.headline << "|" << news.source << "|live|" << news.date << "|"
                      << news.status << "\n";
            }
        }
        return total;
    }

public:
    Verifier(NewsList& l, HashTable& h, DateTree& t, SourceList& s, int& n)
        : list(l), index(h), tree(t), sources(s), nextId(n), checkNumber(0) {}

    CheckResult check(const string& claim, const string& source, bool useLive, bool showReport) {
        CheckResult result;
        result.claim = claim;
        checkNumber++;

        // Step 1: keywords of the claim
        string words[MAX_KEYWORDS];
        int wordCount = extractKeywords(claim, words, MAX_KEYWORDS);

        // Step 2: get the latest reports from the internet and store them
        int liveFound = 0;
        if (useLive && wordCount >= 2) {
            if (showReport) cout << "Searching the latest news online...\n";
            liveFound = addLiveReports(words, wordCount);
            if (liveFound < 0 && showReport) cout << "No internet connection: checking stored news only.\n";
        }

        // Step 3: use the HASH TABLE to count shared words with every stored news
        static int shared[MAX_ID];            // shared[id] = number of common keywords
        for (int id = 0; id < MAX_ID; id++) shared[id] = 0;
        int ids[MAX_MATCHES];
        for (int w = 0; w < wordCount; w++) {
            int found = index.search(words[w], ids, MAX_MATCHES);
            for (int k = 0; k < found; k++) shared[ids[k]]++;
        }

        // Step 4: similarity = 2 x common / (claim words + news words)
        static Match matches[MAX_MATCHES];
        int matchCount = 0;
        for (int id = 0; id < MAX_ID && matchCount < MAX_MATCHES; id++) {
            if (shared[id] == 0) continue;
            News* n = list.findById(id);
            if (n == NULL) continue;
            matches[matchCount].news = n;
            matches[matchCount].similarity = 2.0 * shared[id] / (wordCount + n->keywordCount);
            matchCount++;
        }
        sortMatches(matches, matchCount);    // Step 5: best match first

        // When the online search worked, only reports returned by THIS search count as
        // support. Old stored news about a similar topic must not "confirm" a new claim.
        bool liveWorked = useLive && liveFound >= 0;

        // Step 6: look at the evidence
        double bestVerified = 0, bestFake = 0, bestUnverified = 0, bestFactCheck = 0;
        News* verifiedNews = NULL;
        News* fakeNews = NULL;
        News* unverifiedNews = NULL;
        News* factCheckNews = NULL;
        string publishers[20];               // different trusted publishers reporting the same story
        int publisherCount = 0;
        News* reports[MAX_MATCHES];          // the trusted reports that tell the same story
        int reportCount = 0;
        double newestHours = -1;             // age of the newest trusted report found online
        for (int i = 0; i < matchCount; i++) {
            News* n = matches[i].news;
            double s = matches[i].similarity;
            if (n->status == "VERIFIED") {
                if (liveWorked && n->seenInCheck != checkNumber) continue;   // not part of today's search
                if (verifiedNews == NULL) { verifiedNews = n; bestVerified = s; }
                if (s >= SAME_STORY) {
                    reports[reportCount++] = n;
                    bool counted = false;    // linear search: is this publisher already counted?
                    for (int p = 0; p < publisherCount; p++) {
                        if (toLowerCase(publishers[p]) == toLowerCase(n->source)) counted = true;
                    }
                    if (!counted && publisherCount < 20) publishers[publisherCount++] = n->source;
                    if (n->hoursAgo >= 0 && (newestHours < 0 || n->hoursAgo < newestHours)) newestHours = n->hoursAgo;
                }
            } else if (n->status == "DEBUNKED" && fakeNews == NULL) {
                fakeNews = n;
                bestFake = s;
            } else if (n->status == "UNVERIFIED" && unverifiedNews == NULL &&
                       (!liveWorked || n->seenInCheck == checkNumber)) {
                unverifiedNews = n;
                bestUnverified = s;
            } else if (n->status == "FACTCHECK" && factCheckNews == NULL) {
                factCheckNews = n;
                bestFactCheck = s;
            }
        }
        bool hasEvidence = verifiedNews != NULL && bestVerified >= RELEVANT;

        // Do the trusted reports say the OPPOSITE of the claim? (e.g. claim "lose", reports "win")
        string claimSide = "", reportSide = "";
        for (int p = 0; p < PAIRS && claimSide.empty() && reportCount > 0; p++) {
            for (int side = 0; side < 2 && claimSide.empty(); side++) {
                const string& mine = side == 0 ? SIDE_A[p] : SIDE_B[p];
                const string& other = side == 0 ? SIDE_B[p] : SIDE_A[p];
                bool claimHasMine = false;
                for (int w = 0; w < wordCount; w++) claimHasMine = claimHasMine || inGroup(words[w], mine);
                if (!claimHasMine) continue;
                bool reportsHaveMine = false, reportsHaveOther = false;
                for (int r = 0; r < reportCount; r++) {
                    reportsHaveMine = reportsHaveMine || newsHasGroup(reports[r], mine);
                    reportsHaveOther = reportsHaveOther || newsHasGroup(reports[r], other);
                }
                if (!reportsHaveMine && reportsHaveOther) {   // the reports only use the opposite words
                    claimSide = mine;
                    reportSide = other;
                }
            }
        }

        // Step 7: the three factors, each between 0 and 1
        double matchValue = 0.0;
        if (hasEvidence) matchValue = bestVerified >= FULL_MATCH ? 1.0 : bestVerified / FULL_MATCH;
        double supportValue = (publisherCount >= 3 ? 3 : publisherCount) / 3.0;
        int rating = sources.findRating(source);
        double sourceValue = rating >= 0 ? rating / 100.0 : 0.3;   // unknown source -> 0.3

        // Step 8: score out of 100
        double score = W_MATCH * matchValue + W_SUPPORT * supportValue + W_SOURCE * sourceValue;
        score = (int)(score * 10 + 0.5) / 10.0;                      // round to 1 decimal

        // Step 9: verdict from the score
        string verdict, reason, note;
        if (score >= 80) verdict = "LIKELY VERIFIED";
        else if (score >= 50) verdict = "NEEDS FURTHER VERIFICATION";
        else verdict = "POTENTIALLY SUSPICIOUS";

        // Step 10: special rules that override the score
        bool factCheckMatches = factCheckNews != NULL && bestFactCheck >= SAME_STORY;
        if (wordCount < 2) {
            verdict = "NEEDS FURTHER VERIFICATION";
            reason = "Too few meaningful words to compare.";
        } else if (hasDenialWord(claim) && (bestFake >= STRONG || bestVerified >= STRONG)) {
            verdict = "NEEDS FURTHER VERIFICATION";
            reason = "It contains a word like 'not' or 'fake'. Matching words cannot tell if it agrees or disagrees with the news.";
        } else if (fakeNews != NULL && bestFake >= STRONG && bestFake >= bestVerified) {
            verdict = "POTENTIALLY SUSPICIOUS";
            reason = "It closely matches a story that was already proved FALSE (#" + to_string(fakeNews->id) + ").";
            if (score > 30) score = 30;
        } else if (!claimSide.empty()) {
            verdict = "POTENTIALLY SUSPICIOUS";
            reason = "Trusted reports about this story say the opposite. The claim uses words like '" +
                     claimSide.substr(0, claimSide.find(' ')) + "', but the reports use words like '" +
                     reportSide.substr(0, reportSide.find(' ')) + "'.";
            if (score > 30) score = 30;
        } else if (factCheckMatches && (publisherCount < 2 || bestFactCheck > bestVerified)) {
            verdict = "POTENTIALLY SUSPICIOUS";
            reason = "A fact-check article (" + factCheckNews->source + ") is the closest match, or fewer than 2 trusted publishers report it.";
            if (score > 30) score = 30;
        } else if (unverifiedNews != NULL && bestUnverified >= STRONG && publisherCount == 0 &&
                   (sources.findRating(unverifiedNews->source) < 0 || sources.findRating(unverifiedNews->source) >= 50)) {
            verdict = "NEEDS FURTHER VERIFICATION";
            reason = "Only smaller publishers we have not rated as trusted (" + unverifiedNews->source + ") report it.";
        } else if (liveWorked && publisherCount == 0) {
            verdict = "POTENTIALLY SUSPICIOUS";
            reason = "We searched the latest news and no trusted outlet reports this. "
                     "Real news like this is normally reported by major outlets within hours.";
            if (score > 30) score = 30;
        } else if (!hasEvidence) {
            if (rating >= 0 && rating < 40) {
                verdict = "POTENTIALLY SUSPICIOUS";
                reason = "No news supports it, and the source is not reliable.";
            } else {
                verdict = "NEEDS FURTHER VERIFICATION";
                reason = "No news supports it. Missing evidence does not prove it is false.";
            }
        } else if (verdict == "POTENTIALLY SUSPICIOUS" && !(rating >= 0 && rating < 40)) {
            verdict = "NEEDS FURTHER VERIFICATION";
            reason = "It only partly matches trusted news. That is not enough to confirm it, "
                     "but nothing suggests it is false.";
        }
        if (factCheckMatches && verdict != "POTENTIALLY SUSPICIOUS")
            note = "A fact-check article (" + factCheckNews->source + ") mentions this topic. Read it before sharing.";
        result.verdict = verdict;
        result.score = score;

        if (showReport) {
            cout << "\n============== VERIFICATION REPORT ==============\n";
            cout << "Claim    : " << claim << "\n";
            cout << "Keywords :";
            for (int w = 0; w < wordCount; w++) cout << " " << words[w];
            cout << "\nSource   : " << (source.empty() ? "(not given)" : source);
            if (rating >= 0) cout << "  (trust " << rating << "/100)";
            if (useLive && liveFound >= 0) cout << "\nOnline   : " << liveFound << " reports downloaded from Google News";
            cout << "\n-------------------------------------------------\n";
            cout << "Keyword match      " << matchValue << " x " << (int)W_MATCH << " = " << matchValue * W_MATCH << "\n";
            cout << "Trusted publishers " << supportValue << " x " << (int)W_SUPPORT << " = " << supportValue * W_SUPPORT
                 << "   (" << publisherCount << " report the same story)\n";
            cout << "Source trust       " << sourceValue << " x " << (int)W_SOURCE << " = " << sourceValue * W_SOURCE << "\n";
            cout << "-------------------------------------------------\n";
            cout << "SCORE   : " << score << " / 100\n";
            cout << "VERDICT : " << verdict << "\n";
            if (!reason.empty()) cout << "Reason  : " << reason << "\n";
            if (!note.empty()) cout << "Note    : " << note << "\n";
            if (publisherCount > 0 && verdict != "POTENTIALLY SUSPICIOUS") {
                cout << "Reported by:";
                for (int p = 0; p < publisherCount; p++) cout << (p ? ", " : " ") << publishers[p];
                if (newestHours >= 0) cout << "  (newest report " << (int)newestHours << " hours ago)";
                cout << "\n";
            }
            cout << "Closest news:\n";
            for (int i = 0; i < matchCount && i < 5; i++) {
                cout << "  " << (int)(matches[i].similarity * 100) << "% similar ";
                matches[i].news->print();
            }
            if (matchCount == 0) cout << "  (none)\n";
            cout << "=================================================\n";
        }
        return result;
    }
};

// ---------------------------------------------------------------------
// PART 11: LOADING AND SAVING FILES
// ---------------------------------------------------------------------
// File format, one news per line:  id|headline|source|category|YYYY-MM-DD|STATUS
bool loadNews(const string& fileName, NewsList& list, HashTable& index, DateTree& tree, int& nextId) {
    ifstream file(fileName.c_str());
    if (!file) return false;
    string line, parts[6];
    while (getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (splitLine(line, parts, 6) != 6) continue;   // skip broken lines
        News news;
        news.id = stoi(parts[0]);
        if (news.id <= 0 || news.id >= MAX_ID || list.findById(news.id) != NULL) continue;
        news.headline = parts[1];
        news.source = parts[2];
        news.category = parts[3];
        news.date = parts[4];
        news.status = trim(parts[5]);
        storeNews(news, list, index, tree);
        if (news.id >= nextId) nextId = news.id + 1;
    }
    return true;
}

void appendHistory(const CheckResult& r) {
    ofstream file("data/history.txt", ios::app);     // ios::app = add at the end, never erase
    file << r.verdict << "|" << r.score << "|" << r.claim << "\n";
}

// Turns "LIKELY_VERIFIED" (used in test_cases.txt) into "LIKELY VERIFIED".
string codeToVerdict(const string& code) {
    if (code == "LIKELY_VERIFIED") return "LIKELY VERIFIED";
    if (code == "NEEDS_VERIFICATION") return "NEEDS FURTHER VERIFICATION";
    return "POTENTIALLY SUSPICIOUS";
}

// The test cases are checked against the stored news only (no internet),
// so the result is the same every time.
void runTestCases(Verifier& verifier) {
    ifstream file("data/test_cases.txt");
    if (!file) {
        cout << "  data/test_cases.txt not found.\n";
        return;
    }
    string line, parts[4];
    int total = 0, passed = 0;
    while (getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (splitLine(line, parts, 4) < 4) continue;
        CheckResult r = verifier.check(parts[0], parts[1], false, false);
        bool ok = r.verdict == codeToVerdict(trim(parts[3]));
        total++;
        if (ok) passed++;
        cout << (ok ? "  PASS  " : "  FAIL  ") << r.verdict << " (" << r.score << ")  " << parts[0] << "\n";
    }
    cout << "  Correct: " << passed << " of " << total << " = " << (total ? passed * 100 / total : 0) << "%\n";
}

// ---------------------------------------------------------------------
// PART 12: MAIN MENU
// ---------------------------------------------------------------------
int main() {
#ifdef _WIN32
    system("chcp 65001 > nul");               // let the Windows console show symbols like the rupee sign
#endif
    NewsList list;
    HashTable index;
    DateTree tree;
    SourceList sources;
    HistoryStack history;
    int nextId = 1;

    if (!loadNews("data/news.txt", list, index, tree, nextId) || !sources.load("data/sources.txt")) {
        cout << "Could not open the data files. Run the program from the folder that contains \"data\".\n";
        return 1;
    }
    int storedCount = list.size();
    loadNews("data/live_news.txt", list, index, tree, nextId);   // reports saved from earlier online checks
    Verifier verifier(list, index, tree, sources, nextId);
    cout << fixed << setprecision(2);         // print numbers with 2 decimals
    cout << "===== VeriNews =====\nLoaded " << storedCount << " stored news and " << list.size() - storedCount
         << " saved online reports.\n";

    while (true) {
        cout << "\n1. Check a news headline (searches the latest news online)\n"
             << "2. Check many headlines (queue)\n"
             << "3. Show recent checks (stack)\n"
             << "4. Show all stored news (linked list)\n"
             << "5. Show stored news sorted by date (BST)\n"
             << "6. Find stored news containing a word (hash table)\n"
             << "7. Add a news record\n"
             << "8. Run the test cases (stored news only)\n"
             << "9. Test the internet connection\n"
             << "0. Exit\n";
        int choice = readNumber("Choice: ", 0, 9);

        if (choice == 0) {
            cout << "Goodbye.\n";
            break;
        } else if (choice == 1) {
            string claim = readLine("Headline: ");
            if (claim.length() < 10) {
                cout << "  Please type at least 10 characters.\n";
                continue;
            }
            string source = readLine("Source (Enter if unknown): ");
            CheckResult r = verifier.check(claim, source, true, true);
            history.push(r);
            appendHistory(r);
        } else if (choice == 2) {
            ClaimQueue queue;
            cout << "Type one headline per line. Press Enter on an empty line to start.\n";
            while (!queue.isFull()) {
                string claim = readLine("> ");
                if (claim.empty()) break;
                queue.enqueue(claim);
            }
            cout << "Checking " << queue.size() << " headline(s) in the order you typed them:\n";
            while (!queue.isEmpty()) {
                string claim = queue.dequeue();          // first in, first out
                CheckResult r = verifier.check(claim, "", true, false);
                cout << "  " << r.verdict << " (" << r.score << ")  " << claim << "\n";
                history.push(r);
                appendHistory(r);
            }
        } else if (choice == 3) {
            history.printNewestFirst();
        } else if (choice == 4) {
            list.printAll();
        } else if (choice == 5) {
            tree.printSorted();
        } else if (choice == 6) {
            string word = removePlural(toLowerCase(readLine("Word: ")));
            int ids[MAX_MATCHES];
            int found = index.search(word, ids, MAX_MATCHES);
            for (int i = 0; i < found; i++) list.findById(ids[i])->print();
            cout << "  " << found << " news record(s) contain \"" << word << "\".\n";
        } else if (choice == 7) {
            if (nextId >= MAX_ID) {
                cout << "  The database is full.\n";
                continue;
            }
            News news;
            news.id = nextId;
            news.headline = readLine("Headline: ");
            news.source = readLine("Source: ");
            news.category = readLine("Category (e.g. sports): ");
            news.date = readLine("Date YYYY-MM-DD: ");
            int s = readNumber("Status 1=VERIFIED 2=DEBUNKED 3=UNVERIFIED: ", 1, 3);
            news.status = s == 1 ? "VERIFIED" : (s == 2 ? "DEBUNKED" : "UNVERIFIED");
            bool hasBar = news.headline.find('|') != string::npos || news.source.find('|') != string::npos ||
                          news.category.find('|') != string::npos;
            if (news.headline.length() < 10 || !isValidDate(news.date) || hasBar) {
                cout << "  Not saved: headline must be 10+ characters, date YYYY-MM-DD, and no '|' character.\n";
                continue;
            }
            storeNews(news, list, index, tree);
            nextId++;
            ofstream file("data/news.txt", ios::app);
            file << news.id << "|" << news.headline << "|" << news.source << "|" << news.category << "|"
                 << news.date << "|" << news.status << "\n";
            cout << "  Saved as news #" << news.id << ".\n";
        } else if (choice == 8) {
            runTestCases(verifier);
        } else if (choice == 9) {
            LiveNews live;
            LiveItem items[3];
            int found = live.search("India", items, 3);
            if (found < 0) {
                cout << "  No connection. Check the internet, and that the command \"curl --version\" works.\n";
            } else {
                cout << "  Connected. Latest headlines:\n";
                for (int i = 0; i < found; i++)
                    cout << "   - " << items[i].title << " (" << items[i].source << ", " << (int)items[i].hoursAgo << " hours ago)\n";
            }
        }
    }
    return 0;
}
