// Example 01 - York H-Link bus status monitor (RX only, never transmits)
//
// York YCWA16NCWQ wired panel: the power & modem board (power supply + modem,
// one side of the A/B two-wire polarity-free bus) talks to the control panel
// (panel MCU + display + IR receiver). The adapter is soldered to 3 wires
// (+5V / GND / RX - listen-only) and prints every checksum-validated frame
// plus the running AC state
// (power / mode / set temperature / room temperature / compressor / blower /
// fan level).
//
// Listen-only by design: the control panel is the closed setting authority on
// the bus; control goes over infrared (see Example 02).
//
// Key facts (all measured, see README for details):
//   - RX carries a physical echo of every TX edge: one re-triggerable monostable
//     stretches each 0-bit pulse to a full bit width. Result: NRZ, idle high,
//     and a single 9600 8E1 software serial port sees ALL traffic - both the
//     panel (addr 0x21) and the indoor unit (addr 0x12) frames.
//   - Message = [addr][00][len][payload][chk]; len = total bytes incl. header;
//     chk = XOR(byte[1] .. byte[n-2]) (address byte excluded).
//   - ACK = [peer addr][0x06], fixed 2 bytes, no checksum, follows every message.
//   - Three data frame types: 18 B (panel->indoor, settings), 39 B (indoor->
//     panel, running status), 48 B (both directions, config block).
//   - Steady state is idle; a heartbeat transaction (4 data frames + 4 ACKs)
//     every ~30 s; extra traffic only after key presses.
//
// Wiring: modem board RX line -> D6 (GPIO12), 9600 baud 8E1 software serial
// Serial monitor: 115200 baud

#define BUS_RX_PIN D6 // H-Link RX line (echo line, all frames visible here)

#include <SoftwareSerial.h>

SoftwareSerial BusSerial;

// ---------------------------------------------------------------------------
// Frame capture (silence-gated: report after >= 300 ms of bus idle)
// ---------------------------------------------------------------------------

#define CAP_MAX_BYTES 512     // capture buffer: all bytes of one silence period
#define FRAME_MAX_BYTES 64    // single message limit (len field, max seen = 48)
#define FRAME_LEN_TOL 4       // long frames occasionally lose 0x00 bytes; the
                              // XOR checksum is insensitive to that, so try
                              // lengths want..want-4 before giving up
#define REPORT_IDLE_MS 300

static unsigned char capByte[CAP_MAX_BYTES];
static unsigned int capLen = 0;
static bool capTrunc = false;
static unsigned long lastByteMs = 0;

// ---------------------------------------------------------------------------
// Checksum gate: only accept self-consistent frames
// ---------------------------------------------------------------------------

// k = index of the address byte, L = total message length
static bool ChecksumOK(unsigned int k, unsigned int L)
{
	if (L < 3 || k + L > capLen)
	{
		return false;
	}
	unsigned char x = 0;
	for (unsigned int i = k + 1; i < k + L - 1; i++)
	{
		x ^= capByte[i];
	}
	return x == capByte[k + L - 1];
}

// Returns the message length if a self-consistent frame starts at k, else 0
// (the caller drops one byte and re-synchronizes).
static unsigned int FrameLenAt(unsigned int k)
{
	if (k >= capLen || (capByte[k] != 0x21 && capByte[k] != 0x12))
	{
		return 0;
	}
	if (k + 1 >= capLen)
	{
		return 0;
	}
	if (capByte[k + 1] == 0x06)
	{
		return 2; // ACK
	}
	if (k + 2 >= capLen)
	{
		return 0;
	}
	unsigned int want = capByte[k + 2];
	if (want < 3 || want > FRAME_MAX_BYTES)
	{
		return 0;
	}
	for (unsigned int d = 0; d <= FRAME_LEN_TOL; d++)
	{
		unsigned int L = want - d;
		if (L < 3)
		{
			break;
		}
		if (ChecksumOK(k, L))
		{
			return L;
		}
	}
	return 0;
}

// ---------------------------------------------------------------------------
// Frame decoding
// ---------------------------------------------------------------------------

// Mode lives in bits 3..6 of idx10 (0x08 cool / 0x10 dry / 0x20 fan / 0x40 heat)
static const char *ModeName(unsigned char v)
{
	switch (v & 0x78)
	{
	case 0x08: return "Cool";
	case 0x10: return "Dry";
	case 0x20: return "Fan";
	case 0x40: return "Heat";
	default:   return "??";
	}
}

