#pragma once

#include <list>
#include <memory>
#include <optional>

#include "exception.hh"
#include "network_interface.hh"

struct RouterData
{
  bool has_data = false;
  std::optional<Address> next_hop = std::nullopt;
  size_t interface_num = 0;
  uint32_t son[2] = { 0, 0 };
};

class Trie
{
public:
  Trie() { node.push_back( RouterData {} ); }
  void insert( uint32_t route_prefix,
               uint8_t prefix_length,
               const std::optional<Address> next_hop,
               const size_t interface_num );
  RouterData find( uint32_t ip );

private:
  std::vector<RouterData> node {};
};

// \brief A router that has multiple network interfaces and
// performs longest-prefix-match routing between them.
class Router
{
public:
  // Add an interface to the router
  // \param[in] interface an already-constructed network interface
  // \returns The index of the interface after it has been added to the router
  size_t add_interface( std::shared_ptr<NetworkInterface> interface )
  {
    _interfaces.push_back( notnull( "add_interface", std::move( interface ) ) );
    return _interfaces.size() - 1;
  }

  // Access an interface by index
  std::shared_ptr<NetworkInterface> interface( const size_t N ) { return _interfaces.at( N ); }

  // Add a route (a forwarding rule)
  void add_route( uint32_t route_prefix,
                  uint8_t prefix_length,
                  std::optional<Address> next_hop,
                  size_t interface_num );

  // Route packets between the interfaces
  void route();

private:
  // The router's collection of network interfaces
  std::vector<std::shared_ptr<NetworkInterface>> _interfaces {};

  // 存的是某个 ip 的转发规则
  Trie trie {};

  void use_ethernet( InternetDatagram& );
};
