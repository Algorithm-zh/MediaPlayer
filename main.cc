#include "player.h"
using namespace std;


int main (int argc, char *argv[]) {

  MediaPlayer player({"../resources/a.flv", "../resources/a.flv", "../resources/a.flv"}); 
  player.start();
  return 0;
}
