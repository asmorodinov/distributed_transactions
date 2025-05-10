#include "enable_ipv4.h"

#include <yt/yt/core/net/address.h>
#include <yt/yt/core/net/config.h>

namespace NMiniYT {

YT_DEFINE_GLOBAL(const NYT::NLogging::TLogger, EnableIPv4Logger, "EnableIPv4");
static constexpr auto& Logger = EnableIPv4Logger;

void EnableIPv4() {
    auto addressResolverConfig = NYT::New<NYT::NNet::TAddressResolverConfig>();
    addressResolverConfig->EnableIPv4 = true;
    addressResolverConfig->EnableIPv6 = false;
    NYT::NNet::TAddressResolver::Get()->Configure(addressResolverConfig);
    YT_LOG_INFO("enabled IPv4");
}

} // namespace NMiniYT
