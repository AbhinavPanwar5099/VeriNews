VeriNews - News Credibility and Verification System (with live news)
Team ByteForce, Graphic Era University

HOW TO RUN
  Windows: double-click run.bat      (console program)
           double-click web.bat      (website at http://localhost:8080, with live checking)
  Any system: g++ -std=c++11 main.cpp -o verinews   then   ./verinews   (or ./verinews --web)
  (On Windows the compile command also needs -lws2_32, for the website part.)
  Keep the "data" folder next to main.cpp. Internet is needed for live checking.
  First choose menu option 9 to test the internet connection.

HOW IT WORKS (option 1)
  1. The headline is cleaned into keywords.
  2. The program downloads the latest Google News reports for those keywords
     (and a second search for fact-check articles) using the "curl" command.
  3. New reports are added to the linked list, hash table and BST, and saved
     in data/live_news.txt, so they can be used again later, even offline.
     Reports from publishers rated 70+ in sources.txt count as TRUSTED.
     Only reports returned by THIS search count as support, so old news
     about a similar topic cannot "confirm" a new claim.
  4. The hash table finds all news sharing words with the headline, the
     similarity is calculated, and insertion sort puts the best match first.
  5. Score = 40 x keyword match + 40 x trusted publishers + 20 x source trust
     80-100 LIKELY VERIFIED, 50-79 NEEDS FURTHER VERIFICATION, below 50 POTENTIALLY SUSPICIOUS
     Nine rules can change the answer (see the top of main.cpp). Important ones:
     - a fact-check article matches the headline           -> POTENTIALLY SUSPICIOUS
     - trusted reports say the opposite ("lose" vs "win")  -> POTENTIALLY SUSPICIOUS
     - the search worked but no publisher reports it        -> POTENTIALLY SUSPICIOUS

FILES
  main.cpp             the whole program (13 parts; Part 13 is the small web server)
  website/index.html   the website; served by the program, it can check headlines live
  data/news.txt        our own stored news, including known fake stories
  data/sources.txt     publishers and how much we trust them (0-100)
  data/test_cases.txt  20 headlines with the expected answer (menu option 8)
  data/live_news.txt   created automatically: reports downloaded online
  data/history.txt     created automatically: every check

DATA STRUCTURES
  Linked list  all news records          Hash table  word -> news IDs
  BST          news sorted by date       Stack       recent checks
  Queue        batch checking            Sorting     insertion sort of matches
  Searching    linear search (stop words, sources, publishers)
