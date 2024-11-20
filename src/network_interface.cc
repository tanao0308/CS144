#include <iostream>

#include "arp_message.hh"
#include "exception.hh"
#include "network_interface.hh"

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface( string_view name,
                                    shared_ptr<OutputPort> port,
                                    const EthernetAddress& ethernet_address,
                                    const Address& ip_address )
  : name_( name )
  , port_( notnull( "OutputPort", move( port ) ) )
  , ethernet_address_( ethernet_address )
  , ip_address_( ip_address )
{
  cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address ) << " and IP address "
       << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but
//! may also be another host if directly connected to the same network as the destination) Note: the Address type
//! can be converted to a uint32_t (raw 32-bit IP address) by using the Address::ipv4_numeric() method.
// ARP 协议的定点发送函数，只知道 ip 如何找到子网中的机器
void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  uint32_t dst_ip = next_hop.ipv4_numeric();
  if ( !ip2mac_.count( dst_ip ) || timer_ - last_broadcast_[dst_ip] >= 30000 ) {
    waiting_dgram_[dst_ip].push_back( std::make_pair( dgram, next_hop ) ); // 这个要在广播之前做！！！不然check6会挂
    // 若上次发这个 IP 的请求间隔 >= 5000 ms ，则广播
    if ( !last_broadcast_.count( dst_ip ) || timer_ - last_broadcast_[dst_ip] >= 5000 ) {
      last_broadcast_[dst_ip] = timer_;
      broadcast( dst_ip );
    }
    return;
  }

  EthernetHeader header { .dst = ip2mac_[dst_ip], .src = ethernet_address_, .type = EthernetHeader::TYPE_IPv4 };
  EthernetFrame frame { .header = header, .payload = serialize( dgram ) };
  transmit( frame );
}

//! \param[in] frame the incoming Ethernet frame
void NetworkInterface::recv_frame( const EthernetFrame& frame )
{
  // 首先过滤所有目的地不是自己的报文
  if ( frame.header.dst != ETHERNET_BROADCAST && frame.header.dst != ethernet_address_ ) {
    return;
  }
  if ( frame.header.type == EthernetHeader::TYPE_IPv4 ) {
    // 将 EthernetFrame 复原为 InternetDatagram
    InternetDatagram dgram;
    if ( !parse( dgram, frame.payload ) ) {
      return;
    }
    datagrams_received_.push( dgram );
  } else if ( frame.header.type == EthernetHeader::TYPE_ARP ) {
    ARPMessage arpmsg;
    parse( arpmsg, frame.payload ); // 从底层的帧里解析出 arp 报文
    // 读取并记录对方传来的 ip 和 mac
    ip2mac_[arpmsg.sender_ip_address] = arpmsg.sender_ethernet_address;
    last_broadcast_[arpmsg.sender_ip_address] = timer_;
    // 若是请求报文且匹配 ip 则需要回复 ARP
    if ( arpmsg.opcode == ARPMessage::OPCODE_REQUEST && ip_address_.ipv4_numeric() == arpmsg.target_ip_address ) {
      ARPMessage reply_arpmsg { .opcode = ARPMessage::OPCODE_REPLY,
                                .sender_ethernet_address = ethernet_address_,
                                .sender_ip_address = ip_address_.ipv4_numeric(),
                                .target_ethernet_address = arpmsg.sender_ethernet_address,
                                .target_ip_address = arpmsg.sender_ip_address };
      EthernetHeader header {
        .dst = arpmsg.sender_ethernet_address, .src = ethernet_address_, .type = EthernetHeader::TYPE_ARP };
      EthernetFrame reply_frame { .header = header, .payload = serialize( reply_arpmsg ) };
      transmit( reply_frame );
    }
    // 检测现在是否可以有之前没传的 IPv4 可以传了
    uint32_t dst_ip = arpmsg.sender_ip_address;
    for ( auto pair : waiting_dgram_[dst_ip] ) {
      send_datagram( pair.first, pair.second );
    }
    waiting_dgram_.erase( dst_ip );
  }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  timer_ += ms_since_last_tick;
}

void NetworkInterface::broadcast( uint32_t dst_ip )
{
  EthernetHeader header { .dst = ETHERNET_BROADCAST, .src = ethernet_address_, .type = EthernetHeader::TYPE_ARP };
  ARPMessage arpmsg { .opcode = ARPMessage::OPCODE_REQUEST,
                      .sender_ethernet_address = ethernet_address_,
                      .sender_ip_address = ip_address_.ipv4_numeric(),
                      .target_ethernet_address = 0,
                      .target_ip_address = dst_ip };
  EthernetFrame frame { .header = header, .payload = serialize( arpmsg ) };
  transmit( frame );
}