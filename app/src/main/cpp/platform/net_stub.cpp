// net_stub.cpp — replaces game/play.cpp's DirectPlay networking for now.
// Single-player only: transmits go nowhere, nothing is received.
// A later phase rebuilds multiplayer on UDP sockets behind this same API.

#include "revolt.h"
#include "dplobby.h"
#include "play.h"

char ReceiveBuff[1024], TransmitBuff[1024];

DPID FromID, ToID, LocalPlayerID, ServerID;
START_DATA StartData;

void TransmitMessage(char *, short, DPID, long) {}
void TransmitMessageGuaranteed(char *, short, DPID, long) {}

bool InitPlay(void) { return false; }   // no session — single player
void KillPlay(void) {}
void ConnectionMenu(void) {}            // Android UI replaces this menu
void DisplayPlayers(void) {}
char GetRemoteMessages(void) { return 0; }
