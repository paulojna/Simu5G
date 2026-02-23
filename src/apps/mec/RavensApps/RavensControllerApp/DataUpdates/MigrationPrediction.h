#ifndef MIGRATIONPREDICTION_H
#define MIGRATIONPREDICTION_H

#include <string>
#include "omnetpp.h"

namespace simu5g {

	class MigrationPrediction
	{
	protected:
		std::string ueAddress;        // user identifier (IP)
		std::string currentMEHId;     // where the user is now (e.g. "mecHost1")
		std::string targetMEHId;      // where to migrate (e.g. "mecHost3", or "" for exit)
		double migrationDelay;        // seconds from now to trigger migration
		omnetpp::simtime_t timestamp; // when this prediction was made

	public:
		MigrationPrediction();
		MigrationPrediction(const std::string& ueAddress,
							const std::string& currentMEHId,
							const std::string& targetMEHId,
							double migrationDelay,
							omnetpp::simtime_t timestamp);
		virtual ~MigrationPrediction();

		// setters
		void setUeAddress(const std::string& ueAddress);
		void setCurrentMEHId(const std::string& currentMEHId);
		void setTargetMEHId(const std::string& targetMEHId);
		void setMigrationDelay(double migrationDelay);
		void setTimestamp(omnetpp::simtime_t timestamp);

		// getters
		const std::string& getUeAddress() const;
		const std::string& getCurrentMEHId() const;
		const std::string& getTargetMEHId() const;
		double getMigrationDelay() const;
		omnetpp::simtime_t getTimestamp() const;

		// convenience
		bool isExitPrediction() const { return targetMEHId.empty(); }
	};

} // namespace simu5g

#endif // MIGRATIONPREDICTION_H