// Fan speed code (idx11 low nibble, one-hot) + sub-level (idx37: 0x04/0x06)
// -> exact panel level. Measured: 1=(08,06) 2=(08,04) 3=(04,06) 4=(04,04)
// 5=(02,04) 6=(02,06). The protocol has only 3 speed levels; the panel's
// 6 levels = 3 levels x 2 sub-levels.
static unsigned char FanLevel(unsigned char idx11, unsigned char idx37)
{
	bool hi = (idx37 & 0x02) != 0; // 0x06 = higher number within the pair
	switch (idx11 & 0x0F)
	{
	case 0x08: return hi ? 1 : 2;
	case 0x04: return hi ? 3 : 4;
	case 0x02: return hi ? 6 : 5;
	default:   return 0; // unknown combination
	}
}

// Running state, refreshed from every 39 B (indoor -> panel) frame. The 39 B
// is the indoor unit's "current truth" and outranks the 18 B settings frame.
struct AcState
{
	bool valid;
	bool power;           // idx10 bit0
	bool compressor;      // idx11 bit6 (forced 0 while locked out / faulted)
	bool blower;          // idx11 bit5 (power-up order: off -> blower -> compressor)
	unsigned char modeBit;      // idx10 bit3..6
	unsigned char setTemp;      // idx12, integer degrees C
	unsigned char roomTemp;     // idx33, integer degrees C
	unsigned char fanRaw;       // idx11 low nibble, one-hot (0x08/0x04/0x02)
	unsigned char fanAux;       // idx37 sub-level (0x04 / 0x06)
};
static AcState acState;
static AcState prevState;
static bool prevStateValid = false;

static void UpdateStateFromFrame(unsigned int k, unsigned int L)
{
	const unsigned char *b = &capByte[k];
	if (L != 39 || b[0] != 0x12)
	{
		return;
	}
	acState.power = (b[10] & 0x01) != 0;
	acState.compressor = (b[11] & 0x40) != 0;
	acState.blower = (b[11] & 0x20) != 0;
	if ((b[10] & 0x78) != 0x00)
	{
		acState.modeBit = b[10] & 0x78;
	}
	acState.setTemp = b[12];
	acState.roomTemp = b[33];
	acState.fanRaw = b[11] & 0x0F;
	acState.fanAux = (b[37] & 0x02) != 0 ? 6 : 4;
	acState.valid = true;
}

static void PrintAcState(const AcState &s)
{
	Serial.printf("[AC] power=%s mode=%s set=%uC room=%uC compressor=%s blower=%s fan=%u\r\n",
		s.power ? "ON" : "OFF", ModeName(s.modeBit), s.setTemp, s.roomTemp,
		s.compressor ? "RUN" : "STOP", s.blower ? "RUN" : "STOP",
		FanLevel(s.fanRaw, s.fanAux == 6 ? 0x06 : 0x04));
}

static void PrintStateChanges()
{
	if (!prevStateValid || !acState.valid)
	{
		return;
	}
	char sbuf[200];
	unsigned int p = (unsigned int)snprintf(sbuf, sizeof(sbuf), "[AC change] ");
	bool any = false;
	if (prevState.power != acState.power)
	{
		p += snprintf(sbuf + p, sizeof(sbuf) - p, "%spower %s->%s ", any ? "| " : "",
			prevState.power ? "ON" : "OFF", acState.power ? "ON" : "OFF");
		any = true;
	}
	if (prevState.compressor != acState.compressor)
	{
		p += snprintf(sbuf + p, sizeof(sbuf) - p, "%scompressor %s->%s ", any ? "| " : "",
			prevState.compressor ? "RUN" : "STOP", acState.compressor ? "RUN" : "STOP");
		any = true;
	}
	if (prevState.blower != acState.blower)
	{
		p += snprintf(sbuf + p, sizeof(sbuf) - p, "%sblower %s->%s ", any ? "| " : "",
			prevState.blower ? "RUN" : "STOP", acState.blower ? "RUN" : "STOP");
		any = true;
	}
	if (prevState.modeBit != acState.modeBit)
	{
		p += snprintf(sbuf + p, sizeof(sbuf) - p, "%smode %s->%s ", any ? "| " : "",
			ModeName(prevState.modeBit), ModeName(acState.modeBit));
		any = true;
	}
	if (prevState.setTemp != acState.setTemp)
	{
		p += snprintf(sbuf + p, sizeof(sbuf) - p, "%sset %u->%uC ", any ? "| " : "",
			prevState.setTemp, acState.setTemp);
		any = true;
	}
	if (prevState.roomTemp != acState.roomTemp)
	{
		p += snprintf(sbuf + p, sizeof(sbuf) - p, "%sroom %u->%uC ", any ? "| " : "",
			prevState.roomTemp, acState.roomTemp);
		any = true;
	}
	if (any)
	{
		Serial.printf("%s\r\n", sbuf);
	}
}

