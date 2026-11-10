//  =====================================================================
//  VeriNews : News Credibility and Verification System
//  Team ByteForce (DSCPP-III-2026-T224), Graphic Era University
//
//  A user types a news headline. The program compares it with stored
//  news (verified and fake) and gives a score from 0 to 100 and one of
//  three answers: LIKELY VERIFIED, NEEDS FURTHER VERIFICATION or
//  POTENTIALLY SUSPICIOUS. 
//
//  Rules that can change the answer (checked in this order):
//    1. Fewer than 2 useful words            -> NEEDS FURTHER VERIFICATION
//    2. Contains "not"/"fake"... and matches  -> NEEDS FURTHER VERIFICATION
//    3. Matches a news proved FALSE           -> POTENTIALLY SUSPICIOUS
//    4. Matches news not checked yet          -> NEEDS FURTHER VERIFICATION
//    5. No supporting news                    -> SUSPICIOUS if the source is bad,
//                                                otherwise NEEDS FURTHER VERIFICATION
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
//  Compile:  g++ main.cpp -o verinews
//  Run from the folder that contains the "data" folder.
// =====================================================================
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
using namespace std;

const int MAX_KEYWORDS = 30;   // most keywords we keep from one headline
const int MAX_ID = 1000;       // news IDs must be smaller than this
const int MAX_MATCHES = 200;   // most matching news we look at for one claim

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
    const string DENIAL[] = {"not", "no", "never", "fake", "false", "hoax", "myth", "rumour", "rumor", "deny", "denies"};
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

