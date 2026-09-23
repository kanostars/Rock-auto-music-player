#pragma once
#include "playback/performance_engine.h"
#include "output_discovery.h"
namespace rock {
std::unique_ptr<KeyOutput> createKeyOutput();
DiscoveryResult discoverOutputKeyboards();
}
