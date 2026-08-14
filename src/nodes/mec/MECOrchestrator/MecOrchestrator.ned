//
//                  Simu5G
//
// Authors: Giovanni Nardini, Giovanni Stea, Antonio Virdis (University of Pisa)
//
// This file is part of a software released under the license included in file
// "license.pdf". Please read LICENSE and README files before using it.
// The above files and the present reference are part of the software itself,
// and cannot be removed from it.
//

package simu5g.nodes.mec.MECOrchestrator;

//
// This module implements the MEC orchestrator of a MEC system.
// It does not follow ETSI compliant APIs, but the it handles the lifecycle operations
// of the standard by using OMNeT++ features.
// Communications with the LCM proxy occur via connections, while the MEC hosts associated with
// the MEC system (and the MEC orchestrator) are managed with the mecHostList parameter.
// This MEC orchestrator provides:
//   - MEC app instantiation
//   - MEC app termination
//   - MEC app run-time onboarding
//
simple MecOrchestrator
{
    parameters:
        @display("i=device/mainframe;bgb=1006.50995,692.76");
        string selectionPolicy = default("LocationBased"); // available policies: "MecHostBased", "MecServiceBased", "AvailableResourcesBased", "LocationBased"
        // Which RAVENS streams the orchestrator acts on. Every strategy hears all
        // of them; they differ only in what they do with each, which is what
        // makes them comparable.
        //   RemoveOnExit        - events, and only departures
        //   MigrateOnChange     - events (reactive)
        //   MigrateOnPrediction - events and predictions (proactive)
        string reactionStrategy = default("MigrateOnPrediction");
        // MigrateOnPrediction only. Whether a confirmed handover still moves the
        // application when the model did not predict it, or predicted it too late.
        //
        // On, a proactive run is really predictions plus a reactive safety net,
        // and a result cannot be attributed to either — a useless model still
        // looks passable because handovers quietly do the work. Off, the model
        // runs alone and the run measures what it is worth. Handovers are
        // recorded as decisions in both, so the two are compared on the same
        // rows, and RAVENS sends exactly the same thing either way.
        bool reactiveFallback = default(true);
        // Where the decision log is written: one row per orchestration decision,
        // under <decisionLogPath>run_<N>/. Point it at the same base path as the
        // RAVENS Controller and both sides land in the same run directory, so the
        // decisions join against what the Controller recorded. Empty disables the
        // log — a run then produces no record of what it decided, which is only
        // ever right for a throwaway run.
        string decisionLogPath = default("");
        // The reinforcement-learning engine the Learning strategy asks for its
        // actions, outside the simulation. Same form as the Controller's
        // predictionServerUrl, and pointed at a different port because the two
        // are different servers: models that describe live behind the
        // Controller, the model that decides lives here.
        //
        // Empty is fatal under that strategy rather than merely quiet: a
        // proactive run without a model server degrades to reactive, but a
        // learning run without an engine has no policy at all.
        string learningEngineUrl = default("");

        // How long to wait for one step. Generous because a training step runs a
        // gradient update, which is far slower than the prediction server's
        // inference — and because a timeout here ends the run, since a step with
        // no answer cannot be told apart from one where every user waits.
        double learningEngineTimeout @unit("s") = default(60s);

        // The learning ablation. Off, the agent sees only telemetry and the two
        // views, and has to read momentum out of the samples itself; on, it is
        // told where the model expects each user to go and learns whether to
        // believe it. Everything else is identical between the two, which is why
        // this is a parameter rather than a second profile.
        //
        // Off by default, and deliberately: this answer is what tells the
        // Controller whether to call the model server, so defaulting it on would
        // give every learning run a hidden dependency on a prediction server
        // being up. The arm that needs predictions asks for them.
        bool learningUsesPredictions = default(false);

        int mecHostIndex = default(10); // to be used with the MecHostBased policybased
        string mecHostList = default("");
        string mecApplicationPackageList = default("");


        // times to simulate lifecycle operations
        double onboardingTime @unit(s) = default(50ms);
        double instantiationTime @unit(s) = default(50ms);
        double terminationTime @unit(s) = default(500ms);

        double migrationTime @unit("s") = default(12s);
        double migrationTimeout @unit("s") = default(5s);

    gates:

        output toUALCMP;
        input fromUALCMP;

        // One way. Nothing was ever sent back to the Controller, and the return
        // gate that used to be declared here had no branch in handleMessage to
        // receive anything either.
        input fromRavensController;

}
