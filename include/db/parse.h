#pragma once
#include <memory>

#include "include/proxy/base.h"
#include "include/proxy/socks.h"

namespace Database
{
    std::shared_ptr<Proxy::BaseProxy> ParseFromJson(json &j)
    {
        if (!j.contains("type")) return nullptr;
        if (j["type"].get<std::string>() == Constants::ProxyTypes::Socks)
        {
            auto proxy = j.template get<Proxy::SocksProxy>();
            return std::make_shared<Proxy::SocksProxy>(proxy);
        }
    }

    json MarshallToJson(const std::shared_ptr<Proxy::BaseProxy>& proxy)
    {
        json j;
        if (proxy->type == Constants::ProxyTypes::Socks)
        {
            const auto socksProxy = std::dynamic_pointer_cast<Proxy::SocksProxy>(proxy).get();
            Proxy::to_json(j, *socksProxy);
            return j;
        }
    }
}
