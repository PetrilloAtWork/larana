/////////////////////////////////////////////////////////////////////////////
/// Class:       MCParticleHitMatching
/// Module Type: producer
/// File:        MCParticleHitMatching_module.cc
///
/// Author:         Wesley Ketchum and Yun-Tse Tsai
/// E-mail address: wketchum@fnal.gov and yuntse@stanford.edu
///
/// This module uses the larsoft backtracker to match hits to truth-level
/// MCParticles (from LArG4 output). It originated from MCTruthT0Matching
/// module, and follows a similar strategy. All MCParticles matching the hit
/// are associated, with information on the amount of contributing energy and
/// number of electrons stored in assn metadata. 
/// 
/// Input: MCParticles (via Backtracker) and recob::Hit collection
/// Output: recob::Hit/simb::MCParticle assns, with BackTrackerHitMatchingData.
///
/////////////////////////////////////////////////////////////////////////////

// Framework includes
#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "canvas/Persistency/Common/FindManyP.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Principal/Event.h" 
#include "canvas/Persistency/Common/Ptr.h" 
#include "canvas/Persistency/Common/PtrVector.h" 
#include "art/Framework/Principal/Run.h"
#include "art/Framework/Principal/SubRun.h"
#include "art/Framework/Services/Optional/TFileService.h" 
#include "art/Framework/Services/Optional/TFileDirectory.h"

#include "canvas/Utilities/InputTag.h"
#include "fhiclcpp/ParameterSet.h"
#include "messagefacility/MessageLogger/MessageLogger.h"

#include <memory>
#include <iostream>
#include <map>
#include <iterator>

// LArSoft
#include "lardataobj/RecoBase/Hit.h"

#include "lardata/Utilities/AssociationUtil.h"
#include "lardata/DetectorInfoServices/LArPropertiesService.h"
#include "lardata/DetectorInfoServices/DetectorPropertiesService.h"
#include "nusimdata/SimulationBase/MCParticle.h"
#include "nusimdata/SimulationBase/MCTruth.h"
#include "larsim/MCCheater/BackTracker.h"
#include "lardataobj/AnalysisBase/BackTrackerMatchingData.h"

namespace t0 {
  class MCParticleHitMatching;
}

class t0::MCParticleHitMatching : public art::EDProducer {
public:
    explicit MCParticleHitMatching(fhicl::ParameterSet const & p);
    // The destructor generated by the compiler is fine for classes
    // without bare pointers or other resource use.

    // Plugins should not be copied or assigned.
    MCParticleHitMatching(MCParticleHitMatching const &) = delete;
    MCParticleHitMatching(MCParticleHitMatching &&) = delete;
    MCParticleHitMatching & operator = (MCParticleHitMatching const &) = delete;
    MCParticleHitMatching & operator = (MCParticleHitMatching &&) = delete;

    // Required functions.
    void produce(art::Event & e) override;

    // Selected optional functions.
    void beginJob() override;
    void reconfigure(fhicl::ParameterSet const & p) override;

private:
  
    art::InputTag fHitModuleLabel;
    art::InputTag fMCParticleModuleLabel;

    struct TrackIDEinfo {
        float E;
        float NumElectrons;
    };
    std::unordered_map<int, TrackIDEinfo> fTrkIDECollector;

};


t0::MCParticleHitMatching::MCParticleHitMatching(fhicl::ParameterSet const & p)
{
    reconfigure(p);
    produces< art::Assns<recob::Hit , simb::MCParticle, anab::BackTrackerHitMatchingData > > ();
}

void t0::MCParticleHitMatching::reconfigure(fhicl::ParameterSet const & p)
{
    fHitModuleLabel = p.get<art::InputTag>("HitModuleLabel");
    fMCParticleModuleLabel = p.get<art::InputTag>("MCParticleModuleLabel");
}

void t0::MCParticleHitMatching::beginJob()
{
}

