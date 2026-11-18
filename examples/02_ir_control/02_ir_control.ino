// Example 02 - York AC infrared control (HITACHI_AC3 184-bit state frame)
//
// Why IR and not the bus: the panel is the closed setting authority on the
// H-Link bus (see Example 01 / README). Injected bus commands are executed by
// the indoor unit but overwritten by the panel's ~30 s heartbeat. Sending IR
// to the panel is equivalent to "a person pressing a key": the panel state
// changes first, then the panel itself forwards everything to the indoor unit.
// Zero bus traffic, zero risk.
//
// Frame: 23 bytes = HITACHI_AC3 "Normal" 184-bit state frame, 38 kHz, MSB-first
// (remote bit order). Measured timing matches IRremoteESP8266's kHitachiAc3
// constants exactly: header 3400/1660 us, bit mark 460 us, space 0 = 410 us /
// 1 = 1250 us, footer mark 460 us, 100 ms trailing gap.
//
// Two pitfalls that cost days of debugging - do not skip either:
//   * BIT ORDER: the remote sends MSB-first, but IRremoteESP8266's
//     sendHitachiAc3() sends LSB-first. Fix used here: store every byte
//     bit-reversed (Rev8); the library's LSB-first transmission then puts the
//     exact remote bit stream on the wire. (Equivalent alternative: feed the
//     original remote bytes to IRsend::sendGeneric(..., MSBfirst = true).)
//     Never validate bit order with "we sent it, we received it" - that is
//     circular; compare against a real remote capture or the panel's reaction.
//   * FOOTER MARK: the trailing mark is mandatory. A real remote frame has
//     372 edges = header 2 + 184 bits x 2 + footer mark 1 + final gap. If the
//     footer mark is omitted, all data bits are correct yet the panel still
//     drops the frame ("carrier present, pulses present, panel unmoved").
//   * Also send every command TWICE (repeat = 1): the IR receiver AGC
//     desensitizes after long frames.
//
// One command carries ALL parameters (power + mode + temperature + fan), the
// whole frame is built from "value + inverted value" complement pairs:
//   byte15/16 = temperature code / ~temperature
//   byte17/18 = (mode << 4 | fan) / ~byte17
//   byte21/22 = power / ~power
//
// Hardware: IR LED driven directly from D2 (GPIO4) through 100 ohm
// (~15-20 mA peak at 50% duty), proven to reach the panel. 38 kHz carrier.
//
// Serial commands (115200 baud):
//   p = toggle power        c / d / f / h = cool / dry / fan / heat
//   '+' / '-' = set temperature (16..31 C, panel clamps to 19..30)
//   1 / 2 / 3 = fan max / mid / min (panel has no auto fan level)

#define IR_TX_PIN D2 // GPIO4, IR LED through 100 ohm, direct drive

#include <IRremoteESP8266.h>
#include <IRsend.h>

#define IR_AC3_STATE_LEN 23    // "Normal" state frame length (kHitachiAc3StateLength - 4)
#define IR_TX_REPEAT 1         // 1 = send twice (recommended, AGC)

IRsend IrTransmitter(IR_TX_PIN);

// Remote-order (MSB-first) fixed bytes: byte0..14 and 19..20 are constant.
static const unsigned char kFixedBytes[21] = {
	0xC0, 0x0C, 0x00, 0x02, 0xFD, 0xFF, 0x00, 0x67, 0x98, 0x91, 0x6E, 0x80,
	0x7F, 0xFC, 0x03, 0xA4, 0x5B, 0x69, 0x96, 0x80, 0x7F };

// Complete power-off frame (remote bit order after Rev8, verified on hardware)
static const unsigned char kPowerOffState[IR_AC3_STATE_LEN] = {
	0x03, 0x30, 0x00, 0x40, 0xBF, 0xFF, 0x00, 0xE6, 0x19, 0x89, 0x76, 0x01,
	0xFE, 0x3F, 0xC0, 0x25, 0xDA, 0x96, 0x69, 0x01, 0xFE, 0xA0, 0x5F };

// Temperature codes for byte15: 16 C .. 31 C, index = temp - 16
// (all 16 codes captured from the real remote; the panel only accepts 19..30)
static const unsigned char kTempCodes[16] = {
	0xC8, 0xA8, 0xE8, 0x98, 0xD8, 0xB8, 0xF8, 0x84,
	0xC4, 0xA4, 0xE4, 0x94, 0xD4, 0xB4, 0xF4, 0x8C };

