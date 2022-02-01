#include "GearSvc/IGearSvc.h"
#include "PandoraPFAlg.h"
#include "edm4hep/Vector3f.h"
#include "edm4hep/Vector3d.h"
#include "edm4hep/SimCalorimeterHit.h"
#include "edm4hep/CaloHitContribution.h"
#include "edm4hep/ClusterConst.h"
#include <cmath>
#include <algorithm>
#include "gear/BField.h"
#include <gear/GEAR.h>

#include "LCContent.h"


pandora::Pandora* PandoraPFAlg::m_pPandora=0;

DECLARE_COMPONENT( PandoraPFAlg )

template<typename T ,typename T1>
StatusCode getCol(T & t, T1 & t1)
{
    try {
        t1 = t.get();
    }
    catch ( GaudiException &e ) {
        std::cout << "Collection " << t.fullKey() << " is unavailable in event "  << std::endl;
    }
    return StatusCode::SUCCESS;
}



PandoraPFAlg::PandoraPFAlg(const std::string& name, ISvcLocator* svcLoc)
  : GaudiAlgorithm(name, svcLoc),
    _nEvt(0)
{
 m_CollectionMaps = new CollectionMaps();
  
 declareProperty("WriteClusterCollection"              , m_ClusterCollection_w,               "Handle of the ClusterCollection               output collection" );
 declareProperty("WriteReconstructedParticleCollection", m_ReconstructedParticleCollection_w, "Handle of the ReconstructedParticleCollection output collection" );
 declareProperty("WriteVertexCollection"               , m_VertexCollection_w,                "Handle of the VertexCollection                output collection" );

}


void PandoraPFAlg::FinaliseSteeringParameters(ISvcLocator* svcloc)
{
    // ATTN: This function seems to be necessary for operations that cannot easily be performed at construction of the processor,
    // when the steering file is parsed e.g. the call to GEAR to get the inner bfield
    
    m_caloHitCreatorSettings.m_absorberRadLengthECal = m_geometryCreatorSettings.m_absorberRadLengthECal;
    m_caloHitCreatorSettings.m_absorberIntLengthECal = m_geometryCreatorSettings.m_absorberIntLengthECal;
    m_caloHitCreatorSettings.m_absorberRadLengthHCal = m_geometryCreatorSettings.m_absorberRadLengthHCal;
    m_caloHitCreatorSettings.m_absorberIntLengthHCal = m_geometryCreatorSettings.m_absorberIntLengthHCal;
    m_caloHitCreatorSettings.m_absorberRadLengthOther = m_geometryCreatorSettings.m_absorberRadLengthOther;
    m_caloHitCreatorSettings.m_absorberIntLengthOther = m_geometryCreatorSettings.m_absorberIntLengthOther;

    m_caloHitCreatorSettings.m_hCalEndCapInnerSymmetryOrder = m_geometryCreatorSettings.m_hCalEndCapInnerSymmetryOrder;
    m_caloHitCreatorSettings.m_hCalEndCapInnerPhiCoordinate = m_geometryCreatorSettings.m_hCalEndCapInnerPhiCoordinate;
    
    m_trackCreatorSettings.m_prongSplitVertexCollections = m_trackCreatorSettings.m_prongVertexCollections;
    m_trackCreatorSettings.m_prongSplitVertexCollections.insert(m_trackCreatorSettings.m_prongSplitVertexCollections.end(),
        m_trackCreatorSettings.m_splitVertexCollections.begin(), m_trackCreatorSettings.m_splitVertexCollections.end());
    
    IGearSvc*  iSvc = 0;
    StatusCode sc = svcloc->service("GearSvc", iSvc, false);
    if ( !sc ) 
    {
        throw "Failed to find GearSvc ...";
    }
    gear::GearMgr* _GEAR = iSvc->getGearMgr();
    m_settings.m_innerBField = _GEAR->getBField().at(gear::Vector3D(0., 0., 0.)).z();
    std::cout<<"m_innerBField="<<m_settings.m_innerBField<<std::endl;    
    m_mcParticleCreatorSettings.m_bField = m_settings.m_innerBField;
}




