#include <Wire.h>
#include <Adafruit_DRV2605.h>
#include <Adafruit_CircuitPlayground.h>
//#include "MegunoLink.h" //to transform data
//#include "Filter.h" //to transform data

Adafruit_DRV2605 drv;


//flex sensor readings
const int flexPin = A1;
int flexValue;
bool flex_FoG = false;  //flex sensor indicates FoG
int straight;

float prev_Slope = 0;  //previous roc of wave
float slope = 0;       //current roc of wave

unsigned long flex_prevPeak = 0;


//accelerometer readings
float x, y, z;


//prev accelerometer readings (millis is prev time wave changes sign,
//val is previous output from accelerometer)
unsigned long x_prevMillis = 0;
unsigned long y_prevMillis = 0;
unsigned long z_prevMillis = 0;
unsigned long flex_prevMillis = 0;

float x_prevVal = 0;
float y_prevVal = 0;
float z_prevVal = 0;
float flex_prevVal = 0;


float eq_x, eq_y, eq_z;  // accelerometer output when wearer is standing

unsigned long times[] = { 0, 0, 0, 0 };  //list of 1/2 period time in order of x, y, z, flex sensor

bool accel_FoG = false;  //accelerometer indicates FoG


//time when the most recent valid crossing or peak happened
unsigned long xLastCross = 0;
unsigned long yLastCross = 0;
unsigned long zLastCross = 0;
unsigned long flexLastPeak = 0;


//ignore an old period after this amount of time
const unsigned long ACCEL_EVENT_TIMEOUT = 500;
const unsigned long FLEX_EVENT_TIMEOUT = 700;


//minimum movement required so small sensor noise does not count
const float ACCEL_MIN_AMPLITUDE = 0.15;
const int FLEX_MIN_CHANGE = 4;


//FoG period ranges
//normal walking should generally be slower than this range,
//while FoG-like trembling should have shorter periods
const unsigned long ACCEL_MIN_FOG_PERIOD = 80;
const unsigned long ACCEL_MAX_FOG_PERIOD = 220;

const unsigned long FLEX_MIN_FOG_PERIOD = 100;
const unsigned long FLEX_MAX_FOG_PERIOD = 350;


//variables for checking FoG over a time window
unsigned long detectionWindowStart = 0;

const unsigned long DETECTION_WINDOW = 1500;

int accelFoGVotes = 0;
int flexFoGVotes = 0;
int totalSamples = 0;

bool confirmedFoG = false;


//variables for keeping the vibration active after FoG is detected
unsigned long lastFoGDetected = 0;

const unsigned long FOG_HOLD_TIME = 2000;
const unsigned long VIBRATION_PULSE_TIME = 500;


//long FilterWeight = 5; //transform data
//ExponentialFilter<long> ADCFilter(FilterWeight, 0); //transform data


//float FoG_duration = 300; //time duration between sign changes 2 times,
//change if we want to make it reactive to user feedback


void setup() {

  Serial.begin(115200);

  CircuitPlayground.begin();


  //give the wearer time to stand still during calibration
  Serial.println("Stand still with knee straight for calibration...");
  delay(2000);


  //getting equilibrium values of waves for x,y,z
  //average several readings so one noisy reading is not used
  float totalX = 0;
  float totalY = 0;
  float totalZ = 0;

  for (int i = 0; i < 50; i++) {
    totalX += CircuitPlayground.motionX();
    totalY += CircuitPlayground.motionY();
    totalZ += CircuitPlayground.motionZ();

    delay(20);
  }

  eq_x = totalX / 50.0;
  eq_y = totalY / 50.0;
  eq_z = totalZ / 50.0;


  //the adjusted accelerometer waves start around zero
  x_prevVal = 0;
  y_prevVal = 0;
  z_prevVal = 0;


  //not sure if should be 0, may need to set calibration period for normal walking frequency
  unsigned long startingMillis = millis();

  x_prevMillis = startingMillis;
  y_prevMillis = startingMillis;
  z_prevMillis = startingMillis;
  flex_prevMillis = startingMillis;

  flex_prevPeak = 0;


  // set up driver
  Wire.begin();  //Start the Circuit Playground Express I2C bus

  Serial.println("Starting DRV2605L...");

  if (!drv.begin()) {
    Serial.println("DRV2605L not found.");

    while (true) {
      delay(10);
    }
  }

  drv.selectLibrary(1);
  // Library 1 is used for ERM vibration motors.

  drv.setMode(DRV2605_MODE_REALTIME);
  // Real-time playback mode lets us directly turn vibration on/off.

  drv.setRealtimeValue(0);

  Serial.println("DRV2605L found.");


  //set up pin for flex sensor
  pinMode(flexPin, INPUT);


  //average the straight flex sensor value
  long flexTotal = 0;

  for (int i = 0; i < 100; i++) {
    flexTotal += analogRead(flexPin);
    delay(10);
  }

  straight = flexTotal / 100;

  flex_prevVal = straight;
  flex_prevMillis = millis();


  Serial.print("Straight flex value: ");
  Serial.println(straight);


  //start the first FoG detection window
  detectionWindowStart = millis();

  Serial.println("Calibration complete.");
  Serial.println();
}


