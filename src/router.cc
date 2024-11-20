#include "router.hh"

#include <iostream>
#include <limits>

using namespace std;

void Trie::insert( uint32_t route_prefix,
                   uint8_t prefix_length,
                   const optional<Address> next_hop,
                   const size_t interface_num )
{
  int p = 0;
  while ( prefix_length ) {
    prefix_length--;
    bool side = route_prefix & ( (uint32_t)1 << 31 ); // 0 左 1 右
    route_prefix <<= 1;
    if ( !node[p].son[side] ) {
      node[p].son[side] = node.size();
      node.push_back( RouterData {} );
    }
    p = node[p].son[side];
  }
  node[p].has_data = true;
  node[p].next_hop = next_hop;
  node[p].interface_num = interface_num;
}
RouterData Trie::find( uint32_t ip )
{
  RouterData result;
  int p = 0;
  while ( true ) {
    if ( node[p].has_data ) {
      result = node[p];
    }
    bool side = ip & ( (uint32_t)1 << 31 ); // 0 左 1 右
    ip <<= 1;
    if ( !node[p].son[side] ) {
      break;
    }
    p = node[p].son[side];
  }
  return result;
}

// route_prefix: The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
// prefix_length: For this route to be applicable, how many high-order (most-significant) bits of
//    the route_prefix will need to match the corresponding bits of the datagram's destination address?
// next_hop: The IP address of the next hop. Will be empty if the network is directly attached to the router (in
//    which case, the next hop address should be the datagram's final destination).
// interface_num: The index of the interface to send the datagram out on.
// 此函数用来配置路由表，而不是进行数据传输
void Router::add_route( const uint32_t route_prefix,
                        const uint8_t prefix_length,
                        const optional<Address> next_hop,
                        const size_t interface_num )
{
  cerr << "DEBUG: adding route " << Address::from_ipv4_numeric( route_prefix ).ip() << "/"
       << static_cast<int>( prefix_length ) << " => " << ( next_hop.has_value() ? next_hop->ip() : "(direct)" )
       << " on interface " << interface_num << "\n";

  trie.insert( route_prefix, prefix_length, next_hop, interface_num );
}

// Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
// 发给路由器 a 端口的数据，需要根据路由器的转发规则（即 端口 a 收到的数据需要从端口 b 发出给 next_hop 或
// 子网内机器）
void Router::route()
{
  for ( size_t port = 0; port < _interfaces.size(); ++port ) {
    std::queue<InternetDatagram>& dgram_recv = interface( port )->datagrams_received();
    while ( !dgram_recv.empty() ) {
      use_ethernet( dgram_recv.front() );
      dgram_recv.pop();
    }
  }
}

void Router::use_ethernet( InternetDatagram& dgram )
{
  if ( dgram.header.ttl <= 1 ) {
    return;
  }
  --dgram.header.ttl;
  dgram.header.compute_checksum(); // 草了，这里要重新计算 checksum

  uint32_t dst_ip = dgram.header.dst;
  // 根据路由表传输数据报
  auto data = trie.find( dst_ip );
  size_t send_port = data.interface_num;
  if ( data.next_hop.has_value() ) {
    interface( send_port )->send_datagram( dgram, data.next_hop.value() );
  } else {
    Address address = Address::from_ipv4_numeric( dst_ip );
    interface( send_port )->send_datagram( dgram, address );
  }
}