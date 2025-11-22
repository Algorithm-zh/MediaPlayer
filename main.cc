#include "player.h"
using namespace std;


int main (int argc, char *argv[]) {

  MediaPlayer player({"rtsp://admin:admin@172.16.18.109:554/stream2", "rtsp://admin:admin@172.16.18.108:554/stream2", "rtsp://admin:admin@172.16.18.107:554/stream2"}); 
  //MediaPlayer player({"../resources/a.flv", "../resources/a.mp4", "../resources/a.flv"}); 
  player.start();
  return 0;
}
