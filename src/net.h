// Wi-Fi, with non-blocking reconnection.
//
// Nothing here ever blocks. The panel must come up and show its status whether
// or not the network exists, so a display that waits for an access point is a
// display that shows nothing when the router is rebooting.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

namespace net {

// Starts the first connection attempt and returns immediately.
void begin();

// Call every loop. Retries with backoff when disconnected. Cheap when
// connected.
void poll();

bool connected();

}  // namespace net
