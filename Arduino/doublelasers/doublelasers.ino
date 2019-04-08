int laser1 = A0;
int laser2 = A1;
int value1;
int value2;
String output;

void setup() {
  Serial.begin(115200);
}

void loop() { 
  value1 = 256-analogRead(laser1)/4;
  value2 = 256-analogRead(laser2)/4;
  output = String(value1) + " " + String(value2);
  Serial.println(output);
}
