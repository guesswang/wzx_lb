#include "switch-node.h"
#include "ns3/core-module.h"
#include "assert.h"
#include "ns3/boolean.h"
#include "ns3/conweave-routing.h"
#include "ns3/double.h"
#include "ns3/flow-id-tag.h"
#include "ns3/int-header.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4.h"
#include "ns3/letflow-routing.h"
#include "ns3/packet.h"
#include "ns3/pause-header.h"
#include "ns3/settings.h"
#include <random>
#include "ns3/uinteger.h"
#include "ppp-header.h"
#include "qbb-net-device.h"

namespace ns3 {

ns3::Time SwitchNode::lastUpdate = ns3::Seconds(0);
std::vector<int> fastestNexthops;
TypeId SwitchNode::GetTypeId(void) {
    static TypeId tid =
        TypeId("ns3::SwitchNode")
            .SetParent<Node>()
            .AddConstructor<SwitchNode>()
            .AddAttribute("EcnEnabled", "Enable ECN marking.", BooleanValue(false),
                          MakeBooleanAccessor(&SwitchNode::m_ecnEnabled), MakeBooleanChecker())
            .AddAttribute("CcMode", "CC mode.", UintegerValue(0),
                          MakeUintegerAccessor(&SwitchNode::m_ccMode),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("AckHighPrio", "Set high priority for ACK/NACK or not", UintegerValue(0),
                          MakeUintegerAccessor(&SwitchNode::m_ackHighPrio),
                          MakeUintegerChecker<uint32_t>());
    return tid;
}

SwitchNode::SwitchNode() {
    m_ecmpSeed = m_id;
    m_isToR = false;
    m_node_type = 1;
    m_isToR = false;
    m_drill_candidate = 2;
    m_mmu = CreateObject<SwitchMmu>();
    // Conga's Callback for switch functions
    m_mmu->m_congaRouting.SetSwitchSendCallback(MakeCallback(&SwitchNode::DoSwitchSend, this));
    m_mmu->m_congaRouting.SetSwitchSendToDevCallback(
        MakeCallback(&SwitchNode::SendToDevContinue, this));
    // ConWeave's Callback for switch functions
    m_mmu->m_conweaveRouting.SetSwitchSendCallback(MakeCallback(&SwitchNode::DoSwitchSend, this));
    m_mmu->m_conweaveRouting.SetSwitchSendToDevCallback(
        MakeCallback(&SwitchNode::SendToDevContinue, this));

    for (uint32_t i = 0; i < pCnt; i++) {
        m_txBytes[i] = 0;
    }
}

/**
 * @brief Load Balancing
 */
uint32_t SwitchNode::DoLbFlowECMP(Ptr<const Packet> p, const CustomHeader &ch,
                                  const std::vector<int> &nexthops) {
    //if(ch.dip != 184581889) {
        //std::cout << "Switching to DoLbBg because ch.dip is " << ch.dip << std::endl;
        //return DoLbBg(p, ch, nexthops);
    //}
    // pick one next hop based on hash
    union {
        uint8_t u8[4 + 4 + 2 + 2];
        uint32_t u32[3];
    } buf;
    buf.u32[0] = ch.sip;
    buf.u32[1] = ch.dip;
    if (ch.l3Prot == 0x6)
        buf.u32[2] = ch.tcp.sport | ((uint32_t)ch.tcp.dport << 16);
    else if (ch.l3Prot == 0x11)  // XXX RDMA traffic on UDP
        buf.u32[2] = ch.udp.sport | ((uint32_t)ch.udp.dport << 16);
    else if (ch.l3Prot == 0xFC || ch.l3Prot == 0xFD)  // ACK or NACK
        buf.u32[2] = ch.ack.sport | ((uint32_t)ch.ack.dport << 16);
    else {
        std::cout << "[ERROR] Sw(" << m_id << ")," << PARSE_FIVE_TUPLE(ch)
                  << "Cannot support other protoocls than TCP/UDP (l3Prot:" << ch.l3Prot << ")"
                  << std::endl;
        assert(false && "Cannot support other protoocls than TCP/UDP");
    }

    uint32_t hashVal = EcmpHash(buf.u8, 12, m_ecmpSeed);
    uint32_t idx;
    if(nexthops.size()> 1){
        //idx = hashVal % nexthops.size();
        idx = hashVal % 3;
        //std::cout <<"seq ecmp "<< ch.udp.seq/1000 << "choose " << nexthops[idx] << std::endl;
    }
    else{
        idx = 0;
    } 
    return nexthops[idx];
}


uint32_t SwitchNode::DoLbBg(Ptr<const Packet> p, const CustomHeader &ch,
                                  const std::vector<int> &nexthops) {
        return nexthops[0];
}

/*-----------------CONGA-----------------*/
uint32_t SwitchNode::DoLbConga(Ptr<Packet> p, CustomHeader &ch, const std::vector<int> &nexthops) {
    return DoLbFlowECMP(p, ch, nexthops);  // flow ECMP (dummy)
}

/*-----------------Letflow-----------------*/
uint32_t SwitchNode::DoLbLetflow(Ptr<Packet> p, CustomHeader &ch,
                                 const std::vector<int> &nexthops) {
    if (m_isToR && nexthops.size() == 1) {
        if (m_isToR_hostIP.find(ch.sip) != m_isToR_hostIP.end() &&
            m_isToR_hostIP.find(ch.dip) != m_isToR_hostIP.end()) {
            return nexthops[0];  // intra-pod traffic
        }
    }

    /* ONLY called for inter-Pod traffic */
    uint32_t outPort = m_mmu->m_letflowRouting.RouteInput(p, ch);
    if (outPort == LETFLOW_NULL) {
        assert(nexthops.size() == 1);  // Receiver's TOR has only one interface to receiver-server
        outPort = nexthops[0];         // has only one option
    }
    assert(std::find(nexthops.begin(), nexthops.end(), outPort) !=
           nexthops.end());  // Result of Letflow cannot be found in nexthops
    return outPort;
}

uint32_t SwitchNode::DoLbWzx(Ptr<const Packet> p, const CustomHeader &ch,
                             const std::vector<int> &nexthops) {
    //if(ch.dip != 184581889) return DoLbBg(p, ch, nexthops);
    uint32_t seq = ch.udp.seq / 1000;  
    uint32_t selectedPort;
    const Time updateInterval = MicroSeconds(10); //10us

    // std::cout << "++++++++++++++" << std::endl;
    // std::cout << lastUpdate << std::endl;
    if(nexthops.size() > 1){
        if(lastUpdate == Seconds(0)){
            UpdateFastestPaths(nexthops); 
            lastUpdate = Simulator::Now();
        }
        //std::cout << Simulator::Now() <<" - " << lastUpdate << " = " << Simulator::Now() - lastUpdate << std::endl;
        if (Simulator::Now() - lastUpdate > updateInterval) {
            lastUpdate = Simulator::Now();  
            //std::cout << "update---------------" << std::endl;
            UpdateFastestPaths(nexthops); 
        }
        //std::cout << "seq " << seq << " % " << fastestNexthops.size() << std::endl;
        selectedPort = fastestNexthops[seq % fastestNexthops.size()];
        //std::cout << "seq " << seq << " choose port " <<  selectedPort << std::endl;
    }
    else {
        selectedPort = nexthops[0];
        //std::cout << "empty seq " << seq << " choose port " <<  selectedPort << std::endl;
    }
    return selectedPort;
}

// uint32_t SwitchNode::DoLbWzx(Ptr<const Packet> p, const CustomHeader &ch,
//                              const std::vector<int> &nexthops) {
//     if(ch.dip != 184581889) return DoLbBg(p, ch, nexthops);
//     uint32_t seq = ch.udp.seq / 1000;
//     uint32_t selectedPort;

//     if (nexthops.size() > 1) {
//         if (m_seqPortMap.find(seq) != m_seqPortMap.end()) {
//             selectedPort = m_seqPortMap[seq];
//         } else {
//             // 遍历所有端口，找到负载最小的端口
//             uint32_t leastLoadInterface = nexthops[0];
//             uint32_t leastLoad = CalculateInterfaceLoad(nexthops[0]);

//             for (size_t i = 1; i < nexthops.size()-1; ++i) {
//                 uint32_t currentLoad = CalculateInterfaceLoad(nexthops[i]);
//                 if (currentLoad < leastLoad) {
//                     leastLoad = currentLoad;
//                     leastLoadInterface = nexthops[i];
//                 }
//             }

//             selectedPort = leastLoadInterface;
//             m_seqPortMap[seq] = selectedPort;
//         }
//     } else {
//         selectedPort = nexthops[0];
//     }

//     return selectedPort;
//     }

    

void SwitchNode::UpdateFastestPaths(const std::vector<int>& nexthops) {
    std::vector<std::pair<int, uint32_t>> loadInfo;
    //std::cout << "fast!!!!!!!!!!!! " <<std::endl;
    for(size_t i = 1; i < nexthops.size(); ++i){
        uint32_t load = CalculateInterfaceLoad(nexthops[i]);
        loadInfo.push_back({nexthops[i], load});  
    }

    std::sort(loadInfo.begin(), loadInfo.end(), 
              [](const std::pair<int, uint32_t>& a, const std::pair<int, uint32_t>& b) {
                  return a.second < b.second; 
              });
    fastestNexthops.clear();  

    for (size_t i = 0; i < 3; ++i) {
        fastestNexthops.push_back(loadInfo[i].first);
        //std::cout << "path " << i << " port " << fastestNexthops[i] << std::endl;
    }
}

uint32_t SwitchNode::DoLbHalflife(Ptr<const Packet> p, const CustomHeader &ch,
                                  const std::vector<int> &nexthops) {
    if (ch.dip != 184581889) return DoLbBg(p, ch, nexthops);
 
    const Time initialFlowletTimeout = Time(MicroSeconds(180));  // 初始 FTV = 150μs
    const Time decrementFTV = Time(NanoSeconds(80));  // 每次转发减少 130ns

    uint32_t seq = ch.udp.seq / 1000;  
    uint32_t selectedPort;
    uint64_t qpkey = ((uint64_t)ch.sip << 32) | ((uint64_t)ch.udp.sport << 16) | (uint64_t)ch.dip | (uint64_t)ch.udp.dport;

    static std::map<uint64_t, Time> flowletTimeoutMap;
    static std::map<uint64_t, Time> flowletLastActiveMap;
    static std::map<uint64_t, uint32_t> previousBestInterfaceMap;

    bool flowletExists = (flowletTimeoutMap.find(qpkey) != flowletTimeoutMap.end());

    if (nexthops.size() > 1) {
        uint32_t leastLoadInterface;

        if (previousBestInterfaceMap.find(qpkey) != previousBestInterfaceMap.end()) {
            leastLoadInterface = previousBestInterfaceMap[qpkey];
        } else {
            leastLoadInterface = nexthops[0]; // 初始化为第一个可用的路径
        }

        uint32_t randPath1 = *(std::next(nexthops.begin(), rand() % 3));
        uint32_t randPath2 = *(std::next(nexthops.begin(), rand() % 3));

        uint32_t randLoad1 = CalculateInterfaceLoad(randPath1);
        uint32_t randLoad2 = CalculateInterfaceLoad(randPath2);
        uint32_t lastload = CalculateInterfaceLoad(leastLoadInterface);

        if (randLoad1 < lastload) {
            leastLoadInterface = randPath1;
        }
        if (randLoad2 < randLoad1 && randLoad2 < lastload) {
            leastLoadInterface = randPath2;
        }

        if (flowletExists) {
            Time now = Simulator::Now();
            if (now - flowletLastActiveMap[qpkey] < flowletTimeoutMap[qpkey]) {
                selectedPort = previousBestInterfaceMap[qpkey];
                flowletTimeoutMap[qpkey] -= decrementFTV;
            } else {
                flowletTimeoutMap[qpkey] = initialFlowletTimeout;
                selectedPort = leastLoadInterface;
                previousBestInterfaceMap[qpkey] = leastLoadInterface;
            }
            flowletLastActiveMap[qpkey] = now;
        } else {
            flowletTimeoutMap[qpkey] = initialFlowletTimeout;
            flowletLastActiveMap[qpkey] = Simulator::Now();
            selectedPort = leastLoadInterface;
            previousBestInterfaceMap[qpkey] = leastLoadInterface;
        }
    } else {
        selectedPort = nexthops[0];
    }

    return selectedPort;
}




/*-----------------DRILL-----------------*/
uint32_t SwitchNode::CalculateInterfaceLoad(uint32_t interface) {
    Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[interface]);
    NS_ASSERT_MSG(!!device && !!device->GetQueue(),
                  "Error of getting a egress queue for calculating interface load");
    return device->GetQueue()->GetNBytesTotal();  // also used in HPCC
}

uint32_t SwitchNode::DoLbDrill(Ptr<const Packet> p, const CustomHeader &ch,
                               const std::vector<int> &nexthops) {
    //if(ch.sip == 184573953) return nexthops[0];
    // find the Egress (output) link with the smallest local Egress Queue length
    uint32_t leastLoadInterface = 0;
    uint32_t leastLoad = std::numeric_limits<uint32_t>::max();
    auto rand_nexthops = nexthops;
    std::random_shuffle(rand_nexthops.begin(), rand_nexthops.end());

    std::map<uint32_t, uint32_t>::iterator itr = m_previousBestInterfaceMap.find(ch.dip);
    if (itr != m_previousBestInterfaceMap.end()) {
        leastLoadInterface = itr->second;
        leastLoad = CalculateInterfaceLoad(itr->second);
    }

    // uint32_t sampleNum =
    //     m_drill_candidate < rand_nexthops.size() ? m_drill_candidate : rand_nexthops.size();
    if(nexthops.size() > 1){
        for (uint32_t samplePort = 0; samplePort < 2; samplePort++) {
            uint32_t sampleLoad = CalculateInterfaceLoad(rand_nexthops[samplePort]);
            if (sampleLoad < leastLoad) {
                leastLoad = sampleLoad;
                leastLoadInterface = rand_nexthops[samplePort];
            }
        }
        m_previousBestInterfaceMap[ch.dip] = leastLoadInterface;
        std::cout << ch.sip<<" seq drill "<< ch.udp.seq/1000 << "choose " << leastLoadInterface << std::endl;
    }else{
        leastLoadInterface = nexthops[0];
    }   

    return leastLoadInterface;
    // uint32_t selectedPort = nexthops[0];
    // uint32_t leastload = CalculateInterfaceLoad(nexthops[0]);

    // if(nexthops.size()> 1){
    //     auto itr = m_previousBestInterfaceMap.find(ch.dip);
    //     if (itr != m_previousBestInterfaceMap.end()) {
    //         uint32_t lastport = itr->second;
    //         uint32_t lastload = CalculateInterfaceLoad(itr->second);
    //         leastload = lastload;
    //         selectedPort = lastport;
    //     }
    // //     //std::cout <<"before"<<  leastLoadInterface << leastLoad << std::endl;
    // //     //auto rand_nexthops = nexthops;
    //     //std::random_shuffle(rand_nexthops.begin(), rand_nexthops.end());
    //     uint32_t idx1 = nexthops[rand()% nexthops.size()];
    //     uint32_t idx2 = nexthops[rand()% nexthops.size()];
    //     // uint32_t idx1 = nexthops[ch.udp.seq/1000 % nexthops.size()];
    //     // uint32_t idx2 = nexthops[(ch.udp.seq/1000 +1)% nexthops.size()];
    //     uint32_t load1 = CalculateInterfaceLoad(idx1);
    //     uint32_t load2 = CalculateInterfaceLoad(idx2);
    //     //std::cout << "idx1 "<< idx1 << " load1 "<< load1 << "idx2 "<< idx2 << " load2 "<<load2 << " leastload "<<leastload<<std::endl;
    //     if (load1 < leastload) {
    //         leastload = load1;
    //         selectedPort = idx1;
    //     }
    //     if (load2 < leastload) {
    //         leastload = load2;
    //         selectedPort = idx2;
    //     }
    //     //std::cout <<"after "<< leastLoadInterface << leastLoad << std::endl;
    //    // selectedPort = nexthops[rand() % nexthops.size()];
    //     m_previousBestInterfaceMap[ch.dip] = selectedPort;
    //     //std::cout <<"seq drill "<< ch.udp.seq/1000 << "choose " << selectedPort << std::endl;
    // }
    // return selectedPort;
}

/*------------------ConWeave Dummy ----------------*/
uint32_t SwitchNode::DoLbConWeave(Ptr<const Packet> p, const CustomHeader &ch,
                                  const std::vector<int> &nexthops) {
    return DoLbFlowECMP(p, ch, nexthops);  // flow ECMP (dummy)
}
/*----------------------------------*/

void SwitchNode::CheckAndSendPfc(uint32_t inDev, uint32_t qIndex) {
    Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[inDev]);
    bool pClasses[qCnt] = {0};
    m_mmu->GetPauseClasses(inDev, qIndex, pClasses);
    for (int j = 0; j < qCnt; j++) {
        if (pClasses[j]) {
            uint32_t paused_time = device->SendPfc(j, 0);
            m_mmu->SetPause(inDev, j, paused_time);
            m_mmu->m_pause_remote[inDev][j] = true;
            /** PAUSE SEND COUNT ++ */
        }
    }

