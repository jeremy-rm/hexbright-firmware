// -- General Settings

// DEBUG (boolean)
// Whether or not to enable the serial bus for outputting debugging messages.
#define DEBUG 0

// AUTOOFF_DELAY (milliseconds)
// How long the Hexbright must remain in one mode before the next button press cycles to 'Off'.
#define AUTOOFF_DELAY 3000

// LONGPRESS (milliseconds)
// How long the button must be held to distinguish a 'long' press.
#define LONGPRESS_DELAY 750

// STANDBY_DELAY (milliseconds)
// Approximately how long the Hexbright must remain motionless for standby to become active.
#define STANDBY_DELAY 5000

// -- Advanced Settings -- These aren't the droids you're looking for.

// Dazzle mode has a ((DAZZLE_RANDROM - 1)/DAZZLE_RANDOM) * 100 % change of firing the light each loop.
#define DAZZLE_RANDOM 10

// Dazzle mode will delay for DAZZLE_PAUSE milliseconds before looping again.
#define DAZZLE_PAUSE 4

// How long each brightness level remains for flashUp() and flashDown().
#define FLASH_DELAY 250

// A small delay in the main loop to allow the LEDs to become more visible.
#define LED_DELAY 10

// How long the time restricted portion of the main loop delays before repeating.
#define LOOP_DELAY 1000

// Reported analog value at which we'll force a shutdown due to overheating.
#define OVERHEAT 315

// Alpha value for the exponential rolling average used to smooth out sensor data.
#define STANDBY_ALPHA 0.75

// The higher the number the less sensitive standby motion detection will be.
#define STANDBY_THRESHOLD 10

/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

NOTICE: CHANGING ANYTHING BELOW THIS POINT WITHOUT KNOWING WHAT YOU ARE DOING
				WILL CORRUPT YOUR MORALS AND YOU WILL ALMOST CERTAINLY DIE.

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/

#include <Wire.h>

// -- Accelerometer
#define ACC_ADDRESS 0x4C
#define ACC_REG_XOUT 0
#define ACC_REG_YOUT 1
#define ACC_REG_ZOUT 2
#define ACC_REG_TILT 3
#define ACC_REG_INTS 6
#define ACC_REG_MODE 7

// -- Digital Pin Assignments
#define DPIN_RLED_SW 2
#define DPIN_ACC_INT 3
#define DPIN_GLED 5
#define DPIN_PGOOD 7
#define DPIN_PWR 8
#define DPIN_DRV_MODE 9
#define DPIN_DRV_EN 10

// -- Analog Pin Assignments
#define APIN_TEMP 0
#define APIN_CHARGE 3

// -- Modes
#define MODE_0 0 // Off (powerMode0)
#define MODE_1 1 // Illumination Level 1 (powerMode1)
#define MODE_2 2 // Illumination Level 2 (powerMode2)
#define MODE_3 3 // Illumination Level 3 (powerMode3)
#define MODE_D 4 // Dazzle (see 'Mode Specific Tasks' in main loop)

// -- Initial States

// Accelerometer Data
char facc[3];
int pitch[2];
int roll[2];
int delta;

// Initial Mode States
byte mode                = 0; // Current mode.
byte newMode             = 0; // Mode we're currently switching to.
byte oldMode             = 0; // Mode we were just in.

// Initial Feature States
boolean autoOff          = 0; // 0 off, 1 on.
boolean buttonDown       = 0; // Button input state.
boolean standby          = 0; // 0 off, 1 on.
boolean standbyActive    = 0; // 0 off, 1 on.

// Epic Epochs
unsigned long time       = 0; // Epoch since power on.
unsigned long lastMove   = 0; // Epoch since last movement detected beyond STANDBY_THRESHOLD.
unsigned long lastLoop   = 0; // Epoch since completion of time restricted portion of main loop.
unsigned long lastButton = 0; // Epoch since button was last released.

// -- BEGIN SETUP
void setup() {
	// Initialize Power State
	pinMode(DPIN_PWR, OUTPUT);
	digitalWrite(DPIN_PWR, HIGH);

	// Initialize GPIO
	pinMode(DPIN_RLED_SW, INPUT);
	pinMode(DPIN_GLED, OUTPUT);
	pinMode(DPIN_DRV_MODE, OUTPUT);
	pinMode(DPIN_DRV_EN, OUTPUT);
	pinMode(DPIN_ACC_INT, INPUT);
	pinMode(DPIN_PGOOD, INPUT);
	digitalWrite(DPIN_DRV_MODE, LOW);
	digitalWrite(DPIN_DRV_EN, LOW);
	digitalWrite(DPIN_ACC_INT, HIGH);

	// Initialize Serial Buses
	if (DEBUG) Serial.begin(9600);
	Wire.begin();

	// Configure Accelerometer
	byte config[] = {
		ACC_REG_INTS,	// First register (see next line)
		0xE4,			// Interrupts: shakes, taps
		0x00,			// Mode: not enabled yet
		0x00,			// Sample rate: 120 Hz
		0x0F,			// Tap threshold
		0x01			// Tap debounce samples
	};
	Wire.beginTransmission(ACC_ADDRESS);
	Wire.write(config, sizeof(config));
	Wire.endTransmission();

	// Initialize Accelerometer
	byte enable[] = {ACC_REG_MODE, 0x01};	// Mode: active!
	Wire.beginTransmission(ACC_ADDRESS);
	Wire.write(enable, sizeof(enable));
	Wire.endTransmission();

	if (DEBUG) Serial.println("Hexbright_0x4A -- setup complete");
}
// -- END SETUP

// -- BEGIN LOOP
void loop() {
	// Epoch since power on, in milliseconds.
	time = millis();

	// Mode Specific Tasks
	switch (mode) {
		// Dazzle!
		case MODE_D:
			digitalWrite(DPIN_DRV_EN, random(DAZZLE_RANDOM) < 1);
			delay(random(DAZZLE_PAUSE));
		break;
	}

	// Switch/Red LED
	boolean newButtonDown = digitalRead(DPIN_RLED_SW);
	if (standbyActive) {
		pinMode(DPIN_RLED_SW, OUTPUT);
		digitalWrite(DPIN_RLED_SW, (time % 2000 > 1000)?LOW:HIGH);
		delay(LED_DELAY);
		pinMode(DPIN_RLED_SW, INPUT);
	} else {
		pinMode(DPIN_RLED_SW, INPUT);
	}

	// Mode Switching Procedures
	switch (mode) {
		// Off - See powerMode0() for details.
		// Note: Depending on standby mode, this may not actually be 'off' - see powerMode0().
		case MODE_0:
			// If standby is already active, we're cycling from MODE_3, so disable standby.
			if (standby) toggleStandby();
			if (buttonDown && !newButtonDown && (time - lastButton) < LONGPRESS_DELAY)
				// AUTOOFF_DELAY doesn't matter, just cycle to next mode.
				newMode = MODE_1;
			else if (buttonDown && !newButtonDown && (time - lastButton) > LONGPRESS_DELAY) {
				// Long press, activate standby mode.
				toggleStandby();
				// First press should always turn the light on, even in standby, so cycle to mode 1.
				newMode = MODE_1;
			}
		break;
		// Mode 1 - See powerMode1() for details.
		case MODE_1:
			if (buttonDown && !newButtonDown && autoOff && (time - lastButton) < LONGPRESS_DELAY)
				// AUTOOFF_DELAY has passed, turn off.
				newMode = MODE_0;
			if (buttonDown && !newButtonDown && !autoOff && (time - lastButton) < LONGPRESS_DELAY)
				// AUTOOFF_DELAY has not passed, cycle to next mode.
				newMode = MODE_2;
			if (buttonDown && newButtonDown && (time - lastButton) > LONGPRESS_DELAY) {
				// Long press, momentarily go to dazzle until release.
				newMode = MODE_D;
				// Set this so we know what mode to return to after dazzle.
				oldMode = MODE_1;
			}
		break;
		// Mode 2 - See powerMode2() for details.
		case MODE_2:
			if (buttonDown && !newButtonDown && autoOff && (time - lastButton) < LONGPRESS_DELAY)
				// AUTOOFF_DELAY has passed, turn off.
				newMode = MODE_0;
			if (buttonDown && !newButtonDown && !autoOff && (time - lastButton) < LONGPRESS_DELAY)
				// AUTOOFF_DELAY has not passed, cycle to next mode.
				newMode = MODE_3;
			if (buttonDown && newButtonDown && (time - lastButton) > LONGPRESS_DELAY) {
				// Long press, momentarily go to dazzle until release.
				newMode = MODE_D;
				// Set this so we know what mode to return to after dazzle.
				oldMode = MODE_2;
			}
		break;
		// Mode 3 - See powerMode3() for details.
		case MODE_3:
			if (buttonDown && !newButtonDown && (time - lastButton) < LONGPRESS_DELAY)
				// AUTOOFF_DELAY doesn't matter, just turn off.
				newMode = MODE_0;
			if (buttonDown && newButtonDown && (time - lastButton) > LONGPRESS_DELAY) {
				// Long press, momentarily go to dazzle until release.
				newMode = MODE_D;
				// Set this so we know what mode to return to after dazzle.
				oldMode = MODE_3;
			}
		break;
		// Mode D - Dazzle, see 'Mode Specific Tasks' above for details.
		case MODE_D:
			// If the button is not being held, return to previous mode, otherwise continue.
			if (buttonDown && !newButtonDown) {
				newMode = oldMode;
			} else {
				newMode = MODE_D;
			}
		break;
	}

	// Mode Changing Procedures
	if (newMode != mode || standby) {
		if (standbyActive && mode != MODE_0) {
			powerMode0();
		} else {
			if (autoOff) {
				autoOff = 0;
				if (DEBUG) Serial.println("autoOff: unset");
			}
		switch (newMode) {
			case MODE_0:
				powerMode0();
				break;
			case MODE_1:
				powerMode1();
				break;
			case MODE_2:
				powerMode2();
				break;
			case MODE_3:
				powerMode3();
				break;
			case MODE_D:
				powerModeD();
				break;
			}
		}
	}

	// Mode Switching Update & AutoOff Timing
	mode = newMode;
	if (newButtonDown != buttonDown) {
		lastButton = time;
		buttonDown = newButtonDown;
		delay(50);
	} else {
		// AutoOff Timer - Makes the next button push go to MODE_0.
		if (!autoOff && time - lastButton > AUTOOFF_DELAY && mode != MODE_0 && !standby) {
			 if (DEBUG) Serial.println("autoOff: set");
			 autoOff = 1;
		 }
	}

	// Check for overheat condition.
	checkOverheat();

	// Check charge status.
	checkPowerStatus();

	// Time restricted portion of main loop.
	if (time - lastLoop > LOOP_DELAY) {
		// Check the accelerometer for movement.
		if (standby) checkStandby();
		// Tons of debugging information for folks who like that sort of thing.
		if (DEBUG) {
			Serial.print(time);
			Serial.print("ms -- mode:");
			Serial.print(mode);
			if (autoOff) Serial.print(" (autoOff)");
			if (standby) {
				Serial.print(" (standby)");
				Serial.print(" pitch:");
				Serial.print(pitch[0]);
				Serial.print(" roll:");
				Serial.print(roll[0]);
				Serial.print(" delta:");
				Serial.print(delta);
				if (standbyActive) Serial.print(" (standbyActive)");
			}
			Serial.println("");
		}
		// Reset, so we can restrict execution again until the time is right.
		lastLoop = time;
	}
}
// -- END LOOP (Hint: It's a lie.)

// Check the internal hardware temperature and power down the LED driver if necessary.
void checkOverheat() {
	if (mode != MODE_0) {
		if (analogRead(APIN_TEMP) > OVERHEAT) {
			if (DEBUG) Serial.println("WARNING: Overheating!");
			mode = MODE_0;
			digitalWrite(DPIN_DRV_MODE, LOW);
			digitalWrite(DPIN_DRV_EN, LOW);
			digitalWrite(DPIN_PWR, LOW);
		}
	}
}

// Check the current charging status and indicate via the green LED.
void checkPowerStatus() {
	// Tri-state!
	int charge = analogRead(APIN_CHARGE);
	// Charging via USB.
	if (charge < 128) {
		digitalWrite(DPIN_GLED, (time % 2000 > 1000)?LOW:HIGH);
		delay(LED_DELAY);
	}
	// Fully charged, still on USB.
	else if (charge > 768)
		digitalWrite(DPIN_GLED, HIGH);
	// Not connected to USB.
	else
		digitalWrite(DPIN_GLED, LOW);
}

// Poll the accelerometer for changes, then convert to an arbitrary value.
void checkStandby() {
	char acc[3];
	while (1) {
		Wire.beginTransmission(ACC_ADDRESS);
		Wire.write(ACC_REG_XOUT);
		Wire.endTransmission(false);       // End, but do not stop!
		Wire.requestFrom(ACC_ADDRESS, 3);  // This one stops.
		for (int i = 0; i < 3; i++) {
			if (!Wire.available()) continue;
			acc[i] = Wire.read();
			if (acc[i] & 0x40) continue; // Failed read, redo.
			if (acc[i] & 0x20) acc[i] |= 0xC0; // Sign-extend.
			// Exponential rolling average, to help steady out the jitters.
			facc[i] = int(abs((acc[i] * (1 - STANDBY_ALPHA)) + (facc[i]  *  STANDBY_ALPHA)));
		}
		break;
	}
	// Pitch and roll are not realtime values, but rather use the rolling average above.
	pitch[0] = (atan2(facc[1], sqrt(facc[0] * facc[0] + facc[2] * facc[2])) * 180.0) / PI;
	roll[0] = (atan2(facc[0], facc[2]) * 180.0) / PI;
	// Calculate our change in attitude.
	delta = abs(pitch[0] - pitch[1]) + abs(roll[0] - roll[1]);
	// Move current averages to the back of the bus for the next time around.
	pitch[1] = pitch[0];
	roll[1] = roll[0];
	// If distance is greater than the threshold, interrupt standby.
	if (delta > STANDBY_THRESHOLD) {
		lastMove = time;
		standbyActive = 0;
		if (DEBUG) {
			Serial.print("standby interrupt -- delta: ");
			Serial.println(delta);
		}
	}
	// If there's been no movement for longer than the delay, re-enter standby.
	if (time - lastMove > STANDBY_DELAY) {
		standbyActive = 1;
	}
}

// Indicator for powering off from standby mode.
void flashDown() {
	powerMode3();
	delay(FLASH_DELAY);
	powerMode2();
	delay(FLASH_DELAY);
	powerMode1();
	delay(FLASH_DELAY);
	powerMode0();
}

// Indicator for entering standby mode from power off.
void flashUp() {
	powerMode1();
	delay(FLASH_DELAY);
	powerMode2();
	delay(FLASH_DELAY);
	powerMode3();
	delay(FLASH_DELAY);
	powerMode0();
}

// Off.  Or mostly off, depending on standby.
void powerMode0() {
	// If we're in standby, keep the Hexbright powered.
	if (standby) {
		digitalWrite(DPIN_PWR, HIGH);
	// But if we're not in standby, power Hexbright down.
	} else {
		digitalWrite(DPIN_PWR, LOW);
	}
	// Power down the LED driver regardless of the above condition.
	digitalWrite(DPIN_DRV_MODE, LOW);
	digitalWrite(DPIN_DRV_EN, LOW);
}

// Low power level.
void powerMode1() {
	digitalWrite(DPIN_DRV_MODE, LOW);
	analogWrite(DPIN_DRV_EN, 64);
}

// Medium power level.
void powerMode2() {
	digitalWrite(DPIN_DRV_MODE, LOW);
	analogWrite(DPIN_DRV_EN, 255);
}

// High power level.
void powerMode3() {
	digitalWrite(DPIN_DRV_MODE, HIGH);
	analogWrite(DPIN_DRV_EN, 255);
}

// Power level for dazzle.
void powerModeD() {
	digitalWrite(DPIN_DRV_MODE, HIGH);
}

// Set or reset the appropriate items for managing standby and display indicators.
void toggleStandby() {
	if (!standby) {
		if (DEBUG) Serial.println("standby: 1");
		standby = 1;
		standbyActive = 0;
		flashUp();
	} else {
		if (DEBUG) Serial.println("standby: 0");
		standby = 0;
		standbyActive = 0;
		flashDown();
	}
}

// EOF - <3 Mischell