StatusCode PandoraPFAlg::initialize()
{

  std::cout<<"init PandoraPFAlg"<<std::endl;
  for ( const auto& col : m_readCols ) {
      auto seperater = col.find(':');
      std::string colType = col.substr(0, seperater);
      std::string colName = col.substr(seperater+1);
      m_collections[colName] = colType;

      if ( colType == "MCParticle" ) {
          m_dataHandles[colName] =
              new DataHandle<edm4hep::MCParticleCollection>(colName, Gaudi::DataHandle::Reader, this);
      }
      else if ( colType == "Track" ) {
          m_dataHandles[colName] =
              new DataHandle<edm4hep::TrackCollection>(colName, Gaudi::DataHandle::Reader, this);
      }
      else if ( colType == "CalorimeterHit" ) {
          m_dataHandles[colName] =
              new DataHandle<edm4hep::CalorimeterHitCollection>(colName, Gaudi::DataHandle::Reader, this);
      }
      else if ( colType == "Vertex" ) {
          m_dataHandles[colName] =
              new DataHandle<edm4hep::VertexCollection>(colName, Gaudi::DataHandle::Reader, this);
      }
      else if ( colType == "MCRecoTrackerAssociation" ) {
          m_dataHandles[colName] =
              new DataHandle<edm4hep::MCRecoTrackerAssociationCollection>(colName, Gaudi::DataHandle::Reader, this);
      }
      else if ( colType == "MCRecoCaloAssociation" ) {
          m_dataHandles[colName] =
              new DataHandle<edm4hep::MCRecoCaloAssociationCollection>(colName, Gaudi::DataHandle::Reader, this);
      }
      else {
            error() << "invalid collection type: " << colType << endmsg;
            return StatusCode::FAILURE;
      }
  }

  // XML file
  m_settings.m_pandoraSettingsXmlFile =  m_PandoraSettingsXmlFile ; 
  // Hadronic energy non-linearity correction
  m_settings.m_inputEnergyCorrectionPoints = m_InputEnergyCorrectionPoints;
  m_settings.m_outputEnergyCorrectionPoints = m_OutputEnergyCorrectionPoints;
  // B-field parameters
  m_settings.m_muonBarrelBField = m_MuonBarrelBField; 
  m_settings.m_muonEndCapBField = m_MuonEndCapBField;
  
  m_trackCreatorSettings.m_trackCollections = m_TrackCollections ; 
  m_trackCreatorSettings.m_kinkVertexCollections = m_KinkVertexCollections; 
  m_trackCreatorSettings.m_prongVertexCollections = m_ProngVertexCollections;
  m_trackCreatorSettings.m_splitVertexCollections = m_SplitVertexCollections;
  m_trackCreatorSettings.m_v0VertexCollections = m_V0VertexCollections; 
  
  m_caloHitCreatorSettings.m_eCalCaloHitCollections = m_ECalCaloHitCollections;
  m_caloHitCreatorSettings.m_hCalCaloHitCollections = m_HCalCaloHitCollections;
  m_caloHitCreatorSettings.m_lCalCaloHitCollections = m_LCalCaloHitCollections;
  m_caloHitCreatorSettings.m_lHCalCaloHitCollections = m_LHCalCaloHitCollections;
  m_caloHitCreatorSettings.m_muonCaloHitCollections = m_MuonCaloHitCollections; 
  m_mcParticleCreatorSettings.m_mcParticleCollections = m_MCParticleCollections;
  m_mcParticleCreatorSettings.m_CaloHitRelationCollections = m_RelCaloHitCollections; 
  m_mcParticleCreatorSettings.m_TrackRelationCollections = m_RelTrackCollections;
  
  
  // Absorber properties
  m_geometryCreatorSettings.m_absorberRadLengthECal = m_AbsorberRadLengthECal;
  m_geometryCreatorSettings.m_absorberIntLengthECal = m_AbsorberIntLengthECal;
  m_geometryCreatorSettings.m_absorberRadLengthHCal = m_AbsorberRadLengthHCal;
  m_geometryCreatorSettings.m_absorberIntLengthHCal = m_AbsorberIntLengthHCal;
  m_geometryCreatorSettings.m_absorberRadLengthOther = m_AbsorberRadLengthOther;
  m_geometryCreatorSettings.m_absorberIntLengthOther = m_AbsorberIntLengthOther;
  
  // Name of PFO collection written by GaudiPandora
  
  m_pfoCreatorSettings.m_clusterCollectionName = m_ClusterCollectionName;// not used  
  m_pfoCreatorSettings.m_pfoCollectionName = m_PFOCollectionName;//
  m_pfoCreatorSettings.m_startVertexCollectionName = m_StartVertexCollectionName; //
  m_pfoCreatorSettings.m_startVertexAlgName = m_StartVertexAlgorithmName;//
   
  m_pfoCreatorSettings.m_emStochasticTerm = m_EMStochasticTerm;
  m_pfoCreatorSettings.m_hadStochasticTerm = m_HadStochasticTerm;
  m_pfoCreatorSettings.m_emConstantTerm = m_EMConstantTerm;
  m_pfoCreatorSettings.m_hadConstantTerm = m_HadConstantTerm;
  
  // Calibration constants
  m_caloHitCreatorSettings.m_eCalToMip = m_ECalToMipCalibration; 
  m_caloHitCreatorSettings.m_hCalToMip = m_HCalToMipCalibration;
  m_caloHitCreatorSettings.m_eCalMipThreshold = m_ECalMipThreshold; 
  m_caloHitCreatorSettings.m_muonToMip = m_MuonToMipCalibration;
  m_caloHitCreatorSettings.m_hCalMipThreshold = m_HCalMipThreshold; 
  m_caloHitCreatorSettings.m_eCalToEMGeV = m_ECalToEMGeVCalibration;
  m_caloHitCreatorSettings.m_hCalToEMGeV = m_HCalToEMGeVCalibration;
  m_caloHitCreatorSettings.m_eCalToHadGeVEndCap = m_ECalToHadGeVCalibrationEndCap;
  m_caloHitCreatorSettings.m_eCalToHadGeVBarrel = m_ECalToHadGeVCalibrationBarrel; 
  m_caloHitCreatorSettings.m_hCalToHadGeV = m_HCalToHadGeVCalibration;
  m_caloHitCreatorSettings.m_muonDigitalHits = m_DigitalMuonHits; 
  m_caloHitCreatorSettings.m_muonHitEnergy = m_MuonHitEnergy; 
  m_caloHitCreatorSettings.m_maxHCalHitHadronicEnergy = m_MaxHCalHitHadronicEnergy; 
  m_caloHitCreatorSettings.m_nOuterSamplingLayers = m_NOuterSamplingLayers; 
  m_caloHitCreatorSettings.m_layersFromEdgeMaxRearDistance = m_LayersFromEdgeMaxRearDistance; 
  
  // Track relationship parameters
  m_trackCreatorSettings.m_shouldFormTrackRelationships = m_ShouldFormTrackRelationships; 
  // Initial track hit specifications
  m_trackCreatorSettings.m_minTrackHits = m_MinTrackHits;
  m_trackCreatorSettings.m_minFtdTrackHits = m_MinFtdTrackHits; 
  m_trackCreatorSettings.m_maxTrackHits = m_MaxTrackHits; 
  ////m_trackCreatorSettings.m_useOldTrackStateCalculation = m_UseOldTrackStateCalculation;
  // Track PFO usage parameters
  m_trackCreatorSettings.m_d0TrackCut = m_D0TrackCut; 
  m_trackCreatorSettings.m_z0TrackCut = m_Z0TrackCut;
  m_trackCreatorSettings.m_usingNonVertexTracks = m_UseNonVertexTracks;
  m_trackCreatorSettings.m_usingUnmatchedNonVertexTracks = m_UseUnmatchedNonVertexTracks;
  m_trackCreatorSettings.m_usingUnmatchedVertexTracks = m_UseUnmatchedVertexTracks;
  m_trackCreatorSettings.m_unmatchedVertexTrackMaxEnergy = m_UnmatchedVertexTrackMaxEnergy; 
  m_trackCreatorSettings.m_d0UnmatchedVertexTrackCut = m_D0UnmatchedVertexTrackCut; 
  m_trackCreatorSettings.m_z0UnmatchedVertexTrackCut = m_Z0UnmatchedVertexTrackCut;
  m_trackCreatorSettings.m_zCutForNonVertexTracks = m_ZCutForNonVertexTracks;
  // Track "reaches ecal" parameters
  m_trackCreatorSettings.m_reachesECalNTpcHits = m_ReachesECalNTpcHits;
  m_trackCreatorSettings.m_reachesECalNFtdHits = m_ReachesECalNFtdHits;
  m_trackCreatorSettings.m_reachesECalTpcOuterDistance = m_ReachesECalTpcOuterDistance;
  m_trackCreatorSettings.m_reachesECalMinFtdLayer = m_ReachesECalMinFtdLayer;
  m_trackCreatorSettings.m_reachesECalTpcZMaxDistance = m_ReachesECalTpcZMaxDistance;
  m_trackCreatorSettings.m_reachesECalFtdZMaxDistance = m_ReachesECalFtdZMaxDistance;
  m_trackCreatorSettings.m_curvatureToMomentumFactor = m_CurvatureToMomentumFactor;
  m_trackCreatorSettings.m_minTrackECalDistanceFromIp = m_MinTrackECalDistanceFromIp;
  // Final track quality parameters
  m_trackCreatorSettings.m_maxTrackSigmaPOverP = m_MaxTrackSigmaPOverP;
  m_trackCreatorSettings.m_minMomentumForTrackHitChecks = m_MinMomentumForTrackHitChecks;
  m_trackCreatorSettings.m_tpcMembraneMaxZ = m_TpcMembraneMaxZ;
  m_trackCreatorSettings.m_minTpcHitFractionOfExpected = m_MinTpcHitFractionOfExpected;
  m_trackCreatorSettings.m_minFtdHitsForTpcHitFraction = m_MinFtdHitsForTpcHitFraction;
  m_trackCreatorSettings.m_maxTpcInnerRDistance = m_MaxTpcInnerRDistance;
  
  
  // Additional geometry parameters
  m_geometryCreatorSettings.m_eCalEndCapInnerSymmetryOrder = m_ECalEndCapInnerSymmetryOrder;
  m_geometryCreatorSettings.m_eCalEndCapInnerPhiCoordinate = m_ECalEndCapInnerPhiCoordinate;
  m_geometryCreatorSettings.m_eCalEndCapOuterSymmetryOrder = m_ECalEndCapOuterSymmetryOrder;
  m_geometryCreatorSettings.m_eCalEndCapOuterPhiCoordinate = m_ECalEndCapOuterPhiCoordinate;
  m_geometryCreatorSettings.m_hCalEndCapInnerSymmetryOrder = m_HCalEndCapInnerSymmetryOrder;
  m_geometryCreatorSettings.m_hCalEndCapInnerPhiCoordinate = m_HCalEndCapInnerPhiCoordinate;
  m_geometryCreatorSettings.m_hCalEndCapOuterSymmetryOrder = m_HCalEndCapOuterSymmetryOrder;
  m_geometryCreatorSettings.m_hCalEndCapOuterPhiCoordinate = m_HCalEndCapOuterPhiCoordinate;
  m_geometryCreatorSettings.m_hCalRingInnerSymmetryOrder = m_HCalRingInnerSymmetryOrder;
  m_geometryCreatorSettings.m_hCalRingInnerPhiCoordinate = m_HCalRingInnerPhiCoordinate;
  m_geometryCreatorSettings.m_hCalRingOuterSymmetryOrder = m_HCalRingOuterSymmetryOrder; 
  m_geometryCreatorSettings.m_hCalRingOuterPhiCoordinate = m_HCalRingOuterPhiCoordinate;
  
  // For Strip Splitting method and also for hybrid ECAL
  m_caloHitCreatorSettings.m_stripSplittingOn = m_StripSplittingOn;
  m_caloHitCreatorSettings.m_useEcalScLayers = m_UseEcalScLayers;
  // Parameters for hybrid ECAL
  // Energy to MIP for Si-layers and Sc-layers, respectively.
  //Si
  m_caloHitCreatorSettings.m_eCalSiToMip = m_ECalSiToMipCalibration;
  //Sc
  m_caloHitCreatorSettings.m_eCalScToMip = m_ECalScToMipCalibration;
  // MipThreshold for Si-layers and Sc-layers, respectively.
  // Si
  m_caloHitCreatorSettings.m_eCalSiMipThreshold = m_ECalSiMipThreshold;
  //Sc
  m_caloHitCreatorSettings.m_eCalScMipThreshold = m_ECalScMipThreshold;
  // EcalToEM for Si-layers and Sc-layers, respectively.
  //Si
  m_caloHitCreatorSettings.m_eCalSiToEMGeV = m_ECalSiToEMGeVCalibration;
  //Sc
  m_caloHitCreatorSettings.m_eCalScToEMGeV = m_ECalScToEMGeVCalibration;
  // EcalToHad for Si-layers and Sc-layers of the endcaps, respectively.
  //Si
  m_caloHitCreatorSettings.m_eCalSiToHadGeVEndCap = m_ECalSiToHadGeVCalibrationEndCap;
  //Sc
  m_caloHitCreatorSettings.m_eCalScToHadGeVEndCap = m_ECalScToHadGeVCalibrationEndCap;
  // EcalToHad for Si-layers and Sc-layers of the barrel, respectively.
  //Si
  m_caloHitCreatorSettings.m_eCalSiToHadGeVBarrel = m_ECalSiToHadGeVCalibrationBarrel;
  //Sc
  m_caloHitCreatorSettings.m_eCalScToHadGeVBarrel = m_ECalScToHadGeVCalibrationBarrel;

  try
  {
      ISvcLocator* svcloc = serviceLocator();
      this->FinaliseSteeringParameters(svcloc);
      m_pPandora = new pandora::Pandora();
      m_pMCParticleCreator = new MCParticleCreator(m_mcParticleCreatorSettings, m_pPandora);
      m_pGeometryCreator = new GeometryCreator(m_geometryCreatorSettings, m_pPandora);
      PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, m_pGeometryCreator->CreateGeometry(svcloc));
      m_pCaloHitCreator = new CaloHitCreator(m_caloHitCreatorSettings, m_pPandora, svcloc, 0);
      m_pTrackCreator = new TrackCreator(m_trackCreatorSettings, m_pPandora, svcloc);
      m_pPfoCreator = new PfoCreator(m_pfoCreatorSettings, m_pPandora);
      PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, this->RegisterUserComponents());
      PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, PandoraApi::ReadSettings(*m_pPandora, m_settings.m_pandoraSettingsXmlFile));
  }
  catch (pandora::StatusCodeException &statusCodeException)
  {
      std::cout << "Failed to initialize gaudi pandora: " << statusCodeException.ToString() << std::endl;
      throw statusCodeException;
  }
  catch (...)
  {
      std::cout << "Failed to initialize gaudi pandora: unrecognized exception" << std::endl;
      throw;
  }


  return GaudiAlgorithm::initialize();
}

