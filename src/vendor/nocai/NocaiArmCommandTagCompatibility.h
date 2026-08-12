#pragma once

class QLibrary;
class QString;

namespace NocaiArmCommandTagCompatibility {

// Enables the three corrections validated against the ARM64 X-33 SDK: the
// socket-boundary wire tag, response-password constant, and signed head-offset
// loads. Only the loaded code mapping is adjusted; the SDK file is unchanged.
// Unknown builds and instruction layouts are refused.
bool install(const void* owner, QLibrary& library, QString* errorMessage);
void uninstall(const void* owner);

// Retains the selected Ethernet interface's process-scoped promiscuous packet
// membership for the entire application lifetime. The validated ARM SDK needs
// this before it opens its control and data sockets.
bool ensurePromiscuousReceive(QLibrary& library, int printerIndex,
                              QString* detailMessage);

// Clears crash-stale local PrinterSocket channel locks for the currently
// selected printer. This never sends a printer command.
bool clearStaleLocalSocketLocks(QLibrary& library, int printerIndex,
                                QString* detailMessage);

} // namespace NocaiArmCommandTagCompatibility