// Reads one line from the keyboard. Stops the program if input ends.
string readLine(const string& prompt) {
    cout << prompt;
    string line;
    if (!getline(cin, line)) {
        cout << "\nInput closed. Goodbye.\n";
        exit(0);
    }
    return line;
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
    string status;    // VERIFIED, DEBUNKED or UNVERIFIED
    string keywords[MAX_KEYWORDS];
    int keywordCount;

    News() : id(0), keywordCount(0) {}

    void print() const {
        cout << "  #" << id << " [" << status << "] " << date << " | " << source << "\n      " << headline << "\n";
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

    void printAll() const {
        for (NewsNode* p = head; p != NULL; p = p->next) p->data.print();
        cout << "  Total: " << count << " news records.\n";
    }

    int size() const { return count; }
    NewsNode* first() const { return head; }
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

    // Linear search. Returns the rating, or -1 if the source is unknown.
    int findRating(const string& name) const {
        for (int i = 0; i < count; i++) {
            if (toLowerCase(names[i]) == toLowerCase(name)) return ratings[i];
        }
        return -1;
    }
};

// ---------------------------------------------------------------------
// PART 9: THE VERIFIER (the main logic)
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

// Weights of the four factors (they add up to 100) and two limits.
const double W_MATCH = 45, W_SOURCE = 25, W_SUPPORT = 20, W_DATE = 10;
const double STRONG = 0.5;     // similarity that means "same story"
const double RELEVANT = 0.3;   // below this, a stored news item is not evidence

class Verifier {
private:
    NewsList& list;
    HashTable& index;
    SourceList& sources;

public:
    Verifier(NewsList& l, HashTable& h, SourceList& s) : list(l), index(h), sources(s) {}

    CheckResult check(const string& claim, const string& source, const string& date, bool showReport) {
        CheckResult result;
        result.claim = claim;

        // Step 1: keywords of the claim
        string words[MAX_KEYWORDS];
        int wordCount = extractKeywords(claim, words, MAX_KEYWORDS);

        // Step 2: use the HASH TABLE to count shared words with every stored news
        int shared[MAX_ID] = {0};             // shared[id] = number of common keywords
        int ids[MAX_MATCHES];
        for (int w = 0; w < wordCount; w++) {
            int found = index.search(words[w], ids, MAX_MATCHES);
            for (int k = 0; k < found; k++) shared[ids[k]]++;
        }

        // Step 3: similarity = 2 x common / (claim words + news words)
        Match matches[MAX_MATCHES];
        int matchCount = 0;
        for (int id = 0; id < MAX_ID && matchCount < MAX_MATCHES; id++) {
            if (shared[id] == 0) continue;
            News* n = list.findById(id);
            if (n == NULL) continue;
            matches[matchCount].news = n;
            matches[matchCount].similarity = 2.0 * shared[id] / (wordCount + n->keywordCount);
            matchCount++;
        }
        sortMatches(matches, matchCount);    // Step 4: best match first

        // Step 5: look at the evidence
        double bestVerified = 0, bestFake = 0, bestUnverified = 0;
        News* verifiedNews = NULL;
        News* fakeNews = NULL;
        News* unverifiedNews = NULL;
        int supporting = 0;                   // verified news that tell the same story
        for (int i = 0; i < matchCount; i++) {
            News* n = matches[i].news;
            double s = matches[i].similarity;
            if (n->status == "VERIFIED") {
                if (verifiedNews == NULL) { verifiedNews = n; bestVerified = s; }
                if (s >= STRONG) supporting++;
            } else if (n->status == "DEBUNKED" && fakeNews == NULL) {
                fakeNews = n;
                bestFake = s;
            } else if (n->status == "UNVERIFIED" && unverifiedNews == NULL) {
                unverifiedNews = n;
                bestUnverified = s;
            }
        }
        bool hasEvidence = verifiedNews != NULL && bestVerified >= RELEVANT;

        // Step 6: the four factors, each between 0 and 1
        double matchValue = hasEvidence ? bestVerified : 0.0;

        int rating = sources.findRating(source);
        double sourceValue = rating >= 0 ? rating / 100.0 : 0.3;   // unknown source -> 0.3

        double supportValue = (supporting >= 3 ? 3 : supporting) / 3.0;

        double dateValue = 0.0;
        if (hasEvidence) {
            if (date.empty()) dateValue = 0.5;                                    // no date given
            else if (date.substr(0, 7) == verifiedNews->date.substr(0, 7)) dateValue = 1.0; // same month
            else if (date.substr(0, 4) == verifiedNews->date.substr(0, 4)) dateValue = 0.5; // same year
        }

        // Step 7: score out of 100
        double score = W_MATCH * matchValue + W_SOURCE * sourceValue + W_SUPPORT * supportValue + W_DATE * dateValue;
        score = (int)(score * 10 + 0.5) / 10.0;                      // round to 1 decimal

        // Step 8: verdict from the score
        string verdict, reason;
        if (score >= 80) verdict = "LIKELY VERIFIED";
        else if (score >= 50) verdict = "NEEDS FURTHER VERIFICATION";
        else verdict = "POTENTIALLY SUSPICIOUS";

        // Step 9: special rules that override the score
        if (wordCount < 2) {
            verdict = "NEEDS FURTHER VERIFICATION";
            reason = "Too few meaningful words to compare.";
        } else if (hasDenialWord(claim) && (bestFake >= STRONG || bestVerified >= STRONG)) {
            verdict = "NEEDS FURTHER VERIFICATION";
            reason = "It contains a word like 'not' or 'fake'. Matching words cannot tell if it agrees or disagrees with the stored news.";
        } else if (fakeNews != NULL && bestFake >= STRONG && bestFake >= bestVerified) {
            verdict = "POTENTIALLY SUSPICIOUS";
            reason = "It closely matches a story that was already proved FALSE (#" + to_string(fakeNews->id) + ").";
            if (score > 30) score = 30;
        } else if (unverifiedNews != NULL && bestUnverified >= STRONG && bestUnverified > bestVerified) {
            verdict = "NEEDS FURTHER VERIFICATION";
            reason = "The closest stored news (#" + to_string(unverifiedNews->id) + ") has not been checked yet.";
        } else if (!hasEvidence) {
            if (rating >= 0 && rating < 40) {
                verdict = "POTENTIALLY SUSPICIOUS";
                reason = "No stored news supports it, and the source is not reliable.";
            } else {
                verdict = "NEEDS FURTHER VERIFICATION";
                reason = "No stored news supports it. Missing evidence does not prove it is false.";
            }
        }
        result.verdict = verdict;
        result.score = score;

        if (showReport) {
            cout << "\n============== VERIFICATION REPORT ==============\n";
            cout << "Claim    : " << claim << "\n";
            cout << "Keywords :";
            for (int w = 0; w < wordCount; w++) cout << " " << words[w];
            cout << "\nSource   : " << (source.empty() ? "(not given)" : source);
            if (rating >= 0) cout << "  (trust " << rating << "/100)";
            cout << "\n-------------------------------------------------\n";
            cout << "Keyword match   " << matchValue << " x " << (int)W_MATCH << " = " << matchValue * W_MATCH << "\n";
            cout << "Source trust    " << sourceValue << " x " << (int)W_SOURCE << " = " << sourceValue * W_SOURCE << "\n";
            cout << "Supporting news " << supportValue << " x " << (int)W_SUPPORT << " = " << supportValue * W_SUPPORT
                 << "   (" << supporting << " verified reports)\n";
            cout << "Date match      " << dateValue << " x " << (int)W_DATE << " = " << dateValue * W_DATE << "\n";
            cout << "-------------------------------------------------\n";
            cout << "SCORE   : " << score << " / 100\n";
            cout << "VERDICT : " << verdict << "\n";
            if (!reason.empty()) cout << "Reason  : " << reason << "\n";
            cout << "Closest stored news:\n";
            for (int i = 0; i < matchCount && i < 3; i++) {
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
// PART 10: LOADING AND SAVING FILES
// ---------------------------------------------------------------------
// Adds one news record to the linked list, the hash table and the tree.
void storeNews(News news, NewsList& list, HashTable& index, DateTree& tree) {
    news.keywordCount = extractKeywords(news.headline, news.keywords, MAX_KEYWORDS);
    News* stored = list.addAtEnd(news);
    for (int k = 0; k < stored->keywordCount; k++) index.insert(stored->keywords[k], stored->id);
    tree.insert(stored);
}

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
        if (news.id <= 0 || news.id >= MAX_ID) continue;
        news.headline = parts[1];
        news.source = parts[2];
        news.category = parts[3];
        news.date = parts[4];
        news.status = parts[5];
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
        CheckResult r = verifier.check(parts[0], parts[1], parts[2], false);
        bool ok = r.verdict == codeToVerdict(parts[3]);
        total++;
        if (ok) passed++;
        cout << (ok ? "  PASS  " : "  FAIL  ") << r.verdict << " (" << r.score << ")  " << parts[0] << "\n";
    }
    cout << "  Correct: " << passed << " of " << total << " = " << (total ? passed * 100 / total : 0) << "%\n";
}

// ---------------------------------------------------------------------
// PART 11: MAIN MENU
// ---------------------------------------------------------------------
int main() {
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
    Verifier verifier(list, index, sources);
    cout << fixed << setprecision(2);         // print numbers with 2 decimals
    cout << "===== VeriNews =====\nLoaded " << list.size() << " news records.\n";

    while (true) {
        cout << "\n1. Check a news headline\n"
             << "2. Check many headlines (queue)\n"
             << "3. Show recent checks (stack)\n"
             << "4. Show all stored news (linked list)\n"
             << "5. Show stored news sorted by date (BST)\n"
             << "6. Find stored news containing a word (hash table)\n"
             << "7. Add a news record\n"
             << "8. Run the test cases\n"
             << "0. Exit\n";
        int choice = readNumber("Choice: ", 0, 8);

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
            string date = readLine("Date YYYY-MM-DD (Enter to skip): ");
            if (!date.empty() && !isValidDate(date)) {
                cout << "  Wrong date format, the date will be ignored.\n";
                date = "";
            }
            CheckResult r = verifier.check(claim, source, date, true);
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
                CheckResult r = verifier.check(claim, "", "", false);
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
        }
    }
    return 0;
}
