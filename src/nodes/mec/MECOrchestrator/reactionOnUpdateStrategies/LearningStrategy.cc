#include "LearningStrategy.h"
#include "DecisionRecording.h"

#include <map>

namespace simu5g {

LearningStrategy::LearningStrategy(IOrchestratorApi* api, const std::string& engineBaseUrl,
                                   double engineTimeout)
    : ReactionOnUpdate(api),
      client_(engineBaseUrl, engineTimeout),
      builder_(api)
{
}

void LearningStrategy::openEpisode(const std::vector<HostTopology>& hosts,
                                   const std::vector<CellTopology>& cells,
                                   const nlohmann::json& config)
{
    client_.openEpisode(builder_.buildTopology(hosts, cells, config));
}

void LearningStrategy::closeEpisode()
{
    // The terminal observation carries no window of its own: the run ended
    // between two of them. It is sent so the engine sees the state its last
    // action led to, which is what completes that transition.
    nlohmann::json observation = builder_.buildStep(omnetpp::simTime(), omnetpp::simTime(),
                                                    {}, {}, {}, context_);
    client_.closeEpisode(observation);
}

void LearningStrategy::reactOnUpdate(const UserEvent& event)
{
    // Held for the next step. The engine reads the events of a window together
    // with that window's telemetry rather than one at a time.
    context_.events.push_back(event);

    if (event.eventType != USER_EXIT)
        return;

    // Removal on a confirmed exit is a rule of the system, not a decision the
    // engine gets to make — every arm does it, so the learning arm has to as
    // well or it is not being compared on the same thing. Recorded exactly as
    // RemoveOnExit records it, from the shared helper, so the row is identical.
    bool hadApp = !api_->getAppCurrentMEH(event.ueAddress).empty();

    api_->removeAppFromSystem(event.ueAddress, event.fromMEHId);

    OrchestrationDecision decision = decisionFromEvent(event);
    decision.kind = hadApp ? DecisionKind::Remove : DecisionKind::None;
    decision.outcome = hadApp ? DecisionOutcome::Success : DecisionOutcome::NotNeeded;
    if (!hadApp)
        decision.reason = "no application to remove";
    api_->recordDecision(decision);
}

void LearningStrategy::reactOnUpdate(const std::vector<MigrationPrediction>& predictions)
{
    // Accumulated, never acted on. Under this strategy a prediction is an input
    // the engine may believe or ignore — that choice is the ablation the whole
    // arm exists to measure, so acting on one here would settle it in code.
    context_.predictions.insert(context_.predictions.end(), predictions.begin(), predictions.end());
}

void LearningStrategy::reactOnTelemetry(omnetpp::simtime_t windowStart, omnetpp::simtime_t windowEnd,
                                        const std::vector<std::string>& reportingMEHIds,
                                        const std::vector<UserSample>& userSamples,
                                        const std::vector<CellSample>& cellSamples)
{
    lastWindowEnd_ = windowEnd;

    nlohmann::json observation = builder_.buildStep(windowStart, windowEnd, reportingMEHIds,
                                                    userSamples, cellSamples, context_);

    nlohmann::json actions = client_.step(observation);

    // Drained only once the step has been sent: a failed request throws, and
    // the run ends holding the events it never delivered rather than losing
    // them silently.
    context_.events.clear();
    context_.predictions.clear();

    context_.rejectedActions = executeActions(actions);
}

nlohmann::json LearningStrategy::executeActions(const nlohmann::json& actions)
{
    nlohmann::json rejected = nlohmann::json::array();

    // The application view once for the whole step rather than per action: the
    // engine may name every user, and each of those would otherwise be its own
    // scan.
    std::map<std::string, AppPlacement> placements;
    for (const auto& placement : api_->getAppPlacements())
        placements[placement.ueAddress] = placement;

    for (const auto& action : actions)
    {
        const std::string ueAddress = action.value("ueAddress", std::string());
        const std::string type      = action.value("type", std::string());
        const std::string targetMEH = action.value("targetMEH", std::string());

        OrchestrationDecision decision;
        decision.decidedAt  = omnetpp::simTime();
        decision.ueAddress  = canonicalUeAddress(ueAddress);
        decision.trigger    = DecisionTrigger::LearningAction;
        decision.observedAt = lastWindowEnd_;
        decision.toMEHId    = targetMEH;

        // Refusing, in one place: the log gets a row, the engine gets the reason
        // back in the next observation, and nothing is carried out.
        auto refuse = [&](const std::string& reason) {
            decision.kind    = DecisionKind::None;
            decision.outcome = DecisionOutcome::NotNeeded;
            decision.reason  = reason;
            api_->recordDecision(decision);

            nlohmann::json row;
            row["ueAddress"] = ueAddress;
            row["reason"]    = reason;
            rejected.push_back(row);
        };

        if (type != "migrate") {
            refuse("unknown action type: " + type);
            continue;
        }

        auto found = placements.find(canonicalUeAddress(ueAddress));
        if (found == placements.end()) {
            refuse("no application for this user");
            continue;
        }

        const AppPlacement& placement = found->second;

        // Anything not Placed is already in flight or gone. Starting a second
        // move on top of one in progress is the case the single-destination
        // policy exists to prevent, and it is the migration manager's to decide
        // — not something to start from here.
        if (placement.state != MecAppRegistry::AppState::Placed) {
            refuse(std::string("application is ") + appStateName(placement.state));
            continue;
        }

        if (targetMEH.empty()) {
            refuse("no target host named");
            continue;
        }

        if (targetMEH == placement.mecHost) {
            refuse("already on the target host");
            continue;
        }

        decision.fromMEHId = placement.mecHost;

        MigrationResult result = api_->migrateApp(ueAddress, targetMEH, placement.mecHost);
        fillFromMigrationResult(decision, result);
        api_->recordDecision(decision);

        // A migration that did not start is reported back too. To the engine it
        // is the same situation as a refusal — the action it chose did not
        // happen — and only the reason differs.
        if (!result.success) {
            nlohmann::json row;
            row["ueAddress"] = ueAddress;
            row["reason"]    = result.errorMessage;
            rejected.push_back(row);
        }
    }

    return rejected;
}

}
