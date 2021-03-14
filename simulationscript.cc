#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/lte-module.h"
#include "ns3/config-store.h"
#include <ns3/buildings-helper.h>
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-module.h"
#include "ns3/flow-monitor.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"

using namespace ns3;

int main(int argc, char *argv[])
{
    CommandLine cmd(__FILE__);
    cmd.Parse(argc, argv);
    Time simTime = MilliSeconds(5000);

    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
    Ptr<EpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
    lteHelper->SetEpcHelper(epcHelper);
    Ptr<Node> pgw = epcHelper->GetPgwNode(); // Combined pgw and sgw node

    // Console log, bruk "> Loggingfile.out 2&>1" på slutten
    LogComponentEnable("LteEnbPhy", LOG_LEVEL_ALL);
    LogComponentEnable("LteEnbRrc", LOG_LEVEL_ALL);

    Config::SetDefault("ns3::LteUePhy::TxPower", DoubleValue(25));
    Config::SetDefault("ns3::LteEnbPhy::TxPower", DoubleValue(45));
    Config::SetDefault("ns3::LteEnbPhy::NoiseFigure", DoubleValue(7));
    Config::SetDefault("ns3::LteUePhy::NoiseFigure", DoubleValue(3));

    lteHelper->SetEnbDeviceAttribute("DlEarfcn", UintegerValue(100));
    lteHelper->SetEnbDeviceAttribute("UlEarfcn", UintegerValue(18100));
    lteHelper->SetEnbDeviceAttribute("UlBandwidth", UintegerValue(25));
    lteHelper->SetEnbDeviceAttribute("DlBandwidth", UintegerValue(25));

    ConfigStore inputConfig;
    inputConfig.ConfigureDefaults();
    cmd.Parse(argc, argv);

    // Create a single remotehost
    NodeContainer remoteHostContainer;
    remoteHostContainer.Create(1);
    Ptr<Node> remoteHost = remoteHostContainer.Get(0);
    InternetStackHelper internet;
    internet.Install(remoteHostContainer);

    // Create the internet
    PointToPointHelper p2ph;
    p2ph.SetDeviceAttribute("DataRate", DataRateValue(DataRate("100Gb/s")));
    p2ph.SetDeviceAttribute("Mtu", UintegerValue(1500));
    p2ph.SetChannelAttribute("Delay", TimeValue(MilliSeconds(10)));
    NetDeviceContainer internetDevices = p2ph.Install(pgw, remoteHost);
    Ipv4AddressHelper ipv4h;
    ipv4h.SetBase("100.0.0.0", "255.0.0.0"); // Ip address of remote host
    Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign(internetDevices);
    Ipv4Address remoteHostAddr = internetIpIfaces.GetAddress(1);
    Ipv4StaticRoutingHelper ipv4RoutingHelper;
    Ptr<Ipv4StaticRouting> remoteHostStaticRouting = ipv4RoutingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
    remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

    NodeContainer enbNodes;
    NodeContainer ueNodes;
    enbNodes.Create(1);
    ueNodes.Create(2);

    // Positioning and mobility
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel"); // Position at (0,0,0)
    mobility.Install(enbNodes);
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(50.0, 0.0, 0.0));
    positionAlloc->Add(Vector(0.0, 70.0, 0.0));
    mobility.SetPositionAllocator(positionAlloc);
    mobility.Install(ueNodes);

    NetDeviceContainer enbDevices;
    NetDeviceContainer ueDevices;
    enbDevices = lteHelper->InstallEnbDevice(enbNodes);
    ueDevices = lteHelper->InstallUeDevice(ueNodes);

    internet.Install(ueNodes);
    Ipv4InterfaceContainer ueIpIfaces;
    ueIpIfaces = epcHelper->AssignUeIpv4Address(NetDeviceContainer(ueDevices));
    for (uint32_t u = 0; u < ueNodes.GetN(); ++u)
    {
        Ptr<Node> ueNode = ueNodes.Get(u);
        // Set the default gateway for the UE
        Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting(ueNode->GetObject<Ipv4>());
        ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
    }
    // Attach UEs to enB
    lteHelper->Attach(ueDevices, enbDevices.Get(0));

    // // Set EPS bearer
    enum EpsBearer::Qci q = EpsBearer::GBR_GAMING;
    EpsBearer bearer(q);
    Ptr<EpcTft> tft{Create<EpcTft>()};
    EpcTft::PacketFilter packetFilter;
    packetFilter.localPortStart = 2100;
    packetFilter.localPortEnd = 2100;
    EpcTft::PacketFilter packetFilter2;
    packetFilter2.localPortStart = 1200;
    packetFilter2.localPortEnd = 1200;
    tft->Add(packetFilter);
    tft->Add(packetFilter2);
    lteHelper->ActivateDedicatedEpsBearer(ueDevices, bearer, tft);

    // Create application streams
    uint16_t dlPort = 1200;
    uint16_t ulPort = 2100;
    uint16_t otherPort = 3000;
    Time interPacketInterval = MilliSeconds(100);
    ApplicationContainer clientApps;
    ApplicationContainer serverApps;
    for (uint32_t u = 0; u < ueNodes.GetN(); ++u)
    {
        // Downlink UDP
        PacketSinkHelper dlPacketSinkHelper("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), dlPort));
        serverApps.Add(dlPacketSinkHelper.Install(ueNodes.Get(u)));
        UdpClientHelper dlClient(ueIpIfaces.GetAddress(u), dlPort);
        dlClient.SetAttribute("Interval", TimeValue(interPacketInterval));
        dlClient.SetAttribute("MaxPackets", UintegerValue(1000000));
        clientApps.Add(dlClient.Install(remoteHost));

        // Downlink TCP
        PacketSinkHelper dlPacketSinkHelper2{"ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), dlPort)};
        serverApps.Add(dlPacketSinkHelper2.Install(ueNodes.Get(u)));
        BulkSendHelper tcpClient{"ns3::TcpSocketFactory", InetSocketAddress(remoteHostAddr, dlPort)};
        tcpClient.SetAttribute("MaxBytes", UintegerValue(100000));
        tcpClient.SetAttribute("SendSize", UintegerValue(10));
        tcpClient.SetAttribute("Remote", AddressValue(InetSocketAddress(ueIpIfaces.GetAddress(u), dlPort)));
        clientApps.Add(tcpClient.Install(remoteHost));

        // Uplink UDP
        ++ulPort;
        PacketSinkHelper ulPacketSinkHelper("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), ulPort));
        serverApps.Add(ulPacketSinkHelper.Install(remoteHost));
        UdpClientHelper ulClient(remoteHostAddr, ulPort);
        ulClient.SetAttribute("Interval", TimeValue(interPacketInterval));
        ulClient.SetAttribute("MaxPackets", UintegerValue(1000000));
        clientApps.Add(ulClient.Install(ueNodes.Get(u)));

        // Uplink TCP
        PacketSinkHelper dlPacketSinkHelper2Ul{"ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), ulPort)};
        serverApps.Add(dlPacketSinkHelper2Ul.Install(ueNodes.Get(u)));
        BulkSendHelper tcpClientUl{"ns3::TcpSocketFactory", InetSocketAddress(remoteHostAddr, ulPort)};
        tcpClientUl.SetAttribute("MaxBytes", UintegerValue(100000));
        tcpClientUl.SetAttribute("SendSize", UintegerValue(10));
        tcpClientUl.SetAttribute("Remote", AddressValue(InetSocketAddress(ueIpIfaces.GetAddress(u), ulPort)));
        clientApps.Add(tcpClientUl.Install(remoteHost));

        // Inter-UE UDP
        ++otherPort;
        PacketSinkHelper packetSinkHelper("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), otherPort));
        serverApps.Add(packetSinkHelper.Install(ueNodes.Get(u)));
        UdpClientHelper client(ueIpIfaces.GetAddress(u), otherPort);
        client.SetAttribute("Interval", TimeValue(interPacketInterval));
        client.SetAttribute("MaxPackets", UintegerValue(1000000));
        clientApps.Add(client.Install(ueNodes.Get((u + 1) % 2))); //Number of node pairs = 2

        // Inter-UE TCP
        PacketSinkHelper dlPacketSinkHelper2Other{"ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), otherPort)};
        serverApps.Add(dlPacketSinkHelper2Other.Install(ueNodes.Get(u)));
        BulkSendHelper tcpClientOther{"ns3::TcpSocketFactory", InetSocketAddress(remoteHostAddr, otherPort)};
        tcpClientOther.SetAttribute("MaxBytes", UintegerValue(100000));
        tcpClientOther.SetAttribute("SendSize", UintegerValue(10));
        tcpClientOther.SetAttribute("Remote", AddressValue(InetSocketAddress(ueIpIfaces.GetAddress(u), otherPort)));
        clientApps.Add(client.Install(ueNodes.Get((u + 1) % 2))); //Number of node pairs = 2
    }

    serverApps.Start(MilliSeconds(500));
    clientApps.Start(MilliSeconds(500));

    p2ph.EnablePcapAll("corr_traceFile");
    lteHelper->EnableTraces();

    AsciiTraceHelper ascii;
    p2ph.EnableAsciiAll(ascii.CreateFileStream("asciiTrace.tr"));

    Ptr<FlowMonitor> flowMonitor;
    FlowMonitorHelper flowHelper;
    flowMonitor = flowHelper.InstallAll();

    Simulator::Stop(simTime);
    Simulator::Run();
    flowMonitor->SerializeToXmlFile("corr_flows.xml", true, true);

    Simulator::Destroy();
    return 0;
}
