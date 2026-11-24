#pragma once
#include <cstdint>
#include <string>

enum class DoorState { Simulation, Locked, Open, Fault };

struct DoorStatus {
    DoorState state = DoorState::Simulation;
    std::uint64_t revision = 0;
    std::string error;
};