StatusCode PandoraPFAlg::execute()
{
    
    try
    {
        
        updateMap();
        PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, m_pMCParticleCreator->CreateMCParticles(*m_CollectionMaps));
        PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, m_pCaloHitCreator->CreateCaloHits(*m_CollectionMaps));
        PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, m_pMCParticleCreator->CreateCaloHitToMCParticleRelationships(*m_CollectionMaps, m_pCaloHitCreator->GetCalorimeterHitVector() ));
        PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, m_pTrackCreator->CreateTrackAssociations(*m_CollectionMaps));
        PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, m_pTrackCreator->CreateTracks(*m_CollectionMaps));
        PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, m_pMCParticleCreator->CreateTrackToMCParticleRelationships(*m_CollectionMaps, m_pTrackCreator->GetTrackVector() ));
        PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, PandoraApi::ProcessEvent(*m_pPandora));
        PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, m_pPfoCreator->CreateParticleFlowObjects(*m_CollectionMaps, m_ClusterCollection_w, m_ReconstructedParticleCollection_w, m_VertexCollection_w));
        
        StatusCode sc0 = CreateMCRecoParticleAssociation();

        PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, PandoraApi::Reset(*m_pPandora));
        this->Reset();
    }
    catch (pandora::StatusCodeException &statusCodeException)
    {
        std::cout << "Gaudi pandora failed to process event: " << statusCodeException.ToString() << std::endl;
        throw statusCodeException;
    }
    catch (...)
    {
        std::cout << "Gaudi pandora failed to process event: unrecognized exception" << std::endl;
        throw;
    }
  
  info() << "PandoraPFAlg Processed " << _nEvt << " events " << endmsg;
  _nEvt ++ ;

  return StatusCode::SUCCESS;
}

