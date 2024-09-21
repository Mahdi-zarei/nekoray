#include "gRPC.h"

#include <utility>
#include <QStringList>

#include "main/NekoGui.hpp"

#include <QCoreApplication>
#include <QNetworkReply>
#include <QTimer>
#include <QtEndian>
#include <QThread>
#include <QMutex>
#include <QAbstractNetworkCache>

#include <iostream>

namespace QtGrpc {
    const char *GrpcAcceptEncodingHeader = "grpc-accept-encoding";
    const char *AcceptEncodingHeader = "accept-encoding";
    const char *TEHeader = "te";
    const char *GrpcStatusHeader = "grpc-status";
    const char *GrpcStatusMessage = "grpc-message";
    const int GrpcMessageSizeHeaderSize = 5;

    class NoCache : public QAbstractNetworkCache {
    public:
        QNetworkCacheMetaData metaData(const QUrl &url) override {
            return {};
        }
        void updateMetaData(const QNetworkCacheMetaData &metaData) override {
        }
        QIODevice *data(const QUrl &url) override {
            return nullptr;
        }
        bool remove(const QUrl &url) override {
            return false;
        }
        [[nodiscard]] qint64 cacheSize() const override {
            return 0;
        }
        QIODevice *prepare(const QNetworkCacheMetaData &metaData) override {
            return nullptr;
        }
        void insert(QIODevice *device) override {
        }
        void clear() override {
        }
    };

    class Http2GrpcChannelPrivate {
    private:
        QThread *thread;
        QNetworkAccessManager *nm;

        QString url_base;
        QString serviceName;
        QByteArray nekoray_auth;

        // async
        QNetworkReply *post(const QString &method, const QString &service, const QByteArray &args) {
            QUrl callUrl = url_base + "/" + service + "/" + method;
            // qDebug() << "Service call url: " << callUrl;

            QNetworkRequest request(callUrl);
            // request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
            // request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
            request.setAttribute(QNetworkRequest::Http2DirectAttribute, true);
            request.setHeader(QNetworkRequest::ContentTypeHeader, QLatin1String{"application/grpc"});
            request.setRawHeader("Cache-Control", "no-store");
            request.setRawHeader(GrpcAcceptEncodingHeader, QByteArray{"identity,deflate,gzip"});
            request.setRawHeader(AcceptEncodingHeader, QByteArray{"identity,gzip"});
            request.setRawHeader(TEHeader, QByteArray{"trailers"});
            request.setRawHeader("nekoray_auth", nekoray_auth);

            QByteArray msg(GrpcMessageSizeHeaderSize, '\0');
            *reinterpret_cast<int *>(msg.data() + 1) = qToBigEndian((int) args.size());
            msg += args;
            // qDebug() << "SEND: " << msg.size();

            QNetworkReply *networkReply = nm->post(request, msg);
            return networkReply;
        }

        static QByteArray processReply(QNetworkReply *networkReply, QNetworkReply::NetworkError &statusCode) {
            // Check if no network error occured
            if (networkReply->error() != QNetworkReply::NoError) {
                statusCode = networkReply->error();
                return {};
            }

            // Check if server answer with error
            auto errCode = networkReply->rawHeader(GrpcStatusHeader).toInt();
            if (errCode != 0) {
                QStringList errstr;
                errstr << "grpc-status error code:" << Int2String(errCode) << ", error msg:"
                       << QLatin1String(networkReply->rawHeader(GrpcStatusMessage));
                MW_show_log(errstr.join(" "));
                statusCode = QNetworkReply::NetworkError::ProtocolUnknownError;
                return {};
            }
            statusCode = QNetworkReply::NetworkError::NoError;
            return networkReply->readAll().mid(GrpcMessageSizeHeaderSize);
        }

        QNetworkReply::NetworkError call(const QString &method, const QString &service, const QByteArray &args, QByteArray &qByteArray, int timeout_ms) {
            QNetworkReply *networkReply = post(method, service, args);

            QTimer *abortTimer = nullptr;
            if (timeout_ms > 0) {
                abortTimer = new QTimer;
                abortTimer->setSingleShot(true);
                abortTimer->setInterval(timeout_ms);
                QObject::connect(abortTimer, &QTimer::timeout, networkReply, &QNetworkReply::abort);
                abortTimer->start();
            }

            {
                QEventLoop loop;
                QObject::connect(networkReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
                loop.exec();
            }

            if (abortTimer != nullptr) {
                abortTimer->stop();
                abortTimer->deleteLater();
            }

            auto grpcStatus = QNetworkReply::NetworkError::ProtocolUnknownError;
            qByteArray = processReply(networkReply, grpcStatus);
            // qDebug() << __func__ << "RECV: " << qByteArray.toHex() << "grpcStatus" << grpcStatus;
            // qDebug() << networkReply->rawHeaderPairs();

            networkReply->deleteLater();
            return grpcStatus;
        }

    public:
        Http2GrpcChannelPrivate(const QString &url_, const QString &nekoray_auth_, const QString &serviceName_) {
            url_base = "http://" + url_;
            nekoray_auth = nekoray_auth_.toLatin1();
            serviceName = serviceName_;
            //
            thread = new QThread;
            nm = new QNetworkAccessManager();
            nm->setCache(new NoCache);
            nm->moveToThread(thread);
            thread->start();
        }

        ~Http2GrpcChannelPrivate() {
            nm->deleteLater();
            thread->quit();
            thread->wait();
            thread->deleteLater();
        }

        QNetworkReply::NetworkError Call(const QString &methodName,
                                         const google::protobuf::Message &req, google::protobuf::Message *rsp,
                                         int timeout_ms = 0) {
            if (!NekoGui::dataStore->core_running) return QNetworkReply::NetworkError(-1919);

            std::string reqStr;
            req.SerializeToString(&reqStr);
            auto requestArray = QByteArray::fromStdString(reqStr);

            QByteArray responseArray;
            QNetworkReply::NetworkError err;
            QMutex lock;
            lock.lock();

            runOnUiThread(
                [&] {
                    err = call(methodName, serviceName, requestArray, responseArray, timeout_ms);
                    lock.unlock();
                },
                nm);

            lock.lock();
            lock.unlock();
            // qDebug() << "rsp err" << err;
            // qDebug() << "rsp array" << responseArray;

            if (err != QNetworkReply::NetworkError::NoError) {
                return err;
            }
            if (!rsp->ParseFromArray(responseArray.data(), responseArray.size())) {
                return QNetworkReply::NetworkError(-114514);
            }
            return QNetworkReply::NetworkError::NoError;
        }
    };
} // namespace QtGrpc

namespace NekoGui_rpc {

    Client::Client(std::function<void(const QString &)> onError, const QString &target, const QString &token) {
        this->make_grpc_channel = [=]() { return std::make_unique<QtGrpc::Http2GrpcChannelPrivate>(target, token, "libcore.LibcoreService"); };
        this->default_grpc_channel = make_grpc_channel();
        this->onError = std::move(onError);
    }

#define NOT_OK      \
    *rpcOK = false; \
    onError(QString("QNetworkReply::NetworkError code: %1\n").arg(status));

    void Client::Exit() {
        libcore::EmptyReq request;
        libcore::EmptyResp reply;
        default_grpc_channel->Call("Exit", request, &reply, 500);
    }

    QString Client::Start(bool *rpcOK, const libcore::LoadConfigReq &request) {
        libcore::ErrorResp reply;
        auto status = default_grpc_channel->Call("Start", request, &reply);

        if (status == QNetworkReply::NoError) {
            *rpcOK = true;
            return {reply.error().c_str()};
        } else {
            NOT_OK
            return reply.error().c_str();
        }
    }

    QString Client::Stop(bool *rpcOK) {
        libcore::EmptyReq request;
        libcore::ErrorResp reply;
        auto status = default_grpc_channel->Call("Stop", request, &reply);

        if (status == QNetworkReply::NoError) {
            *rpcOK = true;
            return {reply.error().c_str()};
        } else {
            NOT_OK
            return "";
        }
    }

    long long Client::QueryStats(const std::string &tag, const std::string &direct) {
        libcore::QueryStatsReq request;
        request.set_tag(tag);
        request.set_direct(direct);

        libcore::QueryStatsResp reply;
        auto status = default_grpc_channel->Call("QueryStats", request, &reply, 500);

        if (status == QNetworkReply::NoError) {
            return reply.traffic();
        } else {
            return 0;
        }
    }

    libcore::TestResp Client::Test(bool *rpcOK, const libcore::TestReq &request) {
        libcore::TestResp reply;
        auto status = make_grpc_channel()->Call("Test", request, &reply);

        if (status == QNetworkReply::NoError) {
            *rpcOK = true;
            return reply;
        } else {
            NOT_OK
            return reply;
        }
    }

