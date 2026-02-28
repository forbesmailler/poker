#pragma once

#include <cstdint>

namespace poker {

enum class ActionType : uint8_t {
    FOLD = 0,
    CHECK = 1,
    CALL = 2,
    BET = 3,  // Includes raise and all-in
};

struct Action {
    ActionType type;
    int32_t amount;  // Chip amount (for BET); 0 for fold/check/call

    bool operator==(const Action& o) const {
        return type == o.type && amount == o.amount;
    }
    bool operator!=(const Action& o) const { return !(*this == o); }

    static Action fold() { return {ActionType::FOLD, 0}; }
    static Action check() { return {ActionType::CHECK, 0}; }
    static Action call() { return {ActionType::CALL, 0}; }
    static Action bet(int32_t amount) { return {ActionType::BET, amount}; }
};

struct BetSize {
    float pot_fraction;
    bool all_in;
};

} // namespace poker
