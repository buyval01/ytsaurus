#include "bootstrap.h"

#include "private.h"

#include "config.h"
#include "cypress_transaction_service.h"
#include "dynamic_config_manager.h"
#include "master_connector.h"
#include "object_service.h"
#include "response_keeper.h"
#include "sequoia_service.h"
#include "user_directory.h"
#include "user_directory_synchronizer.h"

#include <yt/yt/server/lib/admin/admin_service.h>

#include <yt/yt/server/lib/cypress_registrar/cypress_registrar.h>

#include <yt/yt/server/lib/misc/address_helpers.h>

#include <yt/yt/ytlib/api/native/client.h>
#include <yt/yt/ytlib/api/native/config.h>
#include <yt/yt/ytlib/api/native/connection.h>
#include <yt/yt/ytlib/api/native/helpers.h>

#include <yt/yt/ytlib/cell_master_client/cell_directory_synchronizer.h>

#include <yt/yt/ytlib/hive/cluster_directory.h>
#include <yt/yt/ytlib/hive/cluster_directory_synchronizer.h>

#include <yt/yt/ytlib/distributed_throttler/distributed_throttler.h>

#include <yt/yt/ytlib/orchid/orchid_service.h>

#include <yt/yt/ytlib/sequoia_client/public.h>
#include <yt/yt/ytlib/sequoia_client/sequoia_reign.h>
#include <yt/yt/ytlib/sequoia_client/table_descriptor.h>

#include <yt/yt/client/logging/dynamic_table_log_writer.h>

#include <yt/yt/library/coredumper/public.h>

#include <yt/yt/library/monitoring/http_integration.h>

#include <yt/yt/library/profiling/solomon/public.h>

#include <yt/yt/library/program/build_attributes.h>
#include <yt/yt/library/program/config.h>
#include <yt/yt/library/program/helpers.h>

#include <yt/yt/library/fusion/service_locator.h>

#include <yt/yt/core/bus/tcp/server.h>

#include <yt/yt/core/concurrency/action_queue.h>
#include <yt/yt/core/concurrency/fair_share_thread_pool.h>

#include <yt/yt/core/http/server.h>

#include <yt/yt/core/net/local_address.h>

#include <yt/yt/core/rpc/caching_channel_factory.h>
#include <yt/yt/core/rpc/server.h>

#include <yt/yt/core/rpc/bus/channel.h>
#include <yt/yt/core/rpc/bus/server.h>

#include <yt/yt/core/ytree/virtual.h>

#include <yt/yt/core/misc/configurable_singleton_def.h>