StatusCode PandoraPFAlg::finalize()
{
  info() << "Finalized. Processed " << _nEvt << " events " << endmsg;
  delete m_pPandora;
  delete m_pGeometryCreator;
  delete m_pCaloHitCreator;
  delete m_pTrackCreator;
  delete m_pMCParticleCreator;
  delete m_pPfoCreator;
  return GaudiAlgorithm::finalize();
}




pandora::StatusCode PandoraPFAlg::RegisterUserComponents() const
{
    
    PANDORA_RETURN_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, LCContent::RegisterAlgorithms(*m_pPandora));
    PANDORA_RETURN_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, LCContent::RegisterBasicPlugins(*m_pPandora));

    PANDORA_RETURN_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, LCContent::RegisterBFieldPlugin(*m_pPandora,
        m_settings.m_innerBField, m_settings.m_muonBarrelBField, m_settings.m_muonEndCapBField));

    PANDORA_RETURN_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, LCContent::RegisterNonLinearityEnergyCorrection(*m_pPandora,
        "NonLinearity", pandora::HADRONIC, m_settings.m_inputEnergyCorrectionPoints, m_settings.m_outputEnergyCorrectionPoints));
    
    return pandora::STATUS_CODE_SUCCESS;
}


void PandoraPFAlg::Reset()
{
    m_pCaloHitCreator->Reset();
    m_pTrackCreator->Reset();
    m_pMCParticleCreator->Reset();

    m_CollectionMaps->clear();
}