void loop() {

  unsigned long currentMillis = millis();
  //get current elapsed time


  //get accelerations, subtract eq values so scaled with median value of 0
  x = CircuitPlayground.motionX() - eq_x;
  y = CircuitPlayground.motionY() - eq_y;
  z = CircuitPlayground.motionZ() - eq_z;


  //calculate half cycle of x,y,z data waves to know how frequent changes are

  //X-axis crosses zero and has enough movement to not just be noise
  if (x_prevVal * x < 0 && abs(x_prevVal) >= ACCEL_MIN_AMPLITUDE && abs(x) >= ACCEL_MIN_AMPLITUDE) {
    unsigned long halfPeriod = currentMillis - x_prevMillis;  //time for zero->zero
    x_prevMillis = currentMillis;                             //set prev for next time wave crosses 0
    times[0] = halfPeriod;
    xLastCross = currentMillis;
  }


  //Y-axis crosses zero and has enough movement to not just be noise
  if (y_prevVal * y < 0 && abs(y_prevVal) >= ACCEL_MIN_AMPLITUDE && abs(y) >= ACCEL_MIN_AMPLITUDE) {
    unsigned long halfPeriod = currentMillis - y_prevMillis;
    y_prevMillis = currentMillis;
    times[1] = halfPeriod;
    yLastCross = currentMillis;
  }


  //Z-axis crosses zero and has enough movement to not just be noise
  if (z_prevVal * z < 0 && abs(z_prevVal) >= ACCEL_MIN_AMPLITUDE && abs(z) >= ACCEL_MIN_AMPLITUDE) {
    unsigned long halfPeriod = currentMillis - z_prevMillis;
    z_prevMillis = currentMillis;
    times[2] = halfPeriod;
    zLastCross = currentMillis;
  }


  //save the current values for the next zero-crossing check
  x_prevVal = x;
  y_prevVal = y;
  z_prevVal = z;


  //set conditions for if times in time[] are within the
  //rapid FoG-like frequency range
  //**need to test diff/calibrate to final knee sleeve

  //count how many accelerometer axes currently indicate rapid movement
  int rapidAxes = 0;


  if (times[0] >= ACCEL_MIN_FOG_PERIOD && times[0] <= ACCEL_MAX_FOG_PERIOD && currentMillis - xLastCross < ACCEL_EVENT_TIMEOUT) {
    rapidAxes++;
  }


  if (times[1] >= ACCEL_MIN_FOG_PERIOD && times[1] <= ACCEL_MAX_FOG_PERIOD && currentMillis - yLastCross < ACCEL_EVENT_TIMEOUT) {
    rapidAxes++;
  }


  if (times[2] >= ACCEL_MIN_FOG_PERIOD && times[2] <= ACCEL_MAX_FOG_PERIOD && currentMillis - zLastCross < ACCEL_EVENT_TIMEOUT) {
    rapidAxes++;
  }


  //require 2 out of 3 axes instead of requiring all 3 axes
  if (rapidAxes >= 2) {
    accel_FoG = true;
  } else {
    accel_FoG = false;
  }


  //get flex data

  //average several flex readings to reduce noise
  long flexTotal = 0;

  for (int i = 0; i < 5; i++) {
    flexTotal += analogRead(flexPin);
  }

  flexValue = flexTotal / 5;


  //diff flex logic
  unsigned long flexDeltaTime =
    currentMillis - flex_prevMillis;


  if (flexDeltaTime > 0) {

    int flexChange = flexValue - (int)flex_prevVal;


    slope =
      flexChange / (flexDeltaTime / 1000.0);


    bool meaningfulFlexMovement =
      abs(flexChange) >= FLEX_MIN_CHANGE;


    bool flexDirectionChanged =
      (slope > 0 && prev_Slope < 0) || (slope < 0 && prev_Slope > 0);


    //detect a flex peak only when movement is large enough
    //and the movement direction changes
    if (
      meaningfulFlexMovement && flexDirectionChanged) {
      unsigned long halfPeriod =
        currentMillis - flex_prevPeak;


      //do not count the first peak as a complete period
      if (flex_prevPeak > 0) {
        times[3] = halfPeriod;
      }


      flex_prevPeak = currentMillis;
      flexLastPeak = currentMillis;
    }


    //only update the previous slope after meaningful movement
    //so tiny sensor noise does not repeatedly change its sign
    if (meaningfulFlexMovement) {
      prev_Slope = slope;
    }
  }


  flex_prevMillis = currentMillis;
  flex_prevVal = flexValue;


  //see if flex signals FoG
  //the flex movement must fall inside the FoG-like period range
  if (
    times[3] >= FLEX_MIN_FOG_PERIOD && times[3] <= FLEX_MAX_FOG_PERIOD && currentMillis - flexLastPeak < FLEX_EVENT_TIMEOUT) {
    flex_FoG = true;
  } else {
    flex_FoG = false;
  }


  //count how often each sensor signals FoG during the detection window
  totalSamples++;


  if (accel_FoG) {
    accelFoGVotes++;
  }


  if (flex_FoG) {
    flexFoGVotes++;
  }


  //check all sensor readings after the full detection window
  if (
    currentMillis - detectionWindowStart >= DETECTION_WINDOW) {
    float accelRatio = 0;
    float flexRatio = 0;


    if (totalSamples > 0) {
      accelRatio =
        (float)accelFoGVotes / totalSamples;

      flexRatio =
        (float)flexFoGVotes / totalSamples;
    }


    //only print results once every detection window
    //so the values are slow enough to read
    Serial.println();
    Serial.println("----- Detection Window Results -----");


    Serial.print("Current acceleration: ");
    Serial.print(x);
    Serial.print(", ");
    Serial.print(y);
    Serial.print(", ");
    Serial.println(z);


    Serial.print("Accelerometer periods: ");
    Serial.print(times[0]);
    Serial.print(", ");
    Serial.print(times[1]);
    Serial.print(", ");
    Serial.println(times[2]);


    Serial.print("Rapid accelerometer axes: ");
    Serial.println(rapidAxes);


    Serial.print("Straight flex value: ");
    Serial.println(straight);


    Serial.print("Current flex value: ");
    Serial.println(flexValue);


    Serial.print("Flex period: ");
    Serial.println(times[3]);


    Serial.print("Total samples: ");
    Serial.println(totalSamples);


    Serial.print("Accelerometer FoG votes: ");
    Serial.println(accelFoGVotes);


    Serial.print("Flex FoG votes: ");
    Serial.println(flexFoGVotes);


    Serial.print("Accelerometer FoG ratio: ");
    Serial.println(accelRatio);


    Serial.print("Flex FoG ratio: ");
    Serial.println(flexRatio);


    //the accelerometer must signal FoG in at least 20% of samples
    //the flex sensor must signal FoG in at least 50% of samples
    if (
      accelRatio >= 0.20 && flexRatio >= 0.50) {
      confirmedFoG = true;

      lastFoGDetected = currentMillis;

      Serial.println("FoG CONFIRMED");
    } else {
      confirmedFoG = false;

      Serial.println("FoG NOT confirmed");
    }


    Serial.println("------------------------------------");
    Serial.println();


    //reset for the next detection window
    accelFoGVotes = 0;
    flexFoGVotes = 0;
    totalSamples = 0;

    detectionWindowStart = currentMillis;
  }


  //vibrate if conditions are met
  //need to check if need to move vibration feedback to a separate function
  //so that it will keep pulsing consistent and for constant duration
  //this way prob will be better for vibrating until user resumes walking/stops FoG

  //keep vibrating for two seconds after FoG is confirmed
  if (
    lastFoGDetected > 0 && currentMillis - lastFoGDetected < FOG_HOLD_TIME) {
    unsigned long vibrationTime =
      currentMillis - lastFoGDetected;


    //switch between vibration on and off every 500 milliseconds
    if (
      (vibrationTime / VIBRATION_PULSE_TIME) % 2 == 0) {
      // Vibration strength: 0 = off, 127 = strong
      drv.setRealtimeValue(127);
    } else {
      drv.setRealtimeValue(0);
    }
  } else {
    drv.setRealtimeValue(0);
  }


  //reset for next iteration
  accel_FoG = false;
  flex_FoG = false;


  delay(50);
}