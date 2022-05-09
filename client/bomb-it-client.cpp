#include <bits/stdc++.h>
#include <getopt.h>

using namespace std;

int main(int argc, char *argv[]) {
  if ()

  int opt;
  while ((opt = getopt(argc, argv, "nt:")) != -1) {
    switch (opt) {
      case 'n':
        cout << "n\n";
        break;
      case 't':
        cout << 't' << " " << optarg << endl;
        break;
      default:
        cout << "Error! " << opt << endl;
    }
  }

  cout << "End!" << endl;
}
