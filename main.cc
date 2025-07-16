#include <iostream>
#include "player.h"
using namespace std;
int main (int argc, char *argv[]) {
  MediaPlayer player("../a.mp4"); 
  player.readData();
  return 0;
}