void t0::MCParticleHitMatching::produce(art::Event & evt)
{
    if (evt.isRealData()) return;

    // Access art services...
    art::ServiceHandle<cheat::BackTracker> bt;

    auto mcpartHandle = evt.getValidHandle< std::vector<simb::MCParticle> >(fMCParticleModuleLabel);
    std::unique_ptr< art::Assns<recob::Hit, simb::MCParticle, anab::BackTrackerHitMatchingData > > MCPartHitassn( new art::Assns<recob::Hit, simb::MCParticle, anab::BackTrackerHitMatchingData >);
  
    double maxe = -1;
    double tote = 0;
    // int    trkid = -1;
    int    maxtrkid = -1;
    double maxn = -1;
    double totn = 0;
    int maxntrkid = -1;
    anab::BackTrackerHitMatchingData bthmd;
    std::unordered_map<int,int> trkid_lookup; //indexed by geant4trkid, delivers MC particle location
  
    art::Handle< std::vector<recob::Hit> > hitListHandle;
    evt.getByLabel(fHitModuleLabel,hitListHandle);
  
    if(!hitListHandle.isValid()){
        std::cerr << "Hit handle is not valid!" << std::endl;
        return; // violation of convention!
    }
  
    auto const& hitList(*hitListHandle);
    auto const& mcpartList(*mcpartHandle);

    for(size_t i_h=0; i_h<hitList.size(); ++i_h){
        art::Ptr<recob::Hit> hitPtr(hitListHandle, i_h);
        auto trkide_list = bt->HitToTrackID(hitPtr);
    
        maxe = -1; tote = 0; maxtrkid = -1;
        maxn = -1; totn = 0; maxntrkid = -1;
        fTrkIDECollector.clear();
    
        //for(auto const& t : trkide_list){
        for(size_t i_t=0; i_t<trkide_list.size(); ++i_t){
            auto const& t(trkide_list[i_t]);
            fTrkIDECollector[t.trackID].E += t.energy;
            tote += t.energy;
            if(fTrkIDECollector[t.trackID].E>maxe) { maxe = fTrkIDECollector[t.trackID].E; maxtrkid = t.trackID; }
            fTrkIDECollector[t.trackID].NumElectrons += t.numElectrons;
            totn += t.numElectrons;
            if(fTrkIDECollector[t.trackID].NumElectrons > maxn) {
                maxn = fTrkIDECollector[t.trackID].NumElectrons;
                maxntrkid = t.trackID;
            }
      
            //if not found, find mc particle...
            if(trkid_lookup.find(t.trackID)==trkid_lookup.end()){
                size_t i_p=0;
                while(i_p<mcpartList.size()){
                    if(mcpartList[i_p].TrackId() == t.trackID) { trkid_lookup[t.trackID] = (int)i_p; break;}
                    ++i_p;
                }
                if(i_p==mcpartList.size()) trkid_lookup[t.trackID] = -1;
            }
      
        }
        //end loop on TrackIDs
    
    
        //now find the mcparticle and loop back through ...
        for(auto const& t : fTrkIDECollector){
            int mcpart_i = trkid_lookup[t.first];
            if(mcpart_i==-1) continue; //no mcparticle here
            art::Ptr<simb::MCParticle> mcpartPtr(mcpartHandle, mcpart_i);
            bthmd.ideFraction = t.second.E / tote;
            bthmd.isMaxIDE = (t.first==maxtrkid);
            bthmd.ideNFraction = t.second.NumElectrons / totn;
            bthmd.isMaxIDEN = ( t.first == maxntrkid );
            bthmd.energy = t.second.E;
            bthmd.numElectrons = t.second.NumElectrons;
            MCPartHitassn->addSingle(hitPtr, mcpartPtr, bthmd);
        }
    
    }//end loop on hits

    evt.put(std::move(MCPartHitassn));
} // Produce

DEFINE_ART_MODULE(t0::MCParticleHitMatching)
    
