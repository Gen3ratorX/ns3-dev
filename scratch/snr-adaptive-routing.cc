/*
 * snr-adaptive-routing.cc
 * ------------------------------------------------------------------------
 * NS-3 simulation for: "Design and Simulation of an SNR-Triggered Adaptive
 * Routing Protocol for Wireless Ad Hoc Networks"
 * ------------------------------------------------------------------------
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/internet-module.h"
#include "ns3/aodv-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/netanim-module.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/ipv4-list-routing-helper.h"

#include <map>
#include <cmath>
#include <chrono>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("SnrAdaptiveRouting");

// --------------------------------------------------------------------
// DEBUG INSTRUMENTATION
// --------------------------------------------------------------------
static std::chrono::steady_clock::time_point g_wallStart;

static double
ElapsedWallSeconds ()
{
  return std::chrono::duration<double> (std::chrono::steady_clock::now () - g_wallStart).count ();
}

static void
ProgressTick ()
{
  std::cerr << "[t=" << Simulator::Now ().GetSeconds () << "s sim] "
            << "wall=" << ElapsedWallSeconds () << "s" << std::endl;
  Simulator::Schedule (Seconds (2.0), &ProgressTick);
}

// --------------------------------------------------------------------
// Simulation parameters
// --------------------------------------------------------------------
static double   g_areaSize        = 600.0;    // meters (600x600m maintains mesh connectivity)
static double   g_simTime         = 100.0;    // seconds
static uint32_t g_packetSize      = 512;      // bytes
static double   g_snrThresholdDb  = 8.0;      // adaptive re-route trigger
static double   g_helloInterval   = 1.0;      // seconds, SNR probe interval
static double   g_snrAlpha        = 0.3;      // EWMA weight of the newest SNR sample
static uint32_t g_lowWindows      = 3;        // consecutive low check windows before rerouting

// Maps a neighbor's MAC address to its IP address, built once after address
// assignment. The sniffer trace below only sees MAC headers, so this is how a
// received frame's transmitter is resolved back to an AODV next-hop address.
static std::map<Mac48Address, Ipv4Address> g_macToIp;

// NetAnim handle; null unless --animFile is given.
static AnimationInterface *g_anim = nullptr;

static void
SetNodeColor (uint32_t nodeId, uint8_t r, uint8_t g, uint8_t b)
{
  if (g_anim)
    {
      g_anim->UpdateNodeColor (nodeId, r, g, b);
    }
}

// --------------------------------------------------------------------
// SnrMonitorApp
// --------------------------------------------------------------------
// Tracks SNR per neighbor from overheard frames. When a specific neighbor's
// link degrades below threshold, invalidates only the AODV routes that use
// that neighbor as next hop (via RoutingProtocol::ForceLinkFailure), leaving
// routes through other next hops undisturbed.
class SnrMonitorApp : public Application
{
public:
  static TypeId GetTypeId (void)
  {
    static TypeId tid = TypeId ("SnrMonitorApp")
      .SetParent<Application> ()
      .AddConstructor<SnrMonitorApp> ();
    return tid;
  }

  void Setup (Ptr<aodv::RoutingProtocol> aodv, double thresholdDb)
  {
    m_aodv = aodv;
    m_thresholdDb = thresholdDb;
  }

  void RecordSnr (Ipv4Address from, double snrDb)
  {
    NeighborState &st = m_neighbors[from];
    st.ewmaDb = st.valid ? (g_snrAlpha * snrDb + (1.0 - g_snrAlpha) * st.ewmaDb) : snrDb;
    st.valid = true;
    st.heard = true;
  }

private:
  virtual void StartApplication (void)
  {
    m_event = Simulator::Schedule (Seconds (g_helloInterval), &SnrMonitorApp::CheckRoutes, this);
  }

  virtual void StopApplication (void)
  {
    Simulator::Cancel (m_event);
  }

  void CheckRoutes (void)
  {
    for (auto &entry : m_neighbors)
      {
        NeighborState &st = entry.second;
        if (!st.heard)
          {
            continue; // no fresh sample this window; keep state unchanged
          }
        st.heard = false;

        st.lowCount = (st.ewmaDb < m_thresholdDb) ? st.lowCount + 1 : 0;
        if (m_aodv && st.lowCount >= g_lowWindows)
          {
            std::cerr << "[reroute] t=" << Simulator::Now ().GetSeconds ()
                      << "s next-hop " << entry.first << " smoothed SNR "
                      << st.ewmaDb << "dB below threshold for " << st.lowCount
                      << " windows, invalidating its routes" << std::endl;
            m_aodv->ForceLinkFailure (entry.first);
            st.lowCount = 0;

            uint32_t id = GetNode ()->GetId ();
            SetNodeColor (id, 220, 30, 30);
            Simulator::Schedule (Seconds (0.5), &SetNodeColor, id, 30, 90, 200);
          }
      }

    m_event = Simulator::Schedule (Seconds (g_helloInterval), &SnrMonitorApp::CheckRoutes, this);
  }

  struct NeighborState
  {
    double ewmaDb = 0.0;
    bool valid = false;
    bool heard = false;
    uint32_t lowCount = 0;
  };

  Ptr<aodv::RoutingProtocol> m_aodv;
  double m_thresholdDb = 8.0;
  std::map<Ipv4Address, NeighborState> m_neighbors;
  EventId m_event;
};

// --------------------------------------------------------------------
// Trace sink
// --------------------------------------------------------------------
static std::map<uint32_t, Ptr<SnrMonitorApp>> g_monitorApps;

static void
MonitorSnifferRxTrace (uint32_t nodeId, Ptr<const Packet> packet, uint16_t channelFreqMhz,
                        WifiTxVector txVector, MpduInfo aMpdu, SignalNoiseDbm signalNoise,
                        uint16_t staId)
{
  WifiMacHeader hdr;
  packet->PeekHeader (hdr);

  auto ipIt = g_macToIp.find (hdr.GetAddr2 ());
  if (ipIt == g_macToIp.end ())
    {
      return; // not a frame from a known simulated neighbor (e.g. a control frame)
    }

  auto appIt = g_monitorApps.find (nodeId);
  if (appIt != g_monitorApps.end () && appIt->second)
    {
      double snrDb = signalNoise.signal - signalNoise.noise;
      appIt->second->RecordSnr (ipIt->second, snrDb);
    }
}

int
main (int argc, char *argv[])
{
  g_wallStart = std::chrono::steady_clock::now ();

  uint32_t nNodes = 40;
  std::string protocol = "adaptive"; // "conventional" or "adaptive"

  CommandLine cmd;
  cmd.AddValue ("nNodes", "Number of wireless nodes", nNodes);
  cmd.AddValue ("protocol", "conventional | adaptive", protocol);
  cmd.AddValue ("simTime", "Simulation duration (s)", g_simTime);
  cmd.AddValue ("snrThresholdDb", "SNR re-route threshold (dB)", g_snrThresholdDb);
  cmd.AddValue ("snrAlpha", "EWMA weight of newest SNR sample (0-1]", g_snrAlpha);
  uint32_t aodvQueueLen = 1000;
  cmd.AddValue ("aodvQueueLen", "AODV request queue length (default 64 can livelock, see comment)", aodvQueueLen);
  bool spreadStart = false;
  cmd.AddValue ("spreadStart", "Start nodes at random positions instead of (0,0)", spreadStart);
  std::string animFile = "";
  cmd.AddValue ("animFile", "NetAnim XML output file (empty = disabled)", animFile);
  cmd.AddValue ("lowWindows", "Consecutive low check windows before rerouting", g_lowWindows);
  cmd.Parse (argc, argv);

  bool adaptive = (protocol == "adaptive");

  std::cerr << "[setup] start, wall=" << ElapsedWallSeconds () << "s" << std::endl;

  // ---------------- Nodes ----------------
  NodeContainer nodes;
  nodes.Create (nNodes);
  std::cerr << "[setup] nodes created, wall=" << ElapsedWallSeconds () << "s" << std::endl;

  // ---------------- Wifi PHY / MAC (802.11, ad hoc) ----------------
  WifiHelper wifi;
  wifi.SetStandard (WIFI_STANDARD_80211g);
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                "DataMode", StringValue ("ErpOfdmRate6Mbps"),
                                "ControlMode", StringValue ("ErpOfdmRate6Mbps"));

  YansWifiChannelHelper channel = YansWifiChannelHelper::Default ();
  channel.AddPropagationLoss ("ns3::RangePropagationLossModel",
                              "MaxRange", DoubleValue (300.0));

  YansWifiPhyHelper phy;
  phy.SetChannel (channel.Create ());
  phy.Set ("TxPowerStart", DoubleValue (20.0));
  phy.Set ("TxPowerEnd", DoubleValue (20.0));

  WifiMacHelper mac;
  mac.SetType ("ns3::AdhocWifiMac");

  NetDeviceContainer devices = wifi.Install (phy, mac, nodes);
  std::cerr << "[setup] wifi installed, wall=" << ElapsedWallSeconds () << "s" << std::endl;

  // ---------------- Mobility: Random Waypoint over 600x600 m ----------------
  MobilityHelper mobility;

  Ptr<UniformRandomVariable> xVal = CreateObject<UniformRandomVariable> ();
  xVal->SetAttribute ("Min", DoubleValue (0.0));
  xVal->SetAttribute ("Max", DoubleValue (g_areaSize));

  Ptr<UniformRandomVariable> yVal = CreateObject<UniformRandomVariable> ();
  yVal->SetAttribute ("Min", DoubleValue (0.0));
  yVal->SetAttribute ("Max", DoubleValue (g_areaSize));

  Ptr<RandomRectanglePositionAllocator> posAlloc = CreateObject<RandomRectanglePositionAllocator> ();
  posAlloc->SetAttribute ("X", PointerValue (xVal));
  posAlloc->SetAttribute ("Y", PointerValue (yVal));

  if (spreadStart)
    {
      mobility.SetPositionAllocator (posAlloc);
    }
  mobility.SetMobilityModel ("ns3::RandomWaypointMobilityModel",
                              "Speed", StringValue ("ns3::UniformRandomVariable[Min=1.0|Max=5.0]"),
                              "Pause", StringValue ("ns3::ConstantRandomVariable[Constant=1.0]"),
                              "PositionAllocator", PointerValue (posAlloc));
  mobility.Install (nodes);
  std::cerr << "[setup] mobility installed, wall=" << ElapsedWallSeconds () << "s" << std::endl;

  // ---------------- Internet stack + AODV routing ----------------
  AodvHelper aodv;
  // Enable AODV Hello messages to rapidly purge dead routes
  aodv.Set ("EnableHello", BooleanValue (true));
  // With the ns-3 default (64), a full request queue can livelock at one simulation time:
  // dropping a locally originated packet sends an ICMP unreachable to the node's own address,
  // which is itself deferred into the still-full queue, dropped, and answered with another ICMP.
  aodv.Set ("MaxQueueLen", UintegerValue (aodvQueueLen));

  InternetStackHelper internet;
  internet.SetRoutingHelper (aodv);
  internet.Install (nodes);
  std::cerr << "[setup] internet+aodv installed, wall=" << ElapsedWallSeconds () << "s" << std::endl;

  Ipv4AddressHelper address;
  address.SetBase ("10.0.0.0", "255.255.0.0");
  Ipv4InterfaceContainer interfaces = address.Assign (devices);
  std::cerr << "[setup] addresses assigned, wall=" << ElapsedWallSeconds () << "s" << std::endl;

  for (uint32_t i = 0; i < devices.GetN (); ++i)
    {
      g_macToIp[Mac48Address::ConvertFrom (devices.Get (i)->GetAddress ())] = interfaces.GetAddress (i);
    }

  // ---------------- Adaptive protocol: attach SNR monitors ----------------
  if (adaptive)
    {
      for (uint32_t i = 0; i < nodes.GetN (); ++i)
        {
          Ptr<aodv::RoutingProtocol> aodvRp = nodes.Get (i)->GetObject<aodv::RoutingProtocol> ();

          Ptr<SnrMonitorApp> app = CreateObject<SnrMonitorApp> ();
          app->Setup (aodvRp, g_snrThresholdDb);
          nodes.Get (i)->AddApplication (app);
          app->SetStartTime (Seconds (0.5));
          app->SetStopTime (Seconds (g_simTime));
          g_monitorApps[nodes.Get (i)->GetId ()] = app;

          Ptr<NetDevice> dev = devices.Get (i);
          Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice> (dev);
          if (wifiDev)
            {
              wifiDev->GetPhy ()->TraceConnectWithoutContext (
                "MonitorSnifferRx",
                MakeBoundCallback (&MonitorSnifferRxTrace, nodes.Get (i)->GetId ()));
            }
        }
    }

  // ---------------- CBR traffic: random source/destination pairs ----------------
  uint16_t port = 9;
  ApplicationContainer sinkApps, sourceApps;
  uint32_t nFlows = std::min<uint32_t> (10, nNodes / 2);

  for (uint32_t f = 0; f < nFlows; ++f)
    {
      uint32_t src = f;
      uint32_t dst = (f + nNodes / 2) % nNodes;
      if (src == dst) continue;

      PacketSinkHelper sink ("ns3::UdpSocketFactory",
                              InetSocketAddress (Ipv4Address::GetAny (), port + f));
      sinkApps.Add (sink.Install (nodes.Get (dst)));

      OnOffHelper onoff ("ns3::UdpSocketFactory",
                          InetSocketAddress (interfaces.GetAddress (dst), port + f));
      onoff.SetAttribute ("PacketSize", UintegerValue (g_packetSize));
      onoff.SetAttribute ("DataRate", StringValue ("16kbps"));
      onoff.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
      onoff.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
      sourceApps.Add (onoff.Install (nodes.Get (src)));
    }

  sinkApps.Start (Seconds (1.0));
  sinkApps.Stop (Seconds (g_simTime));
  sourceApps.Start (Seconds (2.0));
  sourceApps.Stop (Seconds (g_simTime - 1.0));
  std::cerr << "[setup] traffic apps installed (" << nFlows << " flows), wall=" << ElapsedWallSeconds () << "s" << std::endl;

  // ---------------- FlowMonitor: throughput / PDR / delay / loss ----------------
  FlowMonitorHelper flowmonHelper;
  Ptr<FlowMonitor> monitor = flowmonHelper.InstallAll ();
  std::cerr << "[setup] flow monitor installed, wall=" << ElapsedWallSeconds () << "s" << std::endl;

  Simulator::Schedule (Seconds (0.0), &ProgressTick);

  // ---------------- NetAnim: blue = node, green = flow source, orange = flow sink,
  // red flash = SNR-triggered reroute on that node ----------------
  AnimationInterface *anim = nullptr;
  if (!animFile.empty ())
    {
      anim = new AnimationInterface (animFile);
      g_anim = anim;
      anim->SetMobilityPollInterval (Seconds (0.5));
      anim->SkipPacketTracing ();
      for (uint32_t i = 0; i < nodes.GetN (); ++i)
        {
          anim->UpdateNodeSize (i, 12.0, 12.0);
          anim->UpdateNodeColor (i, 30, 90, 200);
          anim->UpdateNodeDescription (i, std::to_string (i));
        }
      for (uint32_t f = 0; f < nFlows; ++f)
        {
          uint32_t dst = (f + nNodes / 2) % nNodes;
          anim->UpdateNodeColor (f, 30, 160, 60);
          anim->UpdateNodeColor (dst, 240, 150, 0);
        }
    }

  std::cerr << "[run] starting Simulator::Run(), wall=" << ElapsedWallSeconds () << "s" << std::endl;
  Simulator::Stop (Seconds (g_simTime));
  Simulator::Run ();
  std::cerr << "[run] Simulator::Run() finished, wall=" << ElapsedWallSeconds () << "s" << std::endl;

  monitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier =
      DynamicCast<Ipv4FlowClassifier> (flowmonHelper.GetClassifier ());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();

  uint64_t totalTxPackets = 0, totalRxPackets = 0, totalRxBytes = 0;
  double totalDelaySum = 0.0;

  for (auto const &kv : stats)
    {
      totalTxPackets += kv.second.txPackets;
      totalRxPackets += kv.second.rxPackets;
      totalRxBytes   += kv.second.rxBytes;
      totalDelaySum  += kv.second.delaySum.GetSeconds ();
    }

  double throughputMbps = (totalRxBytes * 8.0) / g_simTime / 1e6;
  double pdrPercent = totalTxPackets > 0 ? (100.0 * totalRxPackets / totalTxPackets) : 0.0;
  double lossPercent = 100.0 - pdrPercent;
  double delayMs = totalRxPackets > 0 ? (1000.0 * totalDelaySum / totalRxPackets) : 0.0;

  std::cout << protocol << "," << nNodes << ","
            << throughputMbps << "," << pdrPercent << ","
            << delayMs << "," << lossPercent << std::endl;

  Simulator::Destroy ();
  g_anim = nullptr;
  delete anim;
  return 0;
}
