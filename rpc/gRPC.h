#pragma once

#ifndef NKR_NO_GRPC

#include "go/grpc_server/gen/libcore.pb.h"
#include <QString>

namespace QtGrpc {
    class Http2GrpcChannelPrivate;
}

namespace NekoGui_rpc {
    enum GeoRuleSetType {ip, site};

    class Client {
    public:
        explicit Client(std::function<void(const QString &)> onError, const QString &target, const QString &token);

        void Exit();

        // QString returns is error string

        QString Start(bool *rpcOK, const libcore::LoadConfigReq &request);

        QString Stop(bool *rpcOK);

        long long QueryStats(const std::string &tag, const std::string &direct);

        libcore::TestResp Test(bool *rpcOK, const libcore::TestReq &request);

        void StopTests(bool *rpcOK);

        libcore::UpdateResp Update(bool *rpcOK, const libcore::UpdateReq &request);

        QStringList GetGeoList(bool *rpcOK, GeoRuleSetType mode);

        QString CompileGeoSet(bool *rpcOK, GeoRuleSetType mode, std::string category);

        QString SetSystemProxy(bool *rpcOK, bool enable);

        libcore::GetSystemDNSResponse GetSystemDNS(bool *rpcOK) const;

        QString SetSystemDNS(bool *rpcOK, const QStringList& servers, bool dhcp, bool clear) const;

    private:
        std::function<std::unique_ptr<QtGrpc::Http2GrpcChannelPrivate>()> make_grpc_channel;
        std::unique_ptr<QtGrpc::Http2GrpcChannelPrivate> default_grpc_channel;
        std::function<void(const QString &)> onError;
    };

    inline Client *defaultClient;
} // namespace NekoGui_rpc
#endif