    for (int j = 0; j < qCnt; j++) {
        if (!m_mmu->m_pause_remote[inDev][j]) continue;

        if (m_mmu->GetResumeClasses(inDev, j)) {
            device->SendPfc(j, 1);
            m_mmu->SetResume(inDev, j);
            m_mmu->m_pause_remote[inDev][j] = false;
        }
    }
}
void SwitchNode::CheckAndSendResume(uint32_t inDev, uint32_t qIndex) {
    Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[inDev]);
    if (m_mmu->GetResumeClasses(inDev, qIndex)) {
        device->SendPfc(qIndex, 1);
        m_mmu->SetResume(inDev, qIndex);
    }
}

/********************************************
 *              MAIN LOGICS                 *
 *******************************************/

// This function can only be called in switch mode
bool SwitchNode::SwitchReceiveFromDevice(Ptr<NetDevice> device, Ptr<Packet> packet,
                                         CustomHeader &ch) {
    SendToDev(packet, ch);
    return true;
}

void SwitchNode::SendToDev(Ptr<Packet> p, CustomHeader &ch) {
    /** HIJACK: hijack the packet and run DoSwitchSend internally for Conga and ConWeave.
     * Note that DoLbConWeave() and DoLbConga() are flow-ECMP function for control packets
     * or intra-ToR traffic.
     */

    // Conga
    if (Settings::lb_mode == 3) {
        m_mmu->m_congaRouting.RouteInput(p, ch);
        return;
    }

    // ConWeave
    if (Settings::lb_mode == 9) {
        m_mmu->m_conweaveRouting.RouteInput(p, ch);
        return;
    }



    // Others
    SendToDevContinue(p, ch);
}

void SwitchNode::SendToDevContinue(Ptr<Packet> p, CustomHeader &ch) {
    int idx = GetOutDev(p, ch);
    if (idx >= 0) {
        NS_ASSERT_MSG(m_devices[idx]->IsLinkUp(),
                      "The routing table look up should return link that is up");

        // determine the qIndex
        uint32_t qIndex;
        if (ch.l3Prot == 0xFF || ch.l3Prot == 0xFE ||
            (m_ackHighPrio &&
             (ch.l3Prot == 0xFD ||
              ch.l3Prot == 0xFC))) {  // QCN or PFC or ACK/NACK, go highest priority
            qIndex = 0;               // high priority
        } else {
            qIndex = (ch.l3Prot == 0x06 ? 1 : ch.udp.pg);  // if TCP, put to queue 1. Otherwise, it
                                                           // would be 3 (refer to trafficgen)
        }

        DoSwitchSend(p, ch, idx, qIndex);  // m_devices[idx]->SwitchSend(qIndex, p, ch);
        return;
    }
    std::cout << "WARNING - Drop occurs in SendToDevContinue()" << std::endl;
    return;  // Drop otherwise
}

int SwitchNode::GetOutDev(Ptr<Packet> p, CustomHeader &ch) {
    // look up entries
    auto entry = m_rtTable.find(ch.dip);

    // no matching entry
    if (entry == m_rtTable.end()) {
        std::cout << "[ERROR] Sw(" << m_id << ")," << PARSE_FIVE_TUPLE(ch)
                  << "No matching entry, so drop this packet at SwitchNode (l3Prot:" << ch.l3Prot
                  << ")" << std::endl;
        assert(false);
    }

    // entry found
    const auto &nexthops = entry->second;
    bool control_pkt =
        (ch.l3Prot == 0xFF || ch.l3Prot == 0xFE || ch.l3Prot == 0xFD || ch.l3Prot == 0xFC);

    if (Settings::lb_mode == 0 || control_pkt) {  // control packet (ACK, NACK, PFC, QCN)
        return DoLbFlowECMP(p, ch, nexthops);     // ECMP routing path decision (4-tuple)
    }

    switch (Settings::lb_mode) {
        case 2:
            return DoLbDrill(p, ch, nexthops);
        case 3:
            return DoLbConga(p, ch, nexthops); /** DUMMY: Do ECMP */
        case 6:
            return DoLbLetflow(p, ch, nexthops);
        case 9:
            return DoLbConWeave(p, ch, nexthops); /** DUMMY: Do ECMP */
        case 7:
	    return DoLbWzx(p, ch, nexthops);
        case 1:
	    return DoLbHalflife(p, ch, nexthops);
        default:
            std::cout << "Unknown lb_mode(" << Settings::lb_mode << ")" << std::endl;
            assert(false);
    }
}

/*
 * The (possible) callback point when conweave dequeues packets from buffer
 */
void SwitchNode::DoSwitchSend(Ptr<Packet> p, CustomHeader &ch, uint32_t outDev, uint32_t qIndex) {
    // admission control
    FlowIdTag t;
    p->PeekPacketTag(t);
    uint32_t inDev = t.GetFlowId();

    /** NOTE:
     * ConWeave control packets have the high priority as ACK/NACK/PFC/etc with qIndex = 0.
     */
    if (inDev == Settings::CONWEAVE_CTRL_DUMMY_INDEV) { // sanity check
        // ConWeave reply is on ACK protocol with high priority, so qIndex should be 0
        assert(qIndex == 0 && m_ackHighPrio == 1 && "ConWeave's reply packet follows ACK, so its qIndex should be 0");
    }

    if (qIndex != 0) {  // not highest priority
        if (m_mmu->CheckEgressAdmission(outDev, qIndex,
                                        p->GetSize())) {  // Egress Admission control
            if (m_mmu->CheckIngressAdmission(inDev, qIndex,
                                             p->GetSize())) {  // Ingress Admission control
                m_mmu->UpdateIngressAdmission(inDev, qIndex, p->GetSize());
                m_mmu->UpdateEgressAdmission(outDev, qIndex, p->GetSize());
            } else { /** DROP: At Ingress */
#if (0)
                // /** NOTE: logging dropped pkts */
                // std::cout << "LostPkt ingress - Sw(" << m_id << ")," << PARSE_FIVE_TUPLE(ch)
                //           << "L3Prot:" << ch.l3Prot
                //           << ",Size:" << p->GetSize()
                //           << ",At " << Simulator::Now() << std::endl;
#endif
                Settings::dropped_pkt_sw_ingress++;
                return;  // drop
            }
        } else { /** DROP: At Egress */
#if (0)
            // /** NOTE: logging dropped pkts */
            // std::cout << "LostPkt egress - Sw(" << m_id << ")," << PARSE_FIVE_TUPLE(ch)
            //           << "L3Prot:" << ch.l3Prot << ",Size:" << p->GetSize() << ",At "
            //           << Simulator::Now() << std::endl;
#endif
            Settings::dropped_pkt_sw_egress++;
            return;  // drop
        }

        CheckAndSendPfc(inDev, qIndex);
    }

    m_devices[outDev]->SwitchSend(qIndex, p, ch);
}

void SwitchNode::SwitchNotifyDequeue(uint32_t ifIndex, uint32_t qIndex, Ptr<Packet> p) {
    FlowIdTag t;
    p->PeekPacketTag(t);
    if (qIndex != 0) {
        uint32_t inDev = t.GetFlowId();
        if (inDev != Settings::CONWEAVE_CTRL_DUMMY_INDEV) {
            // NOTE: ConWeave's probe/reply does not need to pass inDev interface,
            // so skip for conweave's queued packets
            m_mmu->RemoveFromIngressAdmission(inDev, qIndex, p->GetSize());
        }
        m_mmu->RemoveFromEgressAdmission(ifIndex, qIndex, p->GetSize());
        if (m_ecnEnabled) {
            bool egressCongested = m_mmu->ShouldSendCN(ifIndex, qIndex);
            if (egressCongested) {
                PppHeader ppp;
                Ipv4Header h;
                p->RemoveHeader(ppp);
                p->RemoveHeader(h);
                h.SetEcn((Ipv4Header::EcnType)0x03);
                p->AddHeader(h);
                p->AddHeader(ppp);
            }
        }
        // NOTE: ConWeave's probe/reply does not need to pass inDev interface
        if (inDev != Settings::CONWEAVE_CTRL_DUMMY_INDEV) {
            CheckAndSendResume(inDev, qIndex);
        }
    }

    // HPCC's INT
    if (1) {
        uint8_t *buf = p->GetBuffer();
        if (buf[PppHeader::GetStaticSize() + 9] == 0x11) {  // udp packet
            IntHeader *ih = (IntHeader *)&buf[PppHeader::GetStaticSize() + 20 + 8 +
                                              6];  // ppp, ip, udp, SeqTs, INT
            Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(m_devices[ifIndex]);
            if (m_ccMode == 3) {  // HPCC
                ih->PushHop(Simulator::Now().GetTimeStep(), m_txBytes[ifIndex],
                            dev->GetQueue()->GetNBytesTotal(), dev->GetDataRate().GetBitRate());
            }
        }
    }
    m_txBytes[ifIndex] += p->GetSize();
}