// Mode high nibble of byte17 (verified by differential capture):
// heat = 1, dry = 2, fan = 4, cool = 6. (AUTO code not captured yet; send cool.)
#define MODE_NIB_COOL 0x6
#define MODE_NIB_DRY  0x2
#define MODE_NIB_FAN  0x4
#define MODE_NIB_HEAT 0x1

// Fan low nibble of byte17 (labeled by actual airflow, verified against the
// bus side): 0x3 = max, 0x5 = mid, 0x9 = min. 0x1 = "auto" - the panel does
// not support an auto fan level, do not use. Note: the remote's displayed
// level numbers 1..6 are reversed relative to the actual airflow.
#define FAN_NIB_MAX 0x3
#define FAN_NIB_MID 0x5
#define FAN_NIB_MIN 0x9

static unsigned char Rev8(unsigned char b) // bit reversal (see header comment)
{
	unsigned char r = 0;
	for (unsigned char i = 0; i < 8; i++)
	{
		r <<= 1;
		r |= b & 1;
		b >>= 1;
	}
	return r;
}

// Current settings, applied on every send
static bool sPower = false;
static unsigned char sModeNib = MODE_NIB_COOL;
static unsigned char sFanNib = FAN_NIB_MIN;
static unsigned char sTempC = 26;

// Build the 23-byte state frame (library-order bytes = Rev8 of remote bytes)
static void BuildPowerOnState(unsigned char *st)
{
	for (unsigned char i = 0; i < 21; i++)
	{
		st[i] = Rev8(kFixedBytes[i]);
	}
	st[15] = Rev8(kTempCodes[sTempC - 16]);
	st[16] = ~st[15];                                 // rev8(~x) == ~rev8(x)
	st[17] = Rev8((sModeNib << 4) | sFanNib);
	st[18] = ~st[17];                                 // mode x fan complement pair
	st[21] = Rev8(0x09);                              // power ON
	st[22] = ~st[21];                                 // power complement pair
}

static void SendStateFrame(const unsigned char *st)
{
	// Library sends LSB-first; our stored bytes are pre-reversed, so the wire
	// carries the remote's MSB-first stream. IR_TX_REPEAT + 1 = two frames.
	IrTransmitter.sendHitachiAc3(st, IR_AC3_STATE_LEN, IR_TX_REPEAT);
}

static void SendCommand()
{
	unsigned char st[IR_AC3_STATE_LEN];
	if (!sPower)
	{
		memcpy(st, kPowerOffState, sizeof(st));
	}
	else
	{
		BuildPowerOnState(st);
	}
	SendStateFrame(st);

	Serial.printf("IR sent: power=%s mode=%s fan=%s set=%uC\r\n",
		sPower ? "ON" : "OFF",
		sModeNib == MODE_NIB_COOL ? "Cool" : sModeNib == MODE_NIB_DRY ? "Dry"
			: sModeNib == MODE_NIB_FAN ? "Fan" : "Heat",
		sFanNib == FAN_NIB_MAX ? "max" : sFanNib == FAN_NIB_MID ? "mid" : "min",
		sTempC);
}

static void HandleSerialCommand()
{
	int c = Serial.read();
	switch (c)
	{
	case 'p':
		sPower = !sPower;
		break;
	case 'c': sPower = true; sModeNib = MODE_NIB_COOL; break;
	case 'd': sPower = true; sModeNib = MODE_NIB_DRY; break;
	case 'f': sPower = true; sModeNib = MODE_NIB_FAN; break;
	case 'h': sPower = true; sModeNib = MODE_NIB_HEAT;
		sTempC = 20; break; // switching to heat auto-jumps the setpoint to 20 C
	case '+':
		if (sTempC < 31) sTempC++;
		sPower = true;
		break;
	case '-':
		if (sTempC > 16) sTempC--;
		sPower = true;
		break;
	case '1': sPower = true; sFanNib = FAN_NIB_MAX; break;
	case '2': sPower = true; sFanNib = FAN_NIB_MID; break;
	case '3': sPower = true; sFanNib = FAN_NIB_MIN; break;
	default:
		return;
	}
	SendCommand();
}

void setup()
{
	delay(50);
	Serial.begin(115200);
	Serial.println("York IR control start (HITACHI_AC3 184-bit state frame)");

	IrTransmitter.begin();

	Serial.println("Keys: p=power c/d/f/h=cool/dry/fan/heat +/-=temp 1/2/3=fan max/mid/min");
}

void loop()
{
	if (Serial.available() > 0)
	{
		HandleSerialCommand();
	}
}
