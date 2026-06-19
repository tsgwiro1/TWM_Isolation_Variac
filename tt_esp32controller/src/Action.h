/*************************************************************
Copyright(c) 2020 Roger Widmer & Michael Tanner

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files(the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
************************************************************ */

#ifndef _ACTION_h
#define _ACTION_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif

#include <Wire.h>
#include <MCP23017.h>

const int BUTTONREADINTERVALL = 100;
const int BUTTONLONGPRESSTIMEOUT = 2000;
const int LONGPRESS = 2;
const int GROUPITEMS = 3;
const int PINUNUSED = 99;
const int NOPRESETVALUE = -1;

enum class ButtonEvent { PRESSED, LONGPRESSED, RELEASED };

class Action
{
protected:
	uint8_t state;
	uint8_t button_pin;
	uint8_t led_pin;
	uint8_t relais_pin;
	boolean relais_inv;
	uint32_t lastButtonRead;
	uint32_t lastButtonStateChange;
	uint8_t lastButtonState;
	int eeAddress;
	void (*cb)(Action*, ButtonEvent);
	int valuePreset;
  uint32_t timeout;
	Action** group;
	MCP23017* mcp;
	void handleActionGroup();
	void handleRelais(uint8_t _relais_state);
	void handleLED(uint8_t _led_state);
public:
	Action();
	Action(MCP23017& _mcp, uint8_t _button_pin, uint8_t _led_pin);
	void init(uint8_t default_state);
	void setRelais(uint8_t _relais_pin, boolean _relais_inv = false);
	void setCallBack(void (*_methodPtr)(Action*, ButtonEvent));
	void setGroup(Action** _group);
	void toggle();
	void on();
	void off();
	void ledOn();
	void ledOff();
	void relaisOn();
	void relaisOff();
	int getValuePreset();
	void setValuePreset(int _value);
	uint8_t getState();
	void handle();
  void setTimeout(int _milliseconds);
};


#endif

