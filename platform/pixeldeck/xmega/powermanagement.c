#include "powermanagement.h"
#include <avr/io.h>
#include <avr/interrupt.h>
#include "setup.h"
#include "adcMultipin.h"
#include <avr/delay.h>

// Power-Taste (pixeldeck M3): per Polling im 200-Hz-Takt statt Pin-Interrupt.
// Der alte ISR hat die Flanke per |= auf Low-Level-Trigger umgestellt (ISC 2|1 = 3),
// feuerte dann ununterbrochen und blockierte die Hauptschleife (keine Statuspakete mehr).
typedef enum { PWR_WAIT_RELEASE, PWR_RUN, PWR_PENDING, PWR_OFF } pwr_state_t;
static pwr_state_t pwrState = PWR_WAIT_RELEASE;  // Einschalt-Druck abwarten
static uint16_t pwrTicks = 0;
static uint8_t pwrPressTicks = 0;

//volatile uint32_t Boot_Key __attribute__ ((section (".noinit")));
//#define MAGIC_BOOT_KEY            0xDC42ACCA

#define BATTERYLEVELHISTORYSIZE 64
uint16_t batteryLevelHistory[BATTERYLEVELHISTORYSIZE];
uint32_t batteryLevelSum = 0;
uint8_t batteryLevelIdx = 0;
bool batteryLevelSumReady = false;

void PowerLatch(void) {
	PIN_KEEPALIVE_reg.DIRSET = PIN_KEEPALIVE_bm; //pin PD3 (Keepalive) as output
	//if (Boot_Key != MAGIC_BOOT_KEY)
	if (RST.STATUS == RST_PORF_bm)
		PIN_KEEPALIVE_reg.OUTSET = PIN_KEEPALIVE_bm; //pin PD3 high
	else
		PIN_KEEPALIVE_reg.OUTCLR = PIN_KEEPALIVE_bm; //pin PD3 low

	PIN_PWRBTN_reg.DIRCLR = PIN_PWRBTN_bm; //pin PC4 (pwrbtn) as input
	PIN_PWRBTN_reg.PASTE3(PIN,PIN_PWRBTN_bit,CTRL) = PORT_OPC_PULLUP_gc; //pullup

	PIN_CRG_reg.DIRCLR = PIN_CRG_bm;
	PIN_CRG_reg.PASTE3(PIN,PIN_CRG_bit,CTRL) = PORT_OPC_PULLUP_gc;

	PORTC.DIRCLR = PIN3_bm; // Bootloader seems to do strage stuff with its pin
	
	SetupAdcMultipin();
	
	for(uint8_t idx = 0; idx < BATTERYLEVELHISTORYSIZE; idx++) {
		batteryLevelHistory[idx] = getAnalogValue(PIN_BATTERYDIV_bit);
		batteryLevelSum += batteryLevelHistory[idx];
	}
}

void PowerWorker()
{
	
}

void PowerTick(void)
{
	bool pressed = isPowerButtonPressed();
	switch (pwrState) {
	case PWR_WAIT_RELEASE:
		if (!pressed) {
			pwrState = PWR_RUN;
			pwrPressTicks = 0;
		}
		break;
	case PWR_RUN:
		if (!pressed) {
			pwrPressTicks = 0;
		} else if (++pwrPressTicks >= PWR_DEBOUNCE_TICKS) {
			pwrState = PWR_PENDING;  // ab jetzt meldet das Statuspaket BUTTON_PWR
			pwrTicks = 0;
		}
		break;
	case PWR_PENDING:
		// Gnadenfrist: der ESP sichert Spielstand und Einstellungen
		if (++pwrTicks >= PWR_GRACE_TICKS) {
			PIN_KEEPALIVE_reg.OUTCLR = PIN_KEEPALIVE_bm;  // aus, sobald die Taste losgelassen ist
			pwrState = PWR_OFF;
			pwrTicks = 0;
		}
		break;
	case PWR_OFF:
		// Läuft nach dem Loslassen weiter (z. B. Versorgung über USB): wieder einschalten
		if (pressed) {
			pwrTicks = 0;
		} else if (++pwrTicks >= PWR_FALLBACK_TICKS) {
			PIN_KEEPALIVE_reg.OUTSET = PIN_KEEPALIVE_bm;
			pwrState = PWR_RUN;
			pwrPressTicks = 0;
		}
		break;
	}
}

bool isShutdownPending(void)
{
	return pwrState == PWR_PENDING || pwrState == PWR_OFF;
}


uint16_t getBatteryLevel()
{
	//TODO: once timing is possible move sampling to worker

	uint16_t reading = getAnalogValue(PIN_BATTERYDIV_bit);
	batteryLevelSum = batteryLevelSum - batteryLevelHistory[batteryLevelIdx] + reading;
	batteryLevelHistory[batteryLevelIdx] = reading;
	batteryLevelIdx++;
	batteryLevelIdx %= BATTERYLEVELHISTORYSIZE;
	if (batteryLevelIdx == 0)
		batteryLevelSumReady = true;
	
	if (batteryLevelSumReady)
		return batteryLevelSum / BATTERYLEVELHISTORYSIZE;
	return reading;
}

bool isPowerButtonPressed()
{
	return ((PIN_PWRBTN_reg.IN & PIN_PWRBTN_bm) == 0);
}

bool isDeviceCharging() {
	return ((PIN_CRG_reg.IN & PIN_CRG_bm) == 0);
}


