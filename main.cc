#include <iostream>
#include "player.h"
using namespace std;
int main (int argc, char *argv[]) {
  MediaPlayer player("../trailer.mp4"); 
  player.readData();
  return 0;
}
