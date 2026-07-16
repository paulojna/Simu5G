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

#include "CellUEInfo.h"
#include "corenetwork/statsCollector/UeStatsCollector.h"

namespace simu5g {

CellUEInfo::CellUEInfo(){}

CellUEInfo::CellUEInfo(UeStatsCollector* ueCollector, const Ecgi& ecgi):
associateId_(), ecgi_(ecgi)
{
    ueCollector_ = ueCollector;
    associateId_.setAssociateId(ueCollector->getAssociateId());
}

CellUEInfo::CellUEInfo(UeStatsCollector* ueCollector, const mec::Ecgi& ecgi):
associateId_(), ecgi_(ecgi)
{
    ueCollector_ = ueCollector;
    associateId_.setAssociateId(ueCollector->getAssociateId());
}

CellUEInfo::~CellUEInfo(){}

nlohmann::ordered_json CellUEInfo::toJson() const
{
    nlohmann::ordered_json val;

    val["ecgi"] = ecgi_.toJson();
    val["associatedId"] = associateId_.toJson();

    int value;
    value = ueCollector_->get_dl_gbr_delay_ue();
    if(value != -1) val["dl_gbr_delay_ue"] = value;

    value = ueCollector_->get_ul_gbr_delay_ue();
    if(value != -1) val["ul_gbr_delay_ue"] = value;

    value = ueCollector_->get_dl_nongbr_delay_ue();
    if(value != -1) val["dl_nongbr_delay_ue"] = value;

    value = ueCollector_->get_ul_nongbr_delay_ue();
    if(value != -1) val["ul_nongbr_delay_ue"] = value;

    value = ueCollector_->get_dl_gbr_pdr_ue();
    if(value != -1) val["dl_gbr_pdr_ue"] = value;

    value = ueCollector_->get_ul_gbr_pdr_ue();
    if(value != -1) val["ul_gbr_pdr_ue"] = value;

    value = ueCollector_->get_dl_nongbr_pdr_ue();
    if(value != -1) val["dl_nongbr_pdr_ue"] = value;

    value = ueCollector_->get_ul_nongbr_pdr_ue();
    if(value != -1) val["ul_nongbr_pdr_ue"] = value;

    value = ueCollector_->get_dl_nongbr_throughput_ue();
    if(value != -1) val["dl_nongbr_throughput_ue"] = value;

    value = ueCollector_->get_ul_nongbr_throughput_ue();
    if(value != -1) val["ul_nongbr_throughput_ue"] = value;

    value = ueCollector_->get_dl_gbr_throughput_ue();
    if(value != -1) val["dl_gbr_throughput_ue"] = value;

    value = ueCollector_->get_ul_gbr_throughput_ue();
    if(value != -1) val["ul_gbr_throughput_ue"] = value;

    value = ueCollector_->get_dl_nongbr_data_volume_ue();
    if(value != -1) val["dl_nongbr_data_volume_ue"] = value;

    value = ueCollector_->get_ul_nongbr_data_volume_ue();
    if(value != -1) val["ul_nongbr_data_volume_ue"] = value;

    value = ueCollector_->get_dl_gbr_data_volume_ue();
    if(value != -1) val["dl_gbr_data_volume_ue"] = value;

    value = ueCollector_->get_ul_gbr_data_volume_ue();
    if(value != -1) val["ul_gbr_data_volume_ue"] = value;

    // Serving-cell mean RSRP (dBm, per ETSI TS 136 214) measured by the UE on
    // cell broadcasts. The measurement matches the ETSI GS MEC 012 MeasRepUe
    // rsrp field; carrying it in the L2Meas payload is a transport-level
    // extension (the conformant home is a MeasRepUe subscription).
    double rsrp = ueCollector_->get_rsrp_ue();
    if(rsrp != -1.0) val["rsrp"] = rsrp;

    return val;
}

} //namespace