const pandora::Pandora *PandoraPFAlg::GetPandora() const
{
    if (NULL == m_pPandora)
        throw pandora::StatusCodeException(pandora::STATUS_CODE_NOT_INITIALIZED);

    return m_pPandora;
}
PandoraPFAlg::Settings::Settings() :
    m_innerBField(3.5f),
    m_muonBarrelBField(-1.5f),
    m_muonEndCapBField(0.01f)
{
}
CollectionMaps::CollectionMaps()
{
}
void CollectionMaps::clear()
{
collectionMap_MC.clear();
collectionMap_CaloHit.clear();
collectionMap_Vertex.clear();
collectionMap_Track.clear();
collectionMap_CaloRel.clear();
collectionMap_TrkRel.clear();
}

StatusCode PandoraPFAlg::updateMap()
{
    for(auto &v : m_dataHandles){
        //std::cout<<"going to col name="<<v.first<<",with type="<<m_collections[v.first]<<std::endl;
        try{
            if(m_collections[v.first]=="MCParticle"){
                auto handle = dynamic_cast<DataHandle<edm4hep::MCParticleCollection>*> (v.second);
                auto po = handle->get();
                if(po != NULL){
                    std::vector<edm4hep::MCParticle> v_mc;
                    m_CollectionMaps->collectionMap_MC [v.first] = v_mc;
                    for(unsigned int i=0 ; i< po->size(); i++) m_CollectionMaps->collectionMap_MC [v.first].emplace_back(po->at(i).clone());
                    std::cout<<"saved col name="<<v.first<<std::endl;
                }
                else{
                std::cout<<"don't find col name="<<v.first<<std::endl;
                }
            }
            else if(m_collections[v.first]=="CalorimeterHit"){
                auto handle = dynamic_cast<DataHandle<edm4hep::CalorimeterHitCollection>*> (v.second);
                auto po = handle->get();
                if(po != NULL){
                    std::vector<edm4hep::CalorimeterHit> v_cal;
                    m_CollectionMaps->collectionMap_CaloHit[v.first] = v_cal ;
                    for(unsigned int i=0 ; i< po->size(); i++) m_CollectionMaps->collectionMap_CaloHit [v.first].emplace_back(po->at(i).clone());
                    std::cout<<"saved col name="<<v.first<<std::endl;
                }
                else{
                std::cout<<"don't find col name="<<v.first<<std::endl;
                }
            }
            else if(m_collections[v.first]=="Track"){
                auto handle = dynamic_cast<DataHandle<edm4hep::TrackCollection>*> (v.second);
                auto po = handle->get();
                if(po != NULL){
                    std::vector<edm4hep::Track> v_cal;
                    m_CollectionMaps->collectionMap_Track[v.first] = v_cal ;
                    for(unsigned int i=0 ; i< po->size(); i++) m_CollectionMaps->collectionMap_Track [v.first].emplace_back(po->at(i).clone());
                    std::cout<<"saved col name="<<v.first<<std::endl;
                }
                else{
                std::cout<<"don't find col name="<<v.first<<std::endl;
                }
            }
            else if(m_collections[v.first]=="Vertex"){
                auto handle = dynamic_cast<DataHandle<edm4hep::VertexCollection>*> (v.second);
                auto po = handle->get();
                if(po != NULL){
                    std::vector<edm4hep::Vertex> v_cal;
                    m_CollectionMaps->collectionMap_Vertex[v.first] = v_cal ;
                    for(unsigned int i=0 ; i< po->size(); i++) m_CollectionMaps->collectionMap_Vertex [v.first].emplace_back(po->at(i).clone());
                    std::cout<<"saved col name="<<v.first<<std::endl;
                }
                else{
                std::cout<<"don't find col name="<<v.first<<std::endl;
                }
            }
            else if(m_collections[v.first]=="MCRecoCaloAssociation"){
                auto handle = dynamic_cast<DataHandle<edm4hep::MCRecoCaloAssociationCollection>*> (v.second);
                auto po = handle->get();
                if(po != NULL){
                    std::vector<edm4hep::MCRecoCaloAssociation> v_cal;
                    m_CollectionMaps->collectionMap_CaloRel[v.first] = v_cal ;
                    for(unsigned int i=0 ; i< po->size(); i++) m_CollectionMaps->collectionMap_CaloRel [v.first].emplace_back(po->at(i).clone());
                    std::cout<<"saved col name="<<v.first<<std::endl;
                }
                else{
                std::cout<<"don't find col name="<<v.first<<std::endl;
                }
            }
            else if(m_collections[v.first]=="MCRecoTrackerAssociation"){
                auto handle = dynamic_cast<DataHandle<edm4hep::MCRecoTrackerAssociationCollection>*> (v.second);
                auto po = handle->get();
                if(po != NULL){
                    std::vector<edm4hep::MCRecoTrackerAssociation> v_cal;
                    m_CollectionMaps->collectionMap_TrkRel[v.first] = v_cal ;
                    for(unsigned int i=0 ; i< po->size(); i++) m_CollectionMaps->collectionMap_TrkRel [v.first].emplace_back(po->at(i).clone());
                    std::cout<<"saved col name="<<v.first<<std::endl;
                }
                else{
                std::cout<<"don't find col name="<<v.first<<std::endl;
                }
            }
            else{
            std::cout<<"wrong type name for col :"<<v.first<<std::endl;
            }
        }//try
        catch(...){
            std::cout<<"don't find "<<v.first<<"in event"<<std::endl;
            std::cout<<"don't find  col name="<<v.first<<",with type="<<m_collections[v.first]<<" in this event"<<std::endl;
        }
    }
    return StatusCode::SUCCESS;
}