static void PrintHexFrame(unsigned int k, unsigned int L)
{
	for (unsigned int i = 0; i < L; i++)
	{
		Serial.printf("%02X ", capByte[k + i]);
	}
	Serial.println();
}

// Scan the capture buffer: print one line per validated frame and refresh the
// running state from 39 B frames.
static void ReportCaptured()
{
	unsigned int k = 0;
	while (k < capLen)
	{
		unsigned int L = FrameLenAt(k);
		if (L == 0)
		{
			k++; // not a frame header / checksum failed: drop 1 byte, resync
			continue;
		}

		const unsigned char *b = &capByte[k];
		if (L == 2)
		{
			Serial.printf("ACK %s\r\n", b[0] == 0x12 ? "indoor->panel" : "panel->indoor");
		}
		else if (L == 18)
		{
			Serial.printf("18B panel->indoor  power=%s mode=%s set=%uC\r\n",
				(b[10] & 0x01) ? "ON" : "OFF", ModeName(b[10]), b[12]);
		}
		else if (L == 39)
		{
			Serial.printf("39B indoor->panel  power=%s mode=%s set=%uC room=%uC compressor=%s blower=%s fan=%u\r\n",
				(b[10] & 0x01) ? "ON" : "OFF", ModeName(b[10]), b[12], b[33],
				(b[11] & 0x40) ? "RUN" : "STOP", (b[11] & 0x20) ? "RUN" : "STOP",
				FanLevel(b[11], b[37]));
		}
		else if (L == 48)
		{
			unsigned int ci = (b[0] == 0x21) ? 14 : 16; // settings-changed flag
			Serial.printf("48B %s  config block changeFlag=0x%02X%s\r\n",
				b[0] == 0x21 ? "panel->indoor" : "indoor->panel", b[ci],
				b[ci] == 0x40 ? " (settings changed this round)" : "");
		}
		else
		{
			Serial.printf("%uB %s\r\n", L, b[0] == 0x21 ? "panel->indoor" : "indoor->panel");
		}

		UpdateStateFromFrame(k, L);
		k += L;
	}

	if (acState.valid)
	{
		PrintStateChanges();
		PrintAcState(acState);
	}
}

// ---------------------------------------------------------------------------

void setup()
{
	delay(50);
	Serial.begin(115200);
	Serial.println("York H-Link bus monitor start (RX only)");

	// D6 (GPIO12): 9600 baud, 8E1. The frames on the wire are 12-bit chars
	// (8E2), but an 8E1 receiver samples the parity and first stop bits
	// correctly, so every frame decodes. The first byte of each message
	// carries a 9th "address mark" bit -> it shows up as a parity error,
	// which we do not check; the checksum gate does the real validation.
	BusSerial.begin(9600, SWSERIAL_8E1, BUS_RX_PIN, -1);

	Serial.println("Ready");
}

void loop()
{
	// Collect bytes while available
	while (BusSerial.available() > 0)
	{
		int c = BusSerial.read();
		if (c >= 0)
		{
			if (capLen < CAP_MAX_BYTES)
			{
				capByte[capLen++] = (unsigned char)c;
			}
			else
			{
				capTrunc = true;
			}
			lastByteMs = millis();
		}
	}

	// 300 ms of silence = the bus will not produce new bytes anytime soon,
	// so it is safe to print now
	if (capLen > 0 && (millis() - lastByteMs) >= REPORT_IDLE_MS)
	{
		if (capTrunc)
		{
			Serial.println("(capture buffer overflow, bytes dropped)");
		}
		ReportCaptured();
		capLen = 0;
		capTrunc = false;
	}
}