uint32_t SwitchNode::EcmpHash(const uint8_t *key, size_t len, uint32_t seed) {
    uint32_t h = seed;
    if (len > 3) {
        const uint32_t *key_x4 = (const uint32_t *)key;
        size_t i = len >> 2;
        do {
            uint32_t k = *key_x4++;
            k *= 0xcc9e2d51;
            k = (k << 15) | (k >> 17);
            k *= 0x1b873593;
            h ^= k;
            h = (h << 13) | (h >> 19);
            h += (h << 2) + 0xe6546b64;
        } while (--i);
        key = (const uint8_t *)key_x4;
    }
    if (len & 3) {
        size_t i = len & 3;
        uint32_t k = 0;
        key = &key[i - 1];
        do {
            k <<= 8;
            k |= *key--;
        } while (--i);
        k *= 0xcc9e2d51;
        k = (k << 15) | (k >> 17);
        k *= 0x1b873593;
        h ^= k;
    }
    h ^= len;
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

void SwitchNode::SetEcmpSeed(uint32_t seed) { m_ecmpSeed = seed; }

void SwitchNode::AddTableEntry(Ipv4Address &dstAddr, uint32_t intf_idx) {
    uint32_t dip = dstAddr.Get();
    m_rtTable[dip].push_back(intf_idx);
}

void SwitchNode::ClearTable() { m_rtTable.clear(); }

uint64_t SwitchNode::GetTxBytesOutDev(uint32_t outdev) {
    assert(outdev < pCnt);
    return m_txBytes[outdev];
}

} /* namespace ns3 */
