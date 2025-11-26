# RAVENS enabled MECOrchestrator

This MECOrchestrator is a lot different from the original one, and the purpose of this .md file is just to identify some of them. 
The main diferences concern with the architecture (see /services) and the strategies that react to RAVENS updates (see /reaconOnUpdateStrategies). It is also dependent on the RAVENS Controller (another package which was added - see apps/mec/RavensApps).  


## Key Features

- **MEC Application Lifecycle Management:** The orchestrator handles the instantiation, termination, and run-time onboarding of MEC applications.
- **Dynamic Host Selection:** It employs a variety of policies to select the most suitable MEC host for deploying an application, considering factors like available resources, required services, and location.
- **Application Migration:** The orchestrator can migrate applications between MEC hosts to optimize performance and resource utilization, based on user mobility and network conditions.
- **RAVENS Integration:** It integrates with the RAVENS framework to receive real-time updates on user location and network status, enabling proactive and intelligent orchestration decisions.
## Architecture

The current implementation of the `MECOrchestrator` is composed of several key components:

- **`MecAppLifecycleManager`:** Responsible for the lifecycle of MEC applications, including starting, stopping, and onboarding.
- **`MecAppMigrationManager`:** Manages the migration of applications between MEC hosts.
- **`MecAppRegistry`:** A registry that keeps track of all the running MEC applications and their status.
- **`SelectionPolicy`:** A set of policies that determine how to select the best MEC host for an application (some of those are as the original repo).
- **`ReactionOnUpdate`:** A set of strategies that define how the orchestrator should react to updates from the RAVENS framework.

The orchestrator communicates with other components in the MEC system, such as the `UALCMP` (UE Application Lifecycle Management Proxy) and the MEC hosts, to perform its functions (mainly as the original).

## Configuration

It is configured using NED parameters in the simulation's `.ini` file. The following parameters are available:

- `selectionPolicy`: Specifies the host selection policy to use.
- `reactionStrategy`: Specifies the reaction on update strategy to use.
- `mecHostList`: A comma-separated list of the MEC hosts managed by the orchestrator.
- `mecApplicationPackageList`: A list of the MEC application packages to be onboarded at initialization.
- `onboardingTime`: The time it takes to onboard a new application.
- `instantiationTime`: The time ittakes to instantiate a new application.
- `terminationTime`: The time it takes to terminate an application.
- `migrationTime`: The time it takes to migrate an application.
- `migrationTimeout`: A timeout for the migration process.

### Host Selection Policies

The following host selection policies are supported (just the LocationBased one is different from the original):

- **`MecServiceBased`:** Selects the MEC host that provides the services required by the application.
- **`AvailableResourcesBased`:** Selects the MEC host with the most available resources (CPU, RAM, disk).
- **`MecHostBased`:** A simple policy that selects a predefined MEC host.
- **`LocationBased`:** Selects the MEC host that is closest to the user.

### Reaction on Update Strategies (completely different)

The following strategies for reacting to updates from the RAVENS framework are available (to be added more):

- **`RemoveOnExit`:** When a user leaves the network, the associated MEC application is terminated.
- **`MigrateOnChange`:** When a user moves to a new location, the orchestrator checks if a better MEC host is available and migrates the application if necessary.

## RAVENS Integration (completely different)

This `MECOrchestrator` is tightly integrated with the RAVENS (Resource Aware Virtualized Edge Network Slicing) framework. It receives updates from RAVENS about user mobility and network conditions. This information is used to make intelligent decisions about where to place and when to migrate MEC applications. The orchestrator can also send requests to a prediction engine (via `postRequestPrediction`) to forecast future network conditions and proactively optimize resource allocation.

## API

It is important to note that the `MECOrchestrator` **does not** follow the ETSI MEC compliant APIs. Instead, it uses a custom API based on OMNeT++ messages and signals for communication with other components.