namespace NYT::NCypressProxy {

using namespace NAdmin;
using namespace NConcurrency;
using namespace NCoreDump;
using namespace NDistributedThrottler;
using namespace NMonitoring;
using namespace NNet;
using namespace NOrchid;
using namespace NSequoiaClient;
using namespace NYTree;
using namespace NFusion;
using namespace NServer;

////////////////////////////////////////////////////////////////////////////////

constinit const auto Logger = CypressProxyLogger;

////////////////////////////////////////////////////////////////////////////////

class TBootstrap
    : public IBootstrap
{
public:
    TBootstrap(
        TCypressProxyBootstrapConfigPtr config,
        INodePtr configNode,
        IServiceLocatorPtr serviceLocator)
        : Config_(std::move(config))
        , ConfigNode_(std::move(configNode))
        , ServiceLocator_(std::move(serviceLocator))
        , ThreadPool_(CreateFairShareThreadPool(TCypressProxyDynamicConfig::DefaultThreadPoolSize, "CypressProxy"))
    {
        if (Config_->AbortOnUnrecognizedOptions) {
            AbortOnUnrecognizedOptions(Logger(), Config_);
        } else {
            WarnForUnrecognizedOptions(Logger(), Config_);
        }
    }

    TFuture<void> Run() override
    {
        return BIND(&TBootstrap::DoRun, MakeStrong(this))
            .AsyncVia(GetControlInvoker())
            .Run();
    }

    const TCypressProxyBootstrapConfigPtr& GetConfig() const override
    {
        return Config_;
    }

    const TDynamicConfigManagerPtr& GetDynamicConfigManager() const override
    {
        return DynamicConfigManager_;
    }

    const TUserDirectoryPtr& GetUserDirectory() const override
    {
        return UserDirectory_;
    }

    const IUserDirectorySynchronizerPtr& GetUserDirectorySynchronizer() const override
    {
        return UserDirectorySynchronizer_;
    }

    const NRpc::IAuthenticatorPtr& GetNativeAuthenticator() const override
    {
        return NativeAuthenticator_;
    }

    const IInvokerPtr& GetControlInvoker() const override
    {
        return ControlQueue_->GetInvoker();
    }

    const NApi::NNative::IConnectionPtr& GetNativeConnection() const override
    {
        return NativeConnection_;
    }

    const NApi::NNative::IClientPtr& GetNativeRootClient() const override
    {
        return NativeRootClient_;
    }

    ISequoiaClientPtr GetSequoiaClient() const override
    {
        return NativeConnection_->GetSequoiaClient();
    }

    NApi::IClientPtr GetRootClient() const override
    {
        return NativeRootClient_;
    }

    const ISequoiaServicePtr& GetSequoiaService() const override
    {
        return SequoiaService_;
    }

    const ISequoiaResponseKeeperPtr& GetResponseKeeper() const override
    {
        return ResponseKeeper_;
    }

    const IMasterConnectorPtr& GetMasterConnector() const override
    {
        return MasterConnector_;
    }

    IInvokerPtr GetInvoker(const NConcurrency::TFairShareThreadPoolTag& tag) const override
    {
        return ThreadPool_->GetInvoker(tag);
    }

    IDistributedThrottlerFactoryPtr CreateDistributedThrottlerFactory(
        TDistributedThrottlerConfigPtr config,
        IInvokerPtr invoker,
        const std::string& groupId,
        NLogging::TLogger logger,
        NProfiling::TProfiler profiler) const override
    {
        auto selfAddress = BuildServiceAddress(GetLocalHostName(), Config_->RpcPort);
        return NDistributedThrottler::CreateDistributedThrottlerFactory(
            std::move(config),
            NativeConnection_->GetChannelFactory(),
            NativeConnection_,
            std::move(invoker),
            NYPath::TYPath(groupId),
            selfAddress,
            RpcServer_,
            std::move(selfAddress),
            std::move(logger),
            NativeAuthenticator_,
            profiler);
    }

private:
    const TCypressProxyBootstrapConfigPtr Config_;
    const INodePtr ConfigNode_;
    const IServiceLocatorPtr ServiceLocator_;

    const IFairShareThreadPoolPtr ThreadPool_;

    const TActionQueuePtr ControlQueue_ = New<TActionQueue>("Control");

    NApi::NNative::IConnectionPtr NativeConnection_;
    NApi::NNative::IClientPtr NativeRootClient_;
    NRpc::IAuthenticatorPtr NativeAuthenticator_;

    ISequoiaServicePtr SequoiaService_;

    ISequoiaResponseKeeperPtr ResponseKeeper_;

    NBus::IBusServerPtr BusServer_;
    NRpc::IServerPtr RpcServer_;
    NHttp::IServerPtr HttpServer_;

    IObjectServicePtr ObjectService_;

    IMapNodePtr OrchidRoot_;
    IMonitoringManagerPtr MonitoringManager_;

    TDynamicConfigManagerPtr DynamicConfigManager_;

    TUserDirectoryPtr UserDirectory_;
    IUserDirectorySynchronizerPtr UserDirectorySynchronizer_;

    IMasterConnectorPtr MasterConnector_;

    void DoRun()
    {
        DoInitialize();
        DoStart();
    }

    void DoInitialize()
    {
        auto sequoiaTableDescriptorInitialization = ITableDescriptor::Initialize(NRpc::TDispatcher::Get()->GetHeavyInvoker());

        BusServer_ = NBus::CreateBusServer(Config_->BusServer);
        RpcServer_ = NRpc::NBus::CreateBusServer(BusServer_);
        HttpServer_ = NHttp::CreateServer(Config_->CreateMonitoringHttpServerConfig());

        NativeConnection_ = NApi::NNative::CreateConnection(Config_->ClusterConnection);
        NativeRootClient_ = NativeConnection_->CreateNativeClient(NApi::TClientOptions::Root());
        NativeAuthenticator_ = NApi::NNative::CreateNativeAuthenticator(NativeConnection_);

        NLogging::GetDynamicTableLogWriterFactory()->SetClient(NativeRootClient_);

        DynamicConfigManager_ = New<TDynamicConfigManager>(this);
        DynamicConfigManager_->SubscribeConfigChanged(BIND_NO_PROPAGATE(&TBootstrap::OnDynamicConfigChanged, Unretained(this)));

        UserDirectory_ = New<TUserDirectory>();
        UserDirectorySynchronizer_ = CreateUserDirectorySynchronizer(
            Config_->UserDirectorySynchronizer,
            GetRootClient(),
            UserDirectory_,
            GetControlInvoker(),
            Config_->Testing->EnableUserDirectorySync
                ? NApi::EMasterChannelKind::Follower
                : NApi::EMasterChannelKind::Cache);

        MasterConnector_ = CreateMasterConnector(this);

        NMonitoring::Initialize(
            HttpServer_,
            ServiceLocator_->GetServiceOrThrow<NProfiling::TSolomonExporterPtr>(),
            &MonitoringManager_,
            &OrchidRoot_);

        if (Config_->ExposeConfigInOrchid) {
            SetNodeByYPath(
                OrchidRoot_,
                "/config",
                CreateVirtualNode(ConfigNode_));
            SetNodeByYPath(
                OrchidRoot_,
                "/dynamic_config_manager",
                CreateVirtualNode(DynamicConfigManager_->GetOrchidService()));
        }
        SetBuildAttributes(
            OrchidRoot_,
            "cypress_proxy");
        SetNodeByYPath(
            OrchidRoot_,
            "/sequoia_reign",
            ConvertToNode(GetCurrentSequoiaReign()));

        RpcServer_->RegisterService(CreateOrchidService(
            OrchidRoot_,
            GetControlInvoker(),
            /*authenticator*/ nullptr));
        RpcServer_->RegisterService(CreateAdminService(
            GetControlInvoker(),
            ServiceLocator_->FindService<NCoreDump::ICoreDumperPtr>(),
            /*authenticator*/ nullptr));

        SequoiaService_ = CreateSequoiaService(this);
        ResponseKeeper_ = CreateSequoiaResponseKeeper(GetDynamicConfigManager()->GetConfig()->ResponseKeeper, Logger());
        ObjectService_ = CreateObjectService(this);
        RpcServer_->RegisterService(ObjectService_->GetService());
        RpcServer_->RegisterService(CreateCypressTransactionService(this));

        WaitFor(sequoiaTableDescriptorInitialization)
            .ThrowOnError();
    }

    void DoStart()
    {
        NativeConnection_->GetClusterDirectorySynchronizer()->Start();
        NativeConnection_->GetMasterCellDirectorySynchronizer()->Start();

        MasterConnector_->Start();

        DynamicConfigManager_->Start();
        UserDirectorySynchronizer_->Start();

        YT_LOG_INFO("Listening for HTTP requests (Port: %v)", Config_->MonitoringPort);
        HttpServer_->Start();

        YT_LOG_INFO("Listening for RPC requests (Port: %v)", Config_->RpcPort);
        RpcServer_->Start();
    }

    void OnDynamicConfigChanged(
        const TCypressProxyDynamicConfigPtr& /*oldConfig*/,
        const TCypressProxyDynamicConfigPtr& newConfig)
    {
        TSingletonManager::Reconfigure(newConfig);

        ThreadPool_->SetThreadCount(newConfig->ThreadPoolSize);
        ResponseKeeper_->Reconfigure(newConfig->ResponseKeeper);
    }
};

////////////////////////////////////////////////////////////////////////////////

IBootstrapPtr CreateCypressProxyBootstrap(
    TCypressProxyBootstrapConfigPtr config,
    INodePtr configNode,
    IServiceLocatorPtr serviceLocator)
{
    return New<TBootstrap>(
        std::move(config),
        std::move(configNode),
        std::move(serviceLocator));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCypressProxy
