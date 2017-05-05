
#include <Arduino.h>
#include <Bounce2.h>
#include <ADC.h>
#include <elapsedMillis.h>

#include "./RunningMedian.h"

#define MIDICHAN 1
#define SWITCHCNT 11
#define LEDCNT 11

Bounce switches[SWITCHCNT];

/*
Input mappings (pin => switch label)

22 => 1
21 => 2
20 => 3
19 => 4
23 => B
18 => C

3 => 5
2 => 6
1 => 7
0 => 8
4 => G

Output mappings (pin => switch label)
16 => 1
15 => 2
14 => 3
12 => 4
17 => C

8 => 5
9 => 6
11 => 7
10 => 8
6 => A
7 => B
*/

int inMap[SWITCHCNT] = {22, 21, 20, 19, 3, 2, 1, 0, 23, 18, 4};
const char *inLabel[SWITCHCNT] = {"1", "2", "3", "4", "5", "6", "7", "8", "B", "C", "G"};

int outMap[LEDCNT] = {16, 15, 14, 12, 8, 9, 11, 10, 17, 6, 7};
const char *outLabel[LEDCNT] = {"1", "2", "3", "4", "5", "6", "7", "8", "C", "A", "B"};

int switchMode[SWITCHCNT] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int switchCount[SWITCHCNT] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
int switchState[SWITCHCNT] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

void showLEDs(uint16_t v) {
		for (uint8_t i = 0; i < LEDCNT; i++) {
			pinMode(outMap[i], ((v & 0x1) == 0x1) ? OUTPUT : INPUT);
			v >>= 1;
		}
}

template <int CC>
class Control {

	public:
		Control() : lastSent(0) {
			reset();
		}

		void add(uint16_t val) {
			if (val < min) min = val;
			if (val > max) max = val;

			uint32_t t = val;
			t -= (uint32_t)min;
			t *= (uint32_t)16383; // 14 bits
			t /= (uint32_t)(max-min);

			data.add(t);
		}

		void send() {
			if (enabled && data.getMedian(aval) == data.OK && aval != lastVal) {
				lastVal = aval;

				if (lastSent > 10) {
					lastSent = 0;
					usbMIDI.sendControlChange(CC, (aval >> 7) & 0x7F, MIDICHAN);
					usbMIDI.sendControlChange(CC + 32, aval & 0x7F, MIDICHAN);
				}
			}
		}

		void reset() {
			enabled = false; lastVal = 0; lastSent = 0; data.clear(); min = 65535; max = 0;
		}

		void detectStable() {
			if (data.getCount() < data.getSize()) return;

			uint16_t h, l;
			data.getHighest(h);
			data.getLowest(l);
			if ((h - l) <= 127) enabled = true;
		}

		uint16_t val() {
			return enabled ? lastVal : 0;
		}

		boolean isEnabled() {
			return enabled;
		}

	private:
		RunningMedian<uint16_t, 8> data;
		uint16_t lastVal = 0;
		elapsedMillis lastSent;

		bool enabled;
		uint16_t min;
		uint16_t max;

		uint16_t aval;
};

Control<1> Control1;
Control<7> Control2;
Control<4> Control3;

ADC *adc = new ADC(); // adc object

void setup() {
	Serial.begin(9600);

	byte i;

	for (i = 0; i < SWITCHCNT; i++) {
		pinMode(inMap[i], INPUT_PULLUP);
		switches[i].attach(inMap[i]);
		switches[i].interval(5);
	}

	for (i = 6; i <= 17; i++) {
		pinMode(i, OUTPUT);
	}


	adc->setAveraging(32);
  adc->setResolution(10);
	adc->setConversionSpeed(ADC_CONVERSION_SPEED::LOW_SPEED);
	adc->setSamplingSpeed(ADC_SAMPLING_SPEED::LOW_SPEED);

	adc->enableInterrupts();
	adc->startSingleRead(A10, ADC_0);
}

int j = 0;
elapsedMillis printTime = 0;

enum Mode { SELECT_FOR_PROGRAM, PROGRAM, DISPLAY_PROGRAM, NORMAL };
int mode = SELECT_FOR_PROGRAM;
int programming = 1;
elapsedMillis modeElapsed = 0;


void enterProgramMode() {
	Control1.reset(); Control2.reset(); Control3.reset();
	mode = SELECT_FOR_PROGRAM;
	modeElapsed = 0;
}

void selectForProgram() {
	Control1.detectStable();
	Control2.detectStable();
	Control3.detectStable();

	showLEDs(
		(Control1.isEnabled() ? 0x1 : 0x0) |
		(Control2.isEnabled() ? 0x2 : 0x0) |
		(Control3.isEnabled() ? 0x4 : 0x0) |
		0x200 | 0x400
	);

	for (uint8_t i = 0; i < SWITCHCNT; i++) {
		switches[i].update();
		if (switches[i].fell()) {
			if (i == 10) {
				mode = NORMAL;
			}
			else {
				mode = PROGRAM;
				programming = i;
			}
		}
	}

	if (modeElapsed > 10000) mode = NORMAL;
}

void program() {
	showLEDs(1 << programming | (0x200 << switchMode[programming]));

	for (uint8_t i = 0; i < SWITCHCNT; i++) {
		switches[i].update();
		if (switches[i].fell()) {
			if (i == 10) {
				switchMode[programming] = !switchMode[programming];
			}
			else {
				switchCount[programming] = i+1;
				mode = DISPLAY_PROGRAM;
				modeElapsed = 0;
			}
		}
	}
}

void displayProgram() {
	uint16_t toggle = (modeElapsed / 500) % 2;
	uint16_t leds = (1 << (switchCount[programming] - 1)) | (0x200 << switchMode[programming]);
	showLEDs(toggle ? (1 << programming) : leds);

	if (modeElapsed > 5000) mode = NORMAL;
}

void normal() {

	// MIDI Controllers should discard incoming MIDI messages.
  while (usbMIDI.read()) { /* NOP */ }

	byte i;

	for (i = 0; i < SWITCHCNT; i++) {
		switches[i].update();
		if (switches[i].fell()) {
			if (switchMode[i] == 1) {
				usbMIDI.sendNoteOff(i * 12 + switchState[i], 0, MIDICHAN);
				switchState[i] = (switchState[i] + 1) % switchCount[i];
			}
			usbMIDI.sendNoteOn(i * 12 + switchState[i], 127, MIDICHAN);
		}
		else if (switches[i].rose()) {
			if (switchMode[i] == 0) {
				usbMIDI.sendNoteOff(i * 12 + switchState[i], 0, MIDICHAN);
				switchState[i] = (switchState[i] + 1) % switchCount[i];
			}
		}
	}

	showLEDs(0);

	Control1.send();
	Control2.send();
	Control3.send();

	usbMIDI.send_now();

	if (printTime > 200) {
		Serial.print(Control1.val()); Serial.print("\t");
		Serial.print(Control2.val()); Serial.print("\t");
		Serial.print(Control3.val()); Serial.print("\t");
		Serial.print("\r\n");

		printTime = 0;
	}

	if (switches[10].fell()) enterProgramMode();
}

void loop() {
	if (mode == SELECT_FOR_PROGRAM) selectForProgram();
	else if (mode == PROGRAM) program();
	else if (mode == DISPLAY_PROGRAM) displayProgram();
	else normal();
}

void adc0_isr() {
		// the bits 0-4 of ADC0_SC1A have the channel
    uint8_t pin = ADC::sc1a2channelADC0[ADC0_SC1A&ADC_SC1A_CHANNELS];

		if (pin == A10) {
			Control1.add(adc->readSingle());
			adc->startSingleRead(A11, ADC_0);
		}
		else if (pin == A11) {
			Control2.add(adc->readSingle());
			adc->startSingleRead(A14, ADC_0);
		}
		else {
			Control3.add(adc->readSingle());
			adc->startSingleRead(A10, ADC_0);
		}
}