    void Client::StopTests(bool *rpcOK) {
        const libcore::EmptyReq req;
        libcore::EmptyResp resp;

        auto status = make_grpc_channel()->Call("StopTest", req, &resp);

        if (status == QNetworkReply::NoError) {
            *rpcOK = true;
        } else {
            NOT_OK
        }
    }

    libcore::UpdateResp Client::Update(bool *rpcOK, const libcore::UpdateReq &request) {
        libcore::UpdateResp reply;
        auto status = default_grpc_channel->Call("Update", request, &reply);

        if (status == QNetworkReply::NoError) {
            *rpcOK = true;
            return reply;
        } else {
            NOT_OK
            return reply;
        }
    }

    QStringList Client::GetGeoList(bool *rpcOK, GeoRuleSetType mode) {
        switch (mode) {
            case GeoRuleSetType::ip: {
                libcore::EmptyReq req;
                libcore::GetGeoIPListResponse resp;

                auto status = default_grpc_channel->Call("GetGeoIPList", req, &resp);
                if (status == QNetworkReply::NoError) {
                    QStringList res;
                    for (const auto & i : resp.items()) {
                        res.append(QString::fromStdString(i));
                    }
                    *rpcOK = true;
                    return res;
                } else {
                    NOT_OK
                    return {};
                }
            }
            case GeoRuleSetType::site: {
                libcore::EmptyReq req;
                libcore::GetGeoSiteListResponse resp;

                auto status = default_grpc_channel->Call("GetGeoSiteList", req, &resp);
                if (status == QNetworkReply::NoError) {
                    QStringList res;
                    for (const auto & i : resp.items()) {
                        res.append(QString::fromStdString(i));
                    }
                    *rpcOK = true;
                    return res;
                } else {
                    NOT_OK
                    return {};
                }
            }
        }
        return {};
    }

    QString Client::CompileGeoSet(bool *rpcOK, GeoRuleSetType mode, std::string category) {
        switch (mode) {
            case ip: {
                libcore::CompileGeoIPToSrsRequest req;
                libcore::EmptyResp resp;
                req.set_item(category);

                auto status = default_grpc_channel->Call("CompileGeoIPToSrs", req, &resp);
                if (status == QNetworkReply::NoError) {
                    *rpcOK = true;
                    return "";
                } else {
                    NOT_OK
                    return qt_error_string(status);
                }
            }
            case site: {
                libcore::CompileGeoSiteToSrsRequest req;
                libcore::EmptyResp resp;
                req.set_item(category);

                auto status = default_grpc_channel->Call("CompileGeoSiteToSrs", req, &resp);
                if (status == QNetworkReply::NoError) {
                    *rpcOK = true;
                    return "";
                } else {
                    NOT_OK
                    return qt_error_string(status);
                }
            }
        }
    }

    QString Client::SetSystemProxy(bool *rpcOK, bool enable) {
        libcore::SetSystemProxyRequest req;
        libcore::EmptyResp resp;
        req.set_enable(enable);
        req.set_address(QString("127.0.0.1:" + Int2String(NekoGui::dataStore->inbound_socks_port)).toStdString());

        auto status = default_grpc_channel->Call("SetSystemProxy", req, &resp);
        if (status == QNetworkReply::NoError) {
            *rpcOK = true;
            return "";
        } else {
            NOT_OK
            return qt_error_string(status);
        }
    }

    libcore::GetSystemDNSResponse Client::GetSystemDNS(bool *rpcOK) const {
        libcore::EmptyReq req;
        libcore::GetSystemDNSResponse resp;

        auto status = default_grpc_channel->Call("GetSystemDNS", req, &resp);
        if (status == QNetworkReply::NoError) {
            *rpcOK = true;
            return resp;
        } else {
            NOT_OK
            return {};
        }
    }

    QString Client::SetSystemDNS(bool *rpcOK, const QStringList& servers, const bool dhcp, const bool clear) const {
        libcore::SetSystemDNSRequest req;
        libcore::EmptyResp resp;

        for (const auto& server : servers) {
            req.add_servers(server.toStdString());
        }
        req.set_set_dhcp(dhcp);
        req.set_clear(clear);

        auto status = default_grpc_channel->Call("SetSystemDNS", req, &resp);
        if (status == QNetworkReply::NoError) {
            *rpcOK = true;
            return "";
        } else {
            NOT_OK
            return qt_error_string(status);
        }
    }

} // namespace NekoGui_rpc