// create simple MCRecoParticleAssociation using calorimeter hit only now
StatusCode PandoraPFAlg::CreateMCRecoParticleAssociation()
{
    edm4hep::MCRecoParticleAssociationCollection* pMCRecoParticleAssociationCollection  = m_MCRecoParticleAssociation_w.createAndPut();
    const edm4hep::ReconstructedParticleCollection* reco_col = m_ReconstructedParticleCollection_w.get();
    for(int i=0; i<reco_col->size();i++)
    {
        std::map<int, edm4hep::ConstMCParticle> mc_map;
        std::map<int, float > id_edep_map;
        float tot_en = 0 ;
        const edm4hep::ReconstructedParticle pReco = reco_col->at(i).clone();
        for(int j=0; j < pReco.clusters_size(); j++)
        {
            edm4hep::ConstCluster cluster = pReco.getClusters(j);
            for(int k=0; k < cluster.hits_size(); k++)
            {
                edm4hep::ConstCalorimeterHit hit = cluster.getHits(k);
                for(std::map<std::string, std::vector<edm4hep::MCRecoCaloAssociation> >::iterator iter = m_CollectionMaps->collectionMap_CaloRel.begin(); iter != m_CollectionMaps->collectionMap_CaloRel.end(); iter++)
                {
                    for(std::vector<edm4hep::MCRecoCaloAssociation>::iterator it = iter->second.begin(); it != iter->second.end(); it ++)
                    {
                        if(it->getRec().id() != hit.id()) continue;
                        for(std::vector<edm4hep::ConstCaloHitContribution>::const_iterator itc = it->getSim().contributions_begin(); itc != it->getSim().contributions_end(); itc++)
                        {
                            if(mc_map.find(itc->getParticle().id()) == mc_map.end()) mc_map[itc->getParticle().id()] = itc->getParticle() ;
                            if(id_edep_map.find(itc->getParticle().id()) != id_edep_map.end()) id_edep_map[itc->getParticle().id()] = id_edep_map[itc->getParticle().id()] + itc->getEnergy() ;
                            else                                                               id_edep_map[itc->getParticle().id()] = itc->getEnergy() ;
                            tot_en += itc->getEnergy() ;
                        }
                    }
                }
            }
        }
        for(std::map<int, edm4hep::ConstMCParticle>::iterator it = mc_map.begin(); it != mc_map.end(); it ++)
        {      
            edm4hep::MCRecoParticleAssociation association = pMCRecoParticleAssociationCollection->create();
            association.setRec(pReco);
            association.setSim(it->second);
            if(tot_en==0) 
            {
                association.setWeight(0);
                std::cout<<"Found 0 cluster energy"<<std::endl;
            }  
            else if(id_edep_map.find(it->first) != id_edep_map.end()) association.setWeight(id_edep_map[it->first]/tot_en);
            else std::cout<<"Error in creating MCRecoParticleAssociation"<<std::endl;
        }
    }
    return StatusCode::SUCCESS;
}


