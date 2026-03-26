#include "MigrationPrediction.h"

namespace simu5g {

	MigrationPrediction::MigrationPrediction()
		: ueAddress("")
		, currentMEHId("")
		, targetMEHId("")
		, migrationDelay(0.0)
		, timestamp(0)
	{
	}

	MigrationPrediction::MigrationPrediction(const std::string& ueAddress, const std::string& currentMEHId, const std::string& targetMEHId, double migrationDelay,
											 omnetpp::simtime_t timestamp):
		ueAddress(ueAddress),
		currentMEHId(currentMEHId),
		targetMEHId(targetMEHId),
		migrationDelay(migrationDelay),
		timestamp(timestamp){}

	MigrationPrediction::~MigrationPrediction()
	{
	}

	// setters
	void MigrationPrediction::setUeAddress(const std::string& ueAddress) { this->ueAddress = ueAddress; }
	void MigrationPrediction::setCurrentMEHId(const std::string& currentMEHId) { this->currentMEHId = currentMEHId; }
	void MigrationPrediction::setTargetMEHId(const std::string& targetMEHId) { this->targetMEHId = targetMEHId; }
	void MigrationPrediction::setMigrationDelay(double migrationDelay) { this->migrationDelay = migrationDelay; }
	void MigrationPrediction::setTimestamp(omnetpp::simtime_t timestamp) { this->timestamp = timestamp; }

	// getters
	const std::string& MigrationPrediction::getUeAddress() const { return ueAddress; }
	const std::string& MigrationPrediction::getCurrentMEHId() const { return currentMEHId; }
	const std::string& MigrationPrediction::getTargetMEHId() const { return targetMEHId; }
	double MigrationPrediction::getMigrationDelay() const { return migrationDelay; }
	omnetpp::simtime_t MigrationPrediction::getTimestamp() const { return timestamp; }

} // namespace simu5g
