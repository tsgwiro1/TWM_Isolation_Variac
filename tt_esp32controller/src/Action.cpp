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

#include "Action.h"

void Action::handleActionGroup()
{
    if (group != nullptr) {
        Action** g = group;
        for (uint8_t i = 0; i < GROUPITEMS; i++) {
            Action* a = *g;
            if (a->button_pin != this->button_pin) {
                a->off();
            }
            g++;
        }
    }
}

void Action::handleRelais(uint8_t _relais_state)
{
    if (relais_pin != PINUNUSED) {
        uint8_t rs = relais_inv ? !_relais_state : _relais_state;
        mcp->digitalWrite(relais_pin, rs);
    }
}

void Action::handleLED(uint8_t _led_state){
    if (led_pin != PINUNUSED) {
        mcp->digitalWrite(led_pin, _led_state);    
    }
}

Action::Action()
{   
}

Action::Action(MCP23017& _mcp, uint8_t _button_pin, uint8_t _led_pin)
{
    cb = nullptr;
    lastButtonState = HIGH;
    lastButtonStateChange = 0;
    lastButtonRead = 0;
    state = 0;
    relais_pin = PINUNUSED;
    relais_inv = false;
    valuePreset = NOPRESETVALUE;
    group = nullptr;
    timeout = 0;

    mcp = &_mcp;
    button_pin = _button_pin;
    led_pin = _led_pin;
    pinMode(button_pin, INPUT_PULLUP);
}

void Action::init(uint8_t default_state)
{
    if (default_state) {
        on();
    }
    else {
        off();
    }
}

void Action::setRelais(uint8_t _relais_pin, boolean _relais_inv){
     relais_pin = _relais_pin;
     relais_inv = _relais_inv;
}

void Action::setCallBack(void(*_methodPtr)(Action*, ButtonEvent))
{
    cb = _methodPtr;
}

void Action::setGroup(Action** _group)
{
    group = _group;
}

void Action::toggle()
{
    if (state) {
        off();
    }
    else {
        on();
    }
}

void Action::on()
{
    state = 1;
    ledOn();
    handleActionGroup();
    relaisOn();
}

void Action::off()
{
    state = 0;
    ledOff();
    relaisOff();
}

void Action::relaisOn() {
    handleRelais(HIGH);
}

void Action::relaisOff() {
    handleRelais(LOW);
}

void Action::ledOn()
{
    handleLED(HIGH);
}

void Action::ledOff()
{
    handleLED(LOW);
}

int Action::getValuePreset()
{
    return valuePreset;
}

void Action::setValuePreset(int _value)
{
    valuePreset = _value;
}

uint8_t Action::getState()
{
    return state;
}

void Action::setTimeout(int _value)
{
  timeout = millis() + _value;
}

void Action::handle()
{
    if (lastButtonRead < millis()) {
        if (!digitalRead(button_pin) && lastButtonState == HIGH) {
            lastButtonState = LOW;
            lastButtonStateChange = millis();
            if (cb != nullptr) cb(this, ButtonEvent::PRESSED);
        }
        else if (digitalRead(button_pin) && lastButtonState != HIGH) {
            lastButtonState = HIGH;
            lastButtonStateChange = millis();
            if (cb != nullptr) cb(this, ButtonEvent::RELEASED);
        }
        else if (!digitalRead(button_pin) && lastButtonState == LOW && lastButtonStateChange + BUTTONLONGPRESSTIMEOUT < millis()) {
            lastButtonState = LONGPRESS;
            lastButtonStateChange = millis();
            if (cb != nullptr) cb(this, ButtonEvent::LONGPRESSED);
        }
        lastButtonRead = millis() + BUTTONREADINTERVALL;
    }
    if (timeout && millis() > timeout) {
      off();
      timeout = 0;
    }
}
