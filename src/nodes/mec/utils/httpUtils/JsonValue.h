#ifndef __JSONVALUE_H_
#define __JSONVALUE_H_

#include "nodes/mec/utils/httpUtils/json.hpp"
#include "omnetpp.h"

namespace simu5g {

// How a value becomes JSON when the simulation talks to something outside it.
//
// A simulation time has no JSON type of its own. It is written as the decimal
// string, which is the same text the CSV writes, so a timestamp reads
// identically whether a model is being trained on recorded runs or served
// during a live one.
//
// Shared rather than defined per client because the payloads have to agree:
// the prediction server and the learning engine both receive timestamps that
// offline analysis later joins against the same CSVs. A second copy of this
// rule is a second chance for one of them to drift.
inline nlohmann::json jsonValue(omnetpp::simtime_t value) { return value.str(); }

// Everything else goes through as-is: strings, whole numbers, doubles.
template <typename T>
inline nlohmann::json jsonValue(const T& value) { return value; }

} // namespace simu5g

#endif
