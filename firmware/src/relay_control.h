#ifndef RELAY_CONTROL_H
#define RELAY_CONTROL_H

void initRelays();
void handleAction(const char* action, int duration);
void togglePump();
void toggleFan();
void toggleLight();

#endif
