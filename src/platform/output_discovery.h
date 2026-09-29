#pragma once
#include <QString>
#include <QVariant>
#include <vector>

namespace rock {
struct OutputChoice {
    QString label;
    QVariant id; // Keyboard: Interception slot + hardware ID. Window: HWND as quint64.
    QString detail;
    quint32 processId{};
};
struct DiscoveryResult {
    std::vector<OutputChoice> choices;
    QString error;
};
DiscoveryResult discoverKeyboards();
DiscoveryResult discoverWindows();
}
