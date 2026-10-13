#pragma once
#include "playback/performance_engine.h"
#include "output_discovery.h"
namespace rock {
std::unique_ptr<KeyOutput> createKeyOutput();
DiscoveryResult discoverOutputKeyboards();
enum class InterceptionState { Ready, NoKeyboard, RuntimeUnavailable, DriverUnavailable, Unsupported };
struct InterceptionStatus {
    InterceptionState state{InterceptionState::DriverUnavailable};
    int keyboardCount{};
    QString detail;
};
InterceptionStatus inspectInterception();
// Called from the foreground UI in response to the user's start action.
bool activateOutputWindow(quint64 window,quint32 process);
}
