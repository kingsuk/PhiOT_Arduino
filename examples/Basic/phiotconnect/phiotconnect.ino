#include <PhiOT.h>

PhiOT morse("1002");

void setup() {
  Serial.begin(115200);
  morse.Initialize();
  

}

void loop() {
  morse.phiLoop();
}