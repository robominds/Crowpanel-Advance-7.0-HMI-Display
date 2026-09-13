// Wall-clock time, from the PCF8563 and the network.
//
// The board carries a PCF8563 with a CR1220 backup cell, and that cell was
// confirmed on hardware to hold the clock across a full power cycle - see
// docs/HARDWARE.md section 2.9. So the board knows what time it is the instant
// it boots, before Wi-Fi associates and without the network ever coming back.
//
// The two sources have different jobs. The RTC is authoritative at boot and
// whenever the network is gone. The network is authoritative for accuracy, and
// writes back to the RTC once, so an unplugged board still comes up right.
//
// Everything here works in LOCAL time, because the only consumer is a chart
// axis a person reads.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

#include <ctime>

namespace rtc {

// Sets the timezone, probes the chip, and if it holds a time the chip itself
// vouches for, adopts it as the system clock. Call after Wire.begin() and
// before anything wants the time. Returns false if nothing answered at 0x51.
bool begin(const char* tz);

// True if the chip answered at boot.
bool present();

// Call every loop. Once the network is up this starts SNTP, and on the first
// good answer writes it back to the chip so the next cold boot starts correct.
void poll(bool network_up);

// True when the system clock holds a plausible wall-clock time, from either
// source. False means the chart axis has nothing real to label itself with and
// should say so rather than invent one.
bool hasTime();

// True once the network has corrected the clock this session.
bool syncedFromNetwork();

}  // namespace rtc
