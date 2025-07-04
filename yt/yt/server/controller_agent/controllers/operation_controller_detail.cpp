#include "operation_controller_detail.h"

#include "auto_merge_task.h"
#include "common_profilers.h"
#include "common_state.h"
#include "helpers.h"
#include "input_transaction_manager.h"
#include "job_helpers.h"
#include "job_info.h"
#include "sink.h"
#include "spec_manager.h"
#include "task.h"

#include <yt/yt/server/controller_agent/chunk_list_pool.h>
#include <yt/yt/server/controller_agent/config.h>
#include <yt/yt/server/controller_agent/counter_manager.h>
#include <yt/yt/server/controller_agent/intermediate_chunk_scraper.h>
#include <yt/yt/server/controller_agent/operation.h>
#include <yt/yt/server/controller_agent/private.h>
#include <yt/yt/server/controller_agent/scheduling_context.h>

#include <yt/yt/server/controller_agent/controllers/job_memory.h>
#include <yt/yt/server/controller_agent/controllers/live_preview.h>

#include <yt/yt/server/lib/chunk_pools/helpers.h>
#include <yt/yt/server/lib/chunk_pools/multi_chunk_pool.h>

#include <yt/yt/server/lib/controller_agent/job_report.h>
#include <yt/yt/server/lib/controller_agent/network_project.h>

#include <yt/yt/server/lib/misc/job_reporter.h>
#include <yt/yt/server/lib/misc/job_table_schema.h>

#include <yt/yt/server/lib/scheduler/helpers.h>
#include <yt/yt/server/lib/scheduler/public.h>

#include <yt/yt/server/lib/tablet_node/public.h>

#include <yt/yt/ytlib/chunk_client/chunk_meta_extensions.h>
#include <yt/yt/ytlib/chunk_client/chunk_spec_fetcher.h>
#include <yt/yt/ytlib/chunk_client/chunk_teleporter.h>
#include <yt/yt/ytlib/chunk_client/data_slice_descriptor.h>
#include <yt/yt/ytlib/chunk_client/helpers.h>
#include <yt/yt/ytlib/chunk_client/input_chunk.h>
#include <yt/yt/ytlib/chunk_client/input_chunk_slice.h>
#include <yt/yt/ytlib/chunk_client/legacy_data_slice.h>

#include <yt/yt/ytlib/controller_agent/helpers.h>

#include <yt/yt/ytlib/controller_agent/proto/job.pb.h>

#include <yt/yt/ytlib/cell_master_client/cell_directory.h>
#include <yt/yt/ytlib/cell_master_client/cell_directory_synchronizer.h>

#include <yt/yt/ytlib/cypress_client/rpc_helpers.h>

#include <yt/yt/ytlib/event_log/event_log.h>

#include <yt/yt/ytlib/hive/hive_service_proxy.h>

#include <yt/yt/ytlib/object_client/helpers.h>
#include <yt/yt/ytlib/object_client/master_ypath_proxy.h>
#include <yt/yt/ytlib/object_client/object_service_proxy.h>

#include <yt/yt/ytlib/security_client/helpers.h>

#include <yt/yt/ytlib/query_client/functions_cache.h>

#include <yt/yt/ytlib/scheduler/helpers.h>
#include <yt/yt/ytlib/scheduler/job_resources_helpers.h>

#include <yt/yt/ytlib/table_client/chunk_meta_extensions.h>
#include <yt/yt/ytlib/table_client/chunk_slice_fetcher.h>
#include <yt/yt/ytlib/table_client/columnar_statistics_fetcher.h>
#include <yt/yt/ytlib/table_client/helpers.h>
#include <yt/yt/ytlib/table_client/schema.h>
#include <yt/yt/ytlib/table_client/table_ypath_proxy.h>

#include <yt/yt/ytlib/file_client/file_ypath_proxy.h>

#include <yt/yt/ytlib/tablet_client/backup.h>
#include <yt/yt/ytlib/tablet_client/bulk_insert_locking.h>
#include <yt/yt/ytlib/tablet_client/helpers.h>

#include <yt/yt/ytlib/transaction_client/action.h>
#include <yt/yt/ytlib/transaction_client/helpers.h>
#include <yt/yt/ytlib/transaction_client/transaction_service_proxy.h>

#include <yt/yt/ytlib/api/native/client.h>
#include <yt/yt/ytlib/api/native/config.h>
#include <yt/yt/ytlib/api/native/connection.h>
#include <yt/yt/ytlib/api/native/transaction.h>

#include <yt/yt/ytlib/api/native/proto/transaction_actions.pb.h>

#include <yt/yt/library/heavy_schema_validation/schema_validation.h>

#include <yt/yt/library/query/engine_api/range_inferrer.h>

#include <yt/yt/library/query/base/coordination_helpers.h>
#include <yt/yt/library/query/base/query.h>
#include <yt/yt/library/query/base/query_preparer.h>

#include <yt/yt/library/ytprof/heap_profiler.h>

#include <yt/yt/library/coredumper/coredumper.h>

#include <yt/yt/client/security_client/acl.h>
#include <yt/yt/client/security_client/helpers.h>

#include <yt/yt/client/chunk_client/data_statistics.h>
#include <yt/yt/client/chunk_client/helpers.h>
#include <yt/yt/client/chunk_client/read_limit.h>

#include <yt/yt/client/job_tracker_client/public.h>

#include <yt/yt/client/object_client/helpers.h>

#include <yt/yt/client/table_client/check_schema_compatibility.h>
#include <yt/yt/client/table_client/merge_table_schemas.h>
#include <yt/yt/client/table_client/row_buffer.h>
#include <yt/yt/client/table_client/schema.h>

#include <yt/yt/client/tablet_client/public.h>

#include <yt/yt/client/transaction_client/public.h>
#include <yt/yt/client/transaction_client/timestamp_provider.h>

#include <yt/yt/client/api/transaction.h>

#include <yt/yt/library/re2/re2.h>

#include <yt/yt/library/erasure/impl/codec.h>

#include <yt/yt/library/numeric/algorithm_helpers.h>

#include <yt/yt/core/actions/cancelable_context.h>
#include <yt/yt/core/actions/codicil_guarded_invoker.h>

#include <yt/yt/core/concurrency/action_queue.h>
#include <yt/yt/core/concurrency/fair_share_invoker_pool.h>
#include <yt/yt/core/concurrency/periodic_yielder.h>

#include <yt/yt/core/misc/collection_helpers.h>
#include <yt/yt/core/misc/crash_handler.h>
#include <yt/yt/core/misc/error.h>
#include <yt/yt/core/misc/finally.h>
#include <yt/yt/core/misc/fs.h>

#include <yt/yt/core/profiling/timing.h>

#include <yt/yt/core/ytree/virtual.h>
#include <yt/yt/core/ytree/ypath_resolver.h>
#include <yt/yt/core/ytree/yson_struct_update.h>

#include <yt/yt/core/yson/protobuf_helpers.h>

#include <yt/yt/core/logging/fluent_log.h>

#include <yt/yt/core/phoenix/type_registry.h>
#include <yt/yt/core/phoenix/schemas.h>
#include <yt/yt/core/phoenix/load.h>

#include <library/cpp/yt/memory/chunked_input_stream.h>

#include <library/cpp/iterator/concatenate.h>
#include <library/cpp/iterator/zip.h>

#include <util/generic/algorithm.h>
#include <util/generic/cast.h>
#include <util/generic/vector.h>

#include <util/system/compiler.h>

#include <functional>

namespace NYT::NControllerAgent::NControllers {

using namespace NApi;
using namespace NChunkClient;
using namespace NChunkPools;
using namespace NConcurrency;
using namespace NCypressClient;
using namespace NEventLog;
using namespace NFileClient;
using namespace NFormats;
using namespace NJobTrackerClient;
using namespace NLogging;
using namespace NNodeTrackerClient;
using namespace NObjectClient;
using namespace NQueryClient;
using namespace NRpc;
using namespace NScheduler;
using namespace NSecurityClient;
using namespace NTableClient;
using namespace NTabletClient;
using namespace NTransactionClient;
using namespace NVectorHdrf;
using namespace NYPath;
using namespace NYTProf;
using namespace NYTree;
using namespace NYson;
using namespace NServer;

using NYT::FromProto;
using NYT::ToProto;

using NControllerAgent::NProto::TJobSpec;
using NJobTrackerClient::EJobState;
using NNodeTrackerClient::TNodeId;
using NProfiling::TCpuInstant;
using NControllerAgent::NProto::TJobResultExt;
using NControllerAgent::NProto::TJobSpecExt;
using NTableClient::NProto::TBoundaryKeysExt;
using NTableClient::NProto::THeavyColumnStatisticsExt;
using NTabletNode::DefaultMaxOverlappingStoreCount;

using std::placeholders::_1;

////////////////////////////////////////////////////////////////////////////////

TError GetMaxFailedJobCountReachedError(int maxFailedJobCount)
{
    return TError(NScheduler::EErrorCode::MaxFailedJobsLimitExceeded, "Failed jobs limit exceeded")
        << TErrorAttribute("max_failed_job_count", maxFailedJobCount);
}

////////////////////////////////////////////////////////////////////////////////

TOperationControllerBase::TOperationControllerBase(
    TOperationSpecBasePtr spec,
    TControllerAgentConfigPtr config,
    TOperationOptionsPtr options,
    IOperationControllerHostPtr host,
    TOperation* operation)
    : Host_(std::move(host))
    , Config_(std::move(config))
    , OperationId_(operation->GetId())
    , OperationType_(operation->GetType())
    , StartTime_(operation->GetStartTime())
    , AuthenticatedUser_(operation->GetAuthenticatedUser())
    , SecureVault_(operation->GetSecureVault())
    , UserTransactionId_(operation->GetUserTransactionId())
    , Logger([&] {
        auto logger = ControllerLogger();
        logger = logger.WithTag("OperationId: %v", OperationId_);
        if (spec->EnableTraceLogging) {
            logger = logger.WithMinLevel(ELogLevel::Trace);
        }
        return logger;
    }())
    , CoreNotes_({Format("OperationId: %v", OperationId_)})
    , Acl_(operation->GetAcl())
    , AcoName_(operation->GetAcoName())
    , ControllerEpoch_(operation->GetControllerEpoch())
    , CancelableContext_(New<TCancelableContext>())
    , ChunkScraperInvoker_(Host_->GetChunkScraperThreadPoolInvoker())
    , DiagnosableInvokerPool_(CreateEnumIndexedProfiledFairShareInvokerPool<EOperationControllerQueue>(
        CreateCodicilGuardedInvoker(
            CreateSerializedInvoker(Host_->GetControllerThreadPoolInvoker(), "operation_controller_base"),
            Format(
                "OperationId: %v\nAuthenticatedUser: %v",
                OperationId_,
                AuthenticatedUser_)),
        CreateFairShareCallbackQueue,
        Config_->InvokerPoolTotalTimeAggregationPeriod,
        "OperationController"))
    , InvokerPool_(DiagnosableInvokerPool_)
    , SuspendableInvokerPool_(TransformInvokerPool(InvokerPool_, CreateSuspendableInvoker))
    , CancelableInvokerPool_(TransformInvokerPool(
        SuspendableInvokerPool_,
        BIND(&TCancelableContext::CreateInvoker, CancelableContext_)))
    , JobSpecBuildInvoker_(Host_->GetJobSpecBuildPoolInvoker())
    , RowBuffer_(New<TRowBuffer>(TRowBufferTag(), Config_->ControllerRowBufferChunkSize))
    , InputManager_(New<TInputManager>(this, Logger))
    , DataFlowGraph_(New<TDataFlowGraph>(Logger))
    , LivePreviews_(std::make_shared<TLivePreviewMap>())
    , PoolTreeControllerSettingsMap_(operation->PoolTreeControllerSettingsMap())
    , Spec_(std::move(spec))
    , Options_(std::move(options))
    , SpecManager_(New<TSpecManager>(this, Spec_, Logger))
    , CachedRunningJobs_(
        Config_->CachedRunningJobsUpdatePeriod,
        BIND(&TOperationControllerBase::DoBuildJobsYson, Unretained(this)))
    , SuspiciousJobsYsonUpdater_(New<TPeriodicExecutor>(
        GetCancelableInvoker(),
        BIND(&TThis::UpdateSuspiciousJobsYson, MakeWeak(this)),
        Config_->SuspiciousJobs->UpdatePeriod))
    , RunningJobStatisticsUpdateExecutor_(New<TPeriodicExecutor>(
        GetCancelableInvoker(),
        BIND(&TThis::UpdateAggregatedRunningJobStatistics, MakeWeak(this)),
        Config_->RunningJobStatisticsUpdatePeriod))
    , ScheduleAllocationStatistics_(New<TScheduleAllocationStatistics>(Config_->ScheduleAllocationStatisticsMovingAverageWindowSize))
    , CheckTimeLimitExecutor_(New<TPeriodicExecutor>(
        GetCancelableInvoker(),
        BIND(&TThis::CheckTimeLimit, MakeWeak(this)),
        Config_->OperationTimeLimitCheckPeriod))
    , ExecNodesCheckExecutor_(New<TPeriodicExecutor>(
        GetCancelableInvoker(),
        BIND(&TThis::CheckAvailableExecNodes, MakeWeak(this)),
        Config_->AvailableExecNodesCheckPeriod))
    , AlertManager_(CreateAlertManager(this))
    , MinNeededResourcesSanityCheckExecutor_(New<TPeriodicExecutor>(
        GetCancelableInvoker(),
        BIND(&TThis::CheckMinNeededResourcesSanity, MakeWeak(this)),
        Config_->ResourceDemandSanityCheckPeriod))
    , PeakMemoryUsageUpdateExecutor_(New<TPeriodicExecutor>(
        GetCancelableInvoker(),
        BIND(&TThis::UpdatePeakMemoryUsage, MakeWeak(this)),
        Config_->MemoryWatchdog->MemoryUsageCheckPeriod))
    , ExecNodesUpdateExecutor_(New<TPeriodicExecutor>(
        Host_->GetExecNodesUpdateInvoker(),
        BIND(&TThis::UpdateExecNodes, MakeWeak(this)),
        Config_->ControllerExecNodeInfoUpdatePeriod))
    , EventLogConsumer_(Host_->GetEventLogWriter()->CreateConsumer())
    , LogProgressBackoff_(DurationToCpuDuration(Config_->OperationLogProgressBackoff))
    , ProgressBuildExecutor_(New<TPeriodicExecutor>(
        GetCancelableInvoker(),
        BIND(&TThis::BuildAndSaveProgress, MakeWeak(this)),
        Config_->OperationBuildProgressPeriod))
    , CheckTentativeTreeEligibilityExecutor_(New<TPeriodicExecutor>(
        GetCancelableInvoker(),
        BIND(&TThis::CheckTentativeTreeEligibility, MakeWeak(this)),
        Config_->CheckTentativeTreeEligibilityPeriod))
    , MediumDirectory_(Host_->GetMediumDirectory())
    , ExperimentAssignments_(operation->ExperimentAssignments())
    , UpdateAccountResourceUsageLeasesExecutor_(New<TPeriodicExecutor>(
        GetCancelableInvoker(),
        BIND(&TThis::UpdateAccountResourceUsageLeases, MakeWeak(this)),
        Config_->UpdateAccountResourceUsageLeasesPeriod))
    , TotalJobCounter_(New<TProgressCounter>())
    , TestingAllocationSize_(Spec_->TestingOperationOptions->AllocationSize.value_or(0))
    , AllocationReleaseDelay_(Spec_->TestingOperationOptions->AllocationReleaseDelay)
    , FastIntermediateMediumLimit_(std::min(
        Spec_->FastIntermediateMediumLimit,
        Config_->FastIntermediateMediumLimit))
    , SendRunningAllocationTimeStatisticsUpdatesExecutor_(New<TPeriodicExecutor>(
        GetCancelableInvoker(EOperationControllerQueue::JobEvents),
        BIND_NO_PROPAGATE(&TThis::SendRunningAllocationTimeStatisticsUpdates, MakeWeak(this)),
        Config_->RunningAllocationTimeStatisticsUpdatesSendPeriod))
    , JobAbortsUntilOperationFailure_(Config_->MaxJobAbortsUntilOperationFailure)
{
    // Attach user transaction if any. Don't ping it.
    TTransactionAttachOptions userAttachOptions;
    userAttachOptions.Ping = false;
    userAttachOptions.PingAncestors = false;
    UserTransaction_ = UserTransactionId_
        ? Host_->GetClient()->AttachTransaction(UserTransactionId_, userAttachOptions)
        : nullptr;

    for (const auto& reason : TEnumTraits<EScheduleFailReason>::GetDomainValues()) {
        ExternalScheduleAllocationFailureCounts_[reason] = 0;
    }

    TSchedulingTagFilter filter(Spec_->SchedulingTagFilter);
    ExecNodesDescriptors_ = Host_->GetExecNodeDescriptors(filter, /*onlineOnly*/ false);
    OnlineExecNodesDescriptors_ = Host_->GetExecNodeDescriptors(filter, /*onlineOnly*/ true);

    YT_LOG_INFO("Operation controller instantiated (OperationType: %v, Address: %v)",
        OperationType_,
        static_cast<void*>(this));

    YT_LOG_DEBUG("Set fast intermediate medium limit (ConfigLimit: %v, SpecLimit: %v, EffectiveLimit: %v)",
        Config_->FastIntermediateMediumLimit,
        Spec_->FastIntermediateMediumLimit,
        GetFastIntermediateMediumLimit());
}

void TOperationControllerBase::BuildMemoryUsageYson(TFluentAny fluent) const
{
    fluent
        .Value(GetMemoryUsage());
}

void TOperationControllerBase::BuildStateYson(TFluentAny fluent) const
{
    fluent
        .Value(State_.load());
}

void TOperationControllerBase::BuildTestingState(TFluentAny fluent) const
{
    fluent
        .BeginMap()
            .Item("commit_sleep_started").Value(CommitSleepStarted_)
            .Item("dynamic_spec").Value(SpecManager_->GetSpec())
        .EndMap();
}

const TProgressCounterPtr& TOperationControllerBase::GetTotalJobCounter() const
{
    return TotalJobCounter_;
}

const TScheduleAllocationStatisticsPtr& TOperationControllerBase::GetScheduleAllocationStatistics() const
{
    return ScheduleAllocationStatistics_;
}

const TAggregatedJobStatistics& TOperationControllerBase::GetAggregatedFinishedJobStatistics() const
{
    return AggregatedFinishedJobStatistics_;
}

const TAggregatedJobStatistics& TOperationControllerBase::GetAggregatedRunningJobStatistics() const
{
    return AggregatedRunningJobStatistics_;
}

std::unique_ptr<IHistogram> TOperationControllerBase::ComputeFinalPartitionSizeHistogram() const
{
    return nullptr;
}

// Resource management.
TExtendedJobResources TOperationControllerBase::GetAutoMergeResources(
    const TChunkStripeStatisticsVector& statistics) const
{
    TExtendedJobResources result;
    result.SetUserSlots(1);
    result.SetCpu(1);
    // TODO(max42): this way to estimate memory of an auto-merge job is wrong as it considers each
    // auto-merge task writing to all output tables.
    auto jobProxyMemory = GetFinalIOMemorySize(
        Spec_->AutoMerge->JobIO,
        /*useEstimatedBufferSize*/ true,
        AggregateStatistics(statistics));
    auto jobProxyMemoryWithFixedWriteBufferSize = GetFinalIOMemorySize(
        Spec_->AutoMerge->JobIO,
        /*useEstimatedBufferSize*/ false,
        AggregateStatistics(statistics));

    result.SetJobProxyMemory(jobProxyMemory);
    result.SetJobProxyMemoryWithFixedWriteBufferSize(jobProxyMemoryWithFixedWriteBufferSize);

    return result;
}

void TOperationControllerBase::SleepInInitialize()
{
    if (auto delay = Spec_->TestingOperationOptions->DelayInsideInitialize) {
        TDelayedExecutor::WaitForDuration(*delay);
    }
}

std::vector<TTestAllocationGuard> TOperationControllerBase::TestHeap() const
{
    if (Spec_->TestingOperationOptions->AllocationSize.value_or(0) > 0) {
        auto Logger = ControllerLogger;

        constexpr i64 allocationPartSize = 1_MB;

        std::vector<TTestAllocationGuard> testHeap;

        std::function<void()> incrementer = [
                this,
                this_ = MakeStrong(this),
                operationId = ToString(OperationId_),
                Logger = Logger
            ] {
                auto size = TestingAllocationSize_.fetch_add(allocationPartSize);
                YT_LOG_DEBUG("Testing allocation size was incremented (Size: %v)",
                    size + allocationPartSize);
            };

        std::function<void()> decrementer = [
            this,
            this_ = MakeStrong(this),
            OperationId = ToString(OperationId_),
            Logger = Logger] {
                auto size = TestingAllocationSize_.fetch_sub(allocationPartSize);
                YT_LOG_DEBUG("Testing allocation size was decremented (Size: %v)",
                    size - allocationPartSize);
            };

        while (TestingAllocationSize_ > 0) {
            testHeap.emplace_back(
                allocationPartSize,
                decrementer,
                incrementer,
                AllocationReleaseDelay_.value_or(TDuration::Zero()),
                GetInvoker());
        }

        YT_LOG_DEBUG("Test heap allocation is finished (MemoryUsage: %v)",
            GetMemoryUsage());
        return testHeap;
    }

    return {};
}

void TOperationControllerBase::InitializeClients()
{
    Client_ = Host_
        ->GetClient()
        ->GetNativeConnection()
        ->CreateNativeClient(TClientOptions::FromUser(AuthenticatedUser_));
    InputClient_ = Client_;
    OutputClient_ = Client_;

    SchedulerClient_ = Host_
        ->GetClient()
        ->GetNativeConnection()
        ->CreateNativeClient(TClientOptions::FromUser(SchedulerUserName));

    // TODO(coteeq): SchedulerInputClient may seem unexpected since we now can have
    //               client from another cluster (or even many different clusters).
    //               For now, InputManager will extract `scheduler` username from
    //               this client and will create an appropriate client.
    //               Todo: get rid (or document) of this distinction and make
    //               InputManager create a client from SchedulerUserName directly.
    SchedulerInputClient_ = SchedulerClient_;
    SchedulerOutputClient_ = SchedulerClient_;

    InputManager_->InitializeClients(InputClient_);
}

void TOperationControllerBase::InitializeInputTransactions()
{
    // COMPAT(coteeq): Correct reviving from snapshot relies on order of this vector.
    std::vector<TRichYPath> filesAndTables = GetInputTablePaths();

    for (const auto& userJobSpec : GetUserJobSpecs()) {
        for (const auto& path : userJobSpec->FilePaths) {
            filesAndTables.push_back(path);
        }

        auto layerPaths = GetLayerPaths(userJobSpec);
        for (const auto& path : layerPaths) {
            filesAndTables.push_back(path);
        }

        if (Options_->GpuCheck->UseSeparateRootVolume && userJobSpec->EnableGpuCheck) {
            for (const auto& path : Options_->GpuCheck->LayerPaths) {
                filesAndTables.push_back(path);
            }
        }
    }

    auto clusterResolver = New<TClusterResolver>(InputClient_);
    WaitFor(clusterResolver->Init())
        .ThrowOnError();

    InputTransactions_ = New<TInputTransactionManager>(
        InputClient_,
        std::move(clusterResolver),
        OperationId_,
        filesAndTables,
        HasDiskRequestsWithSpecifiedAccount() || TLayerJobExperiment::IsEnabled(Spec_, GetUserJobSpecs()),
        GetInputTransactionParentId(),
        AuthenticatedUser_,
        Config_,
        Logger);
}

IAttributeDictionaryPtr TOperationControllerBase::CreateTransactionAttributes(ETransactionType transactionType) const
{
    return BuildAttributeDictionaryFluently()
        .Item("title").Value(
            Format("Scheduler %Qlv transaction for operation %v",
                transactionType,
                OperationId_))
        .Item("operation_id").Value(OperationId_)
        .OptionalItem("operation_title", Spec_->Title)
        .Item("operation_type").Value(GetOperationType())
        .Finish();
}

TOperationControllerInitializeResult TOperationControllerBase::InitializeReviving(
    const TControllerTransactionIds& transactions,
    INodePtr cumulativeSpecPatch)
{
    YT_LOG_INFO("Initializing operation for revive");

    InitializeClients();
    InitializeInputTransactions();

    SpecManager_->InitializeReviving(std::move(cumulativeSpecPatch));

    auto attachTransaction = [&] (TTransactionId transactionId, const NNative::IClientPtr& client, bool ping) -> ITransactionPtr {
        if (!transactionId) {
            return nullptr;
        }

        try {
            return AttachTransaction(transactionId, client, ping);
        } catch (const std::exception& ex) {
            YT_LOG_WARNING(ex, "Error attaching operation transaction (OperationId: %v, TransactionId: %v)",
                OperationId_,
                transactionId);
            return nullptr;
        }
    };

    auto outputTransaction = attachTransaction(transactions.OutputId, OutputClient_, true);
    auto debugTransaction = attachTransaction(transactions.DebugId, Client_, true);
    // NB: Async and completion transactions are never reused and thus are not pinged.
    auto asyncTransaction = attachTransaction(transactions.AsyncId, Client_, false);
    auto outputCompletionTransaction = attachTransaction(transactions.OutputCompletionId, OutputClient_, false);
    auto debugCompletionTransaction = attachTransaction(transactions.DebugCompletionId, Client_, false);

    auto inputReviveResult = WaitFor(InputTransactions_->Revive(transactions));
    if (!inputReviveResult.IsOK()) {
        CleanStart_ = true;
        YT_LOG_INFO(
            inputReviveResult,
            "Could not reuse input transactions, will use clean start");
    }

    // Check transactions.
    {
        std::vector<std::pair<ITransactionPtr, TFuture<void>>> asyncCheckResults;

        THashSet<ITransactionPtr> checkedTransactions;
        auto checkTransaction = [&] (
            const ITransactionPtr& transaction,
            ETransactionType transactionType,
            TTransactionId transactionId)
        {
            if (!transaction) {
                CleanStart_ = true;
                YT_LOG_INFO("Operation transaction is missing, will use clean start "
                    "(TransactionType: %v, TransactionId: %v)",
                    transactionType,
                    transactionId);
                return;
            }

            if (checkedTransactions.emplace(transaction).second) {
                asyncCheckResults.emplace_back(transaction, transaction->Ping());
            }
        };

        // NB: Async transaction is not checked.
        if (IsTransactionNeeded(ETransactionType::Output)) {
            checkTransaction(outputTransaction, ETransactionType::Output, transactions.OutputId);
        }
        if (IsTransactionNeeded(ETransactionType::Debug)) {
            checkTransaction(debugTransaction, ETransactionType::Debug, transactions.DebugId);
        }

        for (const auto& [transaction, asyncCheckResult] : asyncCheckResults) {
            auto error = WaitFor(asyncCheckResult);
            if (!error.IsOK()) {
                CleanStart_ = true;
                YT_LOG_INFO(error,
                    "Error renewing operation transaction, will use clean start (TransactionId: %v)",
                    transaction->GetId());
            }
        }
    }

    // Downloading snapshot.
    if (!CleanStart_) {
        auto snapshotOrError = WaitFor(Host_->DownloadSnapshot());
        if (!snapshotOrError.IsOK()) {
            YT_LOG_INFO(snapshotOrError, "Failed to download snapshot, will use clean start");
            CleanStart_ = true;
        } else {
            YT_LOG_INFO("Snapshot successfully downloaded");
            Snapshot_ = snapshotOrError.Value();
            if (Snapshot_.Blocks.empty()) {
                YT_LOG_WARNING("Snapshot is empty, will use clean start");
                CleanStart_ = true;
            }
        }
    }

    // Abort transactions if needed.
    {
        std::vector<TFuture<void>> asyncResults;

        THashSet<ITransactionPtr> abortedTransactions;
        auto scheduleAbort = [&] (const ITransactionPtr& transaction, const NNative::IClientPtr& client) {
            if (transaction && abortedTransactions.emplace(transaction).second) {
                // Transaction object may be in incorrect state, we need to abort using only transaction id.
                asyncResults.push_back(AttachTransaction(transaction->GetId(), client)->Abort());
            }
        };

        scheduleAbort(asyncTransaction, Client_);
        scheduleAbort(outputCompletionTransaction, OutputClient_);
        scheduleAbort(debugCompletionTransaction, Client_);

        if (CleanStart_) {
            YT_LOG_INFO("Aborting operation transactions");
            // NB: Don't touch user transaction.
            scheduleAbort(outputTransaction, OutputClient_);
            scheduleAbort(debugTransaction, Client_);
            asyncResults.push_back(AbortInputTransactions());
        } else {
            YT_LOG_INFO("Reusing operation transactions");
            OutputTransaction_ = outputTransaction;
            DebugTransaction_ = debugTransaction;
            AsyncTransaction_ = WaitFor(StartTransaction(ETransactionType::Async, Client_))
                .ValueOrThrow();
        }

        WaitFor(AllSucceeded(asyncResults))
            .ThrowOnError();
    }

    if (CleanStart_) {
        if (HasJobUniquenessRequirements()) {
            THROW_ERROR_EXCEPTION(
                NScheduler::EErrorCode::OperationFailedOnJobRestart,
                "Cannot use clean restart when option \"fail_on_job_restart\" is set in operation spec or user job spec")
                << TErrorAttribute("reason", EFailOnJobRestartReason::RevivalWithCleanStart);
        }

        YT_LOG_INFO("Using clean start instead of revive");

        Snapshot_ = TOperationSnapshot();
        Y_UNUSED(WaitFor(Host_->RemoveSnapshot()));

        StartTransactions();
        InitializeStructures();

        LockInputs();
    }

    InitUnrecognizedSpec();

    WaitFor(Host_->UpdateInitializedOperationNode(CleanStart_))
        .ThrowOnError();

    SleepInInitialize();

    YT_LOG_INFO("Operation initialized");

    TOperationControllerInitializeResult result;
    FillInitializeResult(&result);
    return result;
}

void TOperationControllerBase::ValidateSecureVault()
{
    if (!SecureVault_) {
        return;
    }
    i64 length = ConvertToYsonString(SecureVault_, EYsonFormat::Text).AsStringBuf().size();
    YT_LOG_DEBUG("Operation secure vault size detected (Size: %v)", length);
    if (length > Config_->SecureVaultLengthLimit) {
        THROW_ERROR_EXCEPTION("Secure vault YSON text representation is too long")
            << TErrorAttribute("size_limit", Config_->SecureVaultLengthLimit);
    }
}

TOperationControllerInitializeResult TOperationControllerBase::InitializeClean()
{
    YT_LOG_INFO("Initializing operation for clean start (Title: %v)",
        Spec_->Title);

    auto initializeAction = BIND([this_ = MakeStrong(this), this] {
        ValidateSecureVault();
        InitializeClients();
        InitializeInputTransactions();
        StartTransactions();
        InitializeStructures();
        LockInputs();
    });

    SleepInInitialize();

    auto initializeFuture = initializeAction
        .AsyncVia(GetCancelableInvoker())
        .Run()
        .WithTimeout(Config_->OperationInitializationTimeout);

    WaitFor(initializeFuture)
        .ThrowOnError();

    InitUnrecognizedSpec();

    WaitFor(Host_->UpdateInitializedOperationNode(/*isCleanOperationStart*/ true))
        .ThrowOnError();

    YT_LOG_INFO("Operation initialized");

    TOperationControllerInitializeResult result;
    FillInitializeResult(&result);
    return result;
}

bool TOperationControllerBase::HasUserJobFilesOrLayers() const
{
    for (const auto& [_, files] : UserJobFiles_) {
        if (!files.empty()) {
            return true;
        }
    }
    return false;
}

void TOperationControllerBase::InitOutputTables()
{
    RegisterOutputTables(GetOutputTablePaths());
}

const IPersistentChunkPoolInputPtr& TOperationControllerBase::GetSink()
{
    return Sink_;
}

void TOperationControllerBase::ValidateAccountPermission(const std::string& account, EPermission permission) const
{
    auto user = AuthenticatedUser_;

    const auto& client = Host_->GetClient();
    auto asyncResult = client->CheckPermission(
        user,
        GetAccountPath(account),
        permission);
    auto result = WaitFor(asyncResult)
        .ValueOrThrow();

    if (result.Action == ESecurityAction::Deny) {
        THROW_ERROR_EXCEPTION("User %Qv has been denied %Qlv access to intermediate account %Qv",
            user,
            permission,
            account);
    }
}

void TOperationControllerBase::InitializeStructures()
{
    InputManager_->InitializeStructures(InputClient_, InputTransactions_);

    DataFlowGraph_->SetNodeDirectory(OutputNodeDirectory_);
    DataFlowGraph_->Initialize();

    InitializeOrchid();

    InitOutputTables();

    if (auto stderrTablePath = GetStderrTablePath()) {
        StderrTable_ = New<TOutputTable>(*stderrTablePath, EOutputTableType::Stderr);
        DataFlowGraph_->RegisterVertex(TDataFlowGraph::StderrDescriptor);
    }

    if (auto coreTablePath = GetCoreTablePath()) {
        CoreTable_ = New<TOutputTable>(*coreTablePath, EOutputTableType::Core);
        DataFlowGraph_->RegisterVertex(TDataFlowGraph::CoreDescriptor);
    }

    InitUpdatingTables();

    for (const auto& userJobSpec : GetUserJobSpecs()) {
        auto& files = UserJobFiles_[userJobSpec];

        // Add regular files.
        for (const auto& path : userJobSpec->FilePaths) {
            files.push_back(TUserFile(
                path,
                InputTransactions_->GetTransactionIdForObject(path),
                /*layer*/ false));
        }

        // Add layer files.
        auto layerPaths = GetLayerPaths(userJobSpec);
        for (const auto& path : layerPaths) {
            files.push_back(TUserFile(
                path,
                InputTransactions_->GetTransactionIdForObject(path),
                /*layer*/ true));
        }

        // Add gpu check layers.
        if (Options_->GpuCheck->UseSeparateRootVolume && userJobSpec->EnableGpuCheck) {
            for (const auto& path : Options_->GpuCheck->LayerPaths) {
                auto file = TUserFile(
                    path,
                    InputTransactions_->GetTransactionIdForObject(path),
                    /*layer*/ true);
                file.GpuCheck = true;
                files.push_back(std::move(file));
            }
        }

        if (userJobSpec->CpuLimit < Options_->MinCpuLimit) {
            SetOperationAlert(
                EOperationAlertType::SpecifiedCpuLimitIsTooSmall,
                TError("Specified CPU limit is too small: %v < %v",
                    userJobSpec->CpuLimit,
                    Options_->MinCpuLimit));
        }
    }

    if (TLayerJobExperiment::IsEnabled(Spec_, GetUserJobSpecs())) {
        auto path = TRichYPath(*Spec_->JobExperiment->BaseLayerPath);
        if (path.GetTransactionId()) {
            THROW_ERROR_EXCEPTION("Transaction id is not supported for \"probing_base_layer_path\"");
        }
        BaseLayer_ = TUserFile(path, InputTransactions_->GetLocalInputTransactionId(), true);
    }

    auto maxInputTableCount = std::min(Config_->MaxInputTableCount, Options_->MaxInputTableCount);
    if (std::ssize(InputManager_->GetInputTables()) > maxInputTableCount) {
        THROW_ERROR_EXCEPTION(
            "Too many input tables: maximum allowed %v, actual %v",
            maxInputTableCount,
            InputManager_->GetInputTables().size());
    }

    if (std::ssize(OutputTables_) > Config_->MaxOutputTableCount) {
        THROW_ERROR_EXCEPTION(
            "Too many output tables: maximum allowed %v, actual %v",
            Config_->MaxOutputTableCount,
            OutputTables_.size());
    }

    InitAccountResourceUsageLeases();

    DoInitialize();
}

void TOperationControllerBase::InitUnrecognizedSpec()
{
    UnrecognizedSpec_ = GetTypedSpec()->GetRecursiveUnrecognized();
}

void TOperationControllerBase::FillInitializeResult(TOperationControllerInitializeResult* result)
{
    result->Attributes.BriefSpec = BuildYsonStringFluently<EYsonType::MapFragment>()
        .Do(BIND(&TOperationControllerBase::BuildBriefSpec, Unretained(this)))
        .Finish();
    result->Attributes.FullSpec = ConvertToYsonString(Spec_);
    result->Attributes.UnrecognizedSpec = ConvertToYsonString(UnrecognizedSpec_);
    result->TransactionIds = GetTransactionIds();
    result->EraseOffloadingTrees = NeedEraseOffloadingTrees();
}

bool TOperationControllerBase::NeedEraseOffloadingTrees() const
{
    bool hasJobsAllowedForOffloading = false;
    auto userJobSpecs = GetUserJobSpecs();
    for (const auto& userJobSpec : userJobSpecs) {
        if (!userJobSpec->NetworkProject || Config_->NetworkProjectsAllowedForOffloading.contains(*userJobSpec->NetworkProject)) {
            hasJobsAllowedForOffloading = true;
        }
    }
    return !userJobSpecs.empty() && !hasJobsAllowedForOffloading;
}

void TOperationControllerBase::ValidateIntermediateDataAccess(const std::string& user, EPermission permission) const
{
    // Permission for IntermediateData can be only Read.
    YT_VERIFY(permission == EPermission::Read);
    Host_->ValidateOperationAccess(user, EPermissionSet(permission));
}

void TOperationControllerBase::InitUpdatingTables()
{
    UpdatingTables_.clear();

    for (auto& table : OutputTables_) {
        UpdatingTables_.emplace_back(table);
    }

    if (StderrTable_) {
        UpdatingTables_.emplace_back(StderrTable_);
    }

    if (CoreTable_) {
        UpdatingTables_.emplace_back(CoreTable_);
    }
}

void TOperationControllerBase::InitializeOrchid()
{
    YT_LOG_DEBUG("Initializing orchid");

    using TLivePreviewMapService = NYTree::TCollectionBoundMapService<TLivePreviewMap>;
    LivePreviewService_ = New<TLivePreviewMapService>(std::weak_ptr<TLivePreviewMap>(LivePreviews_));

    auto createService = [&] (auto fluentMethod, const TString& key) {
        return IYPathService::FromProducer(BIND(
            [
                =,
                this,
                weakThis = MakeWeak(this),
                fluentMethod = std::move(fluentMethod)
            ] (IYsonConsumer* consumer) {
                auto this_ = weakThis.Lock();
                if (!this_) {
                    THROW_ERROR_EXCEPTION(NYTree::EErrorCode::ResolveError, "Operation controller was destroyed");
                }

                YT_LOG_DEBUG(
                    "Handling orchid request in controller (Key: %v)",
                    key);

                BuildYsonFluently(consumer)
                    .Do(fluentMethod);
            }),
            Config_->ControllerStaticOrchidUpdatePeriod);
    };

    // Methods like BuildProgress, BuildBriefProgress and buildJobsYson build map fragment,
    // so we have to enclose them with a map in order to pass into createService helper.
    // TODO(max42): get rid of this when GetOperationInfo is not stopping us from changing Build* signatures any more.
    auto wrapWithMap = [=] (auto fluentMethod) {
        return [=, fluentMethod = std::move(fluentMethod)] (TFluentAny fluent) {
            fluent
                .BeginMap()
                    .Do(fluentMethod)
                .EndMap();
        };
    };

    auto createServiceWithInvoker = [&] (auto fluentMethod, const TString& key) -> IYPathServicePtr {
        return createService(std::move(fluentMethod), key)
            ->Via(InvokerPool_->GetInvoker(EOperationControllerQueue::Default));
    };

    auto createMapServiceWithInvoker = [&] (auto fluentMethod, const TString& key) -> IYPathServicePtr {
        return createServiceWithInvoker(wrapWithMap(std::move(fluentMethod)), key);
    };

    // NB: We may safely pass unretained this below as all the callbacks are wrapped with a createService helper
    // that takes care on checking the controller presence and properly replying in case it is already destroyed.
    auto service = New<TCompositeMapService>()
        ->AddChild(
            "progress",
            createMapServiceWithInvoker(BIND(&TOperationControllerBase::BuildProgress, Unretained(this)), "progress"))
        ->AddChild(
            "brief_progress",
            createMapServiceWithInvoker(BIND(&TOperationControllerBase::BuildBriefProgress, Unretained(this)), "brief_progress"))
        ->AddChild(
            "running_jobs",
            createMapServiceWithInvoker(BIND(&TOperationControllerBase::BuildJobsYson, Unretained(this)), "running_jobs"))
        ->AddChild(
            "retained_finished_jobs",
            createMapServiceWithInvoker(BIND(&TOperationControllerBase::BuildRetainedFinishedJobsYson, Unretained(this)), "retained_finished_jobs"))
        ->AddChild(
            "unavailable_input_chunks",
            createServiceWithInvoker(BIND(&TInputManager::BuildUnavailableInputChunksYson, InputManager_), "unavailable_input_chunks"))
        ->AddChild(
            "memory_usage",
            createService(BIND(&TOperationControllerBase::BuildMemoryUsageYson, Unretained(this)), "memory_usage"))
        ->AddChild(
            "state",
            createService(BIND(&TOperationControllerBase::BuildStateYson, Unretained(this)), "state"))
        ->AddChild(
            "controller",
            createMapServiceWithInvoker(BIND(&TOperationControllerBase::BuildControllerInfoYson, Unretained(this)), "controller"))
        ->AddChild(
            "data_flow_graph",
            DataFlowGraph_->GetService()
                ->WithPermissionValidator(BIND(&TOperationControllerBase::ValidateIntermediateDataAccess, MakeWeak(this))))
        ->AddChild(
            "live_previews",
            LivePreviewService_
                ->WithPermissionValidator(BIND(&TOperationControllerBase::ValidateIntermediateDataAccess, MakeWeak(this))))
        ->AddChild(
            "testing",
            createService(BIND(&TOperationControllerBase::BuildTestingState, Unretained(this)), "testing"));
    service->SetOpaque(false);
    Orchid_.Store(service
        ->Via(InvokerPool_->GetInvoker(EOperationControllerQueue::Default)));

    YT_LOG_DEBUG("Orchid initialized");
}

void TOperationControllerBase::DoInitialize()
{ }

void TOperationControllerBase::LockInputs()
{
    // TODO(max42): why is this done during initialization?
    // Consider moving this call to preparation phase.
    PrepareInputTables();
    InputManager_->LockInputTables();
    LockUserFiles();
}

void TOperationControllerBase::SleepInPrepare()
{
    auto delay = Spec_->TestingOperationOptions->DelayInsidePrepare;
    if (delay) {
        TDelayedExecutor::WaitForDuration(*delay);
    }
}

void TOperationControllerBase::CreateOutputTables(
    const NApi::NNative::IClientPtr& client,
    const std::vector<TUserObject*>& tables,
    TTransactionId defaultTransactionId,
    EOutputTableType outputTableType,
    EObjectType desiredType)
{
    std::vector<TUserObject*> tablesToCreate;
    for (auto* table : tables) {
        if (table->Path.GetCreate()) {
            tablesToCreate.push_back(table);
        }
    }
    if (tablesToCreate.empty()) {
        return;
    }

    YT_LOG_DEBUG("Creating output tables (TableCount: %v, OutputTableType: %v)",
        tablesToCreate.size(),
        outputTableType);

    auto proxy = CreateObjectServiceWriteProxy(client);
    auto batchReq = proxy.ExecuteBatch();

    for (auto* table : tablesToCreate) {
        auto req = TCypressYPathProxy::Create(table->Path.GetPath());
        req->set_ignore_existing(true);
        req->set_type(ToProto(desiredType));

        NCypressClient::SetTransactionId(req, table->TransactionId.value_or(defaultTransactionId));
        GenerateMutationId(req);

        batchReq->AddRequest(req);
    }

    auto batchRspOrError = WaitFor(batchReq->Invoke());
    THROW_ERROR_EXCEPTION_IF_FAILED(GetCumulativeError(batchRspOrError), "Error creating output tables");
}

TOperationControllerPrepareResult TOperationControllerBase::SafePrepare()
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker());

    SleepInPrepare();

    // Testing purpose code.
    if (Config_->EnableControllerFailureSpecOption &&
        Spec_->TestingOperationOptions->ControllerFailure)
    {
        YT_VERIFY(*Spec_->TestingOperationOptions->ControllerFailure !=
            EControllerFailureType::AssertionFailureInPrepare);
    }

    InputManager_->Prepare();

    PrepareInputQuery();

    // Process files.
    if (HasUserJobFilesOrLayers()) {
        GetUserFilesAttributes();
    } else {
        YT_LOG_INFO("Operation has no input files");
    }

    // Process output and stderr tables.
    if (!OutputTables_.empty()) {
        auto userObjectList = MakeUserObjectList(OutputTables_);
        CreateOutputTables(
            OutputClient_,
            userObjectList,
            OutputTransaction_->GetId(),
            EOutputTableType::Output,
            GetOutputTableDesiredType());
        GetUserObjectBasicAttributes(
            OutputClient_,
            userObjectList,
            OutputTransaction_->GetId(),
            Logger,
            EPermission::Write);
    } else {
        YT_LOG_INFO("Operation has no output tables");
    }

    if (StderrTable_) {
        CreateOutputTables(
            Client_,
            {StderrTable_.Get()},
            DebugTransaction_->GetId(),
            EOutputTableType::Stderr,
            EObjectType::Table);
        GetUserObjectBasicAttributes(
            Client_,
            {StderrTable_.Get()},
            DebugTransaction_->GetId(),
            Logger,
            EPermission::Write);
    } else {
        YT_LOG_INFO("Operation has no stderr table");
    }

    if (CoreTable_) {
        CreateOutputTables(
            Client_,
            {CoreTable_.Get()},
            DebugTransaction_->GetId(),
            EOutputTableType::Core,
            EObjectType::Table);
        GetUserObjectBasicAttributes(
            Client_,
            {CoreTable_.Get()},
            DebugTransaction_->GetId(),
            Logger,
            EPermission::Write);
    } else {
        YT_LOG_INFO("Operation has no core table");
    }

    {
        ValidateUpdatingTablesTypes();

        THashSet<TObjectId> updatingTableIds;
        for (const auto& table : UpdatingTables_) {
            bool insertedNew = updatingTableIds.insert(table->ObjectId).second;
            if (!insertedNew) {
                THROW_ERROR_EXCEPTION("Output table %v is specified multiple times",
                    table->GetPath());
            }
        }

        GetOutputTablesSchema();

        std::vector<TTableSchemaPtr> outputTableSchemas;
        outputTableSchemas.resize(OutputTables_.size());
        for (int outputTableIndex = 0; outputTableIndex < std::ssize(OutputTables_); ++outputTableIndex) {
            const auto& table = OutputTables_[outputTableIndex];
            if (table->Dynamic) {
                outputTableSchemas[outputTableIndex] = table->TableUploadOptions.GetUploadSchema();
            }
        }

        PrepareOutputTables();

        for (int outputTableIndex = 0; outputTableIndex < std::ssize(OutputTables_); ++outputTableIndex) {
            const auto& table = OutputTables_[outputTableIndex];
            if (table->Dynamic && *outputTableSchemas[outputTableIndex] != *table->TableUploadOptions.GetUploadSchema()) {
                THROW_ERROR_EXCEPTION(
                    "Schema of output dynamic table %v unexpectedly changed during preparation phase. "
                    "Please send the link to the operation to yt-admin@",
                    table->Path)
                    << TErrorAttribute("original_schema", *outputTableSchemas[outputTableIndex])
                    << TErrorAttribute("upload_schema", *table->TableUploadOptions.GetUploadSchema());
            }
        }

        LockOutputTablesAndGetAttributes();
    }

    InitializeStandardStreamDescriptors();

    CustomPrepare();

    // NB: These calls must be after CustomPrepare() since some controllers may alter input table ranges
    // (e.g. TEraseController which inverts the user-provided range).
    InferInputRanges();
    InitInputStreamDirectory();

    YT_LOG_INFO("Operation prepared");

    TOperationControllerPrepareResult result;
    FillPrepareResult(&result);
    return result;
}

TOperationControllerMaterializeResult TOperationControllerBase::SafeMaterialize()
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker());

    TOperationControllerMaterializeResult result;

    try {
        PeakMemoryUsageUpdateExecutor_->Start();

        // NB(coteeq): Allocate new chunk lists early, so we can actually schedule the first job,
        //             when scheduler gives us one.
        PickIntermediateDataCells();
        InitChunkListPools();

        YT_LOG_INFO(
            "Started fetching input tables (TableCount: %v)",
            InputManager_->GetInputTables().size());

        auto fetchInputTablesStatistics = InputManager_->FetchInputTables();

        YT_LOG_INFO("Finished fetching input tables (TotalChunkCount: %v, TotalExtensionSize: %v, MemoryUsage: %v)",
            fetchInputTablesStatistics.ChunkCount,
            fetchInputTablesStatistics.ExtensionSize,
            GetMemoryUsage());

        FetchUserFiles();
        ValidateUserFileSizes();

        SuppressLivePreviewIfNeeded();


        CreateLivePreviewTables();

        CollectTotals();

        InitializeJobExperiment();

        AlertManager_->StartPeriodicActivity();

        CustomMaterialize();

        InitializeHistograms();

        InitializeSecurityTags();

        YT_LOG_INFO("Tasks prepared (RowBufferCapacity: %v)", RowBuffer_->GetCapacity());

        if (IsCompleted()) {
            // Possible reasons:
            // - All input chunks are unavailable && Strategy == Skip
            // - Merge decided to teleport all input chunks
            // - Anything else?
            YT_LOG_INFO("No jobs needed");
            OnOperationCompleted(/*interrupted*/ false);
            return result;
        }

        UpdateAllTasks();

        if (Config_->TestingOptions->EnableSnapshotCycleAfterMaterialization) {
            TStringStream stringStream;
            SaveSnapshot(&stringStream);
            TOperationSnapshot snapshot;
            snapshot.Version = ToUnderlying(GetCurrentSnapshotVersion());
            snapshot.Blocks = {TSharedRef::FromString(stringStream.Str())};
            DoLoadSnapshot(snapshot);
            UpdateConfig(Config_);
            AlertManager_->StartPeriodicActivity();
        }

        InputManager_->RegisterUnavailableInputChunks(/*reportIfFound*/ true);
        InitIntermediateChunkScraper();

        UpdateGroupedNeededResources();
        // NB(eshcherbin): This update is done to ensure that needed resources amount is computed.
        UpdateAllTasks();

        CheckTimeLimitExecutor_->Start();
        ProgressBuildExecutor_->Start();
        ExecNodesCheckExecutor_->Start();
        SuspiciousJobsYsonUpdater_->Start();
        MinNeededResourcesSanityCheckExecutor_->Start();
        ExecNodesUpdateExecutor_->Start();
        CheckTentativeTreeEligibilityExecutor_->Start();
        UpdateAccountResourceUsageLeasesExecutor_->Start();
        RunningJobStatisticsUpdateExecutor_->Start();
        SendRunningAllocationTimeStatisticsUpdatesExecutor_->Start();

        SpecManager_->SetConfigurator(ConfigureUpdate());

        if (auto maybeDelay = Spec_->TestingOperationOptions->DelayInsideMaterialize) {
            TDelayedExecutor::WaitForDuration(*maybeDelay);
        }

        if (State_ != EControllerState::Preparing) {
            return result;
        }
        State_ = EControllerState::Running;

        LogProgress(/*force*/ true);
    } catch (const std::exception& ex) {
        auto wrappedError = TError(NControllerAgent::EErrorCode::MaterializationFailed, "Materialization failed")
            << ex;
        YT_LOG_INFO(wrappedError);
        DoFailOperation(wrappedError);
        return result;
    }

    InitialGroupedNeededResources_ = GetGroupedNeededResources();

    result.Suspend = Spec_->SuspendOperationAfterMaterialization;
    result.InitialNeededResources = GetNeededResources();
    result.InitialGroupedNeededResources = InitialGroupedNeededResources_;

    YT_LOG_INFO("Materialization finished");

    OnOperationReady();

    return result;
}

void TOperationControllerBase::SaveSnapshot(IZeroCopyOutput* output)
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    using NYT::Save;

    TSaveContext context(output);

    Save(context, NPhoenix::ITypeRegistry::Get()->GetUniverseDescriptor().GetSchemaYson());

    Save(context, this);

    context.Finish();
}

void TOperationControllerBase::SleepInRevive()
{
    if (auto delay = Spec_->TestingOperationOptions->DelayInsideRevive) {
        TDelayedExecutor::WaitForDuration(*delay);
    }
}

// COMPAT(pogorelov)
void TOperationControllerBase::ClearEmptyAllocationsInRevive()
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker());

    std::vector<THashMap<TAllocationId, TAllocation>::iterator> allocationIteratorsToErase;

    for (auto it = begin(AllocationMap_); it != end(AllocationMap_); ++it) {
        if (!it->second.Joblet) {
            allocationIteratorsToErase.push_back(it);
        }
    }

    for (auto it : allocationIteratorsToErase) {
        AllocationMap_.erase(it);
    }
}

TOperationControllerReviveResult TOperationControllerBase::Revive()
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker());

    // A fast path to stop revival if fail_on_job_restart = %true and
    // this is not a vanilla operation.
    ValidateRevivalAllowed();

    if (CleanStart_) {
        TOperationControllerReviveResult result;
        result.RevivedFromSnapshot = false;
        result.ControllerEpoch = ControllerEpoch_;
        static_cast<TOperationControllerPrepareResult&>(result) = Prepare();
        return result;
    }

    SleepInRevive();

    DoLoadSnapshot(Snapshot_);

    // Once again check that revival is allowed (now having the loaded snapshot).
    ValidateSnapshot();

    UpdateConfig(Config_);

    Snapshot_ = TOperationSnapshot();

    TOperationControllerReviveResult result;
    result.RevivedFromSnapshot = true;
    result.ControllerEpoch = ControllerEpoch_;
    result.RevivedBannedTreeIds = BannedTreeIds_;
    FillPrepareResult(&result);

    InitChunkListPools();

    SuppressLivePreviewIfNeeded();
    CreateLivePreviewTables();

    if (IsCompleted()) {
        OnOperationCompleted(/*interrupted*/ false);
        return result;
    }

    UpdateAllTasks();

    InputManager_->RegisterUnavailableInputChunks();
    InitIntermediateChunkScraper();

    if (UnavailableIntermediateChunkCount_ > 0) {
        IntermediateChunkScraper_->Start();
    }

    UpdateGroupedNeededResources();
    // NB(eshcherbin): This update is done to ensure that needed resources amount is computed.
    UpdateAllTasks();

    result.NeededResources = GetNeededResources();

    ReinstallLivePreview();

    if (!Config_->EnableJobRevival) {
        if (HasJobUniquenessRequirements() && RunningJobCount_ != 0) {
            OnJobUniquenessViolated(TError(
                NScheduler::EErrorCode::OperationFailedOnJobRestart,
                "Reviving operation without job revival; failing operation since \"fail_on_job_restart\" option is set in operation spec or user job spec")
                << TErrorAttribute("reason", EFailOnJobRestartReason::JobRevivalDisabled));
            return result;
        }

        AbortAllJoblets(EAbortReason::JobRevivalDisabled, /*honestly*/ true);
    }

    ShouldUpdateProgressAttributesInCypress_ = true;
    ShouldUpdateLightOperationAttributes_ = true;

    CheckTimeLimitExecutor_->Start();
    ProgressBuildExecutor_->Start();
    ExecNodesCheckExecutor_->Start();
    SuspiciousJobsYsonUpdater_->Start();
    AlertManager_->StartPeriodicActivity();
    MinNeededResourcesSanityCheckExecutor_->Start();
    ExecNodesUpdateExecutor_->Start();
    CheckTentativeTreeEligibilityExecutor_->Start();
    UpdateAccountResourceUsageLeasesExecutor_->Start();
    RunningJobStatisticsUpdateExecutor_->Start();
    SendRunningAllocationTimeStatisticsUpdatesExecutor_->Start();

    ClearEmptyAllocationsInRevive();

    result.RevivedAllocations.reserve(std::size(AllocationMap_));

    for (const auto& [_, allocation] : AllocationMap_) {
        const auto& joblet = allocation.Joblet;
        result.RevivedAllocations.push_back(TOperationControllerReviveResult::TRevivedAllocation{
            .AllocationId = AllocationIdFromJobId(joblet->JobId),
            .StartTime = joblet->StartTime,
            .PreemptibleProgressStartTime = joblet->NodeJobStartTime,
            .ResourceLimits = joblet->ResourceLimits,
            .DiskQuota = joblet->DiskQuota,
            .TreeId = joblet->TreeId,
            .NodeId = joblet->NodeDescriptor.Id,
            .NodeAddress = NNodeTrackerClient::GetDefaultAddress(joblet->NodeDescriptor.Addresses),
        });
    }

    result.GroupedNeededResources = GetGroupedNeededResources();
    result.InitialGroupedNeededResources = InitialGroupedNeededResources_;

    // Monitoring tags are transient by design.
    // So after revive we do reset the corresponding alert.
    SetOperationAlert(EOperationAlertType::UserJobMonitoringLimited, TError());

    SpecManager_->SetConfigurator(ConfigureUpdate());
    SpecManager_->ApplySpecPatchReviving();

    YT_LOG_INFO("Operation revived");

    State_ = EControllerState::Running;

    OnOperationReady();

    OnOperationRevived();

    return result;
}

void TOperationControllerBase::AbortAllJoblets(EAbortReason abortReason, bool honestly)
{
    YT_LOG_DEBUG("Aborting all joblets (AbortReason: %v)", abortReason);

    std::vector<TJobToRelease> jobsToRelease;
    jobsToRelease.reserve(std::size(AllocationMap_));

    auto now = TInstant::Now();
    for (const auto& [_, allocation] : AllocationMap_) {
        if (!allocation.Joblet) {
            continue;
        }
        const auto& joblet = allocation.Joblet;

        auto jobSummary = TAbortedJobSummary(joblet->JobId, abortReason);
        jobSummary.FinishTime = now;
        UpdateJobletFromSummary(jobSummary, joblet);
        LogFinishedJobFluently(ELogEventType::JobAborted, joblet)
            .Item("reason").Value(abortReason);
        UpdateAggregatedFinishedJobStatistics(joblet, jobSummary);

        GetJobProfiler()->ProfileAbortedJob(*joblet, jobSummary);

        if (honestly) {
            joblet->Task->OnJobAborted(joblet, jobSummary);
        }

        ReportControllerStateToArchive(joblet, EJobState::Aborted);

        Host_->AbortJob(
            joblet->JobId,
            abortReason,
            /*requestNewJob*/ false);

        jobsToRelease.push_back({joblet->JobId, {}});
    }
    AllocationMap_.clear();
    RunningJobCount_ = 0;

    CachedRunningJobs_.Flush();

    if (!std::empty(jobsToRelease)) {
        YT_LOG_DEBUG(
            "Releasing aborted jobs (JobCount: %v)",
            std::size(jobsToRelease));

        Host_->ReleaseJobs(std::move(jobsToRelease));
    }
}

bool TOperationControllerBase::IsTransactionNeeded(ETransactionType type) const
{
    switch (type) {
        case ETransactionType::Async:
            return IsLegacyIntermediateLivePreviewSupported() || IsLegacyOutputLivePreviewSupported() || GetStderrTablePath();
        case ETransactionType::Input:
            // Input transaction is managed by InputTransactionManager.
            YT_ABORT();
        case ETransactionType::Output:
        case ETransactionType::OutputCompletion:
            // NB: Cannot replace with OutputTables_.empty() here because output tables are not ready yet.
            return !GetOutputTablePaths().empty();
        case ETransactionType::Debug:
        case ETransactionType::DebugCompletion:
            return GetStderrTablePath() || GetCoreTablePath();
        default:
            YT_ABORT();
    }
}

ITransactionPtr TOperationControllerBase::AttachTransaction(
    TTransactionId transactionId,
    const NNative::IClientPtr& client,
    bool ping)
{
    TTransactionAttachOptions options;
    options.Ping = ping;
    options.PingAncestors = false;
    options.PingPeriod = Config_->OperationTransactionPingPeriod;
    return client->AttachTransaction(transactionId, options);
}

void TOperationControllerBase::StartTransactions()
{
    std::vector<TFuture<NNative::ITransactionPtr>> asyncResults = {
        StartTransaction(ETransactionType::Async, Client_),
        StartTransaction(ETransactionType::Output, OutputClient_, GetOutputTransactionParentId()),
        // NB: We do not start Debug transaction under User transaction since we want to save debug results
        // even if user transaction is aborted.
        StartTransaction(ETransactionType::Debug, Client_),
    };

    auto inputTransactionsReadyFuture = InputTransactions_->Start(
        CreateTransactionAttributes(ETransactionType::Input));

    auto results = WaitFor(AllSet(asyncResults))
        .ValueOrThrow();

    {
        AsyncTransaction_ = results[0].ValueOrThrow();
        OutputTransaction_ = results[1].ValueOrThrow();
        DebugTransaction_ = results[2].ValueOrThrow();
    }

    WaitFor(inputTransactionsReadyFuture).ThrowOnError();
}

void TOperationControllerBase::InitInputStreamDirectory()
{
    std::vector<NChunkPools::TInputStreamDescriptor> inputStreams;
    inputStreams.reserve(InputManager_->GetInputTables().size());
    for (const auto& [tableIndex, inputTable] : Enumerate(InputManager_->GetInputTables())) {
        for (const auto& [rangeIndex, range] : Enumerate(inputTable->Path.GetRanges())) {
            auto& descriptor = inputStreams.emplace_back(
                inputTable->Teleportable,
                inputTable->IsPrimary(),
                /*isVersioned*/ inputTable->Dynamic);
            descriptor.SetTableIndex(tableIndex);
            descriptor.SetRangeIndex(rangeIndex);
        }
    }
    InputStreamDirectory_ = TInputStreamDirectory(std::move(inputStreams));

    YT_LOG_INFO("Input stream directory prepared (InputStreamCount: %v)", InputStreamDirectory_.GetDescriptorCount());
}

const TInputStreamDirectory& TOperationControllerBase::GetInputStreamDirectory() const
{
    return InputStreamDirectory_;
}

int TOperationControllerBase::GetPrimaryInputTableCount() const
{
    return std::count_if(
        InputManager_->GetInputTables().begin(),
        InputManager_->GetInputTables().end(),
        [] (const TInputTablePtr& table) { return table->IsPrimary(); });
}

TTransactionId TOperationControllerBase::GetInputTransactionParentId()
{
    return UserTransactionId_;
}

TTransactionId TOperationControllerBase::GetOutputTransactionParentId()
{
    return UserTransactionId_;
}

TAutoMergeDirector* TOperationControllerBase::GetAutoMergeDirector()
{
    return AutoMergeDirector_.get();
}

TFuture<NNative::ITransactionPtr> TOperationControllerBase::StartTransaction(
    ETransactionType type,
    const NNative::IClientPtr& client,
    TTransactionId parentTransactionId)
{
    if (!IsTransactionNeeded(type)) {
        YT_LOG_INFO("Skipping transaction as it is not needed (Type: %v)", type);
        return MakeFuture(NNative::ITransactionPtr());
    }

    auto collectTableCellTags = [] (const std::vector<TOutputTablePtr>& tables) {
        TCellTagList result;

        for (const auto& table : tables) {
            if (!table) {
                continue;
            }

            if (table->ExternalCellTag == NObjectClient::InvalidCellTag) {
                result.push_back(CellTagFromId(table->ObjectId));
            } else {
                result.push_back(table->ExternalCellTag);
            }

            for (const auto& [chunkStripeKey, id] : table->OutputChunkTreeIds) {
                if (TypeFromId(id) == EObjectType::ChunkList) {
                    continue;
                }
                result.push_back(CellTagFromId(id));
            }
        }

        return result;
    };

    TCellTagList replicateToCellTags;
    switch (type) {
        // NB: These transactions are started when no basic attributes have been
        // fetched yet and collecting cell tags is therefore useless.
        case ETransactionType::Async:
        case ETransactionType::Input:
        case ETransactionType::Output:
        case ETransactionType::Debug:
            break;

        case ETransactionType::OutputCompletion:
            replicateToCellTags = collectTableCellTags(OutputTables_);
            break;

        case ETransactionType::DebugCompletion:
            replicateToCellTags = collectTableCellTags({StderrTable_, CoreTable_});
            break;

        default:
            YT_ABORT();
    }
    SortUnique(replicateToCellTags);

    YT_LOG_INFO("Starting transaction (Type: %v, ParentId: %v, ReplicateToCellTags: %v)",
        type,
        parentTransactionId,
        replicateToCellTags);

    TTransactionStartOptions options;
    options.AutoAbort = false;
    options.PingAncestors = false;
    options.Attributes = CreateTransactionAttributes(type);
    options.ParentId = parentTransactionId;
    options.Timeout = Config_->OperationTransactionTimeout;
    options.PingPeriod = Config_->OperationTransactionPingPeriod;
    options.ReplicateToMasterCellTags = std::move(replicateToCellTags);

    auto transactionFuture = client->StartNativeTransaction(
        NTransactionClient::ETransactionType::Master,
        options);

    return transactionFuture.Apply(BIND([=, this, this_ = MakeStrong(this)] (const TErrorOr<NNative::ITransactionPtr>& transactionOrError) {
        THROW_ERROR_EXCEPTION_IF_FAILED(
            transactionOrError,
            "Error starting %Qlv transaction",
            type);

        auto transaction = transactionOrError.Value();

        YT_LOG_INFO("Transaction started (Type: %v, TransactionId: %v)",
            type,
            transaction->GetId());

        return transaction;
    }));
}

TFuture<void> TOperationControllerBase::AbortInputTransactions() const
{
    if (InputTransactions_) {
        return InputTransactions_->Abort(SchedulerInputClient_);
    }
    return VoidFuture;
}

void TOperationControllerBase::PickIntermediateDataCells()
{
    if (GetOutputTablePaths().empty()) {
        return;
    }

    WaitForFast(OutputClient_
        ->GetNativeConnection()
        ->GetMasterCellDirectorySynchronizer()
        ->RecentSync())
        .ThrowOnError();

    IntermediateOutputCellTagList_ = OutputClient_
        ->GetNativeConnection()
        ->GetMasterCellDirectory()
        ->GetMasterCellTagsWithRole(NCellMasterClient::EMasterCellRole::ChunkHost);
    if (IntermediateOutputCellTagList_.empty()) {
        THROW_ERROR_EXCEPTION("No master cells with chunk host role found");
    }

    int intermediateDataCellCount = std::min<int>(Config_->IntermediateOutputMasterCellCount, IntermediateOutputCellTagList_.size());
    // TODO(max42, gritukan): Remove it when new live preview will be ready.
    if (IsLegacyIntermediateLivePreviewSupported()) {
        intermediateDataCellCount = 1;
    }

    PartialShuffle(
        IntermediateOutputCellTagList_.begin(),
        IntermediateOutputCellTagList_.begin() + intermediateDataCellCount,
        IntermediateOutputCellTagList_.end());
    IntermediateOutputCellTagList_.resize(intermediateDataCellCount);

    YT_LOG_DEBUG("Intermediate data cells picked (CellTags: %v)",
        IntermediateOutputCellTagList_);
}

void TOperationControllerBase::InitChunkListPools()
{
    if (!GetOutputTablePaths().empty()) {
        OutputChunkListPool_ = New<TChunkListPool>(
            Config_,
            OutputClient_,
            CancelableInvokerPool_,
            OperationId_,
            OutputTransaction_->GetId());

        CellTagToRequiredOutputChunkListCount_.clear();
        for (const auto& table : OutputTables_) {
            ++CellTagToRequiredOutputChunkListCount_[table->ExternalCellTag];
        }

        for (auto cellTag : IntermediateOutputCellTagList_) {
            ++CellTagToRequiredOutputChunkListCount_[cellTag];
        }
    }

    if (DebugTransaction_) {
        DebugChunkListPool_ = New<TChunkListPool>(
            Config_,
            OutputClient_,
            CancelableInvokerPool_,
            OperationId_,
            DebugTransaction_->GetId());
    }

    CellTagToRequiredDebugChunkListCount_.clear();
    if (StderrTable_) {
        ++CellTagToRequiredDebugChunkListCount_[StderrTable_->ExternalCellTag];
    }
    if (CoreTable_) {
        ++CellTagToRequiredDebugChunkListCount_[CoreTable_->ExternalCellTag];
    }

    YT_LOG_DEBUG("Preallocating chunk lists");
    for (const auto& [cellTag, count] : CellTagToRequiredOutputChunkListCount_) {
        Y_UNUSED(OutputChunkListPool_->HasEnough(cellTag, count));
    }
    for (const auto& [cellTag, count] : CellTagToRequiredDebugChunkListCount_) {
        YT_VERIFY(DebugChunkListPool_);
        Y_UNUSED(DebugChunkListPool_->HasEnough(cellTag, count));
    }
}

void TOperationControllerBase::InitIntermediateChunkScraper()
{
    // NB(arkady-e1ppa):
    // invoker and invokerPool are used only for the interaction with controller.
    // Heavy job of the ChunkScraper is performed in separate scraper invoker.
    IntermediateChunkScraper_ = New<TIntermediateChunkScraper>(
        Config_->ChunkScraper,
        GetCancelableInvoker(),
        CancelableInvokerPool_,
        ChunkScraperInvoker_,
        Host_->GetChunkLocationThrottlerManager(),
        OutputClient_,
        OutputNodeDirectory_,
        BIND_NO_PROPAGATE([this, weakThis = MakeWeak(this)] {
            if (auto this_ = weakThis.Lock()) {
                return GetAliveIntermediateChunks();
            } else {
                return THashSet<TChunkId>();
            }
        }),
        BIND_NO_PROPAGATE(&TThis::OnIntermediateChunkBatchLocated, MakeWeak(this))
            .Via(GetCancelableInvoker()),
        Logger);
}

bool TOperationControllerBase::TryInitAutoMerge(int outputChunkCountEstimate)
{
    AutoMergeEnabled_.resize(OutputTables_.size(), false);

    const auto& autoMergeSpec = Spec_->AutoMerge;
    auto mode = autoMergeSpec->Mode;

    if (mode == EAutoMergeMode::Disabled) {
        return false;
    }

    auto autoMergeError = GetAutoMergeError();
    if (!autoMergeError.IsOK()) {
        SetOperationAlert(EOperationAlertType::AutoMergeDisabled, autoMergeError);
        return false;
    }

    i64 maxIntermediateChunkCount;
    i64 chunkCountPerMergeJob;
    switch (mode) {
        case EAutoMergeMode::Relaxed:
            maxIntermediateChunkCount = std::numeric_limits<int>::max() / 4;
            chunkCountPerMergeJob = 500;
            break;
        case EAutoMergeMode::Economy:
            maxIntermediateChunkCount = std::max(500, static_cast<int>(2.5 * sqrt(outputChunkCountEstimate)));
            chunkCountPerMergeJob = maxIntermediateChunkCount / 10;
            maxIntermediateChunkCount *= OutputTables_.size();
            break;
        case EAutoMergeMode::Manual:
            maxIntermediateChunkCount = *autoMergeSpec->MaxIntermediateChunkCount;
            chunkCountPerMergeJob = *autoMergeSpec->ChunkCountPerMergeJob;
            break;
        default:
            YT_ABORT();
    }

    YT_VERIFY(EstimatedInputStatistics_);
    const i64 desiredChunkSize = autoMergeSpec->JobIO->TableWriter->DesiredChunkSize;
    const i64 maxChunkDataWeight = std::max<i64>(std::min(autoMergeSpec->JobIO->TableWriter->DesiredChunkWeight / 2, Spec_->MaxDataWeightPerJob / 2), 1);
    const i64 desiredChunkDataWeight = std::clamp<i64>(SignedSaturationConversion(desiredChunkSize / EstimatedInputStatistics_->CompressionRatio), 1, maxChunkDataWeight);
    const i64 dataWeightPerJob = desiredChunkDataWeight;

    // NB: If row count limit is set on any output table, we do not
    // enable auto merge as it prematurely stops the operation
    // because wrong statistics are currently used when checking row count.
    for (int index = 0; index < std::ssize(OutputTables_); ++index) {
        if (OutputTables_[index]->Path.GetRowCountLimit()) {
            YT_LOG_INFO("Output table has row count limit, force disabling auto merge (TableIndex: %v)", index);
            auto error = TError("Output table has row count limit, force disabling auto merge")
                << TErrorAttribute("table_index", index);
            SetOperationAlert(EOperationAlertType::AutoMergeDisabled, error);
            return false;
        }
    }

    YT_LOG_INFO("Auto merge parameters calculated ("
        "Mode: %v, OutputChunkCountEstimate: %v, MaxIntermediateChunkCount: %v, ChunkCountPerMergeJob: %v, "
        "ChunkSizeThreshold: %v, DesiredChunkSize: %v, DesiredChunkDataWeight: %v, MaxChunkDataWeight: %v, IntermediateChunkUnstageMode: %v)",
        mode,
        outputChunkCountEstimate,
        maxIntermediateChunkCount,
        chunkCountPerMergeJob,
        autoMergeSpec->ChunkSizeThreshold,
        desiredChunkSize,
        desiredChunkDataWeight,
        maxChunkDataWeight,
        GetIntermediateChunkUnstageMode());

    AutoMergeDirector_ = std::make_unique<TAutoMergeDirector>(
        maxIntermediateChunkCount,
        chunkCountPerMergeJob,
        Logger);

    bool sortedOutputAutoMergeRequired = false;

    const auto standardStreamDescriptors = GetStandardStreamDescriptors();

    std::vector<TOutputStreamDescriptorPtr> outputStreamDescriptors;
    outputStreamDescriptors.reserve(OutputTables_.size());
    for (int index = 0; index < std::ssize(OutputTables_); ++index) {
        const auto& outputTable = OutputTables_[index];
        if (outputTable->Path.GetAutoMerge()) {
            if (outputTable->TableUploadOptions.TableSchema->IsSorted()) {
                sortedOutputAutoMergeRequired = true;
            } else {
                auto streamDescriptor = standardStreamDescriptors[index]->Clone();
                // Auto-merge jobs produce single output, so we override the table
                // index in writer options with 0.
                streamDescriptor->TableWriterOptions = CloneYsonStruct(streamDescriptor->TableWriterOptions);
                streamDescriptor->TableWriterOptions->TableIndex = 0;
                outputStreamDescriptors.push_back(std::move(streamDescriptor));
                AutoMergeEnabled_[index] = true;
            }
        }
    }

    bool autoMergeEnabled = !outputStreamDescriptors.empty();
    if (autoMergeEnabled) {
        AutoMergeTask_ = New<TAutoMergeTask>(
            /*taskHost*/ this,
            chunkCountPerMergeJob,
            autoMergeSpec->ChunkSizeThreshold,
            maxChunkDataWeight,
            dataWeightPerJob,
            std::move(outputStreamDescriptors),
            std::vector<TInputStreamDescriptorPtr>{});
        RegisterTask(AutoMergeTask_);
    }

    if (sortedOutputAutoMergeRequired && !autoMergeEnabled) {
        auto error = TError("Sorted output with auto merge is not supported for now, it will be done in YT-8024");
        SetOperationAlert(EOperationAlertType::AutoMergeDisabled, error);
    }

    return autoMergeEnabled;
}

const NLogging::TLogger& TOperationControllerBase::GetLogger() const
{
    return Logger;
}

IYPathServicePtr TOperationControllerBase::BuildZombieOrchid()
{
    IYPathServicePtr orchid;
    if (auto controllerOrchid = GetOrchid()) {
        auto ysonOrError = WaitFor(AsyncYPathGet(controllerOrchid, ""));
        if (!ysonOrError.IsOK()) {
            return nullptr;
        }
        auto yson = ysonOrError.Value();
        if (!yson) {
            return nullptr;
        }
        auto producer = TYsonProducer(BIND([yson = std::move(yson)] (IYsonConsumer* consumer) {
            consumer->OnRaw(yson);
        }));
        orchid = IYPathService::FromProducer(std::move(producer))
            ->Via(Host_->GetControllerThreadPoolInvoker());
    }
    return orchid;
}

std::vector<TOutputStreamDescriptorPtr> TOperationControllerBase::GetAutoMergeStreamDescriptors()
{
    auto streamDescriptors = GetStandardStreamDescriptors();
    YT_VERIFY(GetAutoMergeDirector());

    std::optional<std::string> intermediateDataAccount;
    if (Spec_->AutoMerge->UseIntermediateDataAccount) {
        ValidateAccountPermission(Spec_->IntermediateDataAccount, EPermission::Use);
        intermediateDataAccount = Spec_->IntermediateDataAccount;
    }

    YT_VERIFY(std::ssize(streamDescriptors) <= std::ssize(AutoMergeEnabled_));

    int autoMergeTaskTableIndex = 0;
    for (int index = 0; index < std::ssize(streamDescriptors); ++index) {
        if (AutoMergeEnabled_[index]) {
            streamDescriptors[index] = streamDescriptors[index]->Clone();
            streamDescriptors[index]->DestinationPool = AutoMergeTask_->GetChunkPoolInput();
            streamDescriptors[index]->ChunkMapping = AutoMergeTask_->GetChunkMapping();
            streamDescriptors[index]->ImmediatelyUnstageChunkLists = true;
            streamDescriptors[index]->RequiresRecoveryInfo = true;
            streamDescriptors[index]->IsFinalOutput = false;
            // NB. The vertex descriptor for auto merge task must be empty, as TAutoMergeTask builds both input
            // and output edges. The underlying operation must not build an output edge, as it doesn't know
            // whether the resulting vertex is shallow_auto_merge or auto_merge.
            streamDescriptors[index]->TargetDescriptor = TDataFlowGraph::TVertexDescriptor();
            streamDescriptors[index]->PartitionTag = autoMergeTaskTableIndex++;
            if (intermediateDataAccount) {
                streamDescriptors[index]->TableWriterOptions->Account = *intermediateDataAccount;
            }
        }
    }
    return streamDescriptors;
}

THashSet<TChunkId> TOperationControllerBase::GetAliveIntermediateChunks() const
{
    THashSet<TChunkId> intermediateChunks;

    for (const auto& [chunkId, job] : ChunkOriginMap_) {
        if (!job->Suspended || !job->Restartable) {
            intermediateChunks.insert(chunkId);
        }
    }

    return intermediateChunks;
}

void TOperationControllerBase::ReinstallLivePreview()
{
    if (IsLegacyOutputLivePreviewSupported()) {
        for (const auto& table : OutputTables_) {
            std::vector<TChunkTreeId> childIds;
            childIds.reserve(table->OutputChunkTreeIds.size());
            for (const auto& [key, chunkTreeId] : table->OutputChunkTreeIds) {
                childIds.push_back(chunkTreeId);
            }
            YT_UNUSED_FUTURE(Host_->AttachChunkTreesToLivePreview(
                AsyncTransaction_->GetId(),
                table->LivePreviewTableId,
                childIds));
        }
    }

    if (IsLegacyIntermediateLivePreviewSupported()) {
        std::vector<TChunkTreeId> childIds;
        childIds.reserve(ChunkOriginMap_.size());
        for (const auto& [chunkId, job] : ChunkOriginMap_) {
            if (!job->Suspended) {
                childIds.push_back(chunkId);
            }
        }
        YT_UNUSED_FUTURE(Host_->AttachChunkTreesToLivePreview(
            AsyncTransaction_->GetId(),
            IntermediateTable_->LivePreviewTableId,
            childIds));
    }
}

void TOperationControllerBase::DoLoadSnapshot(const TOperationSnapshot& snapshot)
{
    YT_LOG_INFO("Started loading snapshot (Size: %v, BlockCount: %v, Version: %v)",
        GetByteSize(snapshot.Blocks),
        snapshot.Blocks.size(),
        snapshot.Version);

    // Deserialization errors must be fatal.
    TCrashOnDeserializationErrorGuard crashOnDeserializationErrorGuard;

    // Snapshot loading must be synchronous.
    TOneShotContextSwitchGuard oneShotContextSwitchGuard(
        BIND([this, this_ = MakeStrong(this)] {
            TStringBuilder stackTrace;
            DumpStackTrace([&stackTrace] (TStringBuf str) {
                stackTrace.AppendString(str);
            });
            YT_LOG_WARNING("Context switch while loading snapshot (StackTrace: %v)",
                stackTrace.Flush());
        }));

    TChunkedInputStream input(snapshot.Blocks);

    TLoadContext context(
        &input,
        RowBuffer_,
        static_cast<ESnapshotVersion>(snapshot.Version));

    std::optional<NPhoenix::TLoadSessionGuard> phoenixLoadSessionGuard;
    if (context.GetVersion() >= ESnapshotVersion::PhoenixSchema) {
        auto schemaYson = Load<TYsonString>(context);
        auto schema = ConvertTo<NPhoenix::TUniverseSchemaPtr>(schemaYson);
        if (GetConfig()->EnableSnapshotPhoenixSchemaDuringSnapshotLoading) {
            phoenixLoadSessionGuard.emplace(std::move(schema));
        }
    }

    NPhoenix::NDetail::TSerializer::InplaceLoad(context, this);

    for (const auto& task : Tasks_) {
        task->Initialize();
    }
    InitializeOrchid();

    YT_LOG_INFO("Finished loading snapshot");
}

void TOperationControllerBase::StartOutputCompletionTransaction()
{
    if (!OutputTransaction_) {
        return;
    }

    OutputCompletionTransaction_ = WaitFor(StartTransaction(
        ETransactionType::OutputCompletion,
        OutputClient_,
        OutputTransaction_->GetId()))
        .ValueOrThrow();

    // Set transaction id to Cypress.
    {
        const auto& client = Host_->GetClient();
        auto proxy = CreateObjectServiceWriteProxy(client);

        auto path = GetOperationPath(OperationId_) + "/@output_completion_transaction_id";
        auto req = TYPathProxy::Set(path);
        req->set_value(ToProto(ConvertToYsonStringNestingLimited(OutputCompletionTransaction_->GetId())));
        WaitFor(proxy.Execute(req))
            .ThrowOnError();
    }
}

void TOperationControllerBase::CommitOutputCompletionTransaction()
{
    auto outputCompletionTransactionId = OutputCompletionTransaction_
        ? OutputCompletionTransaction_->GetId()
        : NullTransactionId;

    YT_LOG_INFO("Committing output completion transaction and setting committed attribute (TransactionId: %v)",
        outputCompletionTransactionId);

    auto setCommittedViaCypressTransactionAction = GetConfig()->SetCommittedAttributeViaTransactionAction;

    auto fetchAttributeAsObjectId = [&, this] (
        const TYPath& path,
        const TString& attribute,
        TTransactionId transactionId = NullTransactionId)
    {
        auto getRequest = TYPathProxy::Get(Format("%v/@%v", path, attribute));
        if (transactionId != NullTransactionId) {
            SetTransactionId(getRequest, transactionId);
        }

        auto proxy = CreateObjectServiceReadProxy(
            Host_->GetClient(),
            EMasterChannelKind::Follower);
        auto getResponse = WaitFor(proxy.Execute(getRequest))
            .ValueOrThrow();

        return ConvertTo<TObjectId>(TYsonString(getResponse->value()));
    };

    auto operationCypressNodeId = fetchAttributeAsObjectId(
        GetOperationPath(OperationId_),
        IdAttributeName,
        outputCompletionTransactionId);
    if (setCommittedViaCypressTransactionAction && OutputCompletionTransaction_) {
        // NB: Transaction action cannot be executed on node's native cell
        // directly because it leads to distributed transaction commit which
        // cannot be done with prerequisites.

        NNative::NProto::TReqSetAttributeOnTransactionCommit action;
        ToProto(action.mutable_node_id(), operationCypressNodeId);
        action.set_attribute(CommittedAttribute);
        action.set_value(ToProto(ConvertToYsonStringNestingLimited(true)));

        auto transactionCoordinatorCellTag = CellTagFromId(OutputCompletionTransaction_->GetId());
        auto connection = Client_->GetNativeConnection();
        OutputCompletionTransaction_->AddAction(
            connection->GetMasterCellId(transactionCoordinatorCellTag),
            MakeTransactionActionData(action));
    } else {
        auto proxy = CreateObjectServiceWriteProxy(Host_->GetClient());

        auto path = GetOperationPath(OperationId_) + "/@" + CommittedAttribute;
        auto req = TYPathProxy::Set(path);
        SetTransactionId(req, outputCompletionTransactionId);
        req->set_value(ToProto(ConvertToYsonStringNestingLimited(true)));
        WaitFor(proxy.Execute(req))
            .ThrowOnError();
    }

    if (OutputCompletionTransaction_) {
        // NB: Every set to `@committed` acquires lock which is promoted to
        // user's transaction on scheduler's transaction commit. To avoid this
        // we manually merge branched node and detach it from transaction.

        std::optional<TTransactionId> parentTransactionId;
        if (Config_->CommitOperationCypressNodeChangesViaSystemTransaction &&
            !setCommittedViaCypressTransactionAction)
        {
            parentTransactionId = fetchAttributeAsObjectId(
                FromObjectId(outputCompletionTransactionId),
                ParentIdAttributeName);
        }

        TTransactionCommitOptions options;
        options.PrerequisiteTransactionIds.push_back(IncarnationIdToTransactionId(Host_->GetIncarnationId()));

        WaitFor(OutputCompletionTransaction_->Commit(options))
            .ThrowOnError();
        OutputCompletionTransaction_.Reset();

        if (parentTransactionId) {
            ManuallyMergeBranchedCypressNode(operationCypressNodeId, *parentTransactionId);
        }

        if (Config_->TestingOptions->AbortOutputTransactionAfterCompletionTransactionCommit) {
            TTransactionAbortOptions options;
            options.Force = true;
            WaitFor(OutputTransaction_->Abort(options))
                .ThrowOnError();
        }
    }

    CommitFinished_ = true;

    YT_LOG_INFO("Output completion transaction committed and committed attribute set (TransactionId: %v)",
        outputCompletionTransactionId);
}

void TOperationControllerBase::ManuallyMergeBranchedCypressNode(
    NCypressClient::TNodeId nodeId,
    TTransactionId transactionId)
{
    YT_LOG_DEBUG(
        "Trying to merge operation Cypress node manually to reduce transaction lock count "
        "(CypressNodeId: %v, TransactionId: %v, OperationId: %v)",
        nodeId,
        transactionId,
        OperationId_);

    try {
        auto targetCellTag = CellTagFromId(nodeId);

        if (auto coordinatorCellTag = CellTagFromId(transactionId); coordinatorCellTag != targetCellTag) {
            // Output completion transaction should become committed on node's
            // native cell.
            auto channel = Host_->GetClient()->GetMasterChannelOrThrow(
                EMasterChannelKind::Leader,
                targetCellTag);
            auto proxy = NHiveClient::THiveServiceProxy(std::move(channel));
            auto request = proxy.SyncWithOthers();
            ToProto(
                request->add_src_cell_ids(),
                Client_->GetNativeConnection()->GetMasterCellId(coordinatorCellTag));
            WaitFor(request->Invoke())
                .ThrowOnError();
        }

        // It is just a way to run custom logic at master.
        // Note that output completion transaction cannot be used here because
        // it's a Cypress transaction while a system is required to run
        // transaction actions.
        auto helperTransaction = WaitFor(Host_->GetClient()->StartNativeTransaction(
            NTransactionClient::ETransactionType::Master,
            {
                .CoordinatorMasterCellTag = targetCellTag,
                .StartCypressTransaction = false,
            }))
            .ValueOrThrow();
        NNative::NProto::TReqMergeToTrunkAndUnlockNode reqCommitBranchNode;
        ToProto(reqCommitBranchNode.mutable_transaction_id(), transactionId);
        ToProto(reqCommitBranchNode.mutable_node_id(), nodeId);
        helperTransaction->AddAction(
            Client_->GetNativeConnection()->GetMasterCellId(targetCellTag),
            MakeTransactionActionData(reqCommitBranchNode));

        TTransactionCommitOptions options;
        options.PrerequisiteTransactionIds = {transactionId};
        WaitFor(helperTransaction->Commit(options))
            .ThrowOnError();
    } catch (const std::exception& ex) {
        YT_LOG_ERROR(ex,
            "Failed to manually merge branched operation Cypress node (CypressNodeId: %v, TransactionId: %v, OperationId: %v)",
            nodeId,
            transactionId,
            OperationId_);
    }
}

void TOperationControllerBase::StartDebugCompletionTransaction()
{
    if (!DebugTransaction_) {
        return;
    }

    DebugCompletionTransaction_ = WaitFor(StartTransaction(
        ETransactionType::DebugCompletion,
        OutputClient_,
        DebugTransaction_->GetId()))
        .ValueOrThrow();

    // Set transaction id to Cypress.
    {
        auto proxy = CreateObjectServiceWriteProxy(Host_->GetClient());

        auto path = GetOperationPath(OperationId_) + "/@debug_completion_transaction_id";
        auto req = TYPathProxy::Set(path);
        req->set_value(ToProto(ConvertToYsonStringNestingLimited(DebugCompletionTransaction_->GetId())));
        WaitFor(proxy.Execute(req))
            .ThrowOnError();
    }
}

void TOperationControllerBase::CommitDebugCompletionTransaction()
{
    if (!DebugTransaction_) {
        return;
    }

    auto debugCompletionTransactionId = DebugCompletionTransaction_->GetId();

    YT_LOG_INFO("Committing debug completion transaction (TransactionId: %v)",
        debugCompletionTransactionId);

    TTransactionCommitOptions options;
    options.PrerequisiteTransactionIds.push_back(IncarnationIdToTransactionId(Host_->GetIncarnationId()));
    WaitFor(DebugCompletionTransaction_->Commit(options))
        .ThrowOnError();
    DebugCompletionTransaction_.Reset();

    YT_LOG_INFO("Debug completion transaction committed (TransactionId: %v)",
        debugCompletionTransactionId);
}

void TOperationControllerBase::SleepInCommitStage(EDelayInsideOperationCommitStage desiredStage)
{
    auto delay = Spec_->TestingOperationOptions->DelayInsideOperationCommit;
    auto stage = Spec_->TestingOperationOptions->DelayInsideOperationCommitStage;
    auto skipOnSecondEntrance = Spec_->TestingOperationOptions->NoDelayOnSecondEntranceToCommit;

    {
        auto proxy = CreateObjectServiceWriteProxy(Host_->GetClient());

        auto path = GetOperationPath(OperationId_) + "/@testing";
        auto req = TYPathProxy::Get(path);
        auto rspOrError = WaitFor(proxy.Execute(req));
        if (rspOrError.IsOK()) {
            auto rspNode = ConvertToNode(NYson::TYsonString(rspOrError.ValueOrThrow()->value()));
            CommitSleepStarted_ = rspNode->AsMap()->GetChildValueOrThrow<bool>("commit_sleep_started");
        }
    }

    if (delay && stage && *stage == desiredStage && (!CommitSleepStarted_ || !skipOnSecondEntrance)) {
        CommitSleepStarted_ = true;
        TDelayedExecutor::WaitForDuration(*delay);
    }
}

i64 TOperationControllerBase::GetPartSize(EOutputTableType tableType)
{
    if (tableType == EOutputTableType::Stderr) {
        return GetStderrTableWriterConfig()->MaxPartSize;
    }
    if (tableType == EOutputTableType::Core) {
        return GetCoreTableWriterConfig()->MaxPartSize;
    }

    YT_ABORT();
}

void TOperationControllerBase::BuildFeatureYson(TFluentAny fluent) const
{
    fluent.BeginList()
        .Item().Value(ControllerFeatures_)
        .DoFor(Tasks_, [] (TFluentList fluent, const TTaskPtr& task) {
            fluent.Item().Do(BIND(&TTask::BuildFeatureYson, task));
        })
    .EndList();
}

void TOperationControllerBase::CommitFeatures()
{
    LogStructuredEventFluently(ControllerFeatureStructuredLogger(), NLogging::ELogLevel::Info)
        .Item("operation_id").Value(ToString(GetOperationId()))
        .Item("start_time").Value(StartTime_)
        .Item("finish_time").Value(FinishTime_)
        .Item("experiment_assignment_names").DoListFor(
            ExperimentAssignments_,
            [] (TFluentList fluent, const TExperimentAssignmentPtr& experiment) {
                fluent.Item().Value(experiment->GetName());
            })
        .Item("features").Do(
            BIND(&TOperationControllerBase::BuildFeatureYson, Unretained(this)));

    auto featureYson = BuildYsonStringFluently().Do(
        BIND(&TOperationControllerBase::BuildFeatureYson, Unretained(this)));
    ValidateYson(featureYson, GetYsonNestingLevelLimit());

    WaitFor(Host_->UpdateControllerFeatures(featureYson))
        .ThrowOnError();
}

void TOperationControllerBase::FinalizeFeatures()
{
    FinishTime_ = TInstant::Now();

    for (const auto& task : Tasks_) {
        task->FinalizeFeatures();
    }

    auto inputStatisticsOrZeros = EstimatedInputStatistics_.value_or(TInputStatistics());
    ControllerFeatures_.AddTag("authenticated_user", GetAuthenticatedUser());
    ControllerFeatures_.AddTag("operation_type", GetOperationType());
    ControllerFeatures_.AddTag("total_estimated_input_data_weight", inputStatisticsOrZeros.DataWeight);
    ControllerFeatures_.AddTag("total_estimated_input_row_count", inputStatisticsOrZeros.RowCount);
    ControllerFeatures_.AddTag("total_estimated_input_value_count", inputStatisticsOrZeros.ValueCount);
    ControllerFeatures_.AddTag("total_estimated_input_chunk_count", inputStatisticsOrZeros.ChunkCount);
    ControllerFeatures_.AddTag("total_estimated_input_compressed_data_size", inputStatisticsOrZeros.CompressedDataSize);
    ControllerFeatures_.AddTag("total_estimated_input_uncompressed_data_size", inputStatisticsOrZeros.UncompressedDataSize);
    ControllerFeatures_.AddTag("total_job_count", GetTotalJobCount());

    ControllerFeatures_.AddSingular("operation_count", 1);
    ControllerFeatures_.AddSingular("wall_time", (FinishTime_ - StartTime_).MilliSeconds());
    ControllerFeatures_.AddSingular("peak_controller_memory_usage", PeakMemoryUsage_);

    ControllerFeatures_.AddSingular(
        "job_count",
        BuildYsonNodeFluently().Value(GetTotalJobCounter()));
}

void TOperationControllerBase::FinalizeSubscriptions()
{
    for (const auto& task : Tasks_) {
        task->FinalizeSubscriptions();
    }
}

void TOperationControllerBase::SafeCommit()
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker());

    YT_LOG_INFO("Committing results");

    SleepInCommitStage(EDelayInsideOperationCommitStage::Start);

    RemoveRemainingJobsOnOperationFinished();

    FinalizeSubscriptions();

    FinalizeFeatures();

    StartOutputCompletionTransaction();
    StartDebugCompletionTransaction();

    SleepInCommitStage(EDelayInsideOperationCommitStage::Stage1);
    BeginUploadOutputTables(UpdatingTables_);
    SleepInCommitStage(EDelayInsideOperationCommitStage::Stage2);
    TeleportOutputChunks();
    SleepInCommitStage(EDelayInsideOperationCommitStage::Stage3);
    AttachOutputChunks(UpdatingTables_);
    SleepInCommitStage(EDelayInsideOperationCommitStage::Stage4);
    EndUploadOutputTables(UpdatingTables_);
    SleepInCommitStage(EDelayInsideOperationCommitStage::Stage5);

    CustomCommit();
    CommitFeatures();

    if (Config_->RegisterLockableDynamicTables) {
        THashMap<TCellTag, std::vector<TTableId>> lockableOutputDynamicTables;
        ForEachLockableDynamicTable([&lockableOutputDynamicTables] (const TOutputTablePtr& table) {
            lockableOutputDynamicTables[table->ExternalCellTag].push_back(table->ObjectId);
        });
        RegisterLockableDynamicTables(lockableOutputDynamicTables);
    } else {
        THashMap<TCellTag, std::vector<TLockableDynamicTable>> lockableOutputDynamicTables;
        ForEachLockableDynamicTable([&lockableOutputDynamicTables] (const TOutputTablePtr& table) {
            lockableOutputDynamicTables[table->ExternalCellTag].push_back(TLockableDynamicTable{
                .TableId = table->ObjectId,
                .ExternalTransactionId = table->ExternalTransactionId,
            });
        });
        LockDynamicTables(
            std::move(lockableOutputDynamicTables),
            OutputClient_->GetNativeConnection(),
            Config_->BulkInsertLockChecker,
            Logger);
    }

    CommitOutputCompletionTransaction();
    CommitDebugCompletionTransaction();
    SleepInCommitStage(EDelayInsideOperationCommitStage::Stage6);
    CommitTransactions();

    CancelableContext_->Cancel(TError("Operation committed"));

    YT_LOG_INFO("Results committed");
}

void TOperationControllerBase::RegisterLockableDynamicTables(
    const THashMap<TCellTag, std::vector<TTableId>>& lockableOutputDynamicTables)
{
    YT_VERIFY(Config_->RegisterLockableDynamicTables);

    if (lockableOutputDynamicTables.empty() || !OutputTransaction_) {
        return;
    }

    auto transactionCoordinatorCellTag = CellTagFromId(OutputTransaction_->GetId());
    auto channel = OutputClient_->GetMasterChannelOrThrow(
        EMasterChannelKind::Leader,
        transactionCoordinatorCellTag);
    TTransactionServiceProxy proxy(channel);

    auto req = proxy.RegisterLockableDynamicTables();

    ToProto(req->mutable_transaction_id(), OutputTransaction_->GetId());
    ToProto(req->mutable_lockable_dynamic_tables(), lockableOutputDynamicTables);

    WaitFor(req->Invoke())
        .ThrowOnError();
}

void TOperationControllerBase::CommitTransactions()
{
    YT_LOG_INFO("Committing scheduler transactions");

    std::vector<TFuture<TTransactionCommitResult>> commitFutures;

    if (OutputTransaction_) {
        commitFutures.push_back(OutputTransaction_->Commit());
    }

    SleepInCommitStage(EDelayInsideOperationCommitStage::Stage7);

    if (DebugTransaction_) {
        commitFutures.push_back(DebugTransaction_->Commit());
    }

    WaitFor(AllSucceeded(commitFutures))
        .ThrowOnError();

    YT_LOG_INFO("Scheduler transactions committed");

    THashSet<ITransactionPtr> abortedTransactions;
    auto abortTransaction = [&] (const ITransactionPtr& transaction) {
        if (transaction && abortedTransactions.emplace(transaction).second) {
            // Fire-and-forget.
            YT_UNUSED_FUTURE(transaction->Abort());
        }
    };
    YT_UNUSED_FUTURE(AbortInputTransactions());
    abortTransaction(AsyncTransaction_);
}

void TOperationControllerBase::TeleportOutputChunks()
{
    if (OutputTables_.empty()) {
        return;
    }

    auto teleporter = New<TChunkTeleporter>(
        Config_->ChunkTeleporter,
        OutputClient_,
        GetCancelableInvoker(),
        OutputCompletionTransaction_->GetId(),
        Logger);

    for (auto& table : OutputTables_) {
        for (const auto& [key, chunkTreeId] : table->OutputChunkTreeIds) {
            if (IsPhysicalChunkType(TypeFromId(chunkTreeId))) {
                teleporter->RegisterChunk(chunkTreeId, table->ExternalCellTag);
            }
        }
    }

    WaitFor(teleporter->Run())
        .ThrowOnError();
}

void TOperationControllerBase::VerifySortedOutput(TOutputTablePtr table)
{
    const auto& path = table->Path.GetPath();
    YT_LOG_DEBUG("Sorting output chunk tree ids by boundary keys (ChunkTreeCount: %v, Table: %v)",
        table->OutputChunkTreeIds.size(),
        path);

    YT_VERIFY(table->TableUploadOptions.TableSchema->IsSorted());
    const auto& comparator = table->TableUploadOptions.TableSchema->ToComparator();

    std::stable_sort(
        table->OutputChunkTreeIds.begin(),
        table->OutputChunkTreeIds.end(),
        [&] (const auto& lhs, const auto& rhs) -> bool {
            auto lhsBoundaryKeys = lhs.first.AsBoundaryKeys();
            auto rhsBoundaryKeys = rhs.first.AsBoundaryKeys();
            auto minKeyResult = comparator.CompareKeys(lhsBoundaryKeys.MinKey, rhsBoundaryKeys.MinKey);
            if (minKeyResult != 0) {
                return minKeyResult < 0;
            }
            return comparator.CompareKeys(lhsBoundaryKeys.MaxKey, rhsBoundaryKeys.MaxKey) < 0;
        });

    if (!table->OutputChunkTreeIds.empty() &&
        table->TableUploadOptions.UpdateMode == EUpdateMode::Append &&
        table->LastKey)
    {
        YT_LOG_DEBUG(
            "Comparing table last key against first chunk min key (LastKey: %v, MinKey: %v, Comparator: %v)",
            table->LastKey,
            table->OutputChunkTreeIds.begin()->first.AsBoundaryKeys().MinKey,
            comparator);

        int cmp = comparator.CompareKeys(
            table->OutputChunkTreeIds.begin()->first.AsBoundaryKeys().MinKey,
            table->LastKey);

        if (cmp < 0) {
            THROW_ERROR_EXCEPTION(
                NTableClient::EErrorCode::SortOrderViolation,
                "Output table %v is not sorted: job outputs overlap with original table",
                table->GetPath())
                << TErrorAttribute("table_max_key", table->LastKey)
                << TErrorAttribute("job_output_min_key", table->OutputChunkTreeIds.begin()->first.AsBoundaryKeys().MinKey)
                << TErrorAttribute("comparator", comparator);
        }

        if (cmp == 0 && table->TableWriterOptions->ValidateUniqueKeys) {
            THROW_ERROR_EXCEPTION(
                NTableClient::EErrorCode::SortOrderViolation,
                "Output table %v contains duplicate keys: job outputs overlap with original table",
                table->GetPath())
                << TErrorAttribute("table_max_key", table->LastKey)
                << TErrorAttribute("job_output_min_key", table->OutputChunkTreeIds.begin()->first.AsBoundaryKeys().MinKey)
                << TErrorAttribute("comparator", comparator);
        }
    }

    for (auto current = table->OutputChunkTreeIds.begin(); current != table->OutputChunkTreeIds.end(); ++current) {
        auto next = current + 1;
        if (next != table->OutputChunkTreeIds.end()) {
            int cmp = comparator.CompareKeys(next->first.AsBoundaryKeys().MinKey, current->first.AsBoundaryKeys().MaxKey);

            if (cmp < 0) {
                THROW_ERROR_EXCEPTION(
                    NTableClient::EErrorCode::SortOrderViolation,
                    "Output table %v is not sorted: job outputs have overlapping key ranges",
                    table->GetPath())
                    << TErrorAttribute("current_range_max_key", current->first.AsBoundaryKeys().MaxKey)
                    << TErrorAttribute("next_range_min_key", next->first.AsBoundaryKeys().MinKey)
                    << TErrorAttribute("comparator", comparator);
            }

            if (cmp == 0 && table->TableWriterOptions->ValidateUniqueKeys) {
                THROW_ERROR_EXCEPTION(
                    NTableClient::EErrorCode::UniqueKeyViolation,
                    "Output table %v contains duplicate keys: job outputs have overlapping key ranges",
                    table->GetPath())
                    << TErrorAttribute("current_range_max_key", current->first.AsBoundaryKeys().MaxKey)
                    << TErrorAttribute("next_range_min_key", next->first.AsBoundaryKeys().MinKey)
                    << TErrorAttribute("comparator", comparator);
            }
        }
    }
}


void TOperationControllerBase::AttachOutputChunks(const std::vector<TOutputTablePtr>& tableList)
{
    for (const auto& table : tableList) {
        const auto& path = table->GetPath();

        YT_LOG_INFO("Attaching output chunks (Path: %v)",
            path);

        auto channel = OutputClient_->GetMasterChannelOrThrow(
            EMasterChannelKind::Leader,
            table->ExternalCellTag);
        TChunkServiceProxy proxy(channel);

        // Split large outputs into separate requests.
        // For static tables there is always exactly one subrequest. For dynamic tables
        // there may be multiple subrequests, each corresponding to the whole tablet.
        NChunkClient::NProto::TReqAttachChunkTrees* req = nullptr;
        TChunkServiceProxy::TReqExecuteBatchPtr batchReq;

        auto flushSubrequest = [&] (bool requestStatistics) {
            if (req) {
                req->set_request_statistics(requestStatistics);
                req = nullptr;
            }
        };

        auto flushRequest = [&] (bool requestStatistics) {
            if (!batchReq) {
                return;
            }

            if (!requestStatistics && table->Dynamic) {
                YT_ASSERT(!req);
            }
            flushSubrequest(requestStatistics);

            auto batchRspOrError = WaitFor(batchReq->Invoke());
            THROW_ERROR_EXCEPTION_IF_FAILED(GetCumulativeError(batchRspOrError), "Error attaching chunks to output table %v",
                path);

            const auto& batchRsp = batchRspOrError.Value();
            const auto& subresponses = batchRsp->attach_chunk_trees_subresponses();
            if (!table->Dynamic) {
                YT_VERIFY(subresponses.size() == 1);
            }
            if (requestStatistics) {
                for (const auto& rsp : subresponses) {
                    // NB: For a static table statistics are requested only once at the end.
                    // For a dynamic table statistics are requested for each tablet separately.
                    table->DataStatistics += rsp.statistics();
                }
            }

            batchReq.Reset();
        };

        i64 currentRequestSize = 0;

        auto addChunkTree = [&] (TChunkTreeId chunkTreeId, bool isHunk = false) {
            if (batchReq && currentRequestSize >= Config_->MaxChildrenPerAttachRequest) {
                // NB: Static tables do not need statistics for intermediate requests.
                // Dynamic tables need them for each subrequest, so we ensure that
                // the whole request is flushed only when there is no opened subrequest.
                if (!table->Dynamic || !req) {
                    flushRequest(false);
                    currentRequestSize = 0;
                }
            }

            ++currentRequestSize;

            if (!req) {
                if (!batchReq) {
                    batchReq = proxy.ExecuteBatch();
                    GenerateMutationId(batchReq);
                    SetSuppressUpstreamSync(&batchReq->Header(), true);
                    // COMPAT(shakurov): prefer proto ext (above).
                    batchReq->set_suppress_upstream_sync(true);
                }
                req = batchReq->add_attach_chunk_trees_subrequests();
                ToProto(
                    req->mutable_parent_id(),
                    isHunk ? table->OutputHunkChunkListId : table->OutputChunkListId);
                if (table->Dynamic && OperationType_ != EOperationType::RemoteCopy && !table->Path.GetOutputTimestamp()) {
                    ToProto(req->mutable_transaction_id(), table->ExternalTransactionId);
                }
            }

            ToProto(req->add_child_ids(), chunkTreeId);
        };

        if (table->TableUploadOptions.TableSchema->IsSorted() && (table->Dynamic || ShouldVerifySortedOutput())) {
            // Sorted output generated by user operation requires rearranging.

            if (!table->TableUploadOptions.PartiallySorted && ShouldVerifySortedOutput()) {
                VerifySortedOutput(table);
            } else {
                YT_VERIFY(table->Dynamic);
            }

            if (!table->Dynamic) {
                for (const auto& [key, chunkTreeId] : table->OutputChunkTreeIds) {
                    addChunkTree(chunkTreeId);
                }
            } else {
                std::vector<std::vector<TChunkTreeId>> tabletChunks(table->PivotKeys.size());
                std::vector<THashSet<TChunkId>> tabletHunkChunks(table->PivotKeys.size());

                for (const auto& chunk : table->OutputChunks) {
                    auto chunkId  = chunk->GetChunkId();
                    auto& minKey = chunk->BoundaryKeys()->MinKey;
                    auto& maxKey = chunk->BoundaryKeys()->MaxKey;

                    auto start = BinarySearch(0, tabletChunks.size(), [&] (size_t index) {
                        return CompareRows(table->PivotKeys[index], minKey) <= 0;
                    });
                    if (start > 0) {
                        --start;
                    }

                    auto end = BinarySearch(0, tabletChunks.size() - 1, [&] (size_t index) {
                        return CompareRows(table->PivotKeys[index], maxKey) <= 0;
                    });

                    if (CompareRows(table->PivotKeys[end], maxKey) <= 0) {
                        ++end;
                    }

                    std::vector<TChunkId> hunkChunkIds;
                    if (chunk->CompressionDictionaryId()) {
                        YT_VERIFY(table->Schema->HasHunkColumns());
                        YT_VERIFY(!table->OutputHunkChunks.empty());
                        hunkChunkIds.push_back(*chunk->CompressionDictionaryId());
                    }

                    if (chunk->HunkChunkRefsExt()) {
                        YT_VERIFY(table->Schema->HasHunkColumns());
                        YT_VERIFY(!table->OutputHunkChunks.empty());
                        for (const auto& hunkChunkRef : chunk->HunkChunkRefsExt()->refs()) {
                            hunkChunkIds.push_back(FromProto<TChunkId>(hunkChunkRef.chunk_id()));
                            if (hunkChunkRef.has_compression_dictionary_id()) {
                                hunkChunkIds.push_back(FromProto<TChunkId>(hunkChunkRef.compression_dictionary_id()));
                            }
                        }
                    }

                    for (auto index = start; index < end; ++index) {
                        tabletChunks[index].push_back(chunkId);
                        tabletHunkChunks[index].insert(hunkChunkIds.begin(), hunkChunkIds.end());
                    }
                }

                for (int index = 0; index < std::ssize(tabletChunks); ++index) {
                    table->OutputChunkListId = table->TabletChunkListIds[index];
                    for (auto& chunkTree : tabletChunks[index]) {
                        addChunkTree(chunkTree);
                    }
                    flushSubrequest(true);
                }

                if (!table->OutputHunkChunks.empty()) {
                    YT_VERIFY(table->TabletHunkChunkListIds.size() == table->TabletChunkListIds.size());
                    for (int index = 0; index < std::ssize(tabletHunkChunks); ++index) {
                        table->OutputHunkChunkListId = table->TabletHunkChunkListIds[index];
                        for (auto& chunkTree : tabletHunkChunks[index]) {
                            addChunkTree(chunkTree, /*isHunk*/ true);
                        }
                        flushSubrequest(true);
                    }
                }
            }
        } else if (IsOrderedOutputRequired()) {
            YT_LOG_DEBUG("Sorting output chunk tree ids according to a given output order (ChunkTreeCount: %v, Table: %v)",
                table->OutputChunkTreeIds.size(),
                path);

            for (const auto& chunkTreeId : GetOutputChunkTreesInOrder(table)) {
                addChunkTree(chunkTreeId);
            }
        } else {
            YT_LOG_DEBUG("Output chunks do not need to be sorted (ChunkTreeCount: %v, Table: %v)",
                table->OutputChunkTreeIds.size(), path);
            for (const auto& [key, chunkTreeId] : table->OutputChunkTreeIds) {
                YT_VERIFY(!key);
                addChunkTree(chunkTreeId);
            }
        }

        // NB: Don't forget to ask for the statistics in the last request.
        flushRequest(true);

        YT_LOG_INFO("Output chunks attached (Path: %v, Statistics: %v)",
            path,
            table->DataStatistics);
    }
}

void TOperationControllerBase::CustomCommit()
{ }

void TOperationControllerBase::EndUploadOutputTables(const std::vector<TOutputTablePtr>& tables)
{
    THashMap<TCellTag, std::vector<TOutputTablePtr>> nativeCellTagToTables;
    for (const auto& table : tables) {
        nativeCellTagToTables[CellTagFromId(table->ObjectId)].push_back(table);

        YT_LOG_INFO("Finishing upload to output table (Path: %v, Schema: %v)",
            table->GetPath(),
            *table->TableUploadOptions.TableSchema);
    }

    {
        std::vector<TFuture<TObjectServiceProxy::TRspExecuteBatchPtr>> asyncResults;
        for (const auto& [nativeCellTag, tables] : nativeCellTagToTables) {
            auto proxy = CreateObjectServiceWriteProxy(OutputClient_, nativeCellTag);
            auto batchReq = proxy.ExecuteBatch();
            for (const auto& table : tables) {
                {
                    auto req = TTableYPathProxy::EndUpload(table->GetObjectIdPath());
                    SetTransactionId(req, table->UploadTransactionId);
                    GenerateMutationId(req);
                    *req->mutable_statistics() = table->DataStatistics;

                    if (!table->IsFile()) {
                        // COMPAT(h0pless): remove this when all masters are 24.2.
                        req->set_schema_mode(ToProto(table->TableUploadOptions.SchemaMode));

                        req->set_optimize_for(ToProto(table->TableUploadOptions.OptimizeFor));
                        if (table->TableUploadOptions.ChunkFormat) {
                            req->set_chunk_format(ToProto(*table->TableUploadOptions.ChunkFormat));
                        }
                    }
                    req->set_compression_codec(ToProto(table->TableUploadOptions.CompressionCodec));
                    req->set_erasure_codec(ToProto(table->TableUploadOptions.ErasureCodec));
                    if (table->TableUploadOptions.SecurityTags) {
                        ToProto(req->mutable_security_tags()->mutable_items(), *table->TableUploadOptions.SecurityTags);
                    }
                    batchReq->AddRequest(req);
                }
            }

            asyncResults.push_back(batchReq->Invoke());
        }

        auto checkError = [] (const auto& error) {
            THROW_ERROR_EXCEPTION_IF_FAILED(error, "Error finishing upload to output tables");
        };

        auto result = WaitFor(AllSucceeded(asyncResults));
        checkError(result);

        for (const auto& batchRsp : result.Value()) {
            checkError(GetCumulativeError(batchRsp));
        }
    }

    {
        std::vector<TFuture<TObjectServiceProxy::TRspExecuteBatchPtr>> asyncResults;
        for (const auto& [nativeCellTag, tables] : nativeCellTagToTables) {
            std::vector<TOutputTablePtr> debugTables;
            for (const auto& table : tables) {
                if (table->IsDebugTable()) {
                    debugTables.push_back(table);

                    YT_LOG_INFO("Setting custom attributes for debug output table (Path: %v, OutputType: %v)",
                        table->GetPath(),
                        table->OutputType);
                }
            }

            if (debugTables.empty()) {
                continue;
            }

            auto proxy = CreateObjectServiceWriteProxy(OutputClient_, nativeCellTag);
            auto batchReq = proxy.ExecuteBatch();
            for (const auto& table : debugTables) {
                {
                    auto req = TYPathProxy::Set(table->GetObjectIdPath() + "/@part_size");
                    SetTransactionId(req, GetTransactionForOutputTable(table)->GetId());
                    req->set_value(ToProto(ConvertToYsonStringNestingLimited(GetPartSize(table->OutputType))));
                    batchReq->AddRequest(req);
                }
                if (table->OutputType == EOutputTableType::Core) {
                    auto req = TYPathProxy::Set(table->GetObjectIdPath() + "/@sparse");
                    SetTransactionId(req, GetTransactionForOutputTable(table)->GetId());
                    req->set_value(ToProto(ConvertToYsonStringNestingLimited(true)));
                    batchReq->AddRequest(req);
                }
            }

            asyncResults.push_back(batchReq->Invoke());
        }

        auto checkError = [] (const auto& error) {
            THROW_ERROR_EXCEPTION_IF_FAILED(error, "Error setting custom attributes for debug output tables");
        };

        auto result = WaitFor(AllSucceeded(asyncResults));
        checkError(result);

        for (const auto& batchRsp : result.Value()) {
            checkError(GetCumulativeError(batchRsp));
        }
    }

    YT_LOG_INFO("Upload to output tables finished");
}

void TOperationControllerBase::OnJobStarted(const TJobletPtr& joblet)
{
    YT_ASSERT_INVOKER_POOL_AFFINITY(CancelableInvokerPool_);

    if (State_ != EControllerState::Running) {
        YT_LOG_DEBUG("Stale job started, ignored (JobId: %v)", joblet->JobId);
        return;
    }

    YT_LOG_DEBUG("Job started (JobId: %v)", joblet->JobId);

    joblet->LastActivityTime = TInstant::Now();
    joblet->TaskName = joblet->Task->GetVertexDescriptor();

    GetJobProfiler()->ProfileStartedJob(*joblet);

    YT_VERIFY(!std::exchange(joblet->JobState, EJobState::Waiting));

    if (!joblet->Revived) {
        Host_->RegisterJob(
            TStartedJobInfo{
                .JobId = joblet->JobId,
            });
    }

    IncreaseAccountResourceUsageLease(joblet->DiskRequestAccount, joblet->DiskQuota);

    ReportJobCookieToArchive(joblet);
    ReportControllerStateToArchive(joblet, EJobState::Running);
    ReportStartTimeToArchive(joblet);

    LogEventFluently(ELogEventType::JobStarted)
        .Item("job_id").Value(joblet->JobId)
        .Item("allocation_id").Value(AllocationIdFromJobId(joblet->JobId))
        .Item("operation_id").Value(OperationId_)
        .Item("resource_limits").Value(joblet->ResourceLimits)
        .Item("node_address").Value(NNodeTrackerClient::GetDefaultAddress(joblet->NodeDescriptor.Addresses))
        .Item("job_type").Value(joblet->JobType)
        .Item("task_name").Value(joblet->TaskName)
        .Item("tree_id").Value(joblet->TreeId)
        .Do([&] (TFluentMap fluent) {
            EnrichJobInfo(fluent, joblet);
        });

    LogProgress();
}

void TOperationControllerBase::InitializeHistograms()
{
    if (IsInputDataSizeHistogramSupported()) {
        EstimatedInputDataSizeHistogram_ = CreateHistogram();
        InputDataSizeHistogram_ = CreateHistogram();
    }
}

void TOperationControllerBase::AddValueToEstimatedHistogram(const TJobletPtr& joblet)
{
    if (EstimatedInputDataSizeHistogram_) {
        EstimatedInputDataSizeHistogram_->AddValue(joblet->InputStripeList->TotalDataWeight);
    }
}

void TOperationControllerBase::RemoveValueFromEstimatedHistogram(const TJobletPtr& joblet)
{
    if (EstimatedInputDataSizeHistogram_) {
        EstimatedInputDataSizeHistogram_->RemoveValue(joblet->InputStripeList->TotalDataWeight);
    }
}

void TOperationControllerBase::UpdateActualHistogram(const TCompletedJobSummary& jobSummary)
{
    if (InputDataSizeHistogram_ && jobSummary.TotalInputDataStatistics) {
        auto dataWeight = jobSummary.TotalInputDataStatistics->data_weight();
        if (dataWeight > 0) {
            InputDataSizeHistogram_->AddValue(dataWeight);
        }
    }
}

void TOperationControllerBase::InitializeSecurityTags()
{
    std::vector<TSecurityTag> inferredSecurityTags;
    auto addTags = [&] (const auto& moreTags) {
        inferredSecurityTags.insert(inferredSecurityTags.end(), moreTags.begin(), moreTags.end());
    };

    addTags(Spec_->AdditionalSecurityTags);

    for (const auto& table : InputManager_->GetInputTables()) {
        addTags(table->SecurityTags);
    }

    for (const auto& [userJobSpec, files] : UserJobFiles_) {
        for (const auto& file : files) {
            addTags(file.SecurityTags);
        }
    }

    if (BaseLayer_) {
        addTags(BaseLayer_->SecurityTags);
    }

    SortUnique(inferredSecurityTags);

    for (const auto& table : OutputTables_) {
        if (auto explicitSecurityTags = table->Path.GetSecurityTags()) {
            // TODO(babenko): audit
            YT_LOG_INFO("Output table is assigned explicit security tags (Path: %v, InferredSecurityTags: %v, ExplicitSecurityTags: %v)",
                table->GetPath(),
                inferredSecurityTags,
                explicitSecurityTags);
            table->TableUploadOptions.SecurityTags = *explicitSecurityTags;
        } else {
            YT_LOG_INFO("Output table is assigned automatically-inferred security tags (Path: %v, SecurityTags: %v)",
                table->GetPath(),
                inferredSecurityTags);
            table->TableUploadOptions.SecurityTags = inferredSecurityTags;
        }
    }
}

void TOperationControllerBase::ProcessJobFinishedResult(const TJobFinishedResult& result)
{
    if (!result.OperationFailedError.IsOK()) {
        OnOperationFailed(result.OperationFailedError);
    }

    for (const auto& treeId : result.NewlyBannedTrees) {
        MaybeBanInTentativeTree(treeId);
    }
}

bool TOperationControllerBase::OnJobCompleted(
    TJobletPtr joblet,
    std::unique_ptr<TCompletedJobSummary> jobSummary)
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker(Config_->JobEventsControllerQueue));

    YT_VERIFY(jobSummary);

    auto jobId = jobSummary->Id;
    const auto abandoned = jobSummary->Abandoned;

    if (!ShouldProcessJobEvents()) {
        YT_LOG_DEBUG("Stale job completed, ignored (JobId: %v)", jobId);
        return false;
    }

    JobAbortsUntilOperationFailure_.clear();

    YT_LOG_DEBUG(
        "Job completed (JobId: %v, ResultSize: %v, Abandoned: %v, InterruptionReason: %v, Interruptible: %v)",
        jobId,
        jobSummary->Result ? std::make_optional(jobSummary->GetJobResult().ByteSizeLong()) : std::nullopt,
        jobSummary->Abandoned,
        jobSummary->InterruptionReason,
        joblet->JobInterruptible);

    // Testing purpose code.
    if (Config_->EnableControllerFailureSpecOption &&
        Spec_->TestingOperationOptions->ControllerFailure &&
        *Spec_->TestingOperationOptions->ControllerFailure == EControllerFailureType::ExceptionThrownInOnJobCompleted)
    {
        THROW_ERROR_EXCEPTION(NScheduler::EErrorCode::TestingError, "Testing exception");
    }

    // TODO(max42): this code is overcomplicated, rethink it.
    if (!abandoned) {
        const auto& jobResultExt = jobSummary->GetJobResultExt();
        bool restartNeeded = false;
        // TODO(max42): always send restart_needed?
        if (jobResultExt.has_restart_needed()) {
            restartNeeded = jobResultExt.restart_needed();
        } else {
            restartNeeded = jobResultExt.unread_chunk_specs_size() > 0;
        }

        if (restartNeeded) {
            if (joblet->Revived) {
                // TODO(pogorelov): Fix it, we already send it from node.
                // NB: We lose the original interrupt reason during the revival,
                // so we set it to Unknown.
                jobSummary->InterruptionReason = EInterruptionReason::Unknown;
                YT_LOG_DEBUG(
                    "Overriding job interrupt reason due to revival (JobId: %v, InterruptionReason: %v)",
                    jobId,
                    jobSummary->InterruptionReason);
            } else {
                YT_LOG_DEBUG("Job restart is needed (JobId: %v)", jobId);
            }
        } else if (jobSummary->InterruptionReason != EInterruptionReason::None) {
            jobSummary->InterruptionReason = EInterruptionReason::None;
            YT_LOG_DEBUG(
                "Overriding job interrupt reason due to unneeded restart (JobId: %v, InterruptionReason: %v)",
                jobId,
                jobSummary->InterruptionReason);
        }

        YT_VERIFY(
            (jobSummary->InterruptionReason == EInterruptionReason::None && jobResultExt.unread_chunk_specs_size() == 0) ||
            (jobSummary->InterruptionReason != EInterruptionReason::None && (
                jobResultExt.unread_chunk_specs_size() != 0 ||
                jobResultExt.restart_needed())));

        // Validate all node ids of the output chunks and populate the local node directory.
        // In case any id is not known, abort the job.
        for (const auto& chunkSpec : jobResultExt.output_chunk_specs()) {
            if (auto abortedJobSummary = RegisterOutputChunkReplicas(*jobSummary, chunkSpec)) {
                return OnJobAborted(std::move(joblet), std::move(abortedJobSummary));
            }
        }
    }

    // Controller should abort job if its competitor has already completed.
    if (auto maybeAbortReason = joblet->Task->ShouldAbortCompletingJob(joblet)) {
        YT_LOG_DEBUG("Job is considered aborted since its competitor has already completed (JobId: %v)", jobId);
        return OnJobAborted(std::move(joblet), std::make_unique<TAbortedJobSummary>(*jobSummary, *maybeAbortReason));
    }

    TJobFinishedResult taskJobResult;
    std::optional<i64> optionalRowCount;

    {
        // NB: We want to process finished job changes atomically.
        // It is needed to prevent inconsistencies of operation controller state.
        // Such inconsistencies blocks saving job results in case of operation failure
        TForbidContextSwitchGuard guard;

        // NB: We should not explicitly tell node to remove abandoned job because it may be still
        // running at the node.
        if (!abandoned) {
            CompletedJobIdsReleaseQueue_.Push(jobId);
        }

        if (jobSummary->InterruptionReason != EInterruptionReason::None) {
            ExtractInterruptDescriptor(*jobSummary, joblet);
            YT_LOG_DEBUG(
                "Job interrupted (JobId: %v, InterruptionReason: %v, UnreadDataSliceCount: %v, ReadDataSliceCount: %v)",
                jobId,
                jobSummary->InterruptionReason,
                jobSummary->UnreadInputDataSlices.size(),
                jobSummary->ReadInputDataSlices.size());
        }

        UpdateJobletFromSummary(*jobSummary, joblet);

        joblet->Task->UpdateMemoryDigests(joblet, /*resourceOverdraft*/ false);
        UpdateActualHistogram(*jobSummary);

        if (joblet->ShouldLogFinishedEvent()) {
            LogFinishedJobFluently(ELogEventType::JobCompleted, joblet);
        }

        UpdateJobMetrics(joblet, *jobSummary, /*isJobFinished*/ true);
        UpdateAggregatedFinishedJobStatistics(joblet, *jobSummary);

        taskJobResult = joblet->Task->OnJobCompleted(joblet, *jobSummary);

        if (!abandoned) {
            if ((JobSpecCompletedArchiveCount_ < Config_->GuaranteedArchivedJobSpecCountPerOperation ||
                jobSummary->TimeStatistics.ExecDuration.value_or(TDuration()) > Config_->MinJobDurationToArchiveJobSpec) &&
                JobSpecCompletedArchiveCount_ < Config_->MaxArchivedJobSpecCountPerOperation)
            {
                ++JobSpecCompletedArchiveCount_;
                jobSummary->ReleaseFlags.ArchiveJobSpec = true;
            }
        }

        // We want to know row count before moving jobSummary to OnJobFinished.
        if (RowCountLimitTableIndex_ && jobSummary->OutputDataStatistics) {
            optionalRowCount = VectorAtOr(*jobSummary->OutputDataStatistics, *RowCountLimitTableIndex_).row_count();
        }

        GetJobProfiler()->ProfileCompletedJob(*joblet, *jobSummary);

        OnJobFinished(std::move(jobSummary), /*retainJob*/ false);

        if (abandoned) {
            ReleaseJobs({jobId});
        }

        UnregisterJoblet(joblet);
    }

    ProcessJobFinishedResult(taskJobResult);

    UpdateTask(joblet->Task);
    LogProgress();

    if (CheckGracefullyAbortedJobsStatusReceived()) {
        // Operation failed.
        return false;
    }

    if (IsCompleted()) {
        OnOperationCompleted(/*interrupted*/ false);
        return false;
    }

    if (RowCountLimitTableIndex_ && optionalRowCount) {
        switch (joblet->JobType) {
            case EJobType::Map:
            case EJobType::OrderedMap:
            case EJobType::SortedReduce:
            case EJobType::JoinReduce:
            case EJobType::PartitionReduce:
            case EJobType::OrderedMerge:
            case EJobType::UnorderedMerge:
            case EJobType::SortedMerge:
            case EJobType::FinalSort: {
                RegisterOutputRows(*optionalRowCount, *RowCountLimitTableIndex_);
                break;
            }
            default:
                break;
        }
    }

    return true;
}

bool TOperationControllerBase::OnJobFailed(
    TJobletPtr joblet,
    std::unique_ptr<TFailedJobSummary> jobSummary)
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker(Config_->JobEventsControllerQueue));

    auto jobId = jobSummary->Id;

    if (!ShouldProcessJobEvents()) {
        YT_LOG_DEBUG("Stale job failed, ignored (JobId: %v)", jobId);
        return false;
    }

    YT_LOG_DEBUG("Job failed (JobId: %v)", jobId);

    JobAbortsUntilOperationFailure_.clear();

    if (Spec_->IgnoreJobFailuresAtBannedNodes && BannedNodeIds_.find(joblet->NodeDescriptor.Id) != BannedNodeIds_.end()) {
        YT_LOG_DEBUG("Job is considered aborted since it has failed at a banned node "
            "(JobId: %v, Address: %v)",
            jobId,
            NNodeTrackerClient::GetDefaultAddress(joblet->NodeDescriptor.Addresses));
        auto abortedJobSummary = std::make_unique<TAbortedJobSummary>(*jobSummary, EAbortReason::NodeBanned);
        return OnJobAborted(std::move(joblet), std::move(abortedJobSummary));
    }

    if (joblet->CompetitionType == EJobCompetitionType::Experiment) {
        YT_LOG_DEBUG("Failed layer probing job is considered aborted "
            "(JobId: %v)",
            jobId);
        auto abortedJobSummary = std::make_unique<TAbortedJobSummary>(*jobSummary, EAbortReason::JobTreatmentFailed);
        return OnJobAborted(std::move(joblet), std::move(abortedJobSummary));
    }

    auto error = jobSummary->GetError();
    auto maybeExitCode = jobSummary->GetExitCode();

    TJobFinishedResult taskJobResult;

    {
        // NB: We want to process finished job changes atomically.
        // It is needed to prevent inconsistencies of operation controller state.
        // Such inconsistencies blocks saving job results in case of operation failure
        TForbidContextSwitchGuard guard;

        ++FailedJobCount_;
        UpdateFailedJobsExitCodeCounters(maybeExitCode);
        if (FailedJobCount_ == 1) {
            ShouldUpdateLightOperationAttributes_ = true;
            ShouldUpdateProgressAttributesInCypress_ = true;
        }

        UpdateJobletFromSummary(*jobSummary, joblet);

        LogFinishedJobFluently(ELogEventType::JobFailed, joblet)
            .Item("error").Value(error);

        UpdateJobMetrics(joblet, *jobSummary, /*isJobFinished*/ true);
        UpdateAggregatedFinishedJobStatistics(joblet, *jobSummary);

        taskJobResult = joblet->Task->OnJobFailed(joblet, *jobSummary);

        jobSummary->ReleaseFlags.ArchiveJobSpec = true;

        GetJobProfiler()->ProfileFailedJob(*joblet, *jobSummary);

        OnJobFinished(std::move(jobSummary), /*retainJob*/ true);

        auto finallyGuard = Finally([&] {
            // TODO(pogorelov): Remove current exception checking (YT-18911).
            if (std::uncaught_exceptions() == 0 || Config_->ReleaseFailedJobOnException) {
                ReleaseJobs({jobId});
            }
        });

        UnregisterJoblet(joblet);
    }

    ProcessJobFinishedResult(taskJobResult);

    if (CheckGracefullyAbortedJobsStatusReceived()) {
        // Operation failed.
        return false;
    }

    // This failure case has highest priority for users. Therefore check must be performed as early as possible.
    if (IsJobUniquenessRequired(joblet)) {
        OnJobUniquenessViolated(TError(NScheduler::EErrorCode::OperationFailedOnJobRestart,
            "Job failed; failing operation since \"fail_on_job_restart\" option is set in operation spec or user job spec")
            << TErrorAttribute("job_id", joblet->JobId)
            << TErrorAttribute("reason", EFailOnJobRestartReason::JobFailed)
            << error);
        return false;
    }

    if (error.Attributes().Get<bool>("fatal", false)) {
        auto wrappedError = TError("Job failed with fatal error") << error;
        OnOperationFailed(wrappedError);
        return false;
    }

    auto makeOperationFailedError = [&] (TError failureKindError) {
        failureKindError <<= TErrorAttribute("job_id", jobId);
        if (IsFailingByTimeout()) {
            return GetTimeLimitError()
                << std::move(failureKindError)
                << error;
        } else {
            return std::move(failureKindError)
                << error;
        }
    };

    if (int maxFailedJobCount = SpecManager_->GetSpec()->MaxFailedJobCount; FailedJobCount_ >= maxFailedJobCount) {
        OnOperationFailed(makeOperationFailedError(GetMaxFailedJobCountReachedError(maxFailedJobCount)));
        return false;
    }
    if (IsJobsFailToleranceExceeded(maybeExitCode)) {
        auto jobsFailToleranceExceededError = TError(NScheduler::EErrorCode::MaxFailedJobsLimitExceeded, "Jobs fail tolerance exceeded")
            << TErrorAttribute("max_failed_job_count", GetMaxJobFailCountForExitCode(maybeExitCode));

        if (maybeExitCode.has_value()) {
            (jobsFailToleranceExceededError
                <<= TErrorAttribute("exit_code_known", IsExitCodeKnown(*maybeExitCode)))
                <<= TErrorAttribute("exit_code", *maybeExitCode);
        }

        OnOperationFailed(makeOperationFailedError(std::move(jobsFailToleranceExceededError)));
        return false;
    }

    if (Spec_->BanNodesWithFailedJobs) {
        if (BannedNodeIds_.insert(joblet->NodeDescriptor.Id).second) {
            YT_LOG_DEBUG("Node banned due to failed job (JobId: %v, NodeId: %v, Address: %v)",
                jobId,
                joblet->NodeDescriptor.Id,
                NNodeTrackerClient::GetDefaultAddress(joblet->NodeDescriptor.Addresses));
        }
    }

    if (Spec_->SuspendOnJobFailure) {
        Host_->OnOperationSuspended(TError("Job failed with error") << error);
    }

    UpdateTask(joblet->Task);
    LogProgress();

    if (IsCompleted()) {
        OnOperationCompleted(/*interrupted*/ false);
        return false;
    }

    return true;
}

// Should not produce context switches if operation is not finished in process of the method call.
bool TOperationControllerBase::OnJobAborted(
    TJobletPtr joblet,
    std::unique_ptr<TAbortedJobSummary> jobSummary)
{
    YT_ASSERT_INVOKER_POOL_AFFINITY(CancelableInvokerPool_);

    auto jobId = jobSummary->Id;
    auto abortReason = jobSummary->AbortReason;

    if (!ShouldProcessJobEvents()) {
        YT_LOG_DEBUG("Stale job aborted, ignored (JobId: %v)", jobId);
        return false;
    }

    YT_LOG_DEBUG(
        "Job aborted (JobId: %v, AbortReason: %v, JobType: %v)",
        jobId,
        abortReason,
        joblet->JobType);

    auto error = jobSummary->Error;

    if (joblet->JobSpecProtoFuture) {
        joblet->JobSpecProtoFuture.Cancel(error ? *error : TError("Job aborted"));
    }

    TJobFinishedResult taskJobResult;
    std::vector<TChunkId> failedChunkIds;
    bool wasScheduled = jobSummary->Scheduled;

    {
        // NB: We want to process finished job changes atomically.
        // It is needed to prevent inconsistencies of operation controller state.
        // Such inconsistencies blocks saving job results in case of operation failure
        TForbidContextSwitchGuard guard;

        UpdateJobletFromSummary(*jobSummary, joblet);

        if (abortReason == EAbortReason::ResourceOverdraft) {
            joblet->Task->UpdateMemoryDigests(joblet, /*resourceOverdraft*/ true);
        }

        if (wasScheduled) {
            if (joblet->ShouldLogFinishedEvent()) {
                LogFinishedJobFluently(ELogEventType::JobAborted, joblet)
                    .Item("reason").Value(abortReason)
                    .DoIf(jobSummary->Error.has_value(), [&] (TFluentMap fluent) {
                        fluent.Item("error").Value(jobSummary->Error);
                    })
                    .DoIf(jobSummary->PreemptedFor.has_value(), [&] (TFluentMap fluent) {
                        fluent.Item("preempted_for").Value(jobSummary->PreemptedFor);
                    });
            }
            UpdateAggregatedFinishedJobStatistics(joblet, *jobSummary);
        }

        UpdateJobMetrics(joblet, *jobSummary, /*isJobFinished*/ true);

        if (abortReason == EAbortReason::FailedChunks) {
            const auto& jobResultExt = jobSummary->GetJobResultExt();
            failedChunkIds = FromProto<std::vector<TChunkId>>(jobResultExt.failed_chunk_ids());
            YT_LOG_DEBUG(
                "Job aborted because of failed chunks (JobId: %v, SampleFailedChunkIds: %v)",
                jobId,
                MakeShrunkFormattableView(failedChunkIds, TDefaultFormatter(), SampleChunkIdCount));
        }
        taskJobResult = joblet->Task->OnJobAborted(joblet, *jobSummary);

        bool retainJob = (abortReason == EAbortReason::UserRequest) || WasJobGracefullyAborted(jobSummary);

        GetJobProfiler()->ProfileAbortedJob(*joblet, *jobSummary);

        OnJobFinished(std::move(jobSummary), retainJob);

        auto finallyGuard = Finally([&] {
            ReleaseJobs({jobId});
        });

        UnregisterJoblet(joblet);
    }

    ProcessJobFinishedResult(taskJobResult);

    for (auto chunkId : failedChunkIds) {
        OnChunkFailed(chunkId, jobId);
    }

    if (CheckGracefullyAbortedJobsStatusReceived()) {
        // Operation failed.
        return false;
    }

    // This failure case has highest priority for users. Therefore check must be performed as early as possible.
    if (IsJobUniquenessRequired(joblet) &&
        wasScheduled &&
        joblet->IsStarted() &&
        abortReason != EAbortReason::GetSpecFailed)
    {
        OnJobUniquenessViolated(TError(
            NScheduler::EErrorCode::OperationFailedOnJobRestart,
            "Job aborted; failing operation since \"fail_on_job_restart\" option is set in operation spec or user job spec")
            << TErrorAttribute("job_id", joblet->JobId)
            << TErrorAttribute("reason", EFailOnJobRestartReason::JobAborted)
            << TErrorAttribute("job_abort_reason", abortReason));
        return false;
    }

    if (auto it = JobAbortsUntilOperationFailure_.find(abortReason); it != JobAbortsUntilOperationFailure_.end()) {
        if (--it->second == 0) {
            JobAbortsUntilOperationFailure_.clear();
            auto wrappedError = TError("Operation failed due to excessive successive job aborts");
            if (error) {
                wrappedError <<= *error;
            }
            OnOperationFailed(wrappedError);
            return false;
        }
    }

    if (abortReason == EAbortReason::AccountLimitExceeded) {
        Host_->OnOperationSuspended(TError("Account limit exceeded"));
    }

    UpdateTask(joblet->Task);
    LogProgress();

    if (IsCompleted()) {
        OnOperationCompleted(/*interrupted*/ false);
        return false;
    }

    return true;
}

bool TOperationControllerBase::WasJobGracefullyAborted(const std::unique_ptr<TAbortedJobSummary>& jobSummary)
{
    YT_ASSERT_INVOKER_POOL_AFFINITY(CancelableInvokerPool_);

    if (!jobSummary->Result) {
        return false;
    }

    const auto& error = jobSummary->GetError();
    if (auto innerError = error.FindMatching(NExecNode::EErrorCode::AbortByControllerAgent)) {
        return innerError->Attributes().Get("graceful_abort", false);
    }

    return false;
}

void TOperationControllerBase::OnJobStartTimeReceived(
    const TJobletPtr& joblet,
    const std::unique_ptr<TRunningJobSummary>& jobSummary)
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker(Config_->JobEventsControllerQueue));

    auto jobId = joblet->JobId;

    auto nodeJobStartTime = TInstant();

    if (auto jobStartTime = jobSummary->StartTime) {
        nodeJobStartTime = jobStartTime;
    } else if (
        const auto& timeStatistics = jobSummary->TimeStatistics;
        timeStatistics.ExecDuration ||
        timeStatistics.PrepareDuration)
    {
        auto totalDuration =
            timeStatistics.PrepareDuration.value_or(TDuration::Zero()) +
            timeStatistics.ExecDuration.value_or(TDuration::Zero());

        nodeJobStartTime = TInstant::Now() - totalDuration;
    }

    if (nodeJobStartTime && !joblet->IsJobStartedOnNode()) {
        joblet->NodeJobStartTime = nodeJobStartTime;
        RunningAllocationPreemptibleProgressStartTimes_[AllocationIdFromJobId(jobId)] = nodeJobStartTime;
    }
}

template <class TAllocationEvent>
void TOperationControllerBase::ProcessAllocationEvent(TAllocationEvent&& eventSummary, TStringBuf eventType)
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker(Config_->JobEventsControllerQueue));

    if (!ShouldProcessJobEvents()) {
        YT_LOG_DEBUG("Stale allocation %v event, ignored (AllocationIdId: %v)", eventType, eventSummary.Id);
        return;
    }

    auto allocationIt = AllocationMap_.find(eventSummary.Id);

    if (allocationIt == end(AllocationMap_)) {
        YT_LOG_DEBUG(
            "Allocation is not found, ignore %v allocation event (EventSummary: %v)",
            eventType,
            eventSummary);
        return;
    }

    auto& allocation = allocationIt->second;

    YT_LOG_DEBUG(
        "Processing %v allocation event (AllocationId: %v, HasActiveJob: %v)",
        eventType,
        eventSummary.Id,
        static_cast<bool>(allocation.Joblet));

    // NB(pogorelov): Job might be not registered in job tracker (e.g. allocation not scheduled or node did not request job settlement),
    // so joblet may still be present in allocation.
    if (allocation.Joblet) {
        auto jobSummary = CreateAbortedJobSummary(allocation.Joblet->JobId, std::move(eventSummary));
        OnJobAborted(allocation.Joblet, std::move(jobSummary));
    }

    if (ShouldProcessJobEvents()) {
        AllocationMap_.erase(allocationIt);
    }
}

void TOperationControllerBase::SafeOnAllocationAborted(TAbortedAllocationSummary&& abortedAllocationSummary)
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker(Config_->JobEventsControllerQueue));

    ProcessAllocationEvent(std::move(abortedAllocationSummary), "aborted");
}

void TOperationControllerBase::SafeOnAllocationFinished(TFinishedAllocationSummary&& finishedAllocationSummary)
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker(Config_->JobEventsControllerQueue));

    ProcessAllocationEvent(std::move(finishedAllocationSummary), "finished");
}

void TOperationControllerBase::OnJobRunning(
    const TJobletPtr& joblet,
    std::unique_ptr<TRunningJobSummary> jobSummary)
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker(Config_->JobEventsControllerQueue));

    auto jobId = jobSummary->Id;

    if (Spec_->TestingOperationOptions->CrashControllerAgent) {
        bool canCrashControllerAgent = false;
        {
            const auto& client = Host_->GetClient();
            auto proxy = CreateObjectServiceReadProxy(client, EMasterChannelKind::Cache);
            TMasterReadOptions readOptions{
                .ReadFrom = EMasterChannelKind::Cache
            };

            auto userClosure = GetSubjectClosure(
                AuthenticatedUser_,
                proxy,
                client->GetNativeConnection(),
                readOptions);

            canCrashControllerAgent = userClosure.contains(RootUserName) || userClosure.contains(SuperusersGroupName);
        }

        if (canCrashControllerAgent) {
            YT_LOG_ERROR("Crashing controller agent");
            YT_VERIFY(false && "Crashed intentionally by spec option");
        } else {
            auto error = TError(
                "User %Qv is not a superuser but tried to crash controller agent using testing options in spec; "
                "this incident will be reported",
                AuthenticatedUser_);
            YT_LOG_ALERT(error);
            THROW_ERROR_EXCEPTION(error);
        }
    }

    if (State_ != EControllerState::Running) {
        YT_LOG_DEBUG("Stale job running event ignored because controller is not running (JobId: %v, State: %v)", jobId, State_.load());
        return;
    }

    if (jobSummary->StatusTimestamp <= joblet->LastUpdateTime) {
        YT_LOG_DEBUG(
            "Stale job running event ignored because its timestamp is older than joblet last update time "
            "(JobId: %v, JobletLastUpdateTime: %v, StatusTimestamp: %v)",
            jobSummary->Id,
            joblet->LastUpdateTime,
            jobSummary->StatusTimestamp);
        return;
    }

    UpdateJobletFromSummary(*jobSummary, joblet);

    GetJobProfiler()->ProfileRunningJob(*joblet);

    joblet->JobState = EJobState::Running;

    joblet->Task->OnJobRunning(joblet, *jobSummary);

    OnJobStartTimeReceived(joblet, jobSummary);

    if (jobSummary->Statistics) {
        // We actually got fresh running job statistics.

        UpdateJobMetrics(joblet, *jobSummary, /*isJobFinished*/ false);

        TErrorOr<TBriefJobStatisticsPtr> briefStatisticsOrError;

        try {
            briefStatisticsOrError = BuildBriefStatistics(std::move(jobSummary));
        } catch (const std::exception& ex) {
            briefStatisticsOrError = TError(ex);
        }

        AnalyzeBriefStatistics(
            joblet,
            Config_->SuspiciousJobs,
            briefStatisticsOrError);
    }
}

void TOperationControllerBase::SafeAbandonJob(TJobId jobId)
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker(Config_->JobEventsControllerQueue));

    YT_LOG_DEBUG("Abandon job (JobId: %v)", jobId);

    if (State_ != EControllerState::Running) {
        THROW_ERROR_EXCEPTION(
            "Operation %v is not running",
            OperationId_);
    }

    auto joblet = GetJobletOrThrow(jobId);

    switch (joblet->JobType) {
        case EJobType::Map:
        case EJobType::OrderedMap:
        case EJobType::SortedReduce:
        case EJobType::JoinReduce:
        case EJobType::PartitionMap:
        case EJobType::ReduceCombiner:
        case EJobType::PartitionReduce:
        case EJobType::Vanilla:
            break;
        default:
            THROW_ERROR_EXCEPTION(
                "Cannot abandon job %v of operation %v since it has type %Qlv",
                jobId,
                OperationId_,
                joblet->JobType);
    }

    if (!ShouldProcessJobEvents()) {
        THROW_ERROR_EXCEPTION(
            "Cannot abandon job %v of operation %v that is not running",
            jobId,
            OperationId_);
    }

    Host_->AbortJob(jobId, EAbortReason::Abandoned, /*requestNewJob*/ false);

    OnJobCompleted(std::move(joblet), CreateAbandonedJobSummary(jobId));
}

void TOperationControllerBase::SafeInterruptJobByUserRequest(TJobId jobId, TDuration timeout)
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker(Config_->JobEventsControllerQueue));

    YT_LOG_DEBUG(
        "Interrupting job (JobId: %v, Timeout: %v)",
        jobId,
        timeout);

    if (State_ != EControllerState::Running) {
        THROW_ERROR_EXCEPTION(
            "Operation %v is not running",
            OperationId_);
    }

    auto joblet = GetJobletOrThrow(jobId);

    if (!joblet->JobInterruptible) {
        THROW_ERROR_EXCEPTION(
            "Cannot interrupt job %v of type %Qlv "
            "because it does not support interruption or \"interruption_signal\" is not set",
            jobId,
            joblet->JobType);
    }

    InterruptJob(jobId, EInterruptionReason::UserRequest, timeout);
}

void TOperationControllerBase::BuildJobAttributes(
    const TJobletPtr& joblet,
    EJobState state,
    i64 stderrSize,
    TFluentMap fluent) const
{
    YT_LOG_DEBUG("Building job attributes");
    fluent
        .Item("job_type").Value(joblet->JobType)
        .Item("state").Value(state)
        .Item("address").Value(NNodeTrackerClient::GetDefaultAddress(joblet->NodeDescriptor.Addresses))
        .Item("addresses").Value(joblet->NodeDescriptor.Addresses)
        .Item("start_time").Value(joblet->StartTime)
        .Item("account").Value(joblet->DebugArtifactsAccount)
        .Item("progress").Value(joblet->Progress)

        // We use Int64 for `stderr_size' to be consistent with
        // compressed_data_size / uncompressed_data_size attributes.
        .Item("stderr_size").Value(stderrSize)
        .Item("brief_statistics").Value(joblet->BriefStatistics)
        .Item("statistics").Value(joblet->BuildCombinedStatistics())
        .Item("suspicious").Value(joblet->Suspicious)
        .Item("job_competition_id").Value(joblet->CompetitionIds[EJobCompetitionType::Speculative])
        .Item("probing_job_competition_id").Value(joblet->CompetitionIds[EJobCompetitionType::Probing])
        .Item("has_competitors").Value(joblet->HasCompetitors[EJobCompetitionType::Speculative])
        .Item("has_probing_competitors").Value(joblet->HasCompetitors[EJobCompetitionType::Probing])
        .Item("probing").Value(joblet->CompetitionType == EJobCompetitionType::Probing)
        .Item("speculative").Value(joblet->CompetitionType == EJobCompetitionType::Speculative)
        .Item("task_name").Value(joblet->TaskName)
        .Item("job_cookie").Value(joblet->OutputCookie)
        .DoIf(joblet->UserJobMonitoringDescriptor.has_value(), [&] (TFluentMap fluent) {
            fluent.Item("monitoring_descriptor").Value(ToString(*joblet->UserJobMonitoringDescriptor));
        })
        .DoIf(joblet->PredecessorType != EPredecessorType::None, [&] (TFluentMap fluent) {
            fluent
                .Item("predecessor_type").Value(joblet->PredecessorType)
                .Item("predecessor_job_id").Value(joblet->PredecessorJobId);
        })
        .Item("allocation_id").Value(AllocationIdFromJobId(joblet->JobId))
        .Do([&] (TFluentMap fluent) {
            EnrichJobInfo(fluent, joblet);
        });
}

void TOperationControllerBase::BuildFinishedJobAttributes(
    const TJobletPtr& joblet,
    TJobSummary* jobSummary,
    bool hasStderr,
    bool hasFailContext,
    TFluentMap fluent) const
{
    auto stderrSize = hasStderr
        // Report nonzero stderr size as we are sure it is saved.
        ? std::max(joblet->StderrSize, static_cast<i64>(1))
        : 0;

    i64 failContextSize = hasFailContext ? 1 : 0;

    BuildJobAttributes(joblet, jobSummary->State, stderrSize, fluent);

    bool includeError = jobSummary->State == EJobState::Failed ||
        jobSummary->State == EJobState::Aborted;
    fluent
        .Item("finish_time").Value(joblet->FinishTime)
        .DoIf(includeError, [&] (TFluentMap fluent) {
            fluent.Item("error").Value(jobSummary->GetError());
        })
        .DoIf(jobSummary->GetJobResult().HasExtension(TJobResultExt::job_result_ext),
            [&] (TFluentMap fluent)
        {
            const auto& jobResultExt = jobSummary->GetJobResultExt();
            fluent.Item("core_infos").Value(jobResultExt.core_infos());
        })
        .Item("fail_context_size").Value(failContextSize);
}

TFluentLogEvent TOperationControllerBase::LogFinishedJobFluently(
    ELogEventType eventType,
    const TJobletPtr& joblet)
{
    auto statistics = joblet->BuildCombinedStatistics();
    // Table rows cannot have top-level attributes, so we drop statistics timestamp here.
    statistics.SetTimestamp(std::nullopt);

    return LogEventFluently(eventType)
        .Item("job_id").Value(joblet->JobId)
        .Item("allocation_id").Value(AllocationIdFromJobId(joblet->JobId))
        .Item("operation_id").Value(OperationId_)
        .Item("start_time").Value(joblet->StartTime)
        .Item("finish_time").Value(joblet->FinishTime)
        .Item("waiting_for_resources_duration").Value(joblet->WaitingForResourcesDuration)
        .Item("resource_limits").Value(joblet->ResourceLimits)
        .Item("statistics").Value(statistics)
        .Item("node_address").Value(NNodeTrackerClient::GetDefaultAddress(joblet->NodeDescriptor.Addresses))
        .Item("job_type").Value(joblet->JobType)
        .Item("task_name").Value(joblet->TaskName)
        .Item("interruption_reason").Value(joblet->InterruptionReason)
        .Item("job_competition_id").Value(joblet->CompetitionIds[EJobCompetitionType::Speculative])
        .Item("probing_job_competition_id").Value(joblet->CompetitionIds[EJobCompetitionType::Probing])
        .Item("has_competitors").Value(joblet->HasCompetitors[EJobCompetitionType::Speculative])
        .Item("has_probing_competitors").Value(joblet->HasCompetitors[EJobCompetitionType::Probing])
        .Item("tree_id").Value(joblet->TreeId)
        .DoIf(joblet->PredecessorType != EPredecessorType::None, [&] (TFluentMap fluent) {
            fluent
                .Item("predecessor_type").Value(joblet->PredecessorType)
                .Item("predecessor_job_id").Value(joblet->PredecessorJobId);
        })
        .Do([&] (TFluentMap fluent) {
            EnrichJobInfo(fluent, joblet);
        });
}

IYsonConsumer* TOperationControllerBase::GetEventLogConsumer()
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    return EventLogConsumer_.get();
}

const NLogging::TLogger* TOperationControllerBase::GetEventLogger()
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    return &ControllerEventLogger();
}

void TOperationControllerBase::OnChunkFailed(TChunkId chunkId, TJobId jobId)
{
    if (chunkId == NullChunkId) {
        YT_LOG_WARNING("Incompatible unavailable chunk found; deprecated node version");
        return;
    }

    // Dynamic stores cannot be located by the controller, let the job do its job.
    if (IsDynamicTabletStoreType(TypeFromId(chunkId))) {
        return;
    }

    if (!InputManager_->OnInputChunkFailed(chunkId, jobId)) {
        YT_LOG_DEBUG("Intermediate chunk has failed (ChunkId: %v, JobId: %v)", chunkId, jobId);
        if (!OnIntermediateChunkUnavailable(chunkId)) {
            return;
        }

        IntermediateChunkScraper_->Start();
    }
}

void TOperationControllerBase::SafeOnIntermediateChunkBatchLocated(
    std::vector<TScrapedChunkInfo> chunkBatch)
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker());

    int availableCount = 0;
    int unavailableCount = 0;
    for (const auto& chunkInfo : chunkBatch) {
        if (chunkInfo.Missing) {
            // We can unstage intermediate chunks (e.g. in automerge) - just skip them.
            continue;
        }

        // Intermediate chunks are always replicated.
        if (IsUnavailable(chunkInfo.Replicas, NErasure::ECodec::None, GetChunkAvailabilityPolicy())) {
            ++unavailableCount;
            OnIntermediateChunkUnavailable(chunkInfo.ChunkId);
        } else {
            ++availableCount;
            OnIntermediateChunkAvailable(chunkInfo.ChunkId, chunkInfo.Replicas);
        }
    }

    YT_LOG_DEBUG(
        "Intermediate chunk batch has located (ChunkBatchSize: %v, AvailableCount: %v, UnavailableCount: %v)",
        chunkBatch.size(),
        availableCount,
        unavailableCount);
}

bool TOperationControllerBase::OnIntermediateChunkUnavailable(TChunkId chunkId)
{
    auto& completedJob = GetOrCrash(ChunkOriginMap_, chunkId);

    YT_LOG_DEBUG(
        "Intermediate chunk is lost (ChunkId: %v, JobId: %v, Restartable: %v, Suspended: %v)",
        chunkId,
        completedJob->JobId,
        completedJob->Restartable,
        completedJob->Suspended);

    // If completedJob->Restartable == false, that means that source pool/task don't support lost jobs
    // and we have to use scraper to find new replicas of intermediate chunks.

    if (!completedJob->Restartable && Spec_->UnavailableChunkTactics == EUnavailableChunkAction::Fail) {
        auto error = TError("Intermediate chunk is unavailable")
            << TErrorAttribute("chunk_id", chunkId);
        OnOperationFailed(error, true);
        return false;
    }

    // If job is replayable, we don't track individual unavailable chunks,
    // since we will regenerate them all anyway.
    if (!completedJob->Restartable &&
        completedJob->UnavailableChunks.insert(chunkId).second)
    {
        ++UnavailableIntermediateChunkCount_;
    }

    if (completedJob->Suspended) {
        return false;
    }

    YT_LOG_DEBUG(
        "Job is lost (Address: %v, JobId: %v, SourceTask: %v, OutputCookie: %v, InputCookie: %v, "
        "Restartable: %v, ChunkId: %v, UnavailableIntermediateChunkCount: %v)",
        NNodeTrackerClient::GetDefaultAddress(completedJob->NodeDescriptor.Addresses),
        completedJob->JobId,
        completedJob->SourceTask->GetTitle(),
        completedJob->OutputCookie,
        completedJob->InputCookie,
        completedJob->Restartable,
        chunkId,
        UnavailableIntermediateChunkCount_);

    completedJob->Suspended = true;
    completedJob->DestinationPool->Suspend(completedJob->InputCookie);

    if (completedJob->Restartable) {
        TForbidContextSwitchGuard guard;

        completedJob->SourceTask->GetChunkPoolOutput()->Lost(completedJob->OutputCookie);
        completedJob->SourceTask->OnJobLost(completedJob, chunkId);
        UpdateTask(completedJob->SourceTask.Get());
    }

    return true;
}

void TOperationControllerBase::OnIntermediateChunkAvailable(
    TChunkId chunkId,
    const TChunkReplicaList& replicas)
{
    auto& completedJob = GetOrCrash(ChunkOriginMap_, chunkId);

    if (completedJob->Restartable || !completedJob->Suspended) {
        // Job will either be restarted or all chunks are fine.
        return;
    }

    if (completedJob->UnavailableChunks.erase(chunkId) == 1) {
        --UnavailableIntermediateChunkCount_;

        YT_LOG_DEBUG(
            "Unavailable intermediate chunk was found (JobId: %v, InputCookie: %v, ChunkId: %v, UnavailableIntermediateChunkCount: %v)",
            completedJob->JobId,
            completedJob->InputCookie,
            chunkId,
            UnavailableIntermediateChunkCount_);

        for (auto& dataSlice : completedJob->InputStripe->DataSlices) {
            // Intermediate chunks are always unversioned.
            auto inputChunk = dataSlice->GetSingleUnversionedChunk();
            if (inputChunk->GetChunkId() == chunkId) {
                inputChunk->SetReplicas(replicas);
            }
        }

        YT_VERIFY(
            UnavailableIntermediateChunkCount_ > 0 ||
            (UnavailableIntermediateChunkCount_ == 0 && completedJob->UnavailableChunks.empty()));
        if (completedJob->UnavailableChunks.empty()) {
            YT_LOG_DEBUG(
                "Found all unavailable chunks for job, job result is resumed (JobId: %v, InputCookie: %v, UnavailableIntermediateChunkCount: %v)",
                completedJob->JobId,
                completedJob->InputCookie,
                UnavailableIntermediateChunkCount_);

            completedJob->Suspended = false;
            completedJob->DestinationPool->Resume(completedJob->InputCookie);

            // TODO(psushin).
            // Unfortunately we don't know what task we are resuming, so
            // we update them all.
            UpdateAllTasks();
        }
    }
}

bool TOperationControllerBase::AreForeignTablesSupported() const
{
    return false;
}

bool TOperationControllerBase::IsLegacyOutputLivePreviewSupported() const
{
    return !IsLegacyLivePreviewSuppressed_ &&
        (GetLegacyOutputLivePreviewMode() == ELegacyLivePreviewMode::DoNotCare ||
        GetLegacyOutputLivePreviewMode() == ELegacyLivePreviewMode::ExplicitlyEnabled);
}

bool TOperationControllerBase::IsOutputLivePreviewSupported() const
{
    return !OutputTables_.empty();
}

bool TOperationControllerBase::IsLegacyIntermediateLivePreviewSupported() const
{
    return !IsLegacyLivePreviewSuppressed_ &&
        (GetLegacyIntermediateLivePreviewMode() == ELegacyLivePreviewMode::DoNotCare ||
        GetLegacyIntermediateLivePreviewMode() == ELegacyLivePreviewMode::ExplicitlyEnabled);
}

bool TOperationControllerBase::IsIntermediateLivePreviewSupported() const
{
    return false;
}

TDataFlowGraph::TVertexDescriptor TOperationControllerBase::GetOutputLivePreviewVertexDescriptor() const
{
    return TDataFlowGraph::SinkDescriptor;
}

ELegacyLivePreviewMode TOperationControllerBase::GetLegacyOutputLivePreviewMode() const
{
    return ELegacyLivePreviewMode::NotSupported;
}

ELegacyLivePreviewMode TOperationControllerBase::GetLegacyIntermediateLivePreviewMode() const
{
    return ELegacyLivePreviewMode::NotSupported;
}

bool TOperationControllerBase::CheckUserTransactionAlive()
{
    if (!UserTransaction_) {
        return true;
    }

    auto result = WaitFor(UserTransaction_->Ping());
    if (result.FindMatching(NTransactionClient::EErrorCode::NoSuchTransaction)) {
        OnOperationAborted(GetUserTransactionAbortedError(UserTransaction_->GetId()));
        return false;
    }

    return true;
}

void TOperationControllerBase::OnTransactionsAborted(const std::vector<TTransactionId>& transactionIds)
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker());

    if (!CheckUserTransactionAlive()) {
        return;
    }

    DoFailOperation(
        GetSchedulerTransactionsAbortedError(transactionIds),
        /*flush*/ false);
}

TControllerTransactionIds TOperationControllerBase::GetTransactionIds()
{
    auto getId = [] (const NApi::ITransactionPtr& transaction) {
        return transaction ? transaction->GetId() : NTransactionClient::TTransactionId();
    };

    TControllerTransactionIds transactionIds;
    transactionIds.AsyncId = getId(AsyncTransaction_);
    transactionIds.OutputId = getId(OutputTransaction_);
    transactionIds.DebugId = getId(DebugTransaction_);
    transactionIds.OutputCompletionId = getId(OutputCompletionTransaction_);
    transactionIds.DebugCompletionId = getId(DebugCompletionTransaction_);
    InputTransactions_->FillSchedulerTransactionIds(&transactionIds);

    return transactionIds;
}

bool TOperationControllerBase::IsInputDataSizeHistogramSupported() const
{
    return false;
}

void TOperationControllerBase::SafeTerminate(EControllerState finalState)
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker());

    YT_LOG_INFO("Terminating operation controller");

    RemoveRemainingJobsOnOperationFinished();

    if (Spec_->TestingOperationOptions->ThrowExceptionDuringOperationAbort) {
        // NB: Task subscriptions are not finalized on test exception.
        THROW_ERROR_EXCEPTION("Test exception");
    }

    // NB: Errors ignored since we cannot do anything with it.
    Y_UNUSED(WaitFor(Host_->FlushOperationNode()));

    bool debugTransactionCommitted = false;

    // Skip committing anything if operation controller already tried to commit results.
    if (!CommitFinished_) {
        try {
            FinalizeSubscriptions();
        } catch (const std::exception& ex) {
            YT_LOG_ERROR(ex, "Failed to finalize subscriptions");
        }

        try {
            FinalizeFeatures();
            CommitFeatures();
        } catch (const std::exception& ex) {
            YT_LOG_ERROR(ex, "Failed to finalize and commit features");
        }

        std::vector<TOutputTablePtr> tables;
        if (StderrTable_ && StderrTable_->IsPrepared()) {
            tables.push_back(StderrTable_);
        }
        if (CoreTable_ && CoreTable_->IsPrepared()) {
            tables.push_back(CoreTable_);
        }

        if (!tables.empty()) {
            YT_VERIFY(DebugTransaction_);

            try {
                StartDebugCompletionTransaction();
                BeginUploadOutputTables(tables);
                AttachOutputChunks(tables);
                EndUploadOutputTables(tables);
                CommitDebugCompletionTransaction();

                WaitFor(DebugTransaction_->Commit())
                    .ThrowOnError();
                debugTransactionCommitted = true;
            } catch (const std::exception& ex) {
                // Bad luck we can't commit transaction.
                // Such a pity can happen for example if somebody aborted our transaction manually.
                YT_LOG_ERROR(ex, "Failed to commit debug transaction");
                // Intentionally do not wait for abort.
                // Transaction object may be in incorrect state, we need to abort using only transaction id.
                YT_UNUSED_FUTURE(AttachTransaction(DebugTransaction_->GetId(), Client_)->Abort());
            }
        }
    }

    std::vector<TFuture<void>> abortTransactionFutures;
    THashMap<ITransactionPtr, TFuture<void>> transactionToAbortFuture;
    auto abortTransaction = [&] (const ITransactionPtr& transaction, const NNative::IClientPtr& client, bool sync = true) {
        if (transaction) {
            TFuture<void> abortFuture;
            auto it = transactionToAbortFuture.find(transaction);
            if (it == transactionToAbortFuture.end()) {
                // Transaction object may be in incorrect state, we need to abort using only transaction id.
                abortFuture = AttachTransaction(transaction->GetId(), client)->Abort();
                YT_VERIFY(transactionToAbortFuture.emplace(transaction, abortFuture).second);
            } else {
                abortFuture = it->second;
            }

            if (sync) {
                abortTransactionFutures.push_back(abortFuture);
            }
        }
    };

    // NB: We do not abort input transactions synchronously since
    // some of them can belong to an unavailable remote cluster.
    // Moreover if input transaction abort failed it does not harm anything.
    YT_UNUSED_FUTURE(AbortInputTransactions());

    abortTransaction(OutputTransaction_, SchedulerOutputClient_);
    abortTransaction(AsyncTransaction_, SchedulerClient_, /*sync*/ false);
    if (!debugTransactionCommitted) {
        abortTransaction(DebugTransaction_, SchedulerClient_, /*sync*/ false);
    }

    WaitFor(AllSucceeded(abortTransactionFutures))
        .ThrowOnError();

    YT_VERIFY(finalState == EControllerState::Aborted || finalState == EControllerState::Failed);
    State_ = finalState;

    LogProgress(/*force*/ true);

    YT_LOG_INFO("Operation controller terminated");
}

void TOperationControllerBase::SafeComplete()
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker());

    OnOperationCompleted(true);
}

void TOperationControllerBase::CheckTimeLimit()
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker());

    auto timeLimit = GetTimeLimit();
    if (timeLimit) {
        if (TInstant::Now() - StartTime_ > *timeLimit) {
            OnOperationTimeLimitExceeded();
        }
    }
}

void TOperationControllerBase::CheckAvailableExecNodes()
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker());

    if (ShouldSkipSanityCheck()) {
        return;
    }

    // If no available nodes were seen then re-check all nodes on each tick.
    // After such nodes were discovered, only re-check within BannedExecNodesCheckPeriod.
    auto now = TInstant::Now();
    if (AvailableExecNodesObserved_ && now < LastAvailableExecNodesCheckTime_ + Config_->BannedExecNodesCheckPeriod) {
        return;
    }
    LastAvailableExecNodesCheckTime_ = now;

    TSchedulingTagFilter tagFilter(Spec_->SchedulingTagFilter);
    std::string observedExecNodeAddress;
    bool foundMatching = false;
    bool foundMatchingNotBanned = false;
    int otherTreesNodeCount = 0;
    int nonMatchingFilterNodeCount = 0;
    THashMap<TString, TEnumIndexedArray<EJobResourceWithDiskQuotaType, i64>> insufficientResourcesNodeCountPerTask;
    for (const auto& [_, descriptor] : GetExecNodeDescriptors()) {
        bool hasSuitableTree = false;
        for (const auto& [treeName, settings] : PoolTreeControllerSettingsMap_) {
            if (descriptor->CanSchedule(settings.SchedulingTagFilter)) {
                hasSuitableTree = true;
                break;
            }
        }

        if (!hasSuitableTree) {
            ++otherTreesNodeCount;
            continue;
        }

        if (!descriptor->CanSchedule(tagFilter)) {
            ++nonMatchingFilterNodeCount;
            continue;
        }

        bool hasNonTrivialTasks = false;
        bool hasEnoughResources = false;
        for (const auto& task : Tasks_) {
            if (task->HasNoPendingJobs()) {
                continue;
            }
            hasNonTrivialTasks = true;

            const auto& neededResources = task->GetMinNeededResources();
            bool taskHasEnoughResources = true;
            TEnumIndexedArray<EJobResourceWithDiskQuotaType, bool> taskHasEnoughResourcesPerResource;

            auto processJobResourceType = [&] (auto resourceLimit, auto resource, EJobResourceWithDiskQuotaType type) {
                if (resource > resourceLimit) {
                    taskHasEnoughResources = false;
                } else {
                    taskHasEnoughResourcesPerResource[type] = true;
                }
            };

            #define XX(name, Name) processJobResourceType( \
                descriptor->ResourceLimits.Get##Name(), \
                neededResources.ToJobResources().Get##Name(), \
                EJobResourceWithDiskQuotaType::Name);
            ITERATE_JOB_RESOURCES(XX)
            #undef XX

            taskHasEnoughResourcesPerResource[EJobResourceWithDiskQuotaType::DiskQuota]
                = CanSatisfyDiskQuotaRequest(descriptor->DiskResources, neededResources.DiskQuota(), /*considerUsage*/ false);

            taskHasEnoughResources &= taskHasEnoughResourcesPerResource[EJobResourceWithDiskQuotaType::DiskQuota];
            hasEnoughResources |= taskHasEnoughResources;

            if (hasEnoughResources) {
                break;
            }

            for (auto resourceType : TEnumTraits<EJobResourceWithDiskQuotaType>::GetDomainValues()) {
                insufficientResourcesNodeCountPerTask[task->GetVertexDescriptor()][resourceType] +=
                    !taskHasEnoughResourcesPerResource[resourceType];
            }
        }
        if (hasNonTrivialTasks && !hasEnoughResources) {
            continue;
        }

        observedExecNodeAddress = NNodeTrackerClient::GetDefaultAddress(descriptor->Addresses);
        foundMatching = true;

        if (!BannedNodeIds_.contains(descriptor->Id)) {
            foundMatchingNotBanned = true;
            // foundMatchingNotBanned also implies foundMatching, hence we interrupt.
            break;
        }
    }

    if (foundMatching) {
        AvailableExecNodesObserved_ = true;
    }

    if (!AvailableExecNodesObserved_) {
        TStringBuilder errorMessageBuilder;
        errorMessageBuilder.AppendFormat(
            "Found no nodes with enough resources to schedule an allocation that are online in trees %v",
            GetKeys(PoolTreeControllerSettingsMap_));
        if (!tagFilter.IsEmpty()) {
            errorMessageBuilder.AppendFormat(
                " and match scheduling tag filter %Qv",
                tagFilter);
        }

        DoFailOperation(TError(
            NControllerAgent::EErrorCode::NoOnlineNodeToScheduleAllocation,
            errorMessageBuilder.Flush(),
            TError::DisableFormat)
            << TErrorAttribute("other_trees_node_count", otherTreesNodeCount)
            << TErrorAttribute("non_matching_filter_node_count", nonMatchingFilterNodeCount)
            << TErrorAttribute("insufficient_resources_node_count_per_task", insufficientResourcesNodeCountPerTask));
        return;
    }

    if (foundMatching && !foundMatchingNotBanned && Spec_->FailOnAllNodesBanned) {
        TStringBuilder errorMessageBuilder;
        errorMessageBuilder.AppendFormat(
            "All suitable online nodes in trees %v",
            GetKeys(PoolTreeControllerSettingsMap_));
        if (!tagFilter.IsEmpty()) {
            errorMessageBuilder.AppendFormat(
                " that match scheduling tag filter %Qv",
                tagFilter);
        }
        errorMessageBuilder.AppendString(" were banned");

        // NB(eshcherbin): This should happen always, currently this option could be the only reason to ban a node.
        if (Spec_->BanNodesWithFailedJobs) {
            errorMessageBuilder.AppendString(
                "; (\"ban_nodes_with_failed_jobs\" spec option is set, try investigating your job failures)");
        }

        DoFailOperation(TError(errorMessageBuilder.Flush(), TError::DisableFormat));
        return;
    }

    YT_LOG_DEBUG("Available exec nodes check succeeded (ObservedNodeAddress: %v)",
        observedExecNodeAddress);
}

void TOperationControllerBase::CheckMinNeededResourcesSanity()
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker());

    if (ShouldSkipSanityCheck()) {
        return;
    }

    for (const auto& task : Tasks_) {
        if (task->HasNoPendingJobs()) {
            continue;
        }

        const auto& neededResources = task->GetMinNeededResources();
        if (!Dominates(*CachedMaxAvailableExecNodeResources_, neededResources.ToJobResources())) {
            DoFailOperation(
                TError(
                    NControllerAgent::EErrorCode::NoOnlineNodeToScheduleAllocation,
                    "No online node can satisfy the resource demand")
                    << TErrorAttribute("task_name", task->GetTitle())
                    << TErrorAttribute("needed_resources", neededResources.ToJobResources())
                    << TErrorAttribute("max_available_resources", *CachedMaxAvailableExecNodeResources_));
        }
    }
}

TControllerScheduleAllocationResultPtr TOperationControllerBase::SafeScheduleAllocation(
    const TAllocationSchedulingContext& context,
    const TString& treeId)
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker(Config_->ScheduleAllocationControllerQueue));

    MaybeDelay(Spec_->TestingOperationOptions->ScheduleAllocationDelay);

    if (State_ != EControllerState::Running) {
        YT_LOG_DEBUG("Stale schedule allocation attempt");
        return nullptr;
    }

    // SafeScheduleAllocation must be synchronous; context switches are prohibited.
    TForbidContextSwitchGuard contextSwitchGuard;

    auto allocationIt = EmplaceOrCrash(
        AllocationMap_,
        context.GetAllocationId(),
        TAllocation{
            .Id = context.GetAllocationId(),
            .TreeId = treeId,
        });

    auto& allocation = allocationIt->second;

    auto removeAllocationOnScheduleAllocationFailureGuard = Finally([&] {
        AllocationMap_.erase(allocationIt);
    });

    NProfiling::TWallTimer timer;
    auto scheduleAllocationResult = New<TControllerScheduleAllocationResult>();
    DoScheduleAllocation(allocation, context, treeId, scheduleAllocationResult.Get());
    auto scheduleAllocationDuration = timer.GetElapsedTime();
    if (scheduleAllocationResult->StartDescriptor) {
        AvailableExecNodesObserved_ = true;
    }
    scheduleAllocationResult->Duration = scheduleAllocationDuration;
    scheduleAllocationResult->ControllerEpoch = ControllerEpoch_;

    ScheduleAllocationStatistics_->RecordJobResult(*scheduleAllocationResult);
    scheduleAllocationResult->NextDurationEstimate = ScheduleAllocationStatistics_->SuccessfulDurationMovingAverage().GetAverage();

    auto now = NProfiling::GetCpuInstant();
    if (now > ScheduleAllocationStatisticsLogDeadline_) {
        AccountExternalScheduleAllocationFailures();

        YT_LOG_DEBUG(
            "Schedule allocation statistics (Count: %v, TotalDuration: %v, SuccessfulDurationEstimate: %v, FailureReasons: %v)",
            ScheduleAllocationStatistics_->GetCount(),
            ScheduleAllocationStatistics_->GetTotalDuration(),
            ScheduleAllocationStatistics_->SuccessfulDurationMovingAverage().GetAverage(),
            ScheduleAllocationStatistics_->Failed());

        ScheduleAllocationStatisticsLogDeadline_ = now + NProfiling::DurationToCpuDuration(Config_->ScheduleAllocationStatisticsLogBackoff);
    }

    if (scheduleAllocationResult->StartDescriptor) {
        removeAllocationOnScheduleAllocationFailureGuard.Release();

        Host_->RegisterAllocation(TStartedAllocationInfo{
            .AllocationId = context.GetAllocationId(),
            .NodeAddress = NNodeTrackerClient::GetDefaultAddress(context.GetNodeDescriptor().Addresses),
        });
    }

    return scheduleAllocationResult;
}

bool TOperationControllerBase::ShouldSkipScheduleAllocationRequest() const noexcept
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    auto now = TInstant::Now();

    bool forceLogging = LastControllerJobSchedulingThrottlingLogTime_ + Config_->ControllerThrottlingLogBackoff < now;
    if (forceLogging) {
        LastControllerJobSchedulingThrottlingLogTime_ = now;
    }

    // Check job spec limits.
    bool jobSpecThrottlingActive = false;
    {
        auto buildingJobSpecCount = BuildingJobSpecCount_.load();
        auto totalBuildingJobSpecSliceCount = TotalBuildingJobSpecSliceCount_.load();
        auto avgSliceCount = totalBuildingJobSpecSliceCount / std::max<double>(1.0, buildingJobSpecCount);
        if (Options_->ControllerBuildingJobSpecCountLimit) {
            jobSpecThrottlingActive |= buildingJobSpecCount > *Options_->ControllerBuildingJobSpecCountLimit;
        }
        if (Options_->ControllerTotalBuildingJobSpecSliceCountLimit) {
            jobSpecThrottlingActive |= totalBuildingJobSpecSliceCount > *Options_->ControllerTotalBuildingJobSpecSliceCountLimit;
        }

        if (jobSpecThrottlingActive || forceLogging) {
            YT_LOG_DEBUG(
                "Throttling status for building job specs (JobSpecCount: %v, JobSpecCountLimit: %v, TotalJobSpecSliceCount: %v, "
                "TotalJobSpecSliceCountLimit: %v, AvgJobSpecSliceCount: %v, JobSpecThrottlingActive: %v)",
                buildingJobSpecCount,
                Options_->ControllerBuildingJobSpecCountLimit,
                totalBuildingJobSpecSliceCount,
                Options_->ControllerTotalBuildingJobSpecSliceCountLimit,
                avgSliceCount,
                jobSpecThrottlingActive);
        }
    }

    // Check invoker wait time.
    bool waitTimeThrottlingActive = false;
    {
        auto scheduleAllocationInvokerStatistics = GetInvokerStatistics(Config_->ScheduleAllocationControllerQueue);
        auto scheduleJobWaitTime = scheduleAllocationInvokerStatistics.TotalTimeEstimate;
        waitTimeThrottlingActive = scheduleJobWaitTime > Config_->ScheduleAllocationTotalTimeThreshold;

        if (waitTimeThrottlingActive || forceLogging) {
            YT_LOG_DEBUG(
                "Throttling status for wait time "
                "(ScheduleAllocationWaitTime: %v, Threshold: %v, WaitTimeThrottlingActive: %v)",
                scheduleJobWaitTime,
                Config_->ScheduleAllocationTotalTimeThreshold,
                waitTimeThrottlingActive);
        }
    }

    return jobSpecThrottlingActive || waitTimeThrottlingActive;
}

bool TOperationControllerBase::ShouldSkipRunningJobEvents() const noexcept
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    auto now = TInstant::Now();

    bool forceLogging = LastControllerJobEventThrottlingLogTime_ + Config_->ControllerThrottlingLogBackoff < now;
    if (forceLogging) {
        LastControllerJobEventThrottlingLogTime_ = now;
    }

    // Check invoker wait time.
    bool waitTimeThrottlingActive = false;
    {
        auto jobEventsInvokerStatistics = GetInvokerStatistics(Config_->JobEventsControllerQueue);
        auto jobEventsWaitTime = jobEventsInvokerStatistics.TotalTimeEstimate;
        waitTimeThrottlingActive = jobEventsWaitTime > Config_->JobEventsTotalTimeThreshold;

        if (waitTimeThrottlingActive || forceLogging) {
            YT_LOG_DEBUG(
                "Throttling status for job events wait time "
                "(JobEventsWaitTime: %v, Threshold: %v, WaitTimeThrottlingActive: %v)",
                jobEventsWaitTime,
                Config_->JobEventsTotalTimeThreshold,
                waitTimeThrottlingActive);
        }
    }

    return waitTimeThrottlingActive;
}

void TOperationControllerBase::RecordScheduleAllocationFailure(EScheduleFailReason reason) noexcept
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    ExternalScheduleAllocationFailureCounts_[reason].fetch_add(1);
}

void TOperationControllerBase::AccountBuildingJobSpecDelta(int countDelta, i64 totalSliceCountDelta) noexcept
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    BuildingJobSpecCount_.fetch_add(countDelta);
    TotalBuildingJobSpecSliceCount_.fetch_add(totalSliceCountDelta);
}

void TOperationControllerBase::UpdateConfig(const TControllerAgentConfigPtr& config)
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker());

    Config_ = config;

    RunningJobStatisticsUpdateExecutor_->SetPeriod(config->RunningJobStatisticsUpdatePeriod);
    SendRunningAllocationTimeStatisticsUpdatesExecutor_->SetPeriod(config->RunningAllocationTimeStatisticsUpdatesSendPeriod);

    ScheduleAllocationStatistics_->SetMovingAverageWindowSize(config->ScheduleAllocationStatisticsMovingAverageWindowSize);
    DiagnosableInvokerPool_->UpdateActionTimeRelevancyHalflife(config->InvokerPoolTotalTimeAggregationPeriod);
}

void TOperationControllerBase::CustomizeJoblet(const TJobletPtr& /*joblet*/, const TAllocation& /*allocation*/)
{ }

void TOperationControllerBase::CustomizeJobSpec(const TJobletPtr& joblet, TJobSpec* jobSpec) const
{
    YT_ASSERT_INVOKER_AFFINITY(JobSpecBuildInvoker_);

    auto* jobSpecExt = jobSpec->MutableExtension(TJobSpecExt::job_spec_ext);

    jobSpecExt->set_testing_options(ToProto(ConvertToYsonString(Spec_->JobTestingOptions)));

    jobSpecExt->set_enable_prefetching_job_throttler(true);

    jobSpecExt->set_enable_codegen_comparator(Spec_->EnableCodegenComparator);

    jobSpecExt->set_enable_virtual_sandbox(Spec_->EnableVirtualSandbox);

    jobSpecExt->set_enable_root_volume_disk_quota(Spec_->EnableRootVolumeDiskQuota);

    jobSpecExt->set_disable_rename_columns_compatibility_code(Spec_->DisableRenameColumnsCompatibilityCode);

    jobSpecExt->set_use_cluster_throttlers(Spec_->UseClusterThrottlers);

    for (auto& [clusterName, protoRemoteCluster] : *(jobSpecExt->mutable_remote_input_clusters())) {
        auto clusterConfigIt = Config_->RemoteOperations.find(TClusterName(clusterName));
        if (clusterConfigIt != Config_->RemoteOperations.end()) {
            const auto& networks = clusterConfigIt->second->Networks;
            if (networks) {
                ToProto(protoRemoteCluster.mutable_networks(), *networks);
            }
        }
    }

    if (OutputTransaction_) {
        ToProto(jobSpecExt->mutable_output_transaction_id(), OutputTransaction_->GetId());
    }

    if (joblet->EnabledJobProfiler) {
        auto* profiler = jobSpecExt->add_job_profilers();
        ToProto(profiler, *joblet->EnabledJobProfiler);
    }

    if (joblet->Task->GetUserJobSpec()) {
        InitUserJobSpec(
            jobSpecExt->mutable_user_job_spec(),
            joblet);
    }

    if (AcoName_) {
        jobSpecExt->set_aco_name(*AcoName_);
    } else {
        jobSpecExt->set_acl(ToProto(ConvertToYsonString(Acl_)));
    }
}

void TOperationControllerBase::RegisterTask(TTaskPtr task)
{
    task->Initialize();
    task->Prepare();
    task->RegisterCounters(TotalJobCounter_);
    Tasks_.emplace_back(std::move(task));
}

void TOperationControllerBase::UpdateTask(TTask* task)
{
    if (!task) {
        return;
    }

    auto oldPendingJobCount = CachedPendingJobCount_.Load();
    auto newPendingJobCount = CachedPendingJobCount_.Load() + task->GetPendingJobCountDelta();
    CachedPendingJobCount_.Store(newPendingJobCount);

    int oldTotalJobCount = CachedTotalJobCount_;
    int newTotalJobCount = CachedTotalJobCount_ + task->GetTotalJobCountDelta();
    CachedTotalJobCount_ = newTotalJobCount;

    IncreaseNeededResources(task->GetTotalNeededResourcesDelta());

    // TODO(max42): move this logging into pools.
    YT_LOG_DEBUG_IF(
        newPendingJobCount != oldPendingJobCount || newTotalJobCount != oldTotalJobCount,
        "Task updated (Task: %v, PendingJobCount: %v -> %v, TotalJobCount: %v -> %v, NeededResources: %v)",
        task->GetTitle(),
        oldPendingJobCount,
        newPendingJobCount,
        oldTotalJobCount,
        newTotalJobCount,
        CachedNeededResources_);

    task->CheckCompleted();
}

void TOperationControllerBase::UpdateAllTasks()
{
    for (const auto& task : Tasks_) {
        UpdateTask(task.Get());
    }
}

void TOperationControllerBase::UpdateAllTasksIfNeeded()
{
    auto now = NProfiling::GetCpuInstant();
    if (now < TaskUpdateDeadline_) {
        return;
    }
    UpdateAllTasks();
    TaskUpdateDeadline_ = now + NProfiling::DurationToCpuDuration(Config_->TaskUpdatePeriod);
}

void TOperationControllerBase::ResetTaskLocalityDelays()
{
    YT_LOG_DEBUG("Task locality delays are reset");
    for (const auto& task : Tasks_) {
        task->SetDelayedTime(std::nullopt);
    }
}

void TOperationControllerBase::DoScheduleAllocation(
    TAllocation& allocation,
    const TAllocationSchedulingContext& context,
    const TString& treeId,
    TControllerScheduleAllocationResult* scheduleAllocationResult)
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker(Config_->ScheduleAllocationControllerQueue));

    if (!IsRunning()) {
        YT_LOG_TRACE("Operation is not running, scheduling request ignored");
        scheduleAllocationResult->RecordFail(EScheduleFailReason::OperationNotRunning);
        GetScheduleJobProfiler()->ProfileScheduleJobFailure(
            allocation.TreeId,
            EScheduleFailReason::OperationNotRunning);
        return;
    }

    if (GetPendingJobCount().GetJobCountFor(treeId) == 0) {
        YT_LOG_TRACE("No pending jobs left, scheduling request ignored");
        scheduleAllocationResult->RecordFail(EScheduleFailReason::NoPendingJobs);
        GetScheduleJobProfiler()->ProfileScheduleJobFailure(
            allocation.TreeId,
            EScheduleFailReason::NoPendingJobs);
        return;
    }

    if (BannedNodeIds_.find(context.GetNodeDescriptor().Id) != BannedNodeIds_.end()) {
        YT_LOG_TRACE("Node is banned, scheduling request ignored");
        scheduleAllocationResult->RecordFail(EScheduleFailReason::NodeBanned);
        GetScheduleJobProfiler()->ProfileScheduleJobFailure(
            allocation.TreeId,
            EScheduleFailReason::NodeBanned);
        return;
    }

    MaybeDelay(Spec_->TestingOperationOptions->InsideScheduleAllocationDelay);

    TryScheduleFirstJob(allocation, context, scheduleAllocationResult, /*scheduleLocalJob*/ true);
    if (!scheduleAllocationResult->StartDescriptor) {
        TryScheduleFirstJob(allocation,context, scheduleAllocationResult, /*scheduleLocalJob*/ false);
    }
}

void TOperationControllerBase::TryScheduleFirstJob(
    TAllocation& allocation,
    const TAllocationSchedulingContext& context,
    TControllerScheduleAllocationResult* scheduleAllocationResult,
    bool scheduleLocalJob)
{
    if (!IsRunning()) {
        GetScheduleJobProfiler()->ProfileScheduleJobFailure(
            allocation.TreeId,
            EScheduleFailReason::OperationNotRunning);
        scheduleAllocationResult->RecordFail(EScheduleFailReason::OperationNotRunning);
        return;
    }

    for (const auto& task : Tasks_) {
        if (scheduleAllocationResult->IsScheduleStopNeeded()) {
            break;
        }

        if (auto failReason = TryScheduleJob(allocation, *task, context, scheduleLocalJob, std::nullopt)) {
            if (*failReason == EScheduleFailReason::NoPendingJobs) {
                continue;
            }

            scheduleAllocationResult->RecordFail(*failReason);

            GetScheduleJobProfiler()->ProfileScheduleJobFailure(
                allocation.TreeId,
                task->GetJobType(),
                *failReason,
                /*isJobFirst*/ true);
        } else {
            GetScheduleJobProfiler()->ProfileScheduleJobSuccess(
                allocation.TreeId,
                task->GetJobType(),
                /*isJobFirst*/ true);

            auto startDescriptor = task->CreateAllocationStartDescriptor(
                allocation,
                /*allowIdleCpuPolicy*/ IsIdleCpuPolicyAllowedInTree(allocation.TreeId),
                *context.GetScheduleAllocationSpec());
            startDescriptor.AllocationAttributes.EnableMultipleJobs = Spec_->EnableMultipleJobsInAllocation.value_or(false);
            scheduleAllocationResult->StartDescriptor.emplace(std::move(startDescriptor));

            RegisterTestingSpeculativeJobIfNeeded(*task, scheduleAllocationResult->StartDescriptor->Id);
            UpdateTask(task.Get());
            return;
        }
    }

    scheduleAllocationResult->RecordFail(EScheduleFailReason::NoCandidateTasks);
    GetScheduleJobProfiler()->ProfileScheduleJobFailure(
        allocation.TreeId,
        EScheduleFailReason::NoCandidateTasks);
}

// NB(pogorelov): This method is mvp now, it will be improved.
std::optional<EScheduleFailReason> TOperationControllerBase::TryScheduleNextJob(TAllocation& allocation, TJobId lastJobId)
{
    YT_VERIFY(IsRunning());

    TJobSchedulingContext context(
        allocation.Id,
        allocation.Resources.DiskQuota(),
        allocation.NodeDescriptor,
        allocation.PoolPath);

    YT_VERIFY(allocation.Task);

    if (auto failReason = TryScheduleJob(allocation, *allocation.Task, context, /*scheduleLocalJob*/ true, lastJobId)) {
        GetScheduleJobProfiler()->ProfileScheduleJobFailure(
            allocation.TreeId,
            allocation.Task->GetJobType(),
            *failReason,
            /*isJobFirst*/ false);

        auto logSettlementFailed = [&] (EScheduleFailReason reason) {
            YT_LOG_INFO(
                "Failed to settle new job in allocation (AllocationId: %v, FailReason: %v)",
                allocation.Id,
                reason);
        };

        if (*failReason == EScheduleFailReason::NotEnoughChunkLists) {
            logSettlementFailed(*failReason);
            return failReason;
        }
        if (auto failReason = TryScheduleJob(allocation, *allocation.Task, context, /*scheduleLocalJob*/ false, lastJobId)) {
            GetScheduleJobProfiler()->ProfileScheduleJobFailure(
                allocation.TreeId,
                allocation.Task->GetJobType(),
                *failReason,
                /*isJobFirst*/ false);
            logSettlementFailed(*failReason);
            return failReason;
        }
    }

    YT_VERIFY(allocation.Joblet);

    RegisterTestingSpeculativeJobIfNeeded(*allocation.Task, allocation.Id);
    UpdateTask(allocation.Task);

    GetScheduleJobProfiler()->ProfileScheduleJobSuccess(
        allocation.TreeId,
        allocation.Task->GetJobType(),
        /*isJobFirst*/ false);

    return std::nullopt;
}

std::optional<EScheduleFailReason> TOperationControllerBase::TryScheduleJob(
    TAllocation& allocation,
    TTask& task,
    const TSchedulingContext& context,
    bool scheduleLocalJob,
    std::optional<TJobId> previousJobId)
{
    auto nodeId = NodeIdFromAllocationId(allocation.Id);

    auto now = TInstant::Now();

    auto minNeededResources = task.GetMinNeededResources();
    if (!context.CanSatisfyDemand(minNeededResources)) {
        return EScheduleFailReason::NotEnoughResources;
    }

    auto locality = task.GetLocality(nodeId);

    if (scheduleLocalJob) {
        // Make sure that the task has positive locality.
        if (locality <= 0) {
            return EScheduleFailReason::NoLocalJobs;
        }
    } else {
        if (!task.GetDelayedTime()) {
            task.SetDelayedTime(now);
        }

        auto deadline = *task.GetDelayedTime() + task.GetLocalityTimeout();
        if (deadline > now) {
            YT_LOG_DEBUG(
                "Task delayed (Task: %v, Deadline: %v)",
                task.GetTitle(),
                deadline);
            return EScheduleFailReason::TaskDelayed;
        }
    }

    if (task.HasNoPendingJobs(allocation.TreeId)) {
        UpdateTask(&task);
        return EScheduleFailReason::NoPendingJobs;
    }

    YT_LOG_DEBUG(
        "Attempting to schedule job (AllocationId: %v, Kind: %v, Task: %v, Context: %v, Locality: %v, "
        "PendingDataWeight: %v, PendingJobCount: %v, %v)",
        allocation.Id,
        scheduleLocalJob ? "Local" : "NonLocal",
        task.GetTitle(),
        context.ToString(GetMediumDirectory()),
        locality,
        task.GetPendingDataWeight(),
        task.GetPendingJobCount(),
        MakeFormatterWrapper([&] (TStringBuilderBase* builder) {
            if (previousJobId) {
                YT_VERIFY(allocation.LastJobInfo);
                builder->AppendFormat("CompetitionType: %v", allocation.LastJobInfo->CompetitionType);
            }
        }));

    if (!HasEnoughChunkLists(task.IsStderrTableEnabled(), task.IsCoreTableEnabled())) {
        YT_LOG_DEBUG("Job chunk list demand is not met");
        return EScheduleFailReason::NotEnoughChunkLists;
    }

    return task.TryScheduleJob(allocation, context, previousJobId, IsTreeTentative(allocation.TreeId));
}

bool TOperationControllerBase::IsTreeTentative(const TString& treeId) const
{
    return GetOrCrash(PoolTreeControllerSettingsMap_, treeId).Tentative;
}

bool TOperationControllerBase::IsTreeProbing(const TString& treeId) const
{
    return GetOrCrash(PoolTreeControllerSettingsMap_, treeId).Probing;
}

bool TOperationControllerBase::IsIdleCpuPolicyAllowedInTree(const TString& treeId) const
{
    return GetOrCrash(PoolTreeControllerSettingsMap_, treeId).AllowIdleCpuPolicy;
}

void TOperationControllerBase::MaybeBanInTentativeTree(const TString& treeId)
{
    if (!BannedTreeIds_.insert(treeId).second) {
        return;
    }

    Host_->OnOperationBannedInTentativeTree(
        treeId,
        GetAllocationIdsByTreeId(treeId));

    auto error = TError("Operation was banned from tentative tree")
        << TErrorAttribute("tree_id", treeId);
    SetOperationAlert(EOperationAlertType::OperationBannedInTentativeTree, error);
}

TCancelableContextPtr TOperationControllerBase::GetCancelableContext() const
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    return CancelableContext_;
}

IInvokerPtr TOperationControllerBase::GetInvoker(EOperationControllerQueue queue) const
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    return SuspendableInvokerPool_->GetInvoker(queue);
}

IInvokerPoolPtr TOperationControllerBase::GetCancelableInvokerPool() const
{
    return CancelableInvokerPool_;
}

IInvokerPtr TOperationControllerBase::GetChunkScraperInvoker() const
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    return ChunkScraperInvoker_;
}

IInvokerPtr TOperationControllerBase::GetCancelableInvoker(EOperationControllerQueue queue) const
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    return CancelableInvokerPool_->GetInvoker(queue);
}

IInvokerPtr TOperationControllerBase::GetJobSpecBuildInvoker() const
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    return JobSpecBuildInvoker_;
}

TDiagnosableInvokerPool::TInvokerStatistics TOperationControllerBase::GetInvokerStatistics(EOperationControllerQueue queue) const
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    return DiagnosableInvokerPool_->GetInvokerStatistics(queue);
}

TFuture<void> TOperationControllerBase::Suspend()
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    if (Spec_->TestingOperationOptions->DelayInsideSuspend) {
        return AllSucceeded(std::vector<TFuture<void>> {
            SuspendInvokerPool(SuspendableInvokerPool_),
            TDelayedExecutor::MakeDelayed(*Spec_->TestingOperationOptions->DelayInsideSuspend)});
    }

    return SuspendInvokerPool(SuspendableInvokerPool_);
}

void TOperationControllerBase::Resume()
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    ResumeInvokerPool(SuspendableInvokerPool_);
}

void TOperationControllerBase::Cancel()
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    CancelableContext_->Cancel(TError("Operation controller canceled"));

    YT_LOG_INFO("Operation controller canceled");
}

TCompositePendingJobCount TOperationControllerBase::GetPendingJobCount() const
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    // Avoid accessing the state while not prepared.
    if (!IsPrepared()) {
        return TCompositePendingJobCount{};
    }

    // NB: For suspended operations we still report proper pending job count
    // but zero demand.
    if (!IsRunning()) {
        return TCompositePendingJobCount{};
    }

    return CachedPendingJobCount_.Load();
}

i64 TOperationControllerBase::GetFailedJobCount() const
{
    return FailedJobCount_;
}

bool TOperationControllerBase::ShouldUpdateLightOperationAttributes() const
{
    return ShouldUpdateLightOperationAttributes_;
}

void TOperationControllerBase::SetLightOperationAttributesUpdated()
{
    ShouldUpdateLightOperationAttributes_ = false;
}

void TOperationControllerBase::IncreaseNeededResources(const TCompositeNeededResources& resourcesDelta)
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    auto zeroOutNeededResources = false;
    for (const auto& task : Tasks_) {
        if (task->IsCompleted()) {
            continue;
        }

        if (!task->IsNetworkBandwidthToClustersAvailable()) {
            zeroOutNeededResources = true;
            break;
        }
    }

    auto guard = WriterGuard(CachedNeededResourcesLock_);
    if (zeroOutNeededResources) {
        // Network bandwidth to some clusters is not available. So zero out needed resources, to temporarily stop scheduling jobs.
        CachedNeededResources_ = {};
    } else {
        CachedNeededResources_ = CachedNeededResources_ + resourcesDelta;
    }
}

void TOperationControllerBase::IncreaseAccountResourceUsageLease(const std::optional<std::string>& account, const TDiskQuota& delta)
{
    YT_ASSERT_INVOKER_POOL_AFFINITY(CancelableInvokerPool_);

    if (!account || !EnableMasterResourceUsageAccounting_) {
        return;
    }

    auto& info = GetOrCrash(AccountResourceUsageLeaseMap_, *account);

    YT_LOG_DEBUG("Increasing account resource usage lease (Account: %v, CurrentDiskQuota: %v, Delta: %v)",
        account,
        info.DiskQuota,
        delta);

    info.DiskQuota += delta;
    YT_VERIFY(!info.DiskQuota.DiskSpaceWithoutMedium.has_value());
}

void TOperationControllerBase::UpdateAccountResourceUsageLeases()
{
    for (const auto& [account, info] : AccountResourceUsageLeaseMap_) {
        auto it = LastUpdatedAccountResourceUsageLeaseMap_.find(account);
        if (it != LastUpdatedAccountResourceUsageLeaseMap_.end() && info.DiskQuota == it->second.DiskQuota) {
            continue;
        }

        LastUpdatedAccountResourceUsageLeaseMap_[account] = info;

        auto error = WaitFor(Host_->UpdateAccountResourceUsageLease(info.LeaseId, info.DiskQuota));
        if (!error.IsOK()) {
            if (!CheckUserTransactionAlive()) {
                return;
            }

            if (error.FindMatching(NSecurityClient::EErrorCode::AccountLimitExceeded) ||
                error.FindMatching(NSecurityClient::EErrorCode::AuthorizationError) ||
                error.FindMatching(NYTree::EErrorCode::ResolveError) ||
                error.FindMatching(NObjectClient::EErrorCode::InvalidObjectLifeStage))
            {
                DoFailOperation(
                    TError("Failed to update account usage lease")
                        << TErrorAttribute("account", account)
                        << TErrorAttribute("lease_id", info.LeaseId)
                        << TErrorAttribute("operation_id", OperationId_)
                        << TErrorAttribute("resource_usage", info.DiskQuota)
                        << error);
            } else {
                Host_->Disconnect(
                    TError("Failed to update account usage lease")
                        << TErrorAttribute("account", account)
                        << TErrorAttribute("lease_id", info.LeaseId)
                        << TErrorAttribute("operation_id", OperationId_)
                        << TErrorAttribute("resource_usage", info.DiskQuota)
                        << error);
            }
            return;
        } else {
            YT_LOG_DEBUG("Account resource usage lease updated (Account: %v, LeaseId: %v, DiskQuota: %v)",
                account,
                info.LeaseId,
                info.DiskQuota);
        }
    }
}

TCompositeNeededResources TOperationControllerBase::GetNeededResources() const
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    auto guard = ReaderGuard(CachedNeededResourcesLock_);
    return CachedNeededResources_;
}

TAllocationGroupResourcesMap TOperationControllerBase::GetGroupedNeededResources() const
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    return CachedGroupedNeededResources_.Load();
}

void TOperationControllerBase::SafeUpdateGroupedNeededResources()
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker());

    TAllocationGroupResourcesMap groupedNeededResources;
    for (const auto& task : Tasks_) {
        if (task->HasNoPendingJobs()) {
            UpdateTask(task.Get());
            continue;
        }

        const auto& taskName = task->GetVertexDescriptor();

        TJobResourcesWithQuota minNeededResources;
        int pendingJobCount;
        try {
            minNeededResources = task->GetMinNeededResources();
            pendingJobCount = task->GetPendingJobCount().DefaultCount;
        } catch (const std::exception& ex) {
            auto error = TError("Failed to update minimum needed resources or pending job count")
                << TErrorAttribute("task", taskName)
                << ex;
            DoFailOperation(error);
            return;
        }

        TAllocationGroupResources allocationGroupResources{
            .MinNeededResources = std::move(minNeededResources),
            .AllocationCount = pendingJobCount,
        };

        YT_LOG_DEBUG(
            "Updated allocation group needed resources (Task: %v, AllocationGroupResources: %v)",
            taskName,
            allocationGroupResources);

        EmplaceOrCrash(groupedNeededResources, taskName, std::move(allocationGroupResources));
    }

    CachedGroupedNeededResources_.Store(std::move(groupedNeededResources));
}

void TOperationControllerBase::FlushOperationNode(bool checkFlushResult)
{
    YT_LOG_DEBUG("Flushing operation node");
    // Some statistics are reported only on operation end so
    // we need to synchronously check everything and set
    // appropriate alerts before flushing operation node.
    // Flush of newly calculated statistics is guaranteed by OnOperationFailed.
    AlertManager_->Analyze();

    auto flushResult = WaitFor(Host_->FlushOperationNode());
    if (checkFlushResult && !flushResult.IsOK()) {
        // We do not want to complete operation if progress flush has failed.
        DoFailOperation(flushResult, /*flush*/ false);
    }

    YT_LOG_DEBUG("Operation node flushed");
}

void TOperationControllerBase::OnOperationCompleted(bool /* interrupted */)
{
    // This can happen if operation failed during completion in derived class (e.g. SortController).
    if (IsFinished()) {
        return;
    }

    State_ = EControllerState::Completed;

    GetCancelableInvoker()->Invoke(
        BIND([this, this_ = MakeStrong(this)] {
            try {
                AbortAllJoblets(EAbortReason::OperationCompleted, /*honestly*/ true);

                BuildAndSaveProgress();
                FlushOperationNode(/*checkFlushResult*/ true);

                LogProgress(/*force*/ true);

                Host_->OnOperationCompleted();
            } catch (const std::exception& ex) {
                // NB(coteeq): Nothing we can do about it. Agent should've been disconnected from master.
                YT_LOG_WARNING(ex, "Failed to complete operation");
            }
        }));
}

void TOperationControllerBase::OnOperationFailed(const TError& error, bool flush, bool abortAllJoblets)
{
    YT_UNUSED_FUTURE(BIND(&TOperationControllerBase::DoFailOperation, MakeStrong(this))
        .AsyncVia(GetCancelableInvoker())
        .Run(error, flush, abortAllJoblets));
}

void TOperationControllerBase::DoFailOperation(const TError& error, bool flush, bool abortAllJoblets)
{
    YT_ASSERT_INVOKER_POOL_AFFINITY(InvokerPool_);

    if (auto delay = Spec_->TestingOperationOptions->FailOperationDelay; delay) {
        Sleep(*delay);
    }

    WaitFor(BIND([=, this, this_ = MakeStrong(this)] {
        YT_LOG_DEBUG(error, "Operation controller failed (Flush: %v)", flush);

        // During operation failing job aborting can lead to another operation fail, we don't want to invoke it twice.
        if (IsFinished()) {
            return;
        }

        State_ = EControllerState::Failed;

        if (abortAllJoblets) {
            AbortAllJoblets(EAbortReason::OperationFailed, /*honestly*/ true);
        }

        for (const auto& task : Tasks_) {
            task->StopTiming();
        }

        BuildAndSaveProgress();
        LogProgress(/*force*/ true);

        if (flush) {
            // NB: Error ignored since we cannot do anything with it.
            FlushOperationNode(/*checkFlushResult*/ false);
        }

        Error_ = error;

        YT_LOG_DEBUG("Notifying host about operation controller failure");
        Host_->OnOperationFailed(error);
        YT_LOG_DEBUG("Host notified about operation controller failure");
    })
        .AsyncVia(GetInvoker())
        .Run()
        .ToUncancelable())
        .ThrowOnError();
}

void TOperationControllerBase::OnOperationAborted(const TError& error)
{
    YT_ASSERT_INVOKER_POOL_AFFINITY(CancelableInvokerPool_);

    // Cf. OnOperationFailed.
    if (IsFinished()) {
        return;
    }

    State_ = EControllerState::Aborted;

    Host_->OnOperationAborted(error);
}

std::optional<TDuration> TOperationControllerBase::GetTimeLimit() const
{
    auto timeLimit = Config_->OperationTimeLimit;
    if (Spec_->TimeLimit) {
        timeLimit = Spec_->TimeLimit;
    }
    return timeLimit;
}

TError TOperationControllerBase::GetTimeLimitError() const
{
    return TError("Operation is running for too long, aborted")
        << TErrorAttribute("time_limit", GetTimeLimit());
}

void TOperationControllerBase::OnOperationTimeLimitExceeded()
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker());

    if (State_ != EControllerState::Running) {
        YT_LOG_DEBUG(
            "Attempt to report time limit expiration of an operation which is not running (State: %v)",
            State_.load());
        return;
    }

    OperationTimedOut_ = true;

    YT_LOG_DEBUG("Operation timed out");

    GracefullyFailOperation(GetTimeLimitError());
}

bool TOperationControllerBase::HasJobUniquenessRequirements() const
{
    return NControllers::HasJobUniquenessRequirements(Spec_, GetUserJobSpecs());
}

bool TOperationControllerBase::IsJobUniquenessRequired(const TJobletPtr& joblet) const
{
    const auto& userJobSpec = joblet->Task->GetUserJobSpec();
    return Spec_->FailOnJobRestart || (userJobSpec && userJobSpec->FailOnJobRestart);
}

void TOperationControllerBase::OnJobUniquenessViolated(TError error)
{
    YT_ASSERT_INVOKER_POOL_AFFINITY(CancelableInvokerPool_);

    GracefullyFailOperation(std::move(error));
}

void TOperationControllerBase::GracefullyFailOperation(TError error)
{
    YT_ASSERT_INVOKER_POOL_AFFINITY(CancelableInvokerPool_);

    if (State_ != EControllerState::Running) {
        YT_LOG_DEBUG(
            "Attempt to gracefully fail operation which is not running (State: %v)",
            State_.load());

        return;
    }

    State_ = EControllerState::Failing;

    YT_LOG_INFO("Operation gracefully failing");

    bool hasJobsToFail = false;

    struct TJobsToAbort {
        TJobId JobId;
        EJobType JobType;
    };

    std::vector<TJobsToAbort> jobsToAbort;
    jobsToAbort.reserve(size(AllocationMap_));
    for (const auto& [_, allocation] : AllocationMap_) {
        if (!allocation.Joblet) {
            continue;
        }
        const auto& joblet = allocation.Joblet;
        jobsToAbort.push_back({joblet->JobId, joblet->JobType});
    }

    for (const auto& [jobId, jobType] : jobsToAbort) {
        switch (jobType) {
            // TODO(ignat): YT-11247, add helper with list of job types with user code.
            case EJobType::Map:
            case EJobType::OrderedMap:
            case EJobType::SortedReduce:
            case EJobType::JoinReduce:
            case EJobType::PartitionMap:
            case EJobType::ReduceCombiner:
            case EJobType::PartitionReduce:
            case EJobType::Vanilla:
                hasJobsToFail = true;
                Host_->RequestJobGracefulAbort(jobId, EAbortReason::OperationFailed);
                break;
            default:
                AbortJob(jobId, EAbortReason::OperationFailed);
        }
    }

    if (hasJobsToFail) {
        YT_LOG_DEBUG("Postpone operation failure to handle failed jobs");
        OperationFailError_ = error;
        GracefulAbortTimeoutFailureCookie_ = TDelayedExecutor::Submit(
            BIND(
                &TOperationControllerBase::DoFailOperation,
                MakeWeak(this),
                Passed(std::move(error)),
                /*flush*/ true,
                /*abortAllJoblets*/ true),
            Spec_->TimeLimitJobFailTimeout,
            GetCancelableInvoker());
    } else {
        DoFailOperation(error, /*flush*/ true);
    }
}

bool TOperationControllerBase::CheckGracefullyAbortedJobsStatusReceived()
{
    if (IsFailing() && RunningJobCount_ == 0) {
        OnOperationFailed(std::move(OperationFailError_), /*flush*/ true);
        return true;
    }

    return false;
}

const std::vector<TOutputStreamDescriptorPtr>& TOperationControllerBase::GetStandardStreamDescriptors() const
{
    return StandardStreamDescriptors_;
}

void TOperationControllerBase::InitializeStandardStreamDescriptors()
{
    StandardStreamDescriptors_.resize(OutputTables_.size());
    for (int index = 0; index < std::ssize(OutputTables_); ++index) {
        StandardStreamDescriptors_[index] = OutputTables_[index]->GetStreamDescriptorTemplate(index)->Clone();
        StandardStreamDescriptors_[index]->DestinationPool = GetSink();
        StandardStreamDescriptors_[index]->IsFinalOutput = true;
        StandardStreamDescriptors_[index]->LivePreviewIndex = index;
        StandardStreamDescriptors_[index]->TargetDescriptor = TDataFlowGraph::SinkDescriptor;
        StandardStreamDescriptors_[index]->PartitionTag = index;
    }
}

void TOperationControllerBase::AddChunksToUnstageList(std::vector<TInputChunkPtr> chunks)
{
    std::vector<TChunkId> chunkIds;
    for (const auto& chunk : chunks) {
        auto it = LivePreviewChunks_.find(chunk);
        YT_VERIFY(it != LivePreviewChunks_.end());
        auto livePreviewDescriptor = it->second;
        auto result = DataFlowGraph_->TryUnregisterLivePreviewChunk(
            livePreviewDescriptor.VertexDescriptor,
            livePreviewDescriptor.LivePreviewIndex,
            chunk);
        if (!result.IsOK()) {
            static constexpr auto message = "Error unregistering a chunk from a live preview";
            auto tableName = "output_" + ToString(it->second.LivePreviewIndex);
            if (Config_->FailOperationOnErrorsInLivePreview) {
                THROW_ERROR_EXCEPTION(message)
                    << TErrorAttribute("table_name", tableName)
                    << TErrorAttribute("chunk_id", chunk->GetChunkId());
            } else {
                YT_LOG_WARNING(result, "%v (TableName: %v, Chunk: %v)",
                    message,
                    tableName,
                    chunk);
            }
        }
        chunkIds.push_back(chunk->GetChunkId());
        YT_LOG_DEBUG("Releasing intermediate chunk (ChunkId: %v, VertexDescriptor: %v, LivePreviewIndex: %v)",
            chunk->GetChunkId(),
            livePreviewDescriptor.VertexDescriptor,
            livePreviewDescriptor.LivePreviewIndex);
        LivePreviewChunks_.erase(it);
    }
    Host_->AddChunkTreesToUnstageList(std::move(chunkIds), /*recursive*/ false);
}

void TOperationControllerBase::ProcessSafeException(const std::exception& ex)
{
    auto error = TError("Exception thrown in operation controller that led to operation failure")
        << ex;

    YT_LOG_ERROR(error);

    OnOperationFailed(error, /*flush*/ false, /*abortAllJoblets*/ false);
}

void TOperationControllerBase::ProcessSafeException(const TAssertionFailedException& ex)
{
    TControllerAgentCounterManager::Get()->IncrementAssertionsFailed(OperationType_);

    auto error = TError(
        NScheduler::EErrorCode::OperationControllerCrashed,
        "Operation controller crashed; please file a ticket at YTADMINREQ and attach a link to this operation")
        << TErrorAttribute("failed_condition", ex.GetExpression())
        << TErrorAttribute("stack_trace", ex.GetStackTrace())
        << TErrorAttribute("core_path", ex.GetCorePath())
        << TErrorAttribute("operation_id", OperationId_);

    YT_LOG_ERROR(error);

    OnOperationFailed(error, /*flush*/ false, /*abortAllJoblets*/ false);
}

void TOperationControllerBase::SafeInvokeSafely(std::function<void()> closure)
{
    closure();
}

void TOperationControllerBase::OnJobFinished(std::unique_ptr<TJobSummary> summary, bool retainJob)
{
    auto jobId = summary->Id;

    auto joblet = GetJoblet(jobId);
    if (!joblet->IsStarted()) {
        return;
    }

    bool hasStderr = false;
    bool hasFailContext = false;
    int coreInfoCount = 0;

    if (summary->Result) {
        const auto& jobResultExtension = summary->GetJobResult().GetExtension(TJobResultExt::job_result_ext);

        if (jobResultExtension.has_has_stderr()) {
            hasStderr = jobResultExtension.has_stderr();
        } else {
            auto stderrChunkId = FromProto<TChunkId>(jobResultExtension.stderr_chunk_id());
            if (stderrChunkId) {
                Host_->AddChunkTreesToUnstageList({stderrChunkId}, /*recursive*/ false);
            }
            hasStderr = static_cast<bool>(stderrChunkId);
        }

        if (jobResultExtension.has_has_fail_context()) {
            hasFailContext = jobResultExtension.has_fail_context();
        } else {
            auto failContextChunkId = FromProto<TChunkId>(jobResultExtension.fail_context_chunk_id());
            hasFailContext = static_cast<bool>(failContextChunkId);
        }

        coreInfoCount = jobResultExtension.core_infos().size();
    }

    ReportControllerStateToArchive(joblet, summary->State);
    ReportFinishTimeToArchive(joblet);

    bool shouldRetainJob =
        (retainJob && RetainedJobCount_ < Config_->MaxRetainedJobsPerOperation) ||
        (hasStderr && RetainedJobWithStderrCount_ < Spec_->MaxStderrCount) ||
        (coreInfoCount > 0 && RetainedJobsCoreInfoCount_ + coreInfoCount <= Spec_->MaxCoreInfoCount);

    auto releaseJobFlags = summary->ReleaseFlags;
    if (hasStderr && shouldRetainJob) {
        releaseJobFlags.ArchiveStderr = true;
        // Job spec is necessary for ACL checks for stderr.
        releaseJobFlags.ArchiveJobSpec = true;
    }
    if (hasFailContext && shouldRetainJob) {
        releaseJobFlags.ArchiveFailContext = true;
        // Job spec is necessary for ACL checks for fail context.
        releaseJobFlags.ArchiveJobSpec = true;
    }
    releaseJobFlags.ArchiveProfile = true;

    // TODO(gritukan, prime): This is always true.
    if (releaseJobFlags.IsNonTrivial()) {
        JobIdToReleaseFlags_.emplace(jobId, releaseJobFlags);
    }

    if (shouldRetainJob) {
        auto attributesFragment = BuildYsonStringFluently<EYsonType::MapFragment>()
            .Do([&] (TFluentMap fluent) {
                BuildFinishedJobAttributes(
                    joblet,
                    summary.get(),
                    hasStderr,
                    hasFailContext,
                    fluent);
            })
            .Finish();

        {
            auto attributes = BuildYsonStringFluently()
                .DoMap([&] (TFluentMap fluent) {
                    fluent.GetConsumer()->OnRaw(attributesFragment);
                });
            RetainedFinishedJobs_.emplace_back(jobId, std::move(attributes));
        }

        if (hasStderr) {
            ++RetainedJobWithStderrCount_;
        }
        if (retainJob) {
            ++RetainedJobCount_;
        }
        RetainedJobsCoreInfoCount_ += coreInfoCount;
    }

    if (joblet->IsStarted()) {
        IncreaseAccountResourceUsageLease(joblet->DiskRequestAccount, -joblet->DiskQuota);
    }
}

bool TOperationControllerBase::IsPrepared() const
{
    return State_ != EControllerState::Preparing;
}

bool TOperationControllerBase::IsRunning() const
{
    return State_ == EControllerState::Running;
}

bool TOperationControllerBase::IsFailing() const
{
    return State_ == EControllerState::Failing;
}

bool TOperationControllerBase::IsFailingByTimeout() const
{
    return IsFailing() && OperationTimedOut_;
}

bool TOperationControllerBase::IsFinished() const
{
    return State_ == EControllerState::Completed ||
        State_ == EControllerState::Failed ||
        State_ == EControllerState::Aborted;
}

std::pair<ITransactionPtr, std::string> TOperationControllerBase::GetIntermediateMediumTransaction()
{
    return {nullptr, {}};
}

void TOperationControllerBase::UpdateIntermediateMediumUsage(i64 /*usage*/)
{
    YT_UNIMPLEMENTED();
}

const std::vector<TString>& TOperationControllerBase::GetOffloadingPoolTrees()
{
    if (!OffloadingPoolTrees_) {
        OffloadingPoolTrees_.emplace();
        for (const auto& [poolTree, settings]: PoolTreeControllerSettingsMap_) {
            if (settings.Offloading) {
                OffloadingPoolTrees_.value().push_back(poolTree);
            }
        }
    }
    return *OffloadingPoolTrees_;
}

void TOperationControllerBase::InitializeJobExperiment()
{
    if (Spec_->JobExperiment) {
        if (TLayerJobExperiment::IsEnabled(Spec_, GetUserJobSpecs())) {
            YT_VERIFY(BaseLayer_.has_value());
            JobExperiment_ = New<TLayerJobExperiment>(
                *Spec_->DefaultBaseLayerPath,
                *BaseLayer_,
                Config_->EnableBypassArtifactCache,
                Logger);
        } else if (TMtnJobExperiment::IsEnabled(Spec_, GetUserJobSpecs())) {
            JobExperiment_ = New<TMtnJobExperiment>(
                Host_->GetClient(),
                GetAuthenticatedUser(),
                *Spec_->JobExperiment->NetworkProject,
                Logger);
        }
    }
}

TJobExperimentBasePtr TOperationControllerBase::GetJobExperiment()
{
    return JobExperiment_;
}

std::expected<TJobId, EScheduleFailReason> TOperationControllerBase::GenerateJobId(NScheduler::TAllocationId allocationId, TJobId previousJobId)
{
    auto jobIdGuid = previousJobId ? previousJobId.Underlying() : allocationId.Underlying();

    int currentJobCount = jobIdGuid.Parts32[0] >> 24;

    jobIdGuid.Parts32[0] += 1 << 24;

    if (jobIdGuid.Parts32[0] >> 24 == 0 ||
        currentJobCount >= Config_->AllocationJobCountLimit.value_or(
            std::numeric_limits<decltype(Config_->AllocationJobCountLimit)::value_type>::max()))
    {
        YT_LOG_DEBUG(
            "Allocation job count reached limit (JobCount: %v, AllocationId: %v, PreviousJobId: %v)",
            currentJobCount,
            allocationId,
            previousJobId);
        return std::unexpected(EScheduleFailReason::AllocationJobCountReachedLimit);
    }

    YT_LOG_DEBUG(
        "Generating new job id (JobId: %v, AllocationId: %v, PreviousJobId: %v)",
        jobIdGuid,
        allocationId,
        previousJobId);

    return TJobId(jobIdGuid);
}

TJobletPtr TOperationControllerBase::CreateJoblet(
    TTask* task,
    TJobId jobId,
    TString treeId,
    int taskJobIndex,
    std::optional<TString> poolPath,
    bool treeIsTentative)
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    auto joblet = New<TJoblet>(task, NextJobIndex(), taskJobIndex, std::move(treeId), treeIsTentative);

    joblet->StartTime = TInstant::Now();
    joblet->JobId = jobId;
    joblet->PoolPath = std::move(poolPath);

    return joblet;
}

std::shared_ptr<const THashMap<TClusterName, bool>> TOperationControllerBase::GetClusterToNetworkBandwidthAvailability() const
{
    return Host_->GetClusterToNetworkBandwidthAvailability();
}

bool TOperationControllerBase::IsNetworkBandwidthAvailable(const TClusterName& clusterName) const
{
    return Host_->IsNetworkBandwidthAvailable(clusterName);
}

void TOperationControllerBase::SubscribeToClusterNetworkBandwidthAvailabilityUpdated(
        const TClusterName& clusterName,
        const TCallback<void()>& callback) const
{
    Host_->SubscribeToClusterNetworkBandwidthAvailabilityUpdated(clusterName, callback);
}

void TOperationControllerBase::UnsubscribeFromClusterNetworkBandwidthAvailabilityUpdated(
        const TClusterName& clusterName,
        const TCallback<void()>& callback) const
{
    Host_->UnsubscribeFromClusterNetworkBandwidthAvailabilityUpdated(clusterName, callback);
}

TJobFailsTolerancePtr TOperationControllerBase::GetJobFailsTolerance() const
{
    return Config_->EnableJobFailsTolerance
        ? Spec_->JobFailsTolerance
        : TJobFailsTolerancePtr{};
}

bool TOperationControllerBase::IsExitCodeKnown(int exitCode) const
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker(Config_->JobEventsControllerQueue));

    auto jobFailsTolerance = GetJobFailsTolerance();

    if (!jobFailsTolerance) {
        return false;
    }

    return jobFailsTolerance->MaxFailsPerKnownExitCode.contains(exitCode);
}

int TOperationControllerBase::GetMaxJobFailCountForExitCode(std::optional<int> maybeExitCode)
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker(Config_->JobEventsControllerQueue));

    auto jobFailsTolerance = GetJobFailsTolerance();

    YT_VERIFY(jobFailsTolerance);
    if (!maybeExitCode.has_value()) {
        return jobFailsTolerance->MaxFailsNoExitCode;
    }

    return IsExitCodeKnown(*maybeExitCode)
        ? jobFailsTolerance->MaxFailsPerKnownExitCode[*maybeExitCode]
        : jobFailsTolerance->MaxFailsUnknownExitCode;
}

bool TOperationControllerBase::IsJobsFailToleranceExceeded(std::optional<int> maybeExitCode)
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker(Config_->JobEventsControllerQueue));

    const auto& jobFailsTolerance = GetJobFailsTolerance();

    if (!jobFailsTolerance) {
        return false;
    }

    if (!maybeExitCode.has_value()) {
        return NoExitCodeFailCount_ >= jobFailsTolerance->MaxFailsNoExitCode;
    }

    auto exitCode = maybeExitCode.value();

    if (!IsExitCodeKnown(exitCode)) {
        return UnknownExitCodeFailCount_ >= jobFailsTolerance->MaxFailsUnknownExitCode;
    }

    return FailCountsPerKnownExitCode_[exitCode] >= jobFailsTolerance->MaxFailsPerKnownExitCode[exitCode];
}

void TOperationControllerBase::UpdateFailedJobsExitCodeCounters(std::optional<int> maybeExitCode)
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker(Config_->JobEventsControllerQueue));

    const auto& jobFailsTolerance = GetJobFailsTolerance();

    if (!jobFailsTolerance) {
        return;
    }

    if (!maybeExitCode.has_value()) {
        ++NoExitCodeFailCount_;
        return;
    }
    auto exitCode = maybeExitCode.value();

    if (!IsExitCodeKnown(exitCode)) {
        ++UnknownExitCodeFailCount_;
        return;
    }

    ++FailCountsPerKnownExitCode_[exitCode];
}

bool TOperationControllerBase::IsJobIdEarlier(TJobId lhs, TJobId rhs) const noexcept
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    YT_VERIFY(AllocationIdFromJobId(lhs) == AllocationIdFromJobId(rhs));

    return lhs.Underlying().Parts32[0] < rhs.Underlying().Parts32[0];
}

TJobId TOperationControllerBase::GetLaterJobId(TJobId lhs, TJobId rhs) const noexcept
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    if (IsJobIdEarlier(lhs, rhs)) {
        return rhs;
    } else {
        return lhs;
    }
}

void TOperationControllerBase::AsyncAbortJob(TJobId jobId, EAbortReason abortReason)
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    GetCancelableInvoker(Config_->JobEventsControllerQueue)->Invoke(
        BIND(
            &TOperationControllerBase::AbortJob,
            MakeWeak(this),
            jobId,
            abortReason));
}

void TOperationControllerBase::SafeAbortJobByJobTracker(TJobId jobId, EAbortReason abortReason)
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker(Config_->JobEventsControllerQueue));

    auto joblet = FindJoblet(jobId);

    if (!joblet) {
        YT_LOG_DEBUG(
            "Ignore stale job abort request from job tracker (JobId: %v, AbortReason: %v)",
            jobId,
            abortReason);

        return;
    }

    YT_LOG_DEBUG("Aborting job by job tracker request (JobId: %v)", jobId);

    DoAbortJob(std::move(joblet), abortReason, /*requestJobTrackerJobAbortion*/ false, /*force*/ false);
}

void TOperationControllerBase::SuppressLivePreviewIfNeeded()
{
    if (GetLegacyOutputLivePreviewMode() == ELegacyLivePreviewMode::NotSupported &&
        GetLegacyIntermediateLivePreviewMode() == ELegacyLivePreviewMode::NotSupported)
    {
        YT_LOG_INFO("Legacy live preview is not supported for this operation");
        return;
    }

    std::vector<TError> suppressionErrors;

    const auto& connection = Host_->GetClient()->GetNativeConnection();
    for (const auto& table : OutputTables_) {
        if (table->Dynamic) {
            suppressionErrors.push_back(TError("Output table %v is dynamic", table->Path));
            break;
        }
    }

    for (const auto& table : OutputTables_) {
        if (table->ExternalCellTag == connection->GetPrimaryMasterCellTag() &&
            !connection->GetSecondaryMasterCellTags().empty())
        {
            suppressionErrors.push_back(TError(
                "Output table %v is non-external and cluster is multicell",
                table->Path));
            break;
        }
    }

    // TODO(ifsmirnov): YT-11498. This is not the suppression you are looking for.
    for (const auto& table : InputManager_->GetInputTables()) {
        if (table->Schema->HasNontrivialSchemaModification()) {
            suppressionErrors.push_back(TError(
                "Input table %v has non-trivial schema modification",
                table->Path));
            break;
        }
    }

    if (GetLegacyOutputLivePreviewMode() == ELegacyLivePreviewMode::DoNotCare ||
        GetLegacyIntermediateLivePreviewMode() == ELegacyLivePreviewMode::DoNotCare)
    {
        // Some live preview normally should appear, but user did not request anything explicitly.
        // We should check if user is not in legacy live preview blacklist in order to inform him
        // if he is in a blacklist.
        if (NRe2::TRe2::FullMatch(
            NRe2::StringPiece(AuthenticatedUser_.data()),
            *Config_->LegacyLivePreviewUserBlacklist))
        {
            suppressionErrors.push_back(TError(
                "User %Qv belongs to legacy live preview suppression blacklist; in order "
                "to overcome this suppression reason, explicitly specify enable_legacy_live_preview = %%true "
                "in operation spec", AuthenticatedUser_)
                    << TErrorAttribute(
                        "legacy_live_preview_blacklist_regex",
                        Config_->LegacyLivePreviewUserBlacklist->pattern()));
        }
    }

    if (IntermediateOutputCellTagList_.size() != 1 && IsLegacyIntermediateLivePreviewSupported() && suppressionErrors.empty()) {
        suppressionErrors.push_back(TError(
            "Legacy live preview appears to have been disabled in the controller agents config when the operation started"));
    }

    IsLegacyLivePreviewSuppressed_ = !suppressionErrors.empty();
    if (IsLegacyLivePreviewSuppressed_) {
        auto combinedSuppressionError = TError("Legacy live preview is suppressed due to the following reasons")
            << suppressionErrors
            << TErrorAttribute("output_live_preview_mode", GetLegacyOutputLivePreviewMode())
            << TErrorAttribute("intermediate_live_preview_mode", GetLegacyIntermediateLivePreviewMode());
        YT_LOG_INFO("Suppressing live preview due to some reasons (CombinedError: %v)", combinedSuppressionError);
        SetOperationAlert(EOperationAlertType::LegacyLivePreviewSuppressed, combinedSuppressionError);
    } else {
        YT_LOG_INFO("Legacy live preview is not suppressed");
    }
}

void TOperationControllerBase::CreateLivePreviewTables()
{
    const auto& client = Host_->GetClient();
    auto connection = client->GetNativeConnection();

    const bool isLegacyIntermediateLivePreviewSupported = IsLegacyIntermediateLivePreviewSupported();

    TSerializableAccessControlList legacyIntermediateLivePreviewAcl;
    if (isLegacyIntermediateLivePreviewSupported) {
        if (AcoName_) {
            legacyIntermediateLivePreviewAcl = TAccessControlRule(*AcoName_).GetOrLookupAcl(Client_);
        } else {
            legacyIntermediateLivePreviewAcl = Acl_;
        }
    }

    // NB: Use root credentials.
    auto proxy = CreateObjectServiceWriteProxy(client);
    auto batchReq = proxy.ExecuteBatch();

    auto addRequest = [&] (
        const TString& path,
        TCellTag cellTag,
        int replicationFactor,
        NCompression::ECodec compressionCodec,
        const std::optional<std::string>& account,
        const TString& key,
        const TYsonString& acl,
        const TTableSchemaPtr& schema)
    {
        if (!AsyncTransaction_) {
            YT_LOG_INFO("Creating transaction required for the legacy live preview (Type: %v)", ETransactionType::Async);
            AsyncTransaction_ = WaitFor(StartTransaction(ETransactionType::Async, Client_))
                .ValueOrThrow();
        }

        auto req = TCypressYPathProxy::Create(path);
        req->set_type(ToProto(EObjectType::Table));
        req->set_ignore_existing(true);

        const auto nestingLevelLimit = Host_
            ->GetClient()
            ->GetNativeConnection()
            ->GetConfig()
            ->CypressWriteYsonNestingLevelLimit;
        auto attributes = CreateEphemeralAttributes(nestingLevelLimit);
        attributes->Set("replication_factor", replicationFactor);
        // Does this affect anything or is this for viewing only? Should we set the 'media' ('primary_medium') property?
        attributes->Set("compression_codec", compressionCodec);
        if (cellTag == connection->GetPrimaryMasterCellTag()) {
            attributes->Set("external", false);
        } else {
            attributes->Set("external", true);
            attributes->Set("external_cell_tag", cellTag);
        }
        attributes->Set("acl", acl);
        attributes->Set("inherit_acl", false);
        if (schema) {
            attributes->Set("schema", *schema);
        }
        if (account) {
            attributes->Set("account", *account);
        }

        ToProto(req->mutable_node_attributes(), *attributes);
        GenerateMutationId(req);
        SetTransactionId(req, AsyncTransaction_->GetId());

        batchReq->AddRequest(req, key);
    };

    if (IsLegacyOutputLivePreviewSupported()) {
        YT_LOG_INFO("Creating live preview for output tables");

        for (int index = 0; index < std::ssize(OutputTables_); ++index) {
            auto& table = OutputTables_[index];
            auto path = GetOperationPath(OperationId_) + "/output_" + ToString(index);
            addRequest(
                path,
                table->ExternalCellTag,
                table->TableWriterOptions->ReplicationFactor,
                table->TableWriterOptions->CompressionCodec,
                table->TableWriterOptions->Account,
                "create_output",
                table->EffectiveAcl,
                table->TableUploadOptions.TableSchema.Get());
        }
    }

    for (int index = 0; index < std::ssize(OutputTables_); ++index) {
        RegisterLivePreviewTable("output_" + ToString(index), OutputTables_[index]);
    }

    if (StderrTable_) {
        YT_LOG_INFO("Creating live preview for stderr table");

        auto name = "stderr";
        auto path = GetOperationPath(OperationId_) + "/" + name;

        RegisterLivePreviewTable(name, StderrTable_);

        addRequest(
            path,
            StderrTable_->ExternalCellTag,
            StderrTable_->TableWriterOptions->ReplicationFactor,
            StderrTable_->TableWriterOptions->CompressionCodec,
            /*account*/ std::nullopt,
            "create_stderr",
            StderrTable_->EffectiveAcl,
            StderrTable_->TableUploadOptions.TableSchema.Get());
    }

    if (IsIntermediateLivePreviewSupported()) {
        auto name = "intermediate";
        IntermediateTable_->LivePreviewTableName = name;
        (*LivePreviews_)[name] = New<TLivePreview>(
            New<TTableSchema>(),
            OutputNodeDirectory_,
            Logger,
            OperationId_,
            name);
    }

    if (CoreTable_) {
        RegisterLivePreviewTable("core", CoreTable_);
    }

    if (isLegacyIntermediateLivePreviewSupported) {
        YT_LOG_INFO("Creating live preview for intermediate table");

        auto path = GetOperationPath(OperationId_) + "/intermediate";

        auto intermediateDataAcl = MakeOperationArtifactAcl(legacyIntermediateLivePreviewAcl);
        if (Config_->AllowUsersGroupReadIntermediateData) {
            intermediateDataAcl.Entries.emplace_back(
                ESecurityAction::Allow,
                std::vector<std::string>{UsersGroupName},
                EPermissionSet(EPermission::Read));
        }

        YT_VERIFY(IntermediateOutputCellTagList_.size() == 1);
        addRequest(
            path,
            IntermediateOutputCellTagList_.front(),
            1,
            Spec_->IntermediateCompressionCodec,
            Spec_->IntermediateDataAccount,
            "create_intermediate",
            ConvertToYsonStringNestingLimited(intermediateDataAcl),
            nullptr);
    }

    auto batchRspOrError = WaitFor(batchReq->Invoke());
    THROW_ERROR_EXCEPTION_IF_FAILED(GetCumulativeError(batchRspOrError), "Error creating live preview tables");
    const auto& batchRsp = batchRspOrError.Value();

    auto handleResponse = [&] (TLivePreviewTableBase& table, TCypressYPathProxy::TRspCreatePtr rsp) {
        table.LivePreviewTableId = FromProto<NCypressClient::TNodeId>(rsp->node_id());
    };

    if (IsLegacyOutputLivePreviewSupported()) {
        auto rspsOrError = batchRsp->GetResponses<TCypressYPathProxy::TRspCreate>("create_output");
        YT_VERIFY(rspsOrError.size() == OutputTables_.size());

        for (int index = 0; index < std::ssize(OutputTables_); ++index) {
            handleResponse(*OutputTables_[index], rspsOrError[index].Value());
        }

        YT_LOG_INFO("Live preview for output tables created");
    }

    if (StderrTable_) {
        auto rsp = batchRsp->GetResponse<TCypressYPathProxy::TRspCreate>("create_stderr");
        handleResponse(*StderrTable_, rsp.Value());

        YT_LOG_INFO("Live preview for stderr table created");
    }

    if (IsLegacyIntermediateLivePreviewSupported()) {
        auto rsp = batchRsp->GetResponse<TCypressYPathProxy::TRspCreate>("create_intermediate");
        handleResponse(*IntermediateTable_, rsp.Value());

        YT_LOG_INFO("Live preview for intermediate table created");
    }
}

void TOperationControllerBase::SafeOnJobInfoReceivedFromNode(std::unique_ptr<TJobSummary> jobSummary)
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker(Config_->JobEventsControllerQueue));

    auto jobId = jobSummary->Id;

    YT_LOG_DEBUG(
        "Job info received from node (JobId: %v, JobState: %v, HasStatistics: %v, FinishTime: %v)",
        jobId,
        jobSummary->State,
        static_cast<bool>(jobSummary->Statistics),
        jobSummary->FinishTime);

    auto joblet = FindJoblet(jobId);

    if (!joblet) {
        YT_LOG_DEBUG(
            "Received job info for unknown job (JobId: %v)",
            jobId);
        return;
    }

    if (!joblet->IsStarted()) {
        YT_VERIFY(joblet->Revived);

        YT_LOG_DEBUG(
            "Received revived job info for job that is not marked started in snapshot; processing job start "
            "(JobId: %v, JobState: %v)",
            jobId,
            jobSummary->State);

        OnJobStarted(joblet);
    }

    joblet->JobSpecProtoFuture.Reset();

    switch (jobSummary->State) {
        case EJobState::Waiting:
            return;
        case EJobState::Running:
            OnJobRunning(
                joblet,
                SummaryCast<TRunningJobSummary>(std::move(jobSummary)));
            return;
        case EJobState::Completed:
            OnJobCompleted(
                std::move(joblet),
                SummaryCast<TCompletedJobSummary>(std::move(jobSummary)));
            return;
        case EJobState::Failed:
            OnJobFailed(
                std::move(joblet),
                SummaryCast<TFailedJobSummary>(std::move(jobSummary)));
            return;
        case EJobState::Aborted:
            OnJobAborted(
                std::move(joblet),
                SummaryCast<TAbortedJobSummary>(std::move(jobSummary)));
            return;
        default:
            YT_ABORT();
    }
}

void TOperationControllerBase::ValidateInputTablesTypes() const
{
    for (const auto& table : InputManager_->GetInputTables()) {
        if (table->Type != EObjectType::Table) {
            THROW_ERROR_EXCEPTION("Object %v has invalid type: expected %Qlv, actual %Qlv",
                table->GetPath(),
                EObjectType::Table,
                table->Type);
        }
    }
}

void TOperationControllerBase::ValidateUpdatingTablesTypes() const
{
    for (const auto& table : UpdatingTables_) {
        if (table->Type != EObjectType::Table) {
            THROW_ERROR_EXCEPTION("Object %v has invalid type: expected %Qlv, actual %Qlv",
                table->GetPath(),
                EObjectType::Table,
                table->Type);
        }
    }
}

EObjectType TOperationControllerBase::GetOutputTableDesiredType() const
{
    return EObjectType::Table;
}

void TOperationControllerBase::ForEachLockableDynamicTable(std::function<void(const TOutputTablePtr&)> handler)
{
    if (auto explicitOption = Spec_->LockOutputDynamicTables) {
        if (!*explicitOption) {
            YT_LOG_DEBUG("Will not lock output dynamic tables since locking is disabled in spec");
            return;
        }
    } else {
        if (Spec_->Atomicity == EAtomicity::None && !Config_->LockNonAtomicOutputDynamicTables) {
            YT_LOG_DEBUG("Will not lock output tables with atomicity %Qlv", EAtomicity::None);
            return;
        }
    }

    if (OperationType_ == EOperationType::RemoteCopy) {
        YT_LOG_DEBUG("Will not lock output tables since operation is remote copy");
        return;
    }

    for (const auto& table : UpdatingTables_) {
        if (table->Dynamic && !table->Path.GetOutputTimestamp()) {
            handler(table);
        }
    }
}

void TOperationControllerBase::ValidateOutputDynamicTablesAllowed() const
{
    if (Config_->EnableBulkInsertForEveryone ||
        OperationType_ == EOperationType::RemoteCopy ||
        Spec_->AllowOutputDynamicTables ||
        IsBulkInsertAllowedForUser(AuthenticatedUser_, OutputClient_))
    {
        return;
    }

    THROW_ERROR_EXCEPTION(
        "Dynamic output table detected. Please read the \"Bulk insert\" "
        "article in the documenation before running the operation. In the "
        "article you will find a flag to suppress this error and several "
        "hints about operations with dynamic output tables.");
}

void TOperationControllerBase::GetOutputTablesSchema()
{
    YT_LOG_INFO("Getting output tables schema");

    auto proxy = CreateObjectServiceReadProxy(OutputClient_, EMasterChannelKind::Follower);
    auto batchReq = proxy.ExecuteBatch();

    static const auto AttributeKeys = [] {
        return ConcatVectors(
            GetTableUploadOptionsAttributeKeys(),
            std::vector<std::string>{
                "schema_id"
            });
    }();

    YT_LOG_DEBUG("Fetching output tables schema information from primary cell");

    for (const auto& table : UpdatingTables_) {
        auto req = TTableYPathProxy::Get(table->GetObjectIdPath() + "/@");
        ToProto(req->mutable_attributes()->mutable_keys(), AttributeKeys);
        req->Tag() = table;
        SetTransactionId(req, GetTransactionForOutputTable(table)->GetId());
        batchReq->AddRequest(req);
    }

    auto batchRspOrError = WaitFor(batchReq->Invoke());
    THROW_ERROR_EXCEPTION_IF_FAILED(GetCumulativeError(batchRspOrError), "Error getting attributes of output tables from native cell");
    const auto& batchRsp = batchRspOrError.Value();

    THashMap<TOutputTablePtr, IAttributeDictionaryPtr> tableAttributes;
    auto rspsOrError = batchRsp->GetResponses<TTableYPathProxy::TRspGet>();
    for (const auto& rspOrError : rspsOrError) {
        const auto& rsp = rspOrError.Value();

        auto table = std::any_cast<TOutputTablePtr>(rsp->Tag());
        auto attributes = ConvertToAttributes(TYsonString(rsp->value()));
        tableAttributes.emplace(std::move(table), std::move(attributes));
    }

    YT_LOG_DEBUG("Finished fetching output tables schema information from primary cell");

    bool needFetchSchemas = !UpdatingTables_.empty() && !UpdatingTables_[0]->IsFile();
    if (needFetchSchemas) {
        // Fetch the schemas based on schema IDs. We didn't fetch the schemas initially to allow deduplication
        // if there are multiple tables sharing same schema.
        for (const auto& [table, attributes] : tableAttributes) {
            table->SchemaId = attributes->Get<TGuid>("schema_id");
        }

        FetchTableSchemas(
            OutputClient_,
            UpdatingTables_);
    }

    for (const auto& [table, attributes] : tableAttributes) {
        const auto& path = table->Path;

        if (table->IsFile()) {
            table->TableUploadOptions = GetFileUploadOptions(path, *attributes);
            continue;
        } else {
            table->Dynamic = attributes->Get<bool>("dynamic");
            table->TableUploadOptions = GetTableUploadOptions(
                path,
                *attributes,
                table->Schema,
                0); // Here we assume zero row count, we will do additional check later.
        }

        // Will be used by AddOutputTableSpecs.
        table->TableUploadOptions.SchemaId = table->SchemaId;

        if (table->Dynamic) {
            if (!table->TableUploadOptions.TableSchema->IsSorted()) {
                THROW_ERROR_EXCEPTION("Only sorted dynamic table can be updated")
                    << TErrorAttribute("table_path", path);
            }

            ValidateOutputDynamicTablesAllowed();
        }

        if (path.GetOutputTimestamp()) {
            if (table->Dynamic && table->TableUploadOptions.SchemaModification != ETableSchemaModification::None) {
                THROW_ERROR_EXCEPTION("Cannot set \"output_timestamp\" attribute to the dynamic table with nontrivial schema modification");
            }
            auto outputTimestamp = *path.GetOutputTimestamp();
            if (outputTimestamp < MinTimestamp || outputTimestamp > MaxTimestamp) {
                THROW_ERROR_EXCEPTION("Attribute \"output_timestamp\" value is out of range [%v, %v]",
                    MinTimestamp,
                    MaxTimestamp)
                    << TErrorAttribute("output_timestamp", outputTimestamp)
                    << TErrorAttribute("table_path", path);
            }

            table->Timestamp = outputTimestamp;
        } else {
            // TODO(savrus): I would like to see commit ts here. But as for now, start ts suffices.
            table->Timestamp = GetTransactionForOutputTable(table)->GetStartTimestamp();
        }

        // NB(psushin): This option must be set before PrepareOutputTables call.
        table->TableWriterOptions->EvaluateComputedColumns = table->TableUploadOptions.TableSchema->HasMaterializedComputedColumns();

        table->TableWriterOptions->SchemaModification = table->TableUploadOptions.SchemaModification;

        table->TableWriterOptions->VersionedWriteOptions = table->TableUploadOptions.VersionedWriteOptions;

        YT_LOG_DEBUG("Received output table schema (Path: %v, Schema: %v, SchemaId: %v, SchemaMode: %v, LockMode: %v)",
            path,
            *table->TableUploadOptions.TableSchema,
            table->TableUploadOptions.SchemaId,
            table->TableUploadOptions.SchemaMode,
            table->TableUploadOptions.LockMode);
    }

    if (StderrTable_) {
        StderrTable_->TableUploadOptions.TableSchema = GetStderrBlobTableSchema().ToTableSchema();
        StderrTable_->TableUploadOptions.SchemaMode = ETableSchemaMode::Strong;
        if (StderrTable_->TableUploadOptions.UpdateMode == EUpdateMode::Append) {
            THROW_ERROR_EXCEPTION("Cannot write stderr table in append mode");
        }
    }

    if (CoreTable_) {
        CoreTable_->TableUploadOptions.TableSchema = GetCoreBlobTableSchema().ToTableSchema();
        CoreTable_->TableUploadOptions.SchemaMode = ETableSchemaMode::Strong;
        if (CoreTable_->TableUploadOptions.UpdateMode == EUpdateMode::Append) {
            THROW_ERROR_EXCEPTION("Cannot write core table in append mode");
        }
    }
}

void TOperationControllerBase::PrepareInputTables()
{
    if (!AreForeignTablesSupported()) {
        for (const auto& table : InputManager_->GetInputTables()) {
            if (table->IsForeign()) {
                THROW_ERROR_EXCEPTION("Foreign tables are not supported in %Qlv operation", OperationType_)
                    << TErrorAttribute("foreign_table", table->GetPath());
            }
        }
    }
}

void TOperationControllerBase::PatchTableWriteBuffer(
    TTableWriterOptionsPtr& writerOptions,
    ETableSchemaMode schemaMode,
    const TEpochSchema& schema) const
{
    // This is the only reliable case when column count from schema will match column count of data.
    if (writerOptions->OptimizeFor == EOptimizeFor::Scan &&
        !schema->Columns().empty() &&
        schemaMode == NTableClient::ETableSchemaMode::Strong)
    {
        THashSet<TString> groups;
        int singleColumnGroupCount = 0;

        for (const auto& column : schema->Columns()) {
            if (column.Group()) {
                groups.emplace(*column.Group());
            } else {
                ++singleColumnGroupCount;
            }
        }

        int dataBlockWriterCount = singleColumnGroupCount + groups.size();
        writerOptions->BufferSize = std::min(
            dataBlockWriterCount * Config_->DesiredBlockSize,
            Config_->MaxEstimatedWriteBufferSize);
        writerOptions->BlockSize = Config_->DesiredBlockSize;
    }
}

void TOperationControllerBase::PrepareOutputTables()
{ }

void TOperationControllerBase::LockOutputTablesAndGetAttributes()
{
    YT_LOG_INFO("Locking output tables");

    {
        auto proxy = CreateObjectServiceWriteProxy(OutputClient_);
        auto batchReq = proxy.ExecuteBatch();
        for (const auto& table : UpdatingTables_) {
            auto req = TTableYPathProxy::Lock(table->GetObjectIdPath());
            SetTransactionId(req, GetTransactionForOutputTable(table)->GetId());
            GenerateMutationId(req);
            req->set_mode(ToProto(table->TableUploadOptions.LockMode));
            req->Tag() = table;
            batchReq->AddRequest(req);
        }
        auto batchRspOrError = WaitFor(batchReq->Invoke());
        THROW_ERROR_EXCEPTION_IF_FAILED(
            GetCumulativeError(batchRspOrError),
            "Error locking output tables");

        const auto& batchRsp = batchRspOrError.Value();
        for (const auto& rspOrError : batchRsp->GetResponses<TCypressYPathProxy::TRspLock>()) {
            const auto& rsp = rspOrError.Value();
            const auto& table = std::any_cast<TOutputTablePtr>(rsp->Tag());

            auto objectId = FromProto<TObjectId>(rsp->node_id());
            table->Revision = FromProto<NHydra::TRevision>(rsp->revision());

            table->ExternalTransactionId = rsp->has_external_transaction_id()
                ? FromProto<TTransactionId>(rsp->external_transaction_id())
                : GetTransactionForOutputTable(table)->GetId();

            YT_LOG_INFO("Output table locked (Path: %v, ObjectId: %v, Schema: %v, ExternalTransactionId: %v, Revision: %x)",
                table->GetPath(),
                objectId,
                *table->TableUploadOptions.TableSchema,
                table->ExternalTransactionId,
                table->Revision);

            InputManager_->ValidateOutputTableLockedCorrectly(table);
        }
    }

    YT_LOG_INFO("Getting output tables attributes");

    {
        THashMap<TCellTag, TVector<TOutputTablePtr>> perCellUpdatingTables;
        for (const auto& table : UpdatingTables_) {
            perCellUpdatingTables[table->ExternalCellTag].push_back(table);
        }

        std::vector<TFuture<TObjectServiceProxy::TRspExecuteBatchPtr>> futures;
        for (const auto& [externalCellTag, tables] : perCellUpdatingTables) {
            auto proxy = CreateObjectServiceReadProxy(OutputClient_, EMasterChannelKind::Follower, externalCellTag);
            auto batchReq = proxy.ExecuteBatch();

            YT_LOG_DEBUG("Fetching attributes of output tables from external cell (CellTag: %v, NodeCount: %v)",
                externalCellTag,
                tables.size());

            for (const auto& table : tables) {
                auto req = TTableYPathProxy::Get(table->GetObjectIdPath() + "/@");
                ToProto(req->mutable_attributes()->mutable_keys(), std::vector<TString>{
                    "schema_id",
                    "account",
                    "chunk_writer",
                    "primary_medium",
                    "replication_factor",
                    "row_count",
                    "vital",
                    "enable_skynet_sharing",
                    "atomicity",
                });
                req->Tag() = table;
                SetTransactionId(req, table->ExternalTransactionId);

                batchReq->AddRequest(req);
            }

            futures.push_back(batchReq->Invoke());
        }

        auto checkErrorExternalCells = [] (const auto& error) {
            THROW_ERROR_EXCEPTION_IF_FAILED(error, "Error getting attributes of output tables from external cells");
        };

        auto responses = WaitFor(AllSucceeded(futures));
        checkErrorExternalCells(responses);

        THashMap<TOutputTablePtr, IAttributeDictionaryPtr> tableAttributes;
        for (const auto& response : responses.Value()) {
            checkErrorExternalCells(GetCumulativeError(response));
            auto rspsOrErrors = response->GetResponses<TTableYPathProxy::TRspGet>();
            for (const auto& rspOrError : rspsOrErrors) {
                const auto& rsp = rspOrError.Value();

                auto table = std::any_cast<TOutputTablePtr>(rsp->Tag());
                auto attributes = ConvertToAttributes(TYsonString(rsp->value()));
                tableAttributes.emplace(std::move(table), std::move(attributes));
            }
        }

        YT_LOG_DEBUG("Finished fetching output tables schema from external cells");

        for (const auto& [table, attributes] : tableAttributes) {
            if (table->IsFile()) {
                continue;
            }
            auto receivedSchemaId = attributes->GetAndRemove<TGuid>("schema_id");
            if (receivedSchemaId != table->SchemaId) {
                THROW_ERROR_EXCEPTION(
                    NScheduler::EErrorCode::OperationFailedWithInconsistentLocking,
                    "Schema of an output table %v has changed between schema fetch and lock acquisition",
                    table->GetPath())
                    << TErrorAttribute("expected_schema_id", table->SchemaId)
                    << TErrorAttribute("received_schema_id", receivedSchemaId);
            }
        }

        // Getting attributes from primary cell
        auto proxy = CreateObjectServiceReadProxy(OutputClient_, EMasterChannelKind::Follower);
        auto batchReq = proxy.ExecuteBatch();

        YT_LOG_DEBUG("Fetching attributes of output tables from native cell");

        for (const auto& table : UpdatingTables_) {
            auto req = TTableYPathProxy::Get(table->GetObjectIdPath() + "/@");
            ToProto(req->mutable_attributes()->mutable_keys(), std::vector<TString>{
                "effective_acl",
                "tablet_state",
                "backup_state",
                "tablet_statistics",
                "max_overlapping_store_count",
            });
            req->Tag() = table;
            SetTransactionId(req, GetTransactionForOutputTable(table)->GetId());
            batchReq->AddRequest(req);
        }

        auto batchRspOrError = WaitFor(batchReq->Invoke());
        THROW_ERROR_EXCEPTION_IF_FAILED(
            GetCumulativeError(batchRspOrError),
            "Error getting attributes of output tables from native cell");
        const auto& batchRsp = batchRspOrError.Value();

        auto rspsOrError = batchRsp->GetResponses<TTableYPathProxy::TRspGet>();
        for (const auto& rspOrError : rspsOrError) {
            const auto& rsp = rspOrError.Value();

            auto table = std::any_cast<TOutputTablePtr>(rsp->Tag());
            auto attributes = ConvertToAttributes(TYsonString(rsp->value()));

            auto it = tableAttributes.find(table);
            YT_VERIFY(it != tableAttributes.end());
            it->second->MergeFrom(*attributes.Get());
        }

        YT_LOG_DEBUG("Finished fetching output tables schema from native cell");

        YT_LOG_DEBUG("Fetching max heavy columns from master");

        TGetNodeOptions options;
        options.ReadFrom = EMasterChannelKind::Cache;

        auto maxHeavyColumnsRspOrError = WaitFor(OutputClient_->GetNode("//sys/@config/chunk_manager/max_heavy_columns", options));
        THROW_ERROR_EXCEPTION_IF_FAILED(maxHeavyColumnsRspOrError, "Failed to get max heavy columns from master");

        auto maxHeavyColumns = ConvertTo<int>(maxHeavyColumnsRspOrError.Value());

        YT_LOG_DEBUG("Finished fetching max heavy columns (MaxHeavyColumns: %v)", maxHeavyColumns);

        for (const auto& [table, attributes] : tableAttributes) {
            const auto& path = table->GetPath();

            if (table->Dynamic) {
                auto tabletState = attributes->Get<ETabletState>("tablet_state");
                if (OperationType_ == EOperationType::RemoteCopy) {
                    if (tabletState != ETabletState::Unmounted) {
                        THROW_ERROR_EXCEPTION("Remote copy is only allowed to unmounted table, "
                            "while output table %v has tablet state %Qv",
                            path,
                            tabletState);
                    }
                } else {
                    if (tabletState != ETabletState::Mounted && tabletState != ETabletState::Frozen) {
                        THROW_ERROR_EXCEPTION("Output table %v tablet state %Qv does not allow to write into it",
                            path,
                            tabletState);
                    }
                }

                auto backupState = attributes->Get<ETableBackupState>("backup_state", ETableBackupState::None);
                if (backupState != ETableBackupState::None) {
                    THROW_ERROR_EXCEPTION("Output table %v backup state %Qlv does not allow to write into it",
                        path,
                        backupState);
                }

                if (UserTransactionId_ && !Config_->AllowBulkInsertUnderUserTransaction) {
                    THROW_ERROR_EXCEPTION(
                        "Operations with output to dynamic tables cannot be run under user transaction")
                        << TErrorAttribute("user_transaction_id", UserTransactionId_);
                }

                auto atomicity = attributes->Get<EAtomicity>("atomicity");
                if (atomicity != Spec_->Atomicity) {
                    THROW_ERROR_EXCEPTION("Output table %v atomicity %Qv does not match spec atomicity %Qlv",
                        path,
                        atomicity,
                        Spec_->Atomicity);
                }

                if (table->TableUploadOptions.UpdateMode == EUpdateMode::Append) {
                    auto overlappingStoreCount = TryGetInt64(
                        attributes->GetYson("tablet_statistics").ToString(),
                        "/overlapping_store_count");
                    if (!overlappingStoreCount) {
                        THROW_ERROR_EXCEPTION("Output table %v does not have @tablet_statistics/overlapping_store_count attribute",
                            path);
                    }
                    auto maxOverlappingStoreCount = attributes->Get<int>(
                        "max_overlapping_store_count",
                        DefaultMaxOverlappingStoreCount);

                    if (*overlappingStoreCount >= maxOverlappingStoreCount) {
                        THROW_ERROR_EXCEPTION(
                            "Cannot write to output table %v since overlapping store count limit is exceeded",
                            path)
                            << TErrorAttribute("overlapping_store_count", *overlappingStoreCount)
                            << TErrorAttribute("max_overlapping_store_count", maxOverlappingStoreCount);
                    }
                }
            }

            table->Account = attributes->Get<std::string>("account");

            if (table->TableUploadOptions.TableSchema->IsSorted()) {
                table->TableWriterOptions->ValidateSorted = true;
                table->TableWriterOptions->ValidateUniqueKeys = table->TableUploadOptions.TableSchema->IsUniqueKeys();
            } else {
                table->TableWriterOptions->ValidateSorted = false;
            }

            table->TableWriterOptions->CompressionCodec = table->TableUploadOptions.CompressionCodec;
            table->TableWriterOptions->ErasureCodec = table->TableUploadOptions.ErasureCodec;
            table->TableWriterOptions->EnableStripedErasure = table->TableUploadOptions.EnableStripedErasure;
            table->TableWriterOptions->ReplicationFactor = attributes->Get<int>("replication_factor");
            table->TableWriterOptions->MediumName = attributes->Get<std::string>("primary_medium");
            table->TableWriterOptions->Account = attributes->Get<std::string>("account");
            table->TableWriterOptions->ChunksVital = attributes->Get<bool>("vital");
            table->TableWriterOptions->OptimizeFor = table->TableUploadOptions.OptimizeFor;
            table->TableWriterOptions->ChunkFormat = table->TableUploadOptions.ChunkFormat;
            table->TableWriterOptions->EnableSkynetSharing = attributes->Get<bool>("enable_skynet_sharing", false);
            table->TableWriterOptions->MaxHeavyColumns = maxHeavyColumns;

            // Workaround for YT-5827.
            if (table->TableUploadOptions.TableSchema->IsEmpty() &&
                table->TableUploadOptions.TableSchema->IsStrict())
            {
                table->TableWriterOptions->OptimizeFor = EOptimizeFor::Lookup;
                table->TableWriterOptions->ChunkFormat = {};
            }

            if (Spec_->EnableWriteBufferSizeEstimation) {
                PatchTableWriteBuffer(
                    table->TableWriterOptions,
                    table->TableUploadOptions.SchemaMode,
                    table->TableUploadOptions.TableSchema);
            }

            table->EffectiveAcl = attributes->GetYson("effective_acl");
            table->WriterConfig = attributes->FindYson("chunk_writer");

            YT_LOG_INFO("Output table attributes fetched (Path: %v, Options: %v, UploadTransactionId: %v)",
                path,
                ConvertToYsonString(table->TableWriterOptions, EYsonFormat::Text).ToString(),
                table->UploadTransactionId);
        }
    }
}

void TOperationControllerBase::BeginUploadOutputTables(const std::vector<TOutputTablePtr>& tables)
{
    THashMap<TCellTag, std::vector<TOutputTablePtr>> nativeCellTagToTables;
    for (const auto& table : tables) {
        nativeCellTagToTables[CellTagFromId(table->ObjectId)].push_back(table);
    }

    THashMap<TCellTag, std::vector<TOutputTablePtr>> externalCellTagToTables;
    for (const auto& table : tables) {
        externalCellTagToTables[table->ExternalCellTag].push_back(table);
    }

    {
        YT_LOG_INFO("Starting upload for output tables");

        std::vector<TFuture<TObjectServiceProxy::TRspExecuteBatchPtr>> asyncResults;
        for (const auto& [nativeCellTag, tables] : nativeCellTagToTables) {
            auto proxy = CreateObjectServiceWriteProxy(OutputClient_, nativeCellTag);
            auto batchReq = proxy.ExecuteBatch();
            for (const auto& table : tables) {
                auto req = TTableYPathProxy::BeginUpload(table->GetObjectIdPath());
                SetTransactionId(req, GetTransactionForOutputTable(table)->GetId());
                GenerateMutationId(req);
                req->Tag() = table;

                if (!table->IsFile()) {
                    // Schema revision should be equal to 1 iff schema does not change
                    // between being fetched from master while preparing output tables
                    // and sent to master during begin upload.
                    if (table->TableUploadOptions.TableSchema.GetRevision() == 1) {
                        YT_LOG_INFO("Sending schema id (SchemaId: %v)",
                            table->TableUploadOptions.SchemaId);
                        YT_VERIFY(table->TableUploadOptions.SchemaId);
                        ToProto(req->mutable_table_schema_id(), table->TableUploadOptions.SchemaId);
                    } else {
                        // Sending schema, since in this case it might be not registered on master yet.
                        YT_LOG_DEBUG("Sending full table schema to master during begin upload (TableSchemaRevision: %v)",
                            table->TableUploadOptions.TableSchema.GetRevision());
                        ToProto(req->mutable_table_schema(), table->TableUploadOptions.TableSchema.Get());
                    }

                    req->set_schema_mode(ToProto(table->TableUploadOptions.SchemaMode));
                }
                req->set_update_mode(ToProto(table->TableUploadOptions.UpdateMode));
                req->set_lock_mode(ToProto(table->TableUploadOptions.LockMode));
                req->set_upload_transaction_title(Format("Upload to %v from operation %v",
                    table->GetPath(),
                    OperationId_));
                batchReq->AddRequest(req);
            }

            asyncResults.push_back(batchReq->Invoke());
        }

        auto checkError = [] (const auto& error) {
            THROW_ERROR_EXCEPTION_IF_FAILED(error, "Error starting upload for output tables");
        };

        auto result = WaitFor(AllSucceeded(asyncResults));
        checkError(result);

        for (const auto& batchRsp : result.Value()) {
            checkError(GetCumulativeError(batchRsp));
            for (const auto& rspOrError : batchRsp->GetResponses<TTableYPathProxy::TRspBeginUpload>()) {
                const auto& rsp = rspOrError.Value();

                auto table = std::any_cast<TOutputTablePtr>(rsp->Tag());
                table->UploadTransactionId = FromProto<TTransactionId>(rsp->upload_transaction_id());
                table->SchemaId = FromProto<TMasterTableSchemaId>(rsp->upload_chunk_schema_id());
            }
        }
    }

    {
        YT_LOG_INFO("Getting output tables upload parameters");

        std::vector<TFuture<TObjectServiceProxy::TRspExecuteBatchPtr>> asyncResults;
        for (const auto& [externalCellTag, tables] : externalCellTagToTables) {
            auto proxy = CreateObjectServiceReadProxy(OutputClient_, EMasterChannelKind::Follower, externalCellTag);
            auto batchReq = proxy.ExecuteBatch();
            for (const auto& table : tables) {
                auto req = TTableYPathProxy::GetUploadParams(table->GetObjectIdPath());
                SetTransactionId(req, table->UploadTransactionId);
                req->Tag() = table;
                if (table->TableUploadOptions.TableSchema->IsSorted() &&
                    !table->Dynamic &&
                    table->TableUploadOptions.UpdateMode == EUpdateMode::Append)
                {
                    req->set_fetch_last_key(true);
                }
                if (table->Dynamic &&
                    OperationType_ == EOperationType::RemoteCopy &&
                    table->TableUploadOptions.TableSchema->HasHunkColumns())
                {
                    req->set_fetch_hunk_chunk_list_ids(true);
                }
                batchReq->AddRequest(req);
            }

            asyncResults.push_back(batchReq->Invoke());
        }

        auto checkError = [] (const auto& error) {
            THROW_ERROR_EXCEPTION_IF_FAILED(error, "Error getting upload parameters of output tables");
        };

        auto result = WaitFor(AllSucceeded(asyncResults));
        checkError(result);

        for (const auto& batchRsp : result.Value()) {
            checkError(GetCumulativeError(batchRsp));
            for (const auto& rspOrError : batchRsp->GetResponses<TTableYPathProxy::TRspGetUploadParams>()) {
                const auto& rsp = rspOrError.Value();
                auto table = std::any_cast<TOutputTablePtr>(rsp->Tag());

                if (table->Dynamic) {
                    table->PivotKeys = FromProto<std::vector<TLegacyOwningKey>>(rsp->pivot_keys());
                    table->TabletChunkListIds = FromProto<std::vector<TChunkListId>>(rsp->tablet_chunk_list_ids());
                    table->TabletHunkChunkListIds = FromProto<std::vector<TChunkListId>>(rsp->tablet_hunk_chunk_list_ids());
                } else {
                    const auto& schema = table->TableUploadOptions.TableSchema;
                    table->OutputChunkListId = FromProto<TChunkListId>(rsp->chunk_list_id());
                    if (schema->IsSorted() && table->TableUploadOptions.UpdateMode == EUpdateMode::Append) {
                        TUnversionedOwningRow row;
                        FromProto(&row, rsp->last_key());
                        auto fixedRow = LegacyKeyToKeyFriendlyOwningRow(row, schema->GetKeyColumnCount());
                        if (row != fixedRow) {
                            YT_LOG_DEBUG(
                                "Table last key fixed (Path: %v, LastKey: %v -> %v)",
                                table->GetPath(),
                                row,
                                fixedRow);
                            row = fixedRow;
                        }
                        auto capturedRow = RowBuffer_->CaptureRow(row);
                        table->LastKey = TKey::FromRowUnchecked(capturedRow, schema->GetKeyColumnCount());
                        YT_LOG_DEBUG(
                            "Writing to table in sorted append mode (Path: %v, LastKey: %v)",
                            table->GetPath(),
                            table->LastKey);
                    }
                }

                YT_LOG_INFO("Upload parameters of output table received (Path: %v, ChunkListId: %v)",
                    table->GetPath(),
                    table->OutputChunkListId);
            }
        }
    }
}

void TOperationControllerBase::FetchUserFiles()
{
    std::vector<TUserFile*> userFiles;

    TMasterChunkSpecFetcherPtr chunkSpecFetcher;

    auto initializeFetcher = [&] {
        if (chunkSpecFetcher) {
            return;
        }
        chunkSpecFetcher = New<TMasterChunkSpecFetcher>(
            InputManager_->GetClient(LocalClusterName),
            TMasterReadOptions{},
            InputManager_->GetNodeDirectory(LocalClusterName),
            GetCancelableInvoker(),
            Config_->MaxChunksPerFetch,
            Config_->MaxChunksPerLocateRequest,
            [&] (const TChunkOwnerYPathProxy::TReqFetchPtr& req, int fileIndex) {
                const auto& file = *userFiles[fileIndex];
                req->set_fetch_all_meta_extensions(false);
                req->add_extension_tags(TProtoExtensionTag<NChunkClient::NProto::TMiscExt>::Value);
                if (file.Type == EObjectType::File && file.Path.GetColumns() && Spec_->UserFileColumnarStatistics->Enabled) {
                    req->add_extension_tags(TProtoExtensionTag<THeavyColumnStatisticsExt>::Value);
                }
                if (file.Dynamic || IsBoundaryKeysFetchEnabled()) {
                    req->add_extension_tags(TProtoExtensionTag<TBoundaryKeysExt>::Value);
                }
                if (file.Dynamic) {
                    if (!Spec_->EnableDynamicStoreRead.value_or(true)) {
                        req->set_omit_dynamic_stores(true);
                    }
                    if (OperationType_ == EOperationType::RemoteCopy) {
                        req->set_throw_on_chunk_views(true);
                    }
                }
                // NB: We always fetch parity replicas since
                // erasure reader can repair data on flight.
                req->set_fetch_parity_replicas(true);
                AddCellTagToSyncWith(req, file.ObjectId);
                SetTransactionId(req, file.ExternalTransactionId);
            },
            Logger);
    };

    auto addFileForFetching = [&userFiles, &chunkSpecFetcher, &initializeFetcher] (TUserFile& file) {
        initializeFetcher();
        int fileIndex = userFiles.size();
        userFiles.push_back(&file);

        std::vector<TReadRange> readRanges;
        switch (file.Type) {
            case EObjectType::Table:
                readRanges = file.Path.GetNewRanges(file.Schema->ToComparator(), file.Schema->GetKeyColumnTypes());
                break;
            case EObjectType::File:
                readRanges = {TReadRange()};
                break;
            default:
                YT_ABORT();
        }

        chunkSpecFetcher->Add(
            file.ObjectId,
            file.ExternalCellTag,
            file.ChunkCount,
            fileIndex,
            readRanges);
    };

    for (auto& [userJobSpec, files] : UserJobFiles_) {
        for (auto& file : files) {
            YT_LOG_INFO("Adding user file for fetch (Path: %v, TaskTitle: %v)",
                file.Path,
                userJobSpec->TaskTitle);

            addFileForFetching(file);
        }
    }

    if (BaseLayer_) {
        YT_LOG_INFO("Adding base layer for fetch (Path: %v)",
            BaseLayer_->Path);

        addFileForFetching(*BaseLayer_);
    }

    if (!chunkSpecFetcher) {
        YT_LOG_INFO("No user files to fetch");
        return;
    }

    YT_LOG_INFO("Fetching user files");

    WaitFor(chunkSpecFetcher->Fetch())
        .ThrowOnError();

    YT_LOG_INFO("User files fetched (ChunkCount: %v)",
        chunkSpecFetcher->ChunkSpecs().size());

    for (auto& chunkSpec : chunkSpecFetcher->ChunkSpecs()) {
        auto chunkId = FromProto<TChunkId>(chunkSpec.chunk_id());
        if (IsDynamicTabletStoreType(TypeFromId(chunkId))) {
            const auto& fileName = userFiles[chunkSpec.table_index()]->Path;
            THROW_ERROR_EXCEPTION(
                "Dynamic store read is not supported for user files but it is "
                "enabled for user file %Qv; consider disabling dynamic store read "
                "in operation spec by setting \"enable_dynamic_store_read\" option "
                "to false or disable dynamic store read for table by setting attribute "
                "\"enable_dynamic_store_read\" to false and remounting table.",
                fileName);
        }

        // NB(gritukan): all user files chunks should have table_index = 0.
        int tableIndex = chunkSpec.table_index();
        chunkSpec.set_table_index(0);

        userFiles[tableIndex]->ChunkSpecs.push_back(chunkSpec);
    }
}

void TOperationControllerBase::ValidateUserFileSizes()
{
    YT_LOG_INFO("Validating user file sizes");
    bool hasFiles = false;
    for (const auto& [_, files] : UserJobFiles_) {
        for (const auto& file : files) {
            YT_VERIFY(!file.Path.GetCluster().has_value());
            hasFiles = true;
        }
    }

    if (!hasFiles) {
        YT_LOG_INFO("No user files");
        return;
    }

    auto columnarStatisticsFetcher = New<TColumnarStatisticsFetcher>(
        ChunkScraperInvoker_,
        InputManager_->GetClient(LocalClusterName),
        TColumnarStatisticsFetcher::TOptions{
            .Config = Config_->Fetcher,
            .NodeDirectory = InputManager_->GetNodeDirectory(LocalClusterName),
            .ChunkScraper = InputManager_->CreateFetcherChunkScraper(LocalClusterName),
            .Mode = Spec_->UserFileColumnarStatistics->Mode,
            .EnableEarlyFinish = Config_->EnableColumnarStatisticsEarlyFinish,
            .Logger = Logger,
        });

    // Collect columnar statistics for table files with column selectors.
    for (auto& [_, files] : UserJobFiles_) {
        for (auto& file : files) {
            if (file.Type == EObjectType::Table) {
                for (const auto& chunkSpec : file.ChunkSpecs) {
                    auto chunk = New<TInputChunk>(chunkSpec);
                    file.Chunks.emplace_back(chunk);
                    if (file.Path.GetColumns() && Spec_->UserFileColumnarStatistics->Enabled) {
                        auto stableColumnNames = MapNamesToStableNames(
                            *file.Schema,
                            *file.Path.GetColumns(),
                            NonexistentColumnName);
                        columnarStatisticsFetcher->AddChunk(chunk, stableColumnNames);
                    }
                }
            }
        }
    }

    if (columnarStatisticsFetcher->GetChunkCount() > 0) {
        YT_LOG_INFO("Fetching columnar statistics for table files with column selectors (ChunkCount: %v)",
            columnarStatisticsFetcher->GetChunkCount());
        columnarStatisticsFetcher->SetCancelableContext(GetCancelableContext());
        WaitFor(columnarStatisticsFetcher->Fetch())
            .ThrowOnError();
        columnarStatisticsFetcher->ApplyColumnSelectivityFactors();
    }

    auto updateOptional = [] (auto& updated, auto patch, auto defaultValue)
    {
        if (!updated.has_value()) {
            if (patch.has_value()) {
                updated = patch;
            } else {
                updated = defaultValue;
            }
        } else if (patch.has_value()) {
            updated = std::min(updated.value(), patch.value());
        }
    };

    auto userFileLimitsPatch = New<TUserFileLimitsPatchConfig>();
    for (const auto& [treeName, _] : PoolTreeControllerSettingsMap_) {
        auto it = Config_->UserFileLimitsPerTree.find(treeName);
        bool found = it != Config_->UserFileLimitsPerTree.end();
        updateOptional(
            userFileLimitsPatch->MaxSize,
            found ? it->second->MaxSize : std::optional<i64>(),
            Config_->UserFileLimits->MaxSize);
        updateOptional(
            userFileLimitsPatch->MaxTableDataWeight,
            found ? it->second->MaxTableDataWeight : std::optional<i64>(),
            Config_->UserFileLimits->MaxTableDataWeight);
        updateOptional(
            userFileLimitsPatch->MaxChunkCount,
            found ? it->second->MaxChunkCount : std::optional<i64>(),
            Config_->UserFileLimits->MaxChunkCount);
    }

    auto userFileLimits = New<TUserFileLimitsConfig>();
    userFileLimits->MaxSize = userFileLimitsPatch->MaxSize.value();
    userFileLimits->MaxTableDataWeight = userFileLimitsPatch->MaxTableDataWeight.value();
    userFileLimits->MaxChunkCount = userFileLimitsPatch->MaxChunkCount.value();

    auto validateFile = [&userFileLimits, this] (const TUserFile& file) {
        YT_LOG_DEBUG("Validating user file (FileName: %v, Path: %v, Type: %v, HasColumns: %v)",
            file.FileName,
            file.Path,
            file.Type,
            file.Path.GetColumns().operator bool());
        auto chunkCount = file.Type == NObjectClient::EObjectType::File ? file.ChunkCount : file.Chunks.size();
        if (static_cast<i64>(chunkCount) > userFileLimits->MaxChunkCount) {
            THROW_ERROR_EXCEPTION(
                "User file %v exceeds chunk count limit: %v > %v",
                file.Path.GetPath(),
                chunkCount,
                userFileLimits->MaxChunkCount)
                << TErrorAttribute("full_path", file.Path);
        }
        if (file.Type == NObjectClient::EObjectType::Table) {
            i64 dataWeight = 0;
            for (const auto& chunk : file.Chunks) {
                dataWeight += chunk->GetDataWeight();
            }
            if (dataWeight > userFileLimits->MaxTableDataWeight) {
                THROW_ERROR_EXCEPTION(
                    "User file table %v exceeds data weight limit: %v > %v",
                    file.Path.GetPath(),
                    dataWeight,
                    userFileLimits->MaxTableDataWeight)
                    << TErrorAttribute("full_path", file.Path);
            }
        } else {
            i64 uncompressedSize = 0;
            for (const auto& chunkSpec : file.ChunkSpecs) {
                uncompressedSize += GetChunkUncompressedDataSize(chunkSpec);
            }
            if (uncompressedSize > userFileLimits->MaxSize) {
                THROW_ERROR_EXCEPTION(
                    "User file %v exceeds size limit: %v > %v",
                    file.Path,
                    uncompressedSize,
                    userFileLimits->MaxSize);
            }
        }
    };

    for (auto& [_, files] : UserJobFiles_) {
        for (const auto& file : files) {
            validateFile(file);
        }
    }

    if (BaseLayer_) {
        validateFile(*BaseLayer_);
    }
}

void TOperationControllerBase::LockUserFiles()
{
    YT_LOG_INFO("Locking user files");

    auto proxy = CreateObjectServiceWriteProxy(OutputClient_);
    auto batchReq = proxy.ExecuteBatch();

    auto lockFile = [&batchReq] (TUserFile& file) {
        auto req = TFileYPathProxy::Lock(file.Path.GetPath());
        req->set_mode(ToProto(ELockMode::Snapshot));
        GenerateMutationId(req);
        SetTransactionId(req, *file.TransactionId);
        req->Tag() = &file;
        batchReq->AddRequest(req);
    };

    for (auto& [userJobSpec, files] : UserJobFiles_) {
        for (auto& file : files) {
            lockFile(file);
        }
    }

    if (BaseLayer_) {
        lockFile(*BaseLayer_);
    }

    auto batchRspOrError = WaitFor(batchReq->Invoke());
    THROW_ERROR_EXCEPTION_IF_FAILED(
        GetCumulativeError(batchRspOrError),
        "Error locking user files");

    const auto& batchRsp = batchRspOrError.Value();
    for (const auto& rspOrError : batchRsp->GetResponses<TCypressYPathProxy::TRspLock>()) {
        const auto& rsp = rspOrError.Value();
        auto* file = std::any_cast<TUserFile*>(rsp->Tag());
        file->ObjectId = FromProto<TObjectId>(rsp->node_id());
        file->ExternalTransactionId = rsp->has_external_transaction_id()
            ? FromProto<TTransactionId>(rsp->external_transaction_id())
            : *file->TransactionId;
    }
}

void TOperationControllerBase::GetUserFilesAttributes()
{
    // XXX(babenko): refactor; in particular, request attributes from external cells
    YT_LOG_INFO("Getting user files attributes");
    for (const auto& [_, files] : UserJobFiles_) {
        for (const auto& file : files) {
            if (file.Path.GetCluster().has_value()) {
                THROW_ERROR_EXCEPTION("User file must not have \"cluster\" attribute")
                    << TErrorAttribute("file_path", file.Path);
            }
        }
    }

    for (auto& [userJobSpec, files] : UserJobFiles_) {
        GetUserObjectBasicAttributes(
            InputManager_->GetClient(LocalClusterName),
            MakeUserObjectList(files),
            InputTransactions_->GetLocalInputTransactionId(),
            Logger().WithTag("TaskTitle: %v", userJobSpec->TaskTitle),
            EPermission::Read,
            TGetUserObjectBasicAttributesOptions{
                .PopulateSecurityTags = true
            });
    }

    if (BaseLayer_) {
        std::vector<TUserObject*> layers(1, &*BaseLayer_);

        GetUserObjectBasicAttributes(
            InputManager_->GetClient(LocalClusterName),
            layers,
            InputTransactions_->GetLocalInputTransactionId(),
            Logger,
            EPermission::Read,
            TGetUserObjectBasicAttributesOptions{
                .PopulateSecurityTags = true
            });
    }

    for (const auto& files : GetValues(UserJobFiles_)) {
        for (const auto& file : files) {
            const auto& path = file.Path.GetPath();
            if (!file.Layer && file.Type != EObjectType::Table && file.Type != EObjectType::File) {
                THROW_ERROR_EXCEPTION("User file %v has invalid type: expected %Qlv or %Qlv, actual %Qlv",
                    path,
                    EObjectType::Table,
                    EObjectType::File,
                    file.Type);
            } else if (file.Layer && file.Type != EObjectType::File) {
                THROW_ERROR_EXCEPTION("User layer %v has invalid type: expected %Qlv, actual %Qlv",
                    path,
                    EObjectType::File,
                    file.Type);
            }
        }
    }

    if (BaseLayer_ && BaseLayer_->Type != EObjectType::File) {
        THROW_ERROR_EXCEPTION("User layer %v has invalid type: expected %Qlv, actual %Qlv",
            BaseLayer_->Path,
            EObjectType::File,
            BaseLayer_->Type);
    }


    auto proxy = CreateObjectServiceReadProxy(InputManager_->GetClient(LocalClusterName), EMasterChannelKind::Follower);
    auto batchReq = proxy.ExecuteBatch();

    for (const auto& files : GetValues(UserJobFiles_)) {
        for (const auto& file : files) {
            {
                auto req = TYPathProxy::Get(file.GetObjectIdPath() + "/@");
                SetTransactionId(req, *file.TransactionId);
                std::vector<TString> attributeKeys;
                attributeKeys.push_back("file_name");
                attributeKeys.push_back("account");
                switch (file.Type) {
                    case EObjectType::File:
                        attributeKeys.push_back("executable");
                        attributeKeys.push_back("filesystem");
                        attributeKeys.push_back("access_method");
                        break;

                    case EObjectType::Table:
                        attributeKeys.push_back("format");
                        attributeKeys.push_back("dynamic");
                        attributeKeys.push_back("schema");
                        attributeKeys.push_back("retained_timestamp");
                        attributeKeys.push_back("unflushed_timestamp");
                        attributeKeys.push_back("enable_dynamic_store_read");
                        break;

                    default:
                        YT_ABORT();
                }
                attributeKeys.push_back("key");
                attributeKeys.push_back("chunk_count");
                attributeKeys.push_back("content_revision");
                ToProto(req->mutable_attributes()->mutable_keys(), attributeKeys);
                batchReq->AddRequest(req, "get_attributes");
            }

            {
                auto req = TYPathProxy::Get(file.Path.GetPath() + "&/@");
                SetTransactionId(req, *file.TransactionId);
                ToProto(req->mutable_attributes()->mutable_keys(), std::vector<TString>{
                    "key",
                    "file_name"
                });
                batchReq->AddRequest(req, "get_link_attributes");
            }
        }
    }

    auto batchRspOrError = WaitFor(batchReq->Invoke());
    THROW_ERROR_EXCEPTION_IF_FAILED(batchRspOrError, "Error getting attributes of user files");
    const auto& batchRsp = batchRspOrError.Value();

    auto getAttributesRspsOrError = batchRsp->GetResponses<TYPathProxy::TRspGetKey>("get_attributes");
    auto getLinkAttributesRspsOrError = batchRsp->GetResponses<TYPathProxy::TRspGetKey>("get_link_attributes");

    int index = 0;
    for (auto& [userJobSpec, files] : UserJobFiles_) {
        THashSet<TString> userFileNames;
        try {
            for (auto& file : files) {
                const auto& path = file.Path.GetPath();

                {
                    const auto& rspOrError = getAttributesRspsOrError[index];
                    THROW_ERROR_EXCEPTION_IF_FAILED(rspOrError, "Error getting attributes of user file %v", path);
                    const auto& rsp = rspOrError.Value();
                    const auto& linkRsp = getLinkAttributesRspsOrError[index];
                    index++;

                    file.Attributes = ConvertToAttributes(TYsonString(rsp->value()));
                    const auto& attributes = *file.Attributes;

                    try {
                        if (const auto& fileNameFromPath = file.Path.GetFileName()) {
                            file.FileName = *fileNameFromPath;
                        } else {
                            const auto* actualAttributes = &attributes;
                            IAttributeDictionaryPtr linkAttributes;
                            if (linkRsp.IsOK()) {
                                linkAttributes = ConvertToAttributes(TYsonString(linkRsp.Value()->value()));
                                actualAttributes = linkAttributes.Get();
                            }
                            if (const auto& fileNameAttribute = actualAttributes->Find<TString>("file_name")) {
                                file.FileName = *fileNameAttribute;
                            } else if (const auto& keyAttribute = actualAttributes->Find<TString>("key")) {
                                file.FileName = *keyAttribute;
                            } else {
                                THROW_ERROR_EXCEPTION("Couldn't infer file name for user file");
                            }
                        }
                    } catch (const std::exception& ex) {
                        // NB: Some of the above Gets and Finds may throw due to, e.g., type mismatch.
                        THROW_ERROR_EXCEPTION("Error parsing attributes of user file %v",
                            path) << ex;
                    }

                    switch (file.Type) {
                        case EObjectType::File:
                            file.Executable = attributes.Get<bool>("executable", false);
                            file.Executable = file.Path.GetExecutable().value_or(file.Executable);

                            if (file.Layer) {
                                if (attributes.Get<i64>("chunk_count") == 0) {
                                    THROW_ERROR_EXCEPTION("File %v is empty", file.Path);
                                }

                                auto access_method = file.Path.GetAccessMethod();
                                if (!access_method) {
                                    access_method = attributes.Find<TString>("access_method");
                                }

                                std::tie(file.AccessMethod, file.Filesystem) = GetAccessMethodAndFilesystemFromStrings(
                                    access_method.value_or(ToString(ELayerAccessMethod::Local)),
                                    attributes.Find<TString>("filesystem").value_or(ToString(ELayerFilesystem::Archive)));
                            }
                            break;

                        case EObjectType::Table:
                            file.Dynamic = attributes.Get<bool>("dynamic");
                            file.Schema = attributes.Get<TTableSchemaPtr>("schema");
                            if (auto renameDescriptors = file.Path.GetColumnRenameDescriptors()) {
                                YT_LOG_DEBUG("Start renaming columns of user file");
                                auto description = Format("user file %v", file.GetPath());
                                file.Schema = RenameColumnsInSchema(
                                    description,
                                    file.Schema,
                                    file.Dynamic,
                                    *renameDescriptors,
                                    /*changeStableName*/ !Config_->EnableTableColumnRenaming);
                                YT_LOG_DEBUG("Columns of user file are renamed (Path: %v, NewSchema: %v)",
                                    file.GetPath(),
                                    *file.Schema);
                            }
                            file.Format = attributes.FindYson("format");
                            if (!file.Format) {
                                file.Format = file.Path.GetFormat();
                            }
                            // Validate that format is correct.
                            try {
                                if (!file.Format) {
                                    THROW_ERROR_EXCEPTION("Format is not specified");
                                }
                                ConvertTo<TFormat>(file.Format);
                            } catch (const std::exception& ex) {
                                THROW_ERROR_EXCEPTION("Failed to parse format of table file %v",
                                    file.Path) << ex;
                            }
                            // Validate that timestamp is correct.
                            ValidateDynamicTableTimestamp(file.Path, file.Dynamic, *file.Schema, attributes);
                            break;

                        default:
                            YT_ABORT();
                    }

                    file.Account = attributes.Get<std::string>("account");
                    file.ChunkCount = attributes.Get<i64>("chunk_count");
                    file.ContentRevision = attributes.Get<NHydra::TRevision>("content_revision");

                    YT_LOG_INFO("User file locked (Path: %v, TaskTitle: %v, FileName: %v, SecurityTags: %v, ContentRevision: %x)",
                        path,
                        userJobSpec->TaskTitle,
                        file.FileName,
                        file.SecurityTags,
                        file.ContentRevision);
                }

                if (!file.Layer) {
                    const auto& path = file.Path.GetPath();
                    const auto& fileName = file.FileName;

                    if (fileName.empty()) {
                        THROW_ERROR_EXCEPTION("Empty user file name for %v",
                            path);
                    }

                    if (!NFS::IsPathRelativeAndInvolvesNoTraversal(fileName)) {
                        THROW_ERROR_EXCEPTION("User file name %Qv for %v does not point inside the sandbox directory",
                            fileName,
                            path);
                    }

                    if (!userFileNames.insert(fileName).second) {
                        THROW_ERROR_EXCEPTION("Duplicate user file name %Qv for %v",
                            fileName,
                            path);
                    }
                }
            }
        } catch (const std::exception& ex) {
            THROW_ERROR_EXCEPTION("Error getting user file attributes")
                << TErrorAttribute("task_title", userJobSpec->TaskTitle)
                << ex;
        }
    }
}

void TOperationControllerBase::PrepareInputQuery()
{ }

void TOperationControllerBase::ParseInputQuery(
    const TString& queryString,
    const std::optional<TTableSchema>& schema,
    TQueryFilterOptionsPtr queryFilterOptions)
{
    for (const auto& table : InputManager_->GetInputTables()) {
        if (table->Path.GetColumns()) {
            THROW_ERROR_EXCEPTION("Column filter and QL filter cannot appear in the same operation");
        }
    }

    auto externalCGInfo = New<TExternalCGInfo>();
    auto fetchFunctions = [&] (TRange<std::string> names, const TTypeInferrerMapPtr& typeInferrers) {
        MergeFrom(typeInferrers.Get(), *GetBuiltinTypeInferrers());

        std::vector<std::string> externalNames;
        for (const auto& name : names) {
            auto found = typeInferrers->find(name);
            if (found == typeInferrers->end()) {
                externalNames.push_back(name);
            }
        }

        if (externalNames.empty()) {
            return;
        }

        if (!Config_->UdfRegistryPath) {
            THROW_ERROR_EXCEPTION("External UDF registry is not configured")
                << TErrorAttribute("external_names", externalNames);
        }

        std::vector<std::pair<TYPath, std::string>> keys;
        for (const auto& name : externalNames) {
            keys.emplace_back(*Config_->UdfRegistryPath, name);
        }

        auto descriptors = LookupAllUdfDescriptors(keys, Host_->GetClient());

        AppendUdfDescriptors(typeInferrers, externalCGInfo, externalNames, descriptors);
    };

    auto inferSchema = [&] {
        std::vector<TTableSchemaPtr> schemas;
        for (const auto& table : InputManager_->GetInputTables()) {
            schemas.push_back(table->Schema);
        }
        return InferInputSchema(schemas, false);
    };

    auto query = PrepareJobQuery(
        queryString,
        schema ? New<TTableSchema>(*schema) : inferSchema(),
        fetchFunctions);

    auto getColumns = [] (const TTableSchema& desiredSchema, const TTableSchema& tableSchema) {
        std::vector<std::string> columns;
        for (const auto& column : desiredSchema.Columns()) {
            auto columnName = column.Name();
            if (tableSchema.FindColumn(columnName)) {
                columns.push_back(columnName);
            }
        }

        return std::ssize(columns) == tableSchema.GetColumnCount()
            ? std::optional<std::vector<std::string>>()
            : std::make_optional(std::move(columns));
    };

    // Use query column filter for input tables.
    bool allowTimestampColumns = false;
    for (auto table : InputManager_->GetInputTables()) {
        auto columns = getColumns(*query->GetReadSchema(), *table->Schema);
        if (columns) {
            table->Path.SetColumns(*columns);
        }

        allowTimestampColumns |= table->Path.GetVersionedReadOptions().ReadMode == EVersionedIOMode::LatestTimestamp;
    }

    InputQuery_.emplace();
    InputQuery_->Query = std::move(query);
    InputQuery_->ExternalCGInfo = std::move(externalCGInfo);
    InputQuery_->QueryFilterOptions = std::move(queryFilterOptions);

    try {
        ValidateTableSchema(
            *InputQuery_->Query->GetTableSchema(),
            /*isTableDynamic*/ false,
            /*options*/ {
                .AllowUnversionedUpdateColumns = true,
                .AllowTimestampColumns = allowTimestampColumns,
                .AllowOperationColumns = true,
            });
    } catch (const std::exception& ex) {
        THROW_ERROR_EXCEPTION("Error validating output schema of input query")
            << ex;
    }
}

void TOperationControllerBase::WriteInputQueryToJobSpec(TJobSpecExt* jobSpecExt)
{
    auto* querySpec = jobSpecExt->mutable_input_query_spec();
    ToProto(querySpec->mutable_query(), InputQuery_->Query);
    querySpec->mutable_query()->set_input_row_limit(std::numeric_limits<i64>::max() / 4);
    querySpec->mutable_query()->set_output_row_limit(std::numeric_limits<i64>::max() / 4);
    ToProto(querySpec->mutable_external_functions(), InputQuery_->ExternalCGInfo->Functions);
    NScheduler::NProto::ToProto(querySpec->mutable_options(), InputQuery_->QueryFilterOptions);
}

void TOperationControllerBase::CollectTotals()
{
    // This is the sum across all input chunks not accounting lower/upper read limits.
    TInputStatisticsCollector statisticsCollector;

    THashMap<TClusterName, i64> totalInputDataWeightPerCluster;

    for (const auto& table : InputManager_->GetInputTables()) {
        for (const auto& inputChunk : Concatenate(table->Chunks, table->HunkChunks)) {
            if (inputChunk->IsUnavailable(GetChunkAvailabilityPolicy())) {
                auto chunkId = inputChunk->GetChunkId();

                switch (Spec_->UnavailableChunkStrategy) {
                    case EUnavailableChunkAction::Fail:
                        THROW_ERROR_EXCEPTION("Input chunk %v is unavailable",
                            chunkId);

                    case EUnavailableChunkAction::Skip:
                        YT_LOG_TRACE("Skipping unavailable chunk (ChunkId: %v)",
                            chunkId);
                        continue;

                    case EUnavailableChunkAction::Wait:
                        // Do nothing.
                        break;

                    default:
                        YT_ABORT();
                }
            }

            totalInputDataWeightPerCluster[table->ClusterName] += inputChunk->GetDataWeight();

            statisticsCollector.AddChunk(inputChunk, table->IsPrimary());
        }
    }

    EstimatedInputStatistics_.emplace(std::move(statisticsCollector).Finish());

    YT_LOG_INFO("Estimated input totals collected (EstimatedInputStatistics: %v)", *EstimatedInputStatistics_);

    for (const auto& [clusterName, dataWeight] : totalInputDataWeightPerCluster) {
        if (IsLocal(clusterName)) {
            continue;
        }
        auto clusterConfig = GetOrCrash(Config_->RemoteOperations, clusterName);
        if (clusterConfig->MaxTotalDataWeight && *clusterConfig->MaxTotalDataWeight < dataWeight) {
            THROW_ERROR_EXCEPTION(
                "Total estimated input data weight from cluster %Qv is too large",
                clusterName)
                << TErrorAttribute("estimated_data_weight", dataWeight)
                << TErrorAttribute("max_data_weight", *clusterConfig->MaxTotalDataWeight);
        }
    }
}

bool TOperationControllerBase::HasDiskRequestsWithSpecifiedAccount() const
{
    for (const auto& userJobSpec : GetUserJobSpecs()) {
        if (userJobSpec->DiskRequest && userJobSpec->DiskRequest->Account) {
            return true;
        }
    }
    return false;
}

void TOperationControllerBase::InitAccountResourceUsageLeases()
{
    THashSet<std::string> accounts;

    for (const auto& userJobSpec : GetUserJobSpecs()) {
        if (auto& diskRequest = userJobSpec->DiskRequest) {
            auto mediumDirectory = GetMediumDirectory();
            if (!diskRequest->MediumName) {
                continue;
            }
            auto mediumName = *diskRequest->MediumName;
            auto* mediumDescriptor = mediumDirectory->FindByName(mediumName);
            if (!mediumDescriptor) {
                THROW_ERROR_EXCEPTION("Unknown medium %Qv", mediumName);
            }
            diskRequest->MediumIndex = mediumDescriptor->Index;

            if (Config_->ObligatoryAccountMedia.contains(mediumName)) {
                if (!diskRequest->Account) {
                    THROW_ERROR_EXCEPTION("Account must be specified for disk request with given medium")
                        << TErrorAttribute("medium_name", mediumName);
                }
            }
            if (Config_->DeprecatedMedia.contains(mediumName)) {
                THROW_ERROR_EXCEPTION("Medium is deprecated to be used in disk requests")
                    << TErrorAttribute("medium_name", mediumName);
            }
            if (diskRequest->Account) {
                accounts.insert(*diskRequest->Account);
            }
        }
    }

    EnableMasterResourceUsageAccounting_ = Config_->EnableMasterResourceUsageAccounting;
    if (EnableMasterResourceUsageAccounting_) {
        // TODO(ignat): use batching here.
        for (const auto& account : accounts) {
            try {
                ValidateAccountPermission(account, EPermission::Use);

                auto proxy = CreateObjectServiceWriteProxy(OutputClient_);

                auto req = TMasterYPathProxy::CreateObject();
                SetPrerequisites(req, TPrerequisiteOptions{
                    .PrerequisiteTransactionIds = {InputTransactions_->GetLocalInputTransactionId()},
                });

                req->set_type(ToProto(EObjectType::AccountResourceUsageLease));

                auto attributes = CreateEphemeralAttributes();
                attributes->Set("account", account);
                attributes->Set("transaction_id", InputTransactions_->GetLocalInputTransactionId());
                ToProto(req->mutable_object_attributes(), *attributes);

                auto rsp = WaitFor(proxy.Execute(req))
                    .ValueOrThrow();

                AccountResourceUsageLeaseMap_[account] = {
                    .LeaseId = FromProto<TAccountResourceUsageLeaseId>(rsp->object_id()),
                    .DiskQuota = TDiskQuota(),
                };
            } catch (const std::exception& ex) {
                THROW_ERROR_EXCEPTION("Failed to create account resource usage lease")
                    << TErrorAttribute("account", account)
                    << ex;
            }
        }
    }
}

void TOperationControllerBase::CustomPrepare()
{ }

void TOperationControllerBase::CustomMaterialize()
{ }

void TOperationControllerBase::InferInputRanges()
{
    TPeriodicYielder yielder(PrepareYieldPeriod);

    if (!InputQuery_) {
        return;
    }

    YT_LOG_INFO("Inferring ranges for input tables");

    TQueryOptions queryOptions;
    queryOptions.VerboseLogging = true;
    queryOptions.RangeExpansionLimit = Config_->MaxRangesOnTable;

    for (auto& table : InputManager_->GetInputTables()) {
        yielder.TryYield();

        auto ranges = table->Path.GetNewRanges(table->Comparator, table->Schema->GetKeyColumnTypes());

        // XXX(max42): does this ever happen?
        if (ranges.empty()) {
            continue;
        }

        if (!table->Schema->IsSorted()) {
            continue;
        }

        auto inferredRanges = CreateRangeInferrer(
            InputQuery_->Query->WhereClause,
            table->Schema,
            table->Schema->GetKeyColumns(),
            Host_->GetClient()->GetNativeConnection()->GetColumnEvaluatorCache(),
            GetBuiltinRangeExtractors(),
            queryOptions);


        std::vector<TReadRange> resultRanges;
        for (const auto& range : ranges) {
            yielder.TryYield();

            auto legacyRange = ReadRangeToLegacyReadRange(range);
            auto lowerInitial = legacyRange.LowerLimit().HasLegacyKey()
                ? legacyRange.LowerLimit().GetLegacyKey()
                : MinKey();
            auto upperInitial = legacyRange.UpperLimit().HasLegacyKey()
                ? legacyRange.UpperLimit().GetLegacyKey()
                : MaxKey();

            auto subrange = CropItems(
                TRange(inferredRanges),
                [&] (auto it) {
                    return !(lowerInitial < it->second);
                },
                [&] (auto it) {
                    return it->first < upperInitial;
                });

            if (!subrange.Empty()) {
                auto lower = std::max<TUnversionedRow>(subrange.Front().first, lowerInitial);
                auto upper = std::min<TUnversionedRow>(subrange.Back().second, upperInitial);

                ForEachRange(subrange, TRowRange(lower, upper), [&] (auto item) {
                    auto [lower, upper] = item;

                    auto inferredRange = legacyRange;
                    inferredRange.LowerLimit().SetLegacyKey(TLegacyOwningKey(lower));
                    inferredRange.UpperLimit().SetLegacyKey(TLegacyOwningKey(upper));
                    resultRanges.push_back(ReadRangeFromLegacyReadRange(inferredRange, table->Comparator.GetLength()));
                });
            }
        }
        table->Path.SetRanges(resultRanges);

        YT_LOG_DEBUG("Input table ranges inferred (Path: %v, RangeCount: %v, InferredRangeCount: %v)",
            table->GetPath(),
            ranges.size(),
            resultRanges.size());
    }
}

TError TOperationControllerBase::GetAutoMergeError() const
{
    return TError("Automatic output merge is not supported for %Qlv operations", OperationType_);
}

TError TOperationControllerBase::GetUseChunkSliceStatisticsError() const
{
    return TError("Fetching chunk slice statistics is not supported for %Qlv operations", OperationType_);
}

void TOperationControllerBase::FillPrepareResult(TOperationControllerPrepareResult* result)
{
    result->Attributes = BuildYsonStringFluently<EYsonType::MapFragment>()
        .Do(BIND(&TOperationControllerBase::BuildPrepareAttributes, Unretained(this)))
        .Finish();
}

std::vector<TLegacyDataSlicePtr> TOperationControllerBase::CollectPrimaryVersionedDataSlices(i64 sliceSize)
{
    auto createScraperForFetcher = [&] (const TClusterName& clusterName) -> IFetcherChunkScraperPtr {
        if (Spec_->UnavailableChunkStrategy == EUnavailableChunkAction::Wait) {
            auto scraper = InputManager_->CreateFetcherChunkScraper(clusterName);
            DataSliceFetcherChunkScrapers_.push_back(scraper);
            return scraper;
        } else {
            return IFetcherChunkScraperPtr();
        }
    };

    i64 totalDataWeightBefore = 0;

    std::vector<TFuture<void>> asyncResults;
    std::vector<TComparator> comparators;
    std::vector<IChunkSliceFetcherPtr> fetchers;

    for (const auto& table : InputManager_->GetInputTables()) {
        if (!table->IsForeign() && table->Dynamic && table->Schema->IsSorted()) {
            // NB(arkady-e1ppa): We create scaper in thread_pool
            // but fetcher in cancelable control invoker
            // because the latter protects RowBuffer.
            auto fetcher = CreateChunkSliceFetcher(
                Config_->ChunkSliceFetcher,
                InputManager_->GetNodeDirectory(table->ClusterName),
                GetCancelableInvoker(),
                createScraperForFetcher(table->ClusterName),
                InputManager_->GetClient(table->ClusterName),
                RowBuffer_,
                Logger);

            YT_VERIFY(table->Comparator);

            for (const auto& chunk : table->Chunks) {
                if (chunk->IsUnavailable(GetChunkAvailabilityPolicy()) &&
                    Spec_->UnavailableChunkStrategy == EUnavailableChunkAction::Skip)
                {
                    continue;
                }

                auto chunkSlice = CreateInputChunkSlice(chunk);
                InferLimitsFromBoundaryKeys(chunkSlice, RowBuffer_);
                auto dataSlice = CreateUnversionedInputDataSlice(chunkSlice);
                dataSlice->SetInputStreamIndex(InputStreamDirectory_.GetInputStreamIndex(dataSlice->GetTableIndex(), dataSlice->GetRangeIndex()));
                dataSlice->TransformToNew(RowBuffer_, table->Comparator.GetLength());
                fetcher->AddDataSliceForSlicing(dataSlice, table->Comparator, sliceSize, true, /*minManiacDataWeight*/ std::nullopt);
                totalDataWeightBefore += dataSlice->GetDataWeight();
            }

            fetcher->SetCancelableContext(GetCancelableContext());
            asyncResults.emplace_back(fetcher->Fetch());
            fetchers.emplace_back(std::move(fetcher));
            YT_VERIFY(table->Comparator);
            comparators.push_back(table->Comparator);
        }
    }

    YT_LOG_INFO("Collecting primary versioned data slices");

    WaitFor(AllSucceeded(asyncResults))
        .ThrowOnError();

    i64 totalDataSliceCount = 0;
    i64 totalDataWeightAfter = 0;

    std::vector<TLegacyDataSlicePtr> result;
    for (const auto& [fetcher, comparator] : Zip(fetchers, comparators)) {
        for (const auto& chunkSlice : fetcher->GetChunkSlices()) {
            YT_VERIFY(!chunkSlice->IsLegacy);
        }
        auto dataSlices = CombineVersionedChunkSlices(fetcher->GetChunkSlices(), comparator);
        for (auto& dataSlice : dataSlices) {
            YT_LOG_TRACE("Added dynamic table slice (TablePath: %v, Range: %v..%v, ChunkIds: %v)",
                InputManager_->GetInputTables()[dataSlice->GetTableIndex()]->GetPath(),
                dataSlice->LowerLimit(),
                dataSlice->UpperLimit(),
                dataSlice->ChunkSlices);

            dataSlice->SetInputStreamIndex(InputStreamDirectory_.GetInputStreamIndex(dataSlice->GetTableIndex(), dataSlice->GetRangeIndex()));
            totalDataWeightAfter += dataSlice->GetDataWeight();
            result.emplace_back(std::move(dataSlice));
            ++totalDataSliceCount;
        }
    }

    YT_LOG_INFO(
        "Collected versioned data slices (Count: %v, DataWeight: %v -> %v)",
        totalDataSliceCount,
        totalDataWeightBefore,
        totalDataWeightAfter);

    if (Spec_->AdjustDynamicTableDataSlices) {
        double scaleFactor = totalDataWeightBefore / std::max<double>(1.0, totalDataWeightAfter);
        i64 totalDataWeightAdjusted = 0;
        for (const auto& dataSlice : result) {
            for (const auto& chunkSlice : dataSlice->ChunkSlices) {
                chunkSlice->ApplySamplingSelectivityFactor(scaleFactor);
            }
            totalDataWeightAdjusted += dataSlice->GetDataWeight();
        }
        YT_LOG_INFO("Adjusted dynamic table data slices (AdjutedDataWeight: %v)", totalDataWeightAdjusted);
    }

    DataSliceFetcherChunkScrapers_.clear();

    return result;
}

std::vector<TLegacyDataSlicePtr> TOperationControllerBase::CollectPrimaryInputDataSlices(i64 versionedSliceSize)
{
    std::vector<std::vector<TLegacyDataSlicePtr>> dataSlicesByTableIndex(InputManager_->GetInputTables().size());
    for (const auto& chunk : InputManager_->CollectPrimaryUnversionedChunks()) {
        auto dataSlice = CreateUnversionedInputDataSlice(CreateInputChunkSlice(chunk));
        dataSlice->SetInputStreamIndex(InputStreamDirectory_.GetInputStreamIndex(chunk->GetTableIndex(), chunk->GetRangeIndex()));

        const auto& inputTable = InputManager_->GetInputTables()[dataSlice->GetTableIndex()];
        dataSlice->TransformToNew(RowBuffer_, inputTable->Comparator);

        dataSlicesByTableIndex[dataSlice->GetTableIndex()].emplace_back(std::move(dataSlice));
    }

    for (auto& dataSlice : CollectPrimaryVersionedDataSlices(versionedSliceSize)) {
        dataSlicesByTableIndex[dataSlice->GetTableIndex()].emplace_back(std::move(dataSlice));
    }

    std::vector<TLegacyDataSlicePtr> dataSlices;
    for (auto& tableDataSlices : dataSlicesByTableIndex) {
        std::move(tableDataSlices.begin(), tableDataSlices.end(), std::back_inserter(dataSlices));
    }
    return dataSlices;
}

std::vector<std::deque<TLegacyDataSlicePtr>> TOperationControllerBase::CollectForeignInputDataSlices(int foreignKeyColumnCount) const
{
    std::vector<std::deque<TLegacyDataSlicePtr>> result;
    for (const auto& table : InputManager_->GetInputTables()) {
        if (table->IsForeign()) {
            result.push_back(std::deque<TLegacyDataSlicePtr>());

            if (table->Dynamic && table->Schema->IsSorted()) {
                std::vector<TInputChunkSlicePtr> chunkSlices;
                chunkSlices.reserve(table->Chunks.size());
                YT_VERIFY(table->Comparator);
                for (const auto& chunkSpec : table->Chunks) {
                    auto& chunkSlice = chunkSlices.emplace_back(CreateInputChunkSlice(
                        chunkSpec,
                        RowBuffer_->CaptureRow(chunkSpec->BoundaryKeys()->MinKey.Get()),
                        GetKeySuccessor(chunkSpec->BoundaryKeys()->MaxKey.Get(), RowBuffer_)));

                    chunkSlice->TransformToNew(RowBuffer_, table->Comparator.GetLength());
                }

                YT_VERIFY(table->Comparator);
                auto dataSlices = CombineVersionedChunkSlices(chunkSlices, table->Comparator);
                for (auto& dataSlice : dataSlices) {
                    dataSlice->SetInputStreamIndex(InputStreamDirectory_.GetInputStreamIndex(dataSlice->GetTableIndex(), dataSlice->GetRangeIndex()));

                    if (IsUnavailable(dataSlice, GetChunkAvailabilityPolicy())) {
                        switch (Spec_->UnavailableChunkStrategy) {
                            case EUnavailableChunkAction::Skip:
                                continue;

                            case EUnavailableChunkAction::Wait:
                                // Do nothing.
                                break;

                            default:
                                YT_ABORT();
                        }
                    }
                    result.back().push_back(dataSlice);
                }
            } else {
                for (const auto& inputChunk : table->Chunks) {
                    if (inputChunk->IsUnavailable(GetChunkAvailabilityPolicy())) {
                        switch (Spec_->UnavailableChunkStrategy) {
                            case EUnavailableChunkAction::Skip:
                                continue;

                            case EUnavailableChunkAction::Wait:
                                // Do nothing.
                                break;

                            default:
                                YT_ABORT();
                        }
                    }
                    auto chunkSlice = CreateInputChunkSlice(inputChunk);
                    chunkSlice->TransformToNew(RowBuffer_, table->Comparator.GetLength());
                    auto& dataSlice = result.back().emplace_back(CreateUnversionedInputDataSlice(CreateInputChunkSlice(
                        *chunkSlice,
                        table->Comparator,
                        TKeyBound::FromRow() >= GetKeyPrefix(inputChunk->BoundaryKeys()->MinKey.Get(), foreignKeyColumnCount, RowBuffer_),
                        TKeyBound::FromRow() <= GetKeyPrefix(inputChunk->BoundaryKeys()->MaxKey.Get(), foreignKeyColumnCount, RowBuffer_))));
                    dataSlice->SetInputStreamIndex(InputStreamDirectory_.GetInputStreamIndex(dataSlice->GetTableIndex(), dataSlice->GetRangeIndex()));

                    YT_VERIFY(table->Comparator);
                }
            }
        }
    }
    return result;
}

bool TOperationControllerBase::InputHasVersionedTables() const
{
    for (const auto& table : InputManager_->GetInputTables()) {
        if (table->Dynamic && table->Schema->IsSorted()) {
            return true;
        }
    }
    return false;
}

bool TOperationControllerBase::InputHasReadLimits() const
{
    for (const auto& table : InputManager_->GetInputTables()) {
        if (table->Path.HasNontrivialRanges()) {
            return true;
        }
    }
    return false;
}

bool TOperationControllerBase::InputHasDynamicStores() const
{
    for (const auto& table : InputManager_->GetInputTables()) {
        if (table->Dynamic) {
            for (const auto& chunk : table->Chunks) {
                if (chunk->IsDynamicStore()) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool TOperationControllerBase::IsLocalityEnabled() const
{
    YT_VERIFY(EstimatedInputStatistics_);
    return Config_->EnableLocality && EstimatedInputStatistics_->DataWeight > Spec_->MinLocalityInputDataWeight;
}

TString TOperationControllerBase::GetLoggingProgress() const
{
    const auto& jobCounter = GetTotalJobCounter();
    return Format(
        "Jobs = {T: %v, R: %v, C: %v, P: %v, F: %v, A: %v, I: %v}, "
        "UnavailableInputChunks: %v",
        jobCounter->GetTotal(),
        jobCounter->GetRunning(),
        jobCounter->GetCompletedTotal(),
        GetPendingJobCount(),
        jobCounter->GetFailed(),
        jobCounter->GetAbortedTotal(),
        jobCounter->GetInterruptedTotal(),
        GetUnavailableInputChunkCount());
}

void TOperationControllerBase::ExtractInterruptDescriptor(TCompletedJobSummary& jobSummary, const TJobletPtr& joblet) const
{
    std::vector<TLegacyDataSlicePtr> dataSliceList;

    const auto& jobResultExt = jobSummary.GetJobResultExt();

    std::vector<TDataSliceDescriptor> unreadDataSliceDescriptors;
    std::vector<TDataSliceDescriptor> readDataSliceDescriptors;
    if (jobResultExt.unread_chunk_specs_size() > 0) {
        FromProto(
            &unreadDataSliceDescriptors,
            jobResultExt.unread_chunk_specs(),
            jobResultExt.chunk_spec_count_per_unread_data_slice(),
            jobResultExt.virtual_row_index_per_unread_data_slice());
    }
    if (jobResultExt.read_chunk_specs_size() > 0) {
        FromProto(
            &readDataSliceDescriptors,
            jobResultExt.read_chunk_specs(),
            jobResultExt.chunk_spec_count_per_read_data_slice(),
            jobResultExt.virtual_row_index_per_read_data_slice());
    }

    auto extractDataSlice = [&] (const TDataSliceDescriptor& dataSliceDescriptor) {
        std::vector<TInputChunkSlicePtr> chunkSliceList;
        chunkSliceList.reserve(dataSliceDescriptor.ChunkSpecs.size());

        // TODO(gritukan): One day we will do interrupts in non-input tasks.
        TComparator comparator;
        if (joblet->Task->GetIsInput()) {
            comparator = GetInputTable(dataSliceDescriptor.GetDataSourceIndex())->Comparator;
        }

        bool dynamic = InputManager_->GetInputTables()[dataSliceDescriptor.GetDataSourceIndex()]->Dynamic;
        for (const auto& protoChunkSpec : dataSliceDescriptor.ChunkSpecs) {
            auto chunkId = FromProto<TChunkId>(protoChunkSpec.chunk_id());
            auto chunkSlice = New<TInputChunkSlice>(
                InputManager_->GetInputChunk(chunkId, protoChunkSpec.chunk_index()),
                RowBuffer_,
                protoChunkSpec);
            // NB: Dynamic tables use legacy slices for now, so we do not convert dynamic table
            // slices into new.
            if (!dynamic) {
                if (comparator) {
                    chunkSlice->TransformToNew(RowBuffer_, comparator.GetLength());
                    InferLimitsFromBoundaryKeys(chunkSlice, RowBuffer_, std::nullopt, comparator);
                } else {
                    chunkSlice->TransformToNewKeyless();
                }
            }
            chunkSliceList.emplace_back(std::move(chunkSlice));
        }
        TLegacyDataSlicePtr dataSlice;
        if (dynamic) {
            dataSlice = CreateVersionedInputDataSlice(chunkSliceList);
            if (comparator) {
                dataSlice->TransformToNew(RowBuffer_, comparator.GetLength());
            } else {
                dataSlice->TransformToNewKeyless();
            }
        } else {
            YT_VERIFY(chunkSliceList.size() == 1);
            dataSlice = CreateUnversionedInputDataSlice(chunkSliceList[0]);
        }

        YT_VERIFY(!dataSlice->IsLegacy);
        if (comparator) {
            InferLimitsFromBoundaryKeys(dataSlice, RowBuffer_, comparator);
        }

        dataSlice->SetInputStreamIndex(dataSlice->GetTableIndex());
        dataSlice->Tag = dataSliceDescriptor.GetTag();
        return dataSlice;
    };

    for (const auto& dataSliceDescriptor : unreadDataSliceDescriptors) {
        jobSummary.UnreadInputDataSlices.emplace_back(extractDataSlice(dataSliceDescriptor));
    }
    for (const auto& dataSliceDescriptor : readDataSliceDescriptors) {
        jobSummary.ReadInputDataSlices.emplace_back(extractDataSlice(dataSliceDescriptor));
    }
}

TSortColumns TOperationControllerBase::CheckInputTablesSorted(
    const TSortColumns& sortColumns,
    std::function<bool(const TInputTablePtr& table)> inputTableFilter)
{
    YT_VERIFY(!InputManager_->GetInputTables().empty());

    for (const auto& table : InputManager_->GetInputTables()) {
        if (inputTableFilter(table) && !table->Schema->IsSorted()) {
            THROW_ERROR_EXCEPTION("Input table %v is not sorted",
                table->GetPath());
        }
    }

    auto validateColumnFilter = [] (const TInputTablePtr& table, const TSortColumns& sortColumns) {
        auto columns = table->Path.GetColumns();
        if (!columns) {
            return;
        }

        auto columnSet = THashSet<std::string>(columns->begin(), columns->end());
        for (const auto& sortColumn : sortColumns) {
            if (columnSet.find(sortColumn.Name) == columnSet.end()) {
                THROW_ERROR_EXCEPTION("Column filter for input table %v must include key column %Qv",
                    table->GetPath(),
                    sortColumn.Name);
            }
        }
    };

    if (!sortColumns.empty()) {
        for (const auto& table : InputManager_->GetInputTables()) {
            if (!inputTableFilter(table)) {
                continue;
            }

            if (!CheckSortColumnsCompatible(table->Schema->GetSortColumns(), sortColumns)) {
                THROW_ERROR_EXCEPTION("Input table %v is sorted by columns %v that are not compatible "
                    "with the requested columns %v",
                    table->GetPath(),
                    table->Schema->GetSortColumns(),
                    sortColumns);
            }
            validateColumnFilter(table, sortColumns);
        }
        return sortColumns;
    } else {
        for (const auto& referenceTable : InputManager_->GetInputTables()) {
            if (inputTableFilter(referenceTable)) {
                for (const auto& table : InputManager_->GetInputTables()) {
                    if (!inputTableFilter(table)) {
                        continue;
                    }

                    if (table->Schema->GetSortColumns() != referenceTable->Schema->GetSortColumns()) {
                        THROW_ERROR_EXCEPTION("Sort columns do not match: input table %v is sorted by columns %v "
                            "while input table %v is sorted by columns %v",
                            table->GetPath(),
                            table->Schema->GetSortColumns(),
                            referenceTable->GetPath(),
                            referenceTable->Schema->GetSortColumns());
                    }
                    validateColumnFilter(table, referenceTable->Schema->GetSortColumns());
                }
                return referenceTable->Schema->GetSortColumns();
            }
        }
    }
    YT_ABORT();
}

bool TOperationControllerBase::CheckKeyColumnsCompatible(
    const TKeyColumns& fullColumns,
    const TKeyColumns& prefixColumns)
{
    if (fullColumns.size() < prefixColumns.size()) {
        return false;
    }

    return std::equal(prefixColumns.begin(), prefixColumns.end(), fullColumns.begin());
}

bool TOperationControllerBase::CheckSortColumnsCompatible(
    const TSortColumns& fullColumns,
    const TSortColumns& prefixColumns)
{
    if (fullColumns.size() < prefixColumns.size()) {
        return false;
    }

    return std::equal(prefixColumns.begin(), prefixColumns.end(), fullColumns.begin());
}

bool TOperationControllerBase::ShouldVerifySortedOutput() const
{
    return true;
}


bool TOperationControllerBase::IsOrderedOutputRequired() const
{
    return false;
}

std::vector<TChunkTreeId> TOperationControllerBase::GetOutputChunkTreesInOrder(const TOutputTablePtr& /*table*/) const
{
    YT_UNIMPLEMENTED();
}

EChunkAvailabilityPolicy TOperationControllerBase::GetChunkAvailabilityPolicy() const
{
    return Spec_->ChunkAvailabilityPolicy;
}

bool TOperationControllerBase::IsBoundaryKeysFetchEnabled() const
{
    return false;
}

void TOperationControllerBase::RegisterLivePreviewTable(TString name, const TOutputTablePtr& table)
{
    // COMPAT(galtsev)
    if (name.empty()) {
        return;
    }

    if (table->Dynamic) {
        return;
    }

    auto schema = table->TableUploadOptions.TableSchema.Get();
    LivePreviews_->emplace(
        name,
        New<TLivePreview>(std::move(schema), OutputNodeDirectory_, Logger, OperationId_, name, table->Path.GetPath()));
    table->LivePreviewTableName = std::move(name);
}

void TOperationControllerBase::AttachToIntermediateLivePreview(TInputChunkPtr chunk)
{
    if (IsLegacyIntermediateLivePreviewSupported()) {
        AttachToLivePreview(chunk->GetChunkId(), IntermediateTable_->LivePreviewTableId);
    }
    AttachToLivePreview(IntermediateTable_->LivePreviewTableName, chunk);
}

void TOperationControllerBase::AttachToLivePreview(
    TChunkTreeId chunkTreeId,
    NCypressClient::TNodeId tableId)
{
    YT_UNUSED_FUTURE(Host_->AttachChunkTreesToLivePreview(
        AsyncTransaction_->GetId(),
        tableId,
        {chunkTreeId}));
}

void TOperationControllerBase::AttachToLivePreview(
    TStringBuf tableName,
    TInputChunkPtr chunk)
{
    // COMPAT(galtsev)
    if (tableName.empty()) {
        return;
    }

    if (LivePreviews_->contains(tableName)) {
        auto result = (*LivePreviews_)[tableName]->TryInsertChunk(chunk);
        if (!result.IsOK()) {
            static constexpr auto message = "Error registering a chunk in a live preview";
            if (Config_->FailOperationOnErrorsInLivePreview) {
                THROW_ERROR_EXCEPTION(message)
                    << TErrorAttribute("table_name", tableName)
                    << TErrorAttribute("chunk_id", chunk->GetChunkId());
            } else {
                YT_LOG_WARNING(result, "%v (TableName: %v, Chunk: %v)",
                    message,
                    tableName,
                    chunk);
            }
        }
    }
}

void TOperationControllerBase::AttachToLivePreview(
    TStringBuf tableName,
    const TChunkStripePtr& stripe)
{
    for (const auto& dataSlice : stripe->DataSlices) {
        for (const auto& chunkSlice : dataSlice->ChunkSlices) {
            AttachToLivePreview(tableName, chunkSlice->GetInputChunk());
        }
    }
}

void TOperationControllerBase::RegisterStderr(const TJobletPtr& joblet, const TJobSummary& jobSummary)
{
    if (!joblet->StderrTableChunkListId) {
        return;
    }

    YT_VERIFY(StderrTable_);

    const auto& chunkListId = joblet->StderrTableChunkListId;

    const auto& jobResultExt = jobSummary.GetJobResultExt();

    YT_VERIFY(jobResultExt.has_stderr_result());

    const auto& stderrResult = jobResultExt.stderr_result();
    if (stderrResult.empty()) {
        return;
    }
    auto key = BuildBoundaryKeysFromOutputResult(stderrResult, StderrTable_->GetStreamDescriptorTemplate(), RowBuffer_);
    if (!key.MinKey || key.MinKey.GetLength() == 0 || !key.MaxKey || key.MaxKey.GetLength() == 0) {
        YT_LOG_DEBUG("Dropping empty stderr chunk tree (JobId: %v, NodeAddress: %v, ChunkListId: %v)",
            joblet->JobId,
            NNodeTrackerClient::GetDefaultAddress(joblet->NodeDescriptor.Addresses),
            chunkListId);
        return;
    }

    for (const auto& chunkSpec : stderrResult.chunk_specs()) {
        RegisterOutputChunkReplicas(jobSummary, chunkSpec);

        auto chunk = New<TInputChunk>(chunkSpec);
        chunk->BoundaryKeys() = std::make_unique<TOwningBoundaryKeys>(TOwningBoundaryKeys{
            .MinKey = FromProto<TLegacyOwningKey>(stderrResult.min()),
            .MaxKey = FromProto<TLegacyOwningKey>(stderrResult.max()),
        });
        AttachToLivePreview(StderrTable_->LivePreviewTableName, chunk);
        RegisterLivePreviewChunk(TDataFlowGraph::StderrDescriptor, /*index*/ 0, std::move(chunk));
    }

    StderrTable_->OutputChunkTreeIds.emplace_back(key, chunkListId);

    YT_LOG_DEBUG("Stderr chunk tree registered (JobId: %v, NodeAddress: %v, ChunkListId: %v, ChunkCount: %v)",
        joblet->JobId,
        NNodeTrackerClient::GetDefaultAddress(joblet->NodeDescriptor.Addresses),
        chunkListId,
        stderrResult.chunk_specs().size());
}

void TOperationControllerBase::RegisterCores(const TJobletPtr& joblet, const TJobSummary& jobSummary)
{
    if (!joblet->CoreTableChunkListId) {
        return;
    }

    YT_VERIFY(CoreTable_);

    const auto& chunkListId = joblet->CoreTableChunkListId;

    const auto jobResultExt = jobSummary.FindJobResultExt();
    if (!jobResultExt) {
        return;
    }

    for (const auto& coreInfo : jobResultExt->core_infos()) {
        YT_LOG_DEBUG("Core file (JobId: %v, ProcessId: %v, ExecutableName: %v, Size: %v, Error: %v, Cuda: %v)",
            joblet->JobId,
            coreInfo.process_id(),
            coreInfo.executable_name(),
            coreInfo.size(),
            coreInfo.has_error() ? FromProto<TError>(coreInfo.error()) : TError(),
            coreInfo.cuda());
    }

    if (!jobResultExt->has_core_result()) {
        return;
    }
    const auto& coreResult = jobResultExt->core_result();
    if (coreResult.empty()) {
        return;
    }
    auto key = BuildBoundaryKeysFromOutputResult(coreResult, CoreTable_->GetStreamDescriptorTemplate(), RowBuffer_);
    if (!key.MinKey || key.MinKey.GetLength() == 0 || !key.MaxKey || key.MaxKey.GetLength() == 0) {
        YT_LOG_DEBUG("Dropping empty core chunk tree (JobId: %v, NodeAddress: %v, ChunkListId: %v)",
            joblet->JobId,
            NNodeTrackerClient::GetDefaultAddress(joblet->NodeDescriptor.Addresses),
            chunkListId);
        return;
    }
    CoreTable_->OutputChunkTreeIds.emplace_back(key, chunkListId);

    for (const auto& chunkSpec : coreResult.chunk_specs()) {
        RegisterOutputChunkReplicas(jobSummary, chunkSpec);

        auto chunk = New<TInputChunk>(chunkSpec);
        chunk->BoundaryKeys() = std::make_unique<TOwningBoundaryKeys>(TOwningBoundaryKeys{
            .MinKey = FromProto<TLegacyOwningKey>(coreResult.min()),
            .MaxKey = FromProto<TLegacyOwningKey>(coreResult.max()),
        });
        AttachToLivePreview(CoreTable_->LivePreviewTableName, chunk);
        RegisterLivePreviewChunk(TDataFlowGraph::CoreDescriptor, /*index*/ 0, std::move(chunk));
    }

    YT_LOG_DEBUG("Core chunk tree registered (JobId: %v, NodeAddress: %v, ChunkListId: %v, ChunkCount: %v)",
        joblet->JobId,
        NNodeTrackerClient::GetDefaultAddress(joblet->NodeDescriptor.Addresses),
        chunkListId,
        coreResult.chunk_specs().size());
}

const ITransactionPtr TOperationControllerBase::GetTransactionForOutputTable(const TOutputTablePtr& table) const
{
    if (table->OutputType == EOutputTableType::Output) {
        if (OutputCompletionTransaction_) {
            return OutputCompletionTransaction_;
        } else {
            return OutputTransaction_;
        }
    } else {
        YT_VERIFY(table->OutputType == EOutputTableType::Stderr || table->OutputType == EOutputTableType::Core);
        if (DebugCompletionTransaction_) {
            return DebugCompletionTransaction_;
        } else {
            return DebugTransaction_;
        }
    }
}

void TOperationControllerBase::RegisterTeleportChunk(
    TInputChunkPtr chunk,
    TChunkStripeKey key,
    int tableIndex)
{
    auto& table = OutputTables_[tableIndex];

    if (table->TableUploadOptions.TableSchema->IsSorted() && ShouldVerifySortedOutput()) {
        YT_VERIFY(chunk->BoundaryKeys());
        YT_VERIFY(chunk->GetRowCount() > 0);
        YT_VERIFY(chunk->GetUniqueKeys() || !table->TableWriterOptions->ValidateUniqueKeys);

        NControllerAgent::NProto::TOutputResult resultBoundaryKeys;
        resultBoundaryKeys.set_empty(false);
        resultBoundaryKeys.set_sorted(true);
        resultBoundaryKeys.set_unique_keys(chunk->GetUniqueKeys());
        ToProto(resultBoundaryKeys.mutable_min(), chunk->BoundaryKeys()->MinKey);
        ToProto(resultBoundaryKeys.mutable_max(), chunk->BoundaryKeys()->MaxKey);

        key = TChunkStripeKey(
            BuildBoundaryKeysFromOutputResult(
                resultBoundaryKeys,
                StandardStreamDescriptors_[tableIndex],
                RowBuffer_));
    }

    table->OutputChunkTreeIds.emplace_back(key, chunk->GetChunkId());

    if (table->Dynamic) {
        table->OutputChunks.push_back(chunk);
    }

    if (IsLegacyOutputLivePreviewSupported()) {
        AttachToLivePreview(chunk->GetChunkId(), table->LivePreviewTableId);
    }
    if (GetOutputLivePreviewVertexDescriptor() == TDataFlowGraph::SinkDescriptor) {
        AttachToLivePreview(table->LivePreviewTableName, chunk);
    }

    RegisterOutputRows(chunk->GetRowCount(), tableIndex);

    TeleportedOutputRowCount_ += chunk->GetRowCount();

    YT_LOG_DEBUG("Teleport chunk registered (Table: %v, ChunkId: %v, Key: %v)",
        tableIndex,
        chunk->GetChunkId(),
        key);
}

void TOperationControllerBase::RegisterRecoveryInfo(
    const TCompletedJobPtr& completedJob,
    const TChunkStripePtr& stripe)
{
    for (const auto& dataSlice : stripe->DataSlices) {
        // NB: Intermediate slice must be trivial.
        auto chunkId = dataSlice->GetSingleUnversionedChunk()->GetChunkId();
        YT_VERIFY(ChunkOriginMap_.emplace(chunkId, completedJob).second);
    }

    IntermediateChunkScraper_->Restart();
}

TRowBufferPtr TOperationControllerBase::GetRowBuffer()
{
    return RowBuffer_;
}

TSnapshotCookie TOperationControllerBase::OnSnapshotStarted()
{
    YT_ASSERT_INVOKER_AFFINITY(InvokerPool_->GetInvoker(EOperationControllerQueue::Default));

    int snapshotIndex = SnapshotIndex_++;
    if (RecentSnapshotIndex_) {
        YT_LOG_WARNING("Starting next snapshot without completing previous one (SnapshotIndex: %v)",
            snapshotIndex);
    }
    RecentSnapshotIndex_ = snapshotIndex;

    CompletedJobIdsSnapshotCookie_ = CompletedJobIdsReleaseQueue_.Checkpoint();
    IntermediateStripeListSnapshotCookie_ = IntermediateStripeListReleaseQueue_.Checkpoint();
    ChunkTreeSnapshotCookie_ = ChunkTreeReleaseQueue_.Checkpoint();
    YT_LOG_INFO("Storing snapshot cookies (CompletedJobIdsSnapshotCookie: %v, StripeListSnapshotCookie: %v, "
        "ChunkTreeSnapshotCookie: %v, SnapshotIndex: %v)",
        CompletedJobIdsSnapshotCookie_,
        IntermediateStripeListSnapshotCookie_,
        ChunkTreeSnapshotCookie_,
        *RecentSnapshotIndex_);

    TSnapshotCookie result;
    result.SnapshotIndex = *RecentSnapshotIndex_;
    return result;
}

void TOperationControllerBase::SafeOnSnapshotCompleted(const TSnapshotCookie& cookie)
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker());

    // OnSnapshotCompleted should match the most recent OnSnapshotStarted.
    YT_VERIFY(RecentSnapshotIndex_);
    YT_VERIFY(cookie.SnapshotIndex == *RecentSnapshotIndex_);

    // Completed job ids.
    {
        auto headCookie = CompletedJobIdsReleaseQueue_.GetHeadCookie();
        auto jobIdsToRelease = CompletedJobIdsReleaseQueue_.Release(CompletedJobIdsSnapshotCookie_);
        YT_LOG_INFO("Releasing jobs on snapshot completion (SnapshotCookie: %v, HeadCookie: %v, JobCount: %v, SnapshotIndex: %v)",
            CompletedJobIdsSnapshotCookie_,
            headCookie,
            jobIdsToRelease.size(),
            cookie.SnapshotIndex);
        ReleaseJobs(jobIdsToRelease);
    }

    // Stripe lists.
    {
        auto headCookie = IntermediateStripeListReleaseQueue_.GetHeadCookie();
        auto stripeListsToRelease = IntermediateStripeListReleaseQueue_.Release(IntermediateStripeListSnapshotCookie_);
        YT_LOG_INFO("Releasing stripe lists (SnapshotCookie: %v, HeadCookie: %v, StripeListCount: %v, SnapshotIndex: %v)",
            IntermediateStripeListSnapshotCookie_,
            headCookie,
            stripeListsToRelease.size(),
            cookie.SnapshotIndex);

        for (const auto& stripeList : stripeListsToRelease) {
            auto chunks = GetStripeListChunks(stripeList);
            AddChunksToUnstageList(std::move(chunks));
            OnChunksReleased(stripeList->TotalChunkCount);
        }
    }

    // Chunk trees.
    {
        auto headCookie = ChunkTreeReleaseQueue_.GetHeadCookie();
        auto chunkTreeIdsToRelease = ChunkTreeReleaseQueue_.Release(ChunkTreeSnapshotCookie_);
        YT_LOG_INFO("Releasing chunk trees (SnapshotCookie: %v, HeadCookie: %v, ChunkTreeCount: %v, SnapshotIndex: %v)",
            ChunkTreeSnapshotCookie_,
            headCookie,
            chunkTreeIdsToRelease.size(),
            cookie.SnapshotIndex);

        Host_->AddChunkTreesToUnstageList(chunkTreeIdsToRelease, /*recursive*/ true);
    }

    RecentSnapshotIndex_.reset();
    LastSuccessfulSnapshotTime_ = TInstant::Now();
}

bool TOperationControllerBase::HasSnapshot() const
{
    return SnapshotIndex_.load();
}

void TOperationControllerBase::Dispose()
{
    YT_ASSERT_INVOKER_AFFINITY(InvokerPool_->GetInvoker(EOperationControllerQueue::Default));

    YT_VERIFY(IsFinished());

    YT_VERIFY(RunningJobCount_ == 0);

    // Check that all jobs released.
    YT_VERIFY(CompletedJobIdsReleaseQueue_.Checkpoint() == CompletedJobIdsReleaseQueue_.GetHeadCookie());
    {
        auto guard = Guard(JobMetricsDeltaPerTreeLock_);

        for (auto& [treeId, metrics] : JobMetricsDeltaPerTree_) {
            auto totalTime = GetOrCrash(TotalTimePerTree_, treeId);
            auto mainResourceConsumption = GetOrCrash(MainResourceConsumptionPerTree_, treeId);

            switch (State_) {
                case EControllerState::Completed:
                    metrics.Values()[EJobMetricName::TotalTimeOperationCompleted] = totalTime;
                    metrics.Values()[EJobMetricName::MainResourceConsumptionOperationCompleted] = mainResourceConsumption;
                    break;

                case EControllerState::Aborted:
                    metrics.Values()[EJobMetricName::TotalTimeOperationAborted] = totalTime;
                    metrics.Values()[EJobMetricName::MainResourceConsumptionOperationAborted] = mainResourceConsumption;
                    break;

                case EControllerState::Failed:
                    metrics.Values()[EJobMetricName::TotalTimeOperationFailed] = totalTime;
                    metrics.Values()[EJobMetricName::MainResourceConsumptionOperationFailed] = mainResourceConsumption;
                    break;

                default:
                    YT_ABORT();
            }
        }

        YT_LOG_DEBUG(
            "Adding total time per tree to residual job metrics on controller disposal (FinalState: %v, TotalTimePerTree: %v)",
            State_.load(),
            TotalTimePerTree_);
    }
}

void TOperationControllerBase::UpdateRuntimeParameters(const TOperationRuntimeParametersUpdatePtr& update)
{
    if (update->Acl) {
        Acl_ = *update->Acl;
    } else if (update->AcoName) {
        AcoName_ = *update->AcoName;
    }
    if (update->SchedulingTagFilter) {
        Spec_->SchedulingTagFilter = *update->SchedulingTagFilter;
        UpdateExecNodes();
    }
}

TOperationSpecBaseSealedConfigurator TOperationControllerBase::ConfigureUpdate()
{
    auto configurator = GetOperationSpecBaseConfigurator();
    configurator.Field("max_failed_job_count", &TOperationSpecBase::MaxFailedJobCount)
        .Updater(BIND_NO_PROPAGATE([&] (int newMaxFailedJobCount) {
            if (FailedJobCount_ >= newMaxFailedJobCount) {
                OnOperationFailed(GetMaxFailedJobCountReachedError(newMaxFailedJobCount));
            }
        }));

    return std::move(configurator).Seal();
}

void TOperationControllerBase::PatchSpec(INodePtr newCumulativeSpecPatch, bool dryRun)
{
    if (dryRun) {
        THROW_ERROR_EXCEPTION_UNLESS(
            State_.load() == EControllerState::Running,
            "Operation is not running");
        SpecManager_->ValidateSpecPatch(std::move(newCumulativeSpecPatch));
    } else {
        switch (State_) {
            case EControllerState::Preparing:
                YT_ABORT();
            case EControllerState::Running:
                // Ok.
                break;
            case EControllerState::Failing:
            case EControllerState::Completed:
            case EControllerState::Failed:
            case EControllerState::Aborted: {
                YT_LOG_WARNING(
                    "Controller changed state before applying spec patch (ControllerState: %v)",
                    State_.load());
                return;
            }
        }
        SpecManager_->ApplySpecPatch(std::move(newCumulativeSpecPatch));
    }
}

std::any TOperationControllerBase::CreateSafeAssertionGuard() const
{
    return NYT::CreateSafeAssertionGuard(
        Host_->GetCoreDumper(),
        Host_->GetCoreSemaphore(),
        CoreNotes_);
}

TOperationJobMetrics TOperationControllerBase::PullJobMetricsDelta(bool force)
{
    auto guard = Guard(JobMetricsDeltaPerTreeLock_);

    auto now = NProfiling::GetCpuInstant();
    if (!force && LastJobMetricsDeltaReportTime_ + DurationToCpuDuration(Config_->JobMetricsReportPeriod) > now) {
        return {};
    }

    TOperationJobMetrics result;
    for (auto& [treeId, delta] : JobMetricsDeltaPerTree_) {
        if (!delta.IsEmpty()) {
            result.push_back({treeId, delta});
            delta = TJobMetrics();
        }
    }
    LastJobMetricsDeltaReportTime_ = now;

    YT_LOG_DEBUG_UNLESS(result.empty(), "Non-zero job metrics reported");

    return result;
}

TOperationAlertMap TOperationControllerBase::GetAlerts()
{
    auto guard = Guard(AlertsLock_);
    return Alerts_;
}

TOperationInfo TOperationControllerBase::BuildOperationInfo()
{
    // NB: BuildOperationInfo called by GetOperationInfo RPC method, that used scheduler at operation finalization.
    BuildAndSaveProgress();

    TOperationInfo result;

    result.Progress =
        BuildYsonStringFluently<EYsonType::MapFragment>()
            .Do(std::bind(&TOperationControllerBase::BuildProgress, this, _1))
        .Finish();

    result.BriefProgress =
        BuildYsonStringFluently<EYsonType::MapFragment>()
            .Do(std::bind(&TOperationControllerBase::BuildBriefProgress, this, _1))
        .Finish();

    result.Alerts =
        BuildYsonStringFluently<EYsonType::MapFragment>()
            .DoFor(GetAlerts(), [&] (TFluentMap fluent, const auto& pair) {
                auto alertType = pair.first;
                const auto& error = pair.second;
                if (!error.IsOK()) {
                    fluent
                        .Item(FormatEnum(alertType)).Value(error);
                }
            })
        .Finish();

    result.RunningJobs =
        BuildYsonStringFluently<EYsonType::MapFragment>()
            .Do(std::bind(&TOperationControllerBase::BuildJobsYson, this, _1))
        .Finish();

    result.MemoryUsage = GetMemoryUsage();

    result.ControllerState = State_;

    return result;
}

i64 TOperationControllerBase::GetMemoryUsage() const
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    const auto snapshot = GetGlobalMemoryUsageSnapshot();
    YT_VERIFY(snapshot);

    return snapshot->GetUsage(OperationIdTag, ToString(OperationId_));
}

bool TOperationControllerBase::HasEnoughChunkLists(bool isWritingStderrTable, bool isWritingCoreTable)
{
    // We use this "result" variable to make sure that we have enough chunk lists
    // for every cell tag and start allocating them all in advance and simultaneously.
    bool result = true;
    for (auto [cellTag, count] : CellTagToRequiredOutputChunkListCount_) {
        if (count > 0 && !OutputChunkListPool_->HasEnough(cellTag, count)) {
            result = false;
        }
    }
    for (auto [cellTag, count] : CellTagToRequiredDebugChunkListCount_) {
        if (StderrTable_ && !isWritingStderrTable && StderrTable_->ExternalCellTag == cellTag) {
            --count;
        }
        if (CoreTable_ && !isWritingCoreTable && CoreTable_->ExternalCellTag == cellTag) {
            --count;
        }
        YT_VERIFY(DebugChunkListPool_);
        if (count > 0 && !DebugChunkListPool_->HasEnough(cellTag, count)) {
            result = false;
        }
    }
    return result;
}

TChunkListId TOperationControllerBase::ExtractOutputChunkList(TCellTag cellTag)
{
    return OutputChunkListPool_->Extract(cellTag);
}

TChunkListId TOperationControllerBase::ExtractDebugChunkList(TCellTag cellTag)
{
    YT_VERIFY(DebugChunkListPool_);
    return DebugChunkListPool_->Extract(cellTag);
}

void TOperationControllerBase::ReleaseChunkTrees(
    const std::vector<TChunkListId>& chunkTreeIds,
    bool unstageRecursively,
    bool waitForSnapshot)
{
    if (waitForSnapshot) {
        YT_VERIFY(unstageRecursively);
        for (const auto& chunkTreeId : chunkTreeIds) {
            ChunkTreeReleaseQueue_.Push(chunkTreeId);
        }
    } else {
        Host_->AddChunkTreesToUnstageList(chunkTreeIds, unstageRecursively);
    }
}

void TOperationControllerBase::RegisterJoblet(const TJobletPtr& joblet)
{
    auto allocationId = AllocationIdFromJobId(joblet->JobId);

    auto& allocation = GetOrCrash(AllocationMap_, allocationId);
    YT_VERIFY(!allocation.Joblet);
    YT_VERIFY(joblet);

    allocation.Joblet = joblet;

    ++RunningJobCount_;
}

void TOperationControllerBase::UnregisterJoblet(const TJobletPtr& joblet)
{
    ReleaseMonitoringDescriptor(joblet);

    auto& allocation = GetOrCrash(AllocationMap_, AllocationIdFromJobId(joblet->JobId));
    YT_VERIFY(joblet == allocation.Joblet);
    allocation.Joblet.Reset();

    --RunningJobCount_;
}

TAllocation* TOperationControllerBase::FindAllocation(TAllocationId allocationId)
{
    auto allocationIt = AllocationMap_.find(allocationId);
    return allocationIt == end(AllocationMap_) ? nullptr : &allocationIt->second;
}

TJobletPtr TOperationControllerBase::FindJoblet(TAllocationId allocationId) const
{
    auto it = AllocationMap_.find(allocationId);
    return it == end(AllocationMap_) ? nullptr : it->second.Joblet;
}

TJobletPtr TOperationControllerBase::FindJoblet(TJobId jobId) const
{
    auto joblet = FindJoblet(AllocationIdFromJobId(jobId));
    if (joblet && joblet->JobId != jobId) {
        return nullptr;
    }

    return joblet;
}

TJobletPtr TOperationControllerBase::GetJoblet(TJobId jobId) const
{
    auto joblet = GetJoblet(AllocationIdFromJobId(jobId));

    YT_VERIFY(joblet->JobId == jobId);

    return joblet;
}

TJobletPtr TOperationControllerBase::GetJoblet(TAllocationId allocationId) const
{
    const auto& allocation = GetOrCrash(AllocationMap_, allocationId);
    YT_VERIFY(allocation.Joblet);

    return allocation.Joblet;
}

TJobletPtr TOperationControllerBase::GetJobletOrThrow(TJobId jobId) const
{
    auto joblet = FindJoblet(jobId);
    if (!joblet) {
        THROW_ERROR_EXCEPTION(
            NControllerAgent::EErrorCode::NoSuchJob,
            "No such job %v",
            jobId);
    }
    return joblet;
}

std::optional<TJobMonitoringDescriptor> TOperationControllerBase::AcquireMonitoringDescriptorForJob(
    TJobId jobId,
    const TAllocation& /*allocation*/)
{
    YT_LOG_DEBUG(
        "Trying to assign monitoring descriptor to job (JobId: %v)",
        jobId);

    std::optional<TJobMonitoringDescriptor> descriptor;

    if (MonitoringDescriptorPool_.empty()) {
        descriptor = RegisterNewMonitoringDescriptor();
    } else {
        auto it = MonitoringDescriptorPool_.begin();
        auto foundDescriptor = *it;
        MonitoringDescriptorPool_.erase(it);
        descriptor = foundDescriptor;
    }

    if (descriptor) {
        YT_LOG_DEBUG(
            "Monitoring descriptor assigned to job (JobId: %v, MonitoringDescriptor: %v)",
            jobId,
            descriptor);

        ++MonitoredUserJobCount_;
    } else {
        YT_LOG_DEBUG("Failed to assign monitoring descriptor to job (JobId: %v)", jobId);
    }
    return descriptor;
}

std::optional<TJobMonitoringDescriptor> TOperationControllerBase::RegisterNewMonitoringDescriptor()
{
    ++MonitoredUserJobAttemptCount_;
    if (MonitoredUserJobCount_ >= Config_->UserJobMonitoring->ExtendedMaxMonitoredUserJobsPerOperation) {
        SetOperationAlert(
            EOperationAlertType::UserJobMonitoringLimited,
            TError("Limit of monitored user jobs per operation reached, some jobs may be not monitored")
                << TErrorAttribute("operation_type", OperationType_)
                << TErrorAttribute("limit_per_operation", Config_->UserJobMonitoring->ExtendedMaxMonitoredUserJobsPerOperation));
        return {};
    }
    if (MonitoredUserJobCount_ >= Config_->UserJobMonitoring->DefaultMaxMonitoredUserJobsPerOperation &&
        !GetOrDefault(Config_->UserJobMonitoring->EnableExtendedMaxMonitoredUserJobsPerOperation, OperationType_))
    {
        // NB(omgronny): It's OK to reach default max monitored jobs, so we don't set alert here.
        YT_LOG_DEBUG("Limit of monitored user jobs per operation reached "
            "(OperationType: %v, LimitPerOperation: %v)",
            OperationType_,
            Config_->UserJobMonitoring->DefaultMaxMonitoredUserJobsPerOperation);
        return {};
    }

    auto descriptor = Host_->TryAcquireJobMonitoringDescriptor(OperationId_);
    if (!descriptor) {
        SetOperationAlert(
            EOperationAlertType::UserJobMonitoringLimited,
            TError("Limit of monitored user jobs per controller agent reached, some jobs may be not monitored")
                << TErrorAttribute("limit_per_controller_agent", Config_->UserJobMonitoring->MaxMonitoredUserJobsPerAgent));
        return {};
    }
    ++RegisteredMonitoringDescriptorCount_;
    return descriptor;
}

void TOperationControllerBase::ReleaseMonitoringDescriptor(const TJobletPtr& joblet)
{
    const auto& userJobSpec = joblet->Task->GetUserJobSpec();
    if (userJobSpec && userJobSpec->Monitoring->Enable) {
        --MonitoredUserJobAttemptCount_;
    }
    if (joblet->UserJobMonitoringDescriptor) {
        InsertOrCrash(MonitoringDescriptorPool_, *joblet->UserJobMonitoringDescriptor);
        --MonitoredUserJobCount_;
        // NB: We do not want to remove index, but old version of logic can be done with the following call.
        // Host->ReleaseJobMonitoringDescriptor(OperationId, joblet->UserJobMonitoringDescriptor->Index);
    }
    if (MonitoredUserJobCount_ <= MonitoredUserJobAttemptCount_) {
        SetOperationAlert(EOperationAlertType::UserJobMonitoringLimited, TError());
    }
}

int TOperationControllerBase::GetMonitoredUserJobCount() const
{
    return MonitoredUserJobCount_;
}

int TOperationControllerBase::GetRegisteredMonitoringDescriptorCount() const
{
    return RegisteredMonitoringDescriptorCount_;
}

std::vector<TAllocationId> TOperationControllerBase::GetAllocationIdsByTreeId(const TString& treeId)
{
    std::vector<TAllocationId> allocationIds;
    allocationIds.reserve(size(AllocationMap_));
    for (const auto& [allocationId, allocation] : AllocationMap_) {
        if (allocation.Joblet && allocation.Joblet->TreeId == treeId) {
            allocationIds.push_back(allocationId);
        }
    }
    return allocationIds;
}

void TOperationControllerBase::SetProgressAttributesUpdated()
{
    ShouldUpdateProgressAttributesInCypress_ = false;
}

bool TOperationControllerBase::ShouldUpdateProgressAttributes() const
{
    return ShouldUpdateProgressAttributesInCypress_;
}

bool TOperationControllerBase::HasProgress() const
{
    if (!IsPrepared()) {
        return false;
    }

    {
        auto guard = Guard(ProgressLock_);
        return ProgressString_ && BriefProgressString_;
    }
}

void TOperationControllerBase::BuildPrepareAttributes(TFluentMap fluent) const
{
    YT_ASSERT_INVOKER_AFFINITY(InvokerPool_->GetInvoker(EOperationControllerQueue::Default));

    fluent
        .DoIf(static_cast<bool>(AutoMergeDirector_), [&] (TFluentMap fluent) {
            fluent
                .Item("auto_merge").BeginMap()
                    .Item("max_intermediate_chunk_count").Value(AutoMergeDirector_->GetMaxIntermediateChunkCount())
                    .Item("chunk_count_per_merge_job").Value(AutoMergeDirector_->GetChunkCountPerMergeJob())
                .EndMap();
        });
}

void TOperationControllerBase::BuildBriefSpec(TFluentMap fluent) const
{
    std::vector<TYPath> inputPaths;
    for (const auto& path : GetInputTablePaths()) {
        inputPaths.push_back(path.GetPath());
    }

    std::vector<TYPath> outputPaths;
    for (const auto& path : GetOutputTablePaths()) {
        outputPaths.push_back(path.GetPath());
    }

    fluent
        .OptionalItem("title", Spec_->Title)
        .OptionalItem("alias", Spec_->Alias)
        .Item("user_transaction_id").Value(UserTransactionId_)
        .Item("input_table_paths").ListLimited(inputPaths, 1)
        .Item("output_table_paths").ListLimited(outputPaths, 1);
}

void TOperationControllerBase::BuildProgress(TFluentMap fluent)
{
    if (!IsPrepared()) {
        return;
    }

    AccountExternalScheduleAllocationFailures();

    TAggregatedJobStatistics fullJobStatistics;
    try {
        fullJobStatistics = MergeJobStatistics(
            AggregatedFinishedJobStatistics_,
            AggregatedRunningJobStatistics_);
    } catch (const std::exception& ex) {
        SetOperationAlert(EOperationAlertType::IncompatibleStatistics, ex);
        // TODO(pavook): fail the operation after setting this alert.
    }

    fluent
        .Item("state").Value(State_.load())
        .Item("build_time").Value(TInstant::Now())
        .Item("ready_job_count").Value(GetPendingJobCount())
        .Item("job_statistics_v2").Value(fullJobStatistics)
        .Item("job_statistics").Do([this] (TFluentAny fluent) {
            AggregatedFinishedJobStatistics_.SerializeLegacy(fluent.GetConsumer());
        })
        .Item("peak_memory_usage").Value(PeakMemoryUsage_)
        .Item("estimated_input_statistics").BeginMap()
            .Do([&] (TFluentMap fluent) {
                Serialize(EstimatedInputStatistics_.value_or(TInputStatistics()), fluent);
            })
            .Item("unavailable_chunk_count").Value(GetUnavailableInputChunkCount() + UnavailableIntermediateChunkCount_)
            .Item("data_slice_count").Value(GetDataSliceCount())
        .EndMap()
        .Item("live_preview").BeginMap()
            .Item("output_supported").Value(IsLegacyOutputLivePreviewSupported())
            .Item("intermediate_supported").Value(IsLegacyIntermediateLivePreviewSupported())
            .Item("stderr_supported").Value(static_cast<bool>(StderrTable_))
            .Item("core_supported").Value(static_cast<bool>(CoreTable_))
            .Item("virtual_table_format").BeginMap()
                .Item("output_supported").Value(IsOutputLivePreviewSupported())
                .Item("intermediate_supported").Value(IsIntermediateLivePreviewSupported())
                .Item("stderr_supported").Value(static_cast<bool>(StderrTable_))
                .Item("core_supported").Value(static_cast<bool>(CoreTable_))
            .EndMap()
        .EndMap()
        .Item("schedule_job_statistics").BeginMap()
            .Item("count").Value(ScheduleAllocationStatistics_->GetCount())
            .Item("total_duration").Value(ScheduleAllocationStatistics_->GetTotalDuration())
            // COMPAT(eshcherbin)
            .Item("duration").Value(ScheduleAllocationStatistics_->GetTotalDuration())
            .Item("failed").Value(ScheduleAllocationStatistics_->Failed())
            .Item("successful_duration_estimate_us").Value(
                ScheduleAllocationStatistics_->SuccessfulDurationMovingAverage().GetAverage().value_or(TDuration::Zero()).MicroSeconds())
        .EndMap()
        // COMPAT(gritukan): Drop it in favour of "total_job_counter".
        .Item("jobs").Value(GetTotalJobCounter())
        .Item("total_job_counter").Value(GetTotalJobCounter())
        .Item("data_flow_graph").BeginMap()
            .Do(BIND(&TDataFlowGraph::BuildLegacyYson, DataFlowGraph_))
        .EndMap()
        // COMPAT(gritukan): Drop it in favour of per-task histograms.
        .DoIf(static_cast<bool>(EstimatedInputDataSizeHistogram_), [&] (TFluentMap fluent) {
            EstimatedInputDataSizeHistogram_->BuildHistogramView();
            fluent
                .Item("estimated_input_data_size_histogram").Value(*EstimatedInputDataSizeHistogram_);
        })
        .DoIf(static_cast<bool>(InputDataSizeHistogram_), [&] (TFluentMap fluent) {
            InputDataSizeHistogram_->BuildHistogramView();
            fluent
                .Item("input_data_size_histogram").Value(*InputDataSizeHistogram_);
        })
        .Item("snapshot_index").Value(SnapshotIndex_.load())
        .Item("recent_snapshot_index").Value(RecentSnapshotIndex_)
        .Item("last_successful_snapshot_time").Value(LastSuccessfulSnapshotTime_)
        .Item("tasks").DoListFor(GetTopologicallyOrderedTasks(), [=] (TFluentList fluent, const TTaskPtr& task) {
            fluent.Item()
                .BeginMap()
                    .Do(BIND(&TTask::BuildTaskYson, task))
                .EndMap();
        })
        .Item("data_flow").BeginList()
            .Do(BIND(&TDataFlowGraph::BuildDataFlowYson, DataFlowGraph_))
        .EndList();
}

void TOperationControllerBase::BuildBriefProgress(TFluentMap fluent) const
{
    if (IsPrepared()) {
        fluent
            .Item("state").Value(State_.load())
            // COMPAT(gritukan): Drop it in favour of "total_job_counter".
            .Item("jobs").Do(BIND([&] (TFluentAny fluent) {
                SerializeBriefVersion(GetTotalJobCounter(), fluent.GetConsumer());
            }))
            .Item("total_job_counter").Do(BIND([&] (TFluentAny fluent) {
                SerializeBriefVersion(GetTotalJobCounter(), fluent.GetConsumer());
            }))
            .Item("build_time").Value(TInstant::Now())
            .Item("registered_monitoring_descriptor_count").Value(GetRegisteredMonitoringDescriptorCount())
            .Item("input_transaction_id").Value(InputTransactions_->GetLocalInputTransactionId())
            .Item("output_transaction_id").Value(OutputTransaction_ ? OutputTransaction_->GetId() : NullTransactionId);
    }
}

void TOperationControllerBase::SafeBuildAndSaveProgress()
{
    YT_LOG_DEBUG("Building and saving progress");

    auto progressString = BuildYsonStringFluently()
        .BeginMap()
        .Do([&] (TFluentMap fluent) {
            auto asyncResult = WaitFor(
                BIND(&TOperationControllerBase::BuildProgress, MakeStrong(this))
                    .AsyncVia(GetInvoker())
                    .Run(fluent));
                asyncResult
                    .ThrowOnError();
            })
        .EndMap();

    auto briefProgressString = BuildYsonStringFluently()
        .BeginMap()
            .Do([&] (TFluentMap fluent) {
                auto asyncResult = WaitFor(
                    BIND(&TOperationControllerBase::BuildBriefProgress, MakeStrong(this))
                        .AsyncVia(GetInvoker())
                        .Run(fluent));
                asyncResult
                    .ThrowOnError();
            })
        .EndMap();

    {
        auto guard = Guard(ProgressLock_);
        if (!ProgressString_ || ProgressString_ != progressString ||
            !BriefProgressString_ || BriefProgressString_ != briefProgressString)
        {
            ShouldUpdateProgressAttributesInCypress_ = true;
            YT_LOG_DEBUG("New progress is different from previous one, should update progress");
        }
        ProgressString_ = progressString;
        BriefProgressString_ = briefProgressString;
    }
    YT_LOG_DEBUG("Progress built and saved");
}

TYsonString TOperationControllerBase::GetProgress() const
{
    auto guard = Guard(ProgressLock_);
    return ProgressString_;
}

TYsonString TOperationControllerBase::GetBriefProgress() const
{
    auto guard = Guard(ProgressLock_);
    return BriefProgressString_;
}

IYPathServicePtr TOperationControllerBase::GetOrchid() const
{
    return Orchid_.Acquire();
}

void TOperationControllerBase::ZombifyOrchid()
{
    Orchid_.Store(BuildZombieOrchid());
}

const std::vector<NScheduler::TJobShellPtr>& TOperationControllerBase::GetJobShells() const
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    return Spec_->JobShells;
}

NYson::TYsonString TOperationControllerBase::DoBuildJobsYson()
{
    return BuildYsonStringFluently<EYsonType::MapFragment>()
        .DoFor(AllocationMap_, [&] (TFluentMap fluent, const std::pair<const TAllocationId, TAllocation>& pair) {
            const auto& joblet = pair.second.Joblet;

            if (joblet && joblet->IsStarted()) {
                fluent.Item(ToString(joblet->JobId)).BeginMap()
                    .Do([&] (TFluentMap fluent) {
                        BuildJobAttributes(
                            joblet,
                            *joblet->JobState,
                            joblet->StderrSize,
                            fluent);
                    })
                .EndMap();
            }
        })
        .Finish();
}

void TOperationControllerBase::BuildJobsYson(TFluentMap fluent) const
{
    YT_ASSERT_INVOKER_AFFINITY(InvokerPool_->GetInvoker(EOperationControllerQueue::Default));
    fluent.GetConsumer()->OnRaw(CachedRunningJobs_.GetValue());
}

void TOperationControllerBase::BuildRetainedFinishedJobsYson(TFluentMap fluent) const
{
    for (const auto& [jobId, attributes] : RetainedFinishedJobs_) {
        fluent
            .Item(ToString(jobId)).Value(attributes);
    }
}

void TOperationControllerBase::EnrichJobInfo(NYTree::TFluentMap /*fluent*/, const TJobletPtr& /*joblet*/) const
{ }

void TOperationControllerBase::CheckTentativeTreeEligibility()
{
    THashSet<TString> treeIds;
    for (const auto& task : Tasks_) {
        task->LogTentativeTreeStatistics();
        for (const auto& treeId : task->FindAndBanSlowTentativeTrees()) {
            treeIds.insert(treeId);
        }
    }
    for (const auto& treeId : treeIds) {
        MaybeBanInTentativeTree(treeId);
    }
}

TSharedRef TOperationControllerBase::SafeBuildJobSpecProto(
    const TJobletPtr& joblet,
    const std::optional<NScheduler::NProto::TScheduleAllocationSpec>& scheduleAllocationSpec)
{
    YT_ASSERT_INVOKER_AFFINITY(JobSpecBuildInvoker_);

    if (auto buildJobSpecProtoDelay = Spec_->TestingOperationOptions->BuildJobSpecProtoDelay) {
        Sleep(*buildJobSpecProtoDelay);
    }

    return joblet->Task->BuildJobSpecProto(joblet, scheduleAllocationSpec);
}

TJobStartInfo TOperationControllerBase::SafeSettleJob(TAllocationId allocationId, std::optional<TJobId> lastJobId)
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker(EOperationControllerQueue::GetJobSpec));

    if (auto state = State_.load(); state != EControllerState::Running) {
        THROW_ERROR_EXCEPTION(
            NControllerAgent::EErrorCode::OperationIsNotRunning,
            "Operation controller is in %v state",
            state);
    }

    MaybeDelay(Spec_->TestingOperationOptions->SettleJobDelay);

    if (Spec_->TestingOperationOptions->FailSettleJobRequests) {
        THROW_ERROR_EXCEPTION("Testing failure");
    }

    auto maybeAllocation = FindAllocation(allocationId);
    if (!maybeAllocation) {
        YT_LOG_DEBUG(
            "Stale settle job request, no such allocation; send error instead of spec "
            "(AllocationId: %v)",
            allocationId);
        THROW_ERROR_EXCEPTION(
            NScheduler::EErrorCode::NoSuchAllocation,
            "No such allocation %v",
            allocationId);
    }

    auto& allocation = *maybeAllocation;

    if (allocation.Joblet && lastJobId && IsJobIdEarlier(allocation.Joblet->JobId, *lastJobId)) {
        // TODO(pogorelov): Support multijob allocation revival.
        THROW_ERROR_EXCEPTION("Failed to settle new job (looks like allocation has been revived)");
    }

    {
        bool requestIsStale = !allocation.Joblet && !lastJobId;

        THROW_ERROR_EXCEPTION_IF(
            requestIsStale,
            "Settle job request looks like retry");
    }

    if (!allocation.Joblet) {
        if (allocation.NewJobsForbiddenReason) {
            THROW_ERROR_EXCEPTION(
                "Settling new job in allocation is forbidden")
                << TErrorAttribute("reason", *allocation.NewJobsForbiddenReason);
        }

        YT_VERIFY(lastJobId);
        YT_VERIFY(allocation.LastJobInfo);
        auto failReason = TryScheduleNextJob(allocation, GetLaterJobId(allocation.LastJobInfo->JobId, *lastJobId));

        YT_ASSERT(failReason || allocation.Joblet);

        if (failReason) {
            THROW_ERROR_EXCEPTION(
                "Failed to schedule new job in allocation")
                << TErrorAttribute("fail_reason", *failReason);
        }
    }

    // We hold strong ref to not accidentally use object, destroyed during context switch.
    auto joblet = allocation.Joblet;

    if (!joblet->JobSpecProtoFuture) {
        YT_LOG_WARNING("Job spec future is missing (AllocationId: %v, JobId: %v)", allocationId, joblet->JobId);
        THROW_ERROR_EXCEPTION("Job for allocation %v is missing (request looks stale)", allocationId);
    }

    auto specBlob = WaitFor(joblet->JobSpecProtoFuture)
        .ValueOrThrow();

    auto operationState = State_.load();
    if (operationState != EControllerState::Running) {
        YT_LOG_DEBUG(
            "Stale settle job request, operation is already not running; send error instead of spec "
            "(AllocationId: %v, OperationState: %v)",
            allocationId,
            operationState);
        THROW_ERROR_EXCEPTION("Operation is not running");
    }

    //! NB(arkady-e1ppa): Concurrent OnJobAborted(Failed/Completed)
    //! can unregister joblet without changing the operation state yet.
    //! In such cases we might start job which was already finished.
    if (!FindJoblet(joblet->JobId)) {
        YT_LOG_DEBUG(
            "Stale settle job request, job is already finished; send error instead of spec "
            "(AllocationId: %v)",
            allocationId);
        THROW_ERROR_EXCEPTION("Job is already finished");
    }

    YT_VERIFY(allocation.Joblet);

    OnJobStarted(allocation.Joblet);

    return TJobStartInfo{
        .JobId = allocation.Joblet->JobId,
        .JobSpecBlob = std::move(specBlob),
    };
}

TYsonString TOperationControllerBase::GetSuspiciousJobsYson() const
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    auto guard = ReaderGuard(CachedSuspiciousJobsYsonLock_);
    return CachedSuspiciousJobsYson_;
}

void TOperationControllerBase::UpdateSuspiciousJobsYson()
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker());

    // We sort suspicious jobs by their last activity time and then
    // leave top `MaxOrchidEntryCountPerType` for each job type.

    std::vector<TJobletPtr> suspiciousJoblets;
    for (const auto& [_, allocation] : AllocationMap_) {
        if (allocation.Joblet && allocation.Joblet->Suspicious) {
            suspiciousJoblets.push_back(allocation.Joblet);
        }
    }

    std::sort(suspiciousJoblets.begin(), suspiciousJoblets.end(), [] (const TJobletPtr& lhs, const TJobletPtr& rhs) {
        return lhs->LastActivityTime < rhs->LastActivityTime;
    });

    THashMap<EJobType, int> suspiciousJobCountPerType;

    auto yson = BuildYsonStringFluently<EYsonType::MapFragment>()
        .DoFor(
            suspiciousJoblets,
            [&] (TFluentMap fluent, const TJobletPtr& joblet) {
                auto& count = suspiciousJobCountPerType[joblet->JobType];
                if (count < Config_->SuspiciousJobs->MaxOrchidEntryCountPerType) {
                    ++count;
                    fluent.Item(ToString(joblet->JobId))
                        .BeginMap()
                            .Item("operation_id").Value(ToString(OperationId_))
                            .Item("type").Value(joblet->JobType)
                            .Item("brief_statistics").Value(joblet->BriefStatistics)
                            .Item("node").Value(NNodeTrackerClient::GetDefaultAddress(joblet->NodeDescriptor.Addresses))
                            .Item("last_activity_time").Value(joblet->LastActivityTime)
                        .EndMap();
                }
            })
        .Finish();

    {
        auto guard = WriterGuard(CachedSuspiciousJobsYsonLock_);
        CachedSuspiciousJobsYson_ = yson;
    }
}

void TOperationControllerBase::UpdateAggregatedRunningJobStatistics()
{
    auto statisticsLimit = Options_->CustomStatisticsCountLimit;

    YT_LOG_DEBUG(
        "Updating aggregated running job statistics (StatisticsLimit: %v, RunningJobCount: %v)",
        statisticsLimit,
        RunningJobCount_);

    // A lightweight structure that represents a snapshot of a joblet that is safe to be used
    // in a separate invoker. Note that job statistics and controller statistics are const-qualified,
    // thus they are immutable.
    struct TJobletSnapshot
    {
        std::shared_ptr<const TStatistics> JobStatistics;
        std::shared_ptr<const TStatistics> ControllerStatistics;
        TJobStatisticsTags Tags;
    };

    std::vector<TJobletSnapshot> snapshots;
    snapshots.reserve(AllocationMap_.size());
    for (const auto& [allocationId, allocation] : AllocationMap_) {
        if (allocation.Joblet) {
            snapshots.emplace_back(TJobletSnapshot{
                .JobStatistics = allocation.Joblet->JobStatistics,
                .ControllerStatistics = allocation.Joblet->ControllerStatistics,
                .Tags = allocation.Joblet->GetAggregationTags(EJobState::Running),
            });
        }
    }

    // NB: This routine will be done in a separate thread pool.
    auto buildAggregatedStatisticsHeavy = [this, snapshots = std::move(snapshots), statisticsLimit, Logger = this->Logger] {
        TAggregatedJobStatistics runningJobStatistics;
        bool isLimitExceeded = false;

        YT_LOG_DEBUG("Starting aggregated job statistics update heavy routine");

        static const auto AggregationYieldPeriod = TDuration::MilliSeconds(10);

        TPeriodicYielder yielder(AggregationYieldPeriod);

        for (const auto& [jobStatistics, controllerStatistics, tags] : snapshots) {
            SafeUpdateAggregatedJobStatistics(
                this,
                runningJobStatistics,
                tags,
                *jobStatistics,
                *controllerStatistics,
                statisticsLimit,
                &isLimitExceeded);
            yielder.TryYield();
        }

        YT_LOG_DEBUG("Aggregated job statistics update heavy routine finished");

        return std::pair(std::move(runningJobStatistics), isLimitExceeded);
    };

    YT_LOG_DEBUG("Scheduling aggregated job statistics update heavy routine");

    NProfiling::TWallTimer wallTimer;
    auto [runningJobStatistics, isLimitExceeded] = WaitFor(
        BIND(buildAggregatedStatisticsHeavy)
            .AsyncVia(Host_->GetStatisticsOffloadInvoker())
            .Run())
        .Value();

    YT_LOG_DEBUG("New aggregated job statistics are ready (HeavyWallTime: %v)", wallTimer.GetElapsedTime());

    if (isLimitExceeded) {
        SetOperationAlert(EOperationAlertType::CustomStatisticsLimitExceeded,
            TError("Limit for number of custom statistics exceeded for operation, so they are truncated")
                << TErrorAttribute("limit", statisticsLimit));
    }

    // Old aggregated statistics will be destroyed in controller invoker but I am too lazy to fix that now.
    AggregatedRunningJobStatistics_ = std::move(runningJobStatistics);
}

void TOperationControllerBase::ReleaseJobs(const std::vector<TJobId>& jobIds)
{
    YT_ASSERT_INVOKER_POOL_AFFINITY(CancelableInvokerPool_);

    YT_LOG_DEBUG(
        "Releasing jobs (JobCount: %v)",
        std::size(jobIds));

    if (std::empty(jobIds)) {
        return;
    }

    std::vector<TJobToRelease> jobsToRelease;
    jobsToRelease.reserve(jobIds.size());
    for (auto jobId : jobIds) {
        if (auto it = JobIdToReleaseFlags_.find(jobId); it != JobIdToReleaseFlags_.end()) {
            jobsToRelease.emplace_back(TJobToRelease{jobId, it->second});
            JobIdToReleaseFlags_.erase(it);
        }
    }

    Host_->ReleaseJobs(jobsToRelease);
}

// TODO(max42): rename job -> joblet.
void TOperationControllerBase::AnalyzeBriefStatistics(
    const TJobletPtr& joblet,
    const TSuspiciousJobsOptionsPtr& options,
    const TErrorOr<TBriefJobStatisticsPtr>& briefStatisticsOrError)
{
    if (!briefStatisticsOrError.IsOK()) {
        if (joblet->BriefStatistics) {
            // Failures in brief statistics building are normal during job startup,
            // when readers and writers are not built yet. After we successfully built
            // brief statistics once, we shouldn't fail anymore.

            YT_LOG_WARNING(
                briefStatisticsOrError,
                "Failed to build brief job statistics (JobId: %v)",
                joblet->JobId);
        }

        return;
    }

    const auto& briefStatistics = briefStatisticsOrError.Value();

    bool wasActive = !joblet->BriefStatistics ||
        CheckJobActivity(
            joblet->BriefStatistics,
            briefStatistics,
            options,
            joblet->JobType);
    bool shouldIgnore = joblet->ResourceLimits.GetCpu() < options->MinRequiredCpuLimit;

    bool wasSuspicious = joblet->Suspicious;
    joblet->Suspicious = !wasActive &&
        (briefStatistics->Timestamp - joblet->LastActivityTime > options->InactivityTimeout) &&
        !shouldIgnore;
    if (!wasSuspicious && joblet->Suspicious) {
        YT_LOG_DEBUG("Found a suspicious job (JobId: %v, JobType: %v, LastActivityTime: %v, SuspiciousInactivityTimeout: %v, "
            "OldBriefStatistics: %v, NewBriefStatistics: %v)",
            joblet->JobId,
            joblet->JobType,
            joblet->LastActivityTime,
            options->InactivityTimeout,
            joblet->BriefStatistics,
            briefStatistics);
    }

    joblet->BriefStatistics = briefStatistics;

    if (wasActive) {
        joblet->LastActivityTime = joblet->BriefStatistics->Timestamp;
    }
}

void TOperationControllerBase::UpdateAggregatedFinishedJobStatistics(const TJobletPtr& joblet, const TJobSummary& jobSummary)
{
    i64 statisticsLimit = Options_->CustomStatisticsCountLimit;
    bool isLimitExceeded = false;

    SafeUpdateAggregatedJobStatistics(
        this,
        AggregatedFinishedJobStatistics_,
        joblet->GetAggregationTags(jobSummary.State),
        *joblet->JobStatistics,
        *joblet->ControllerStatistics,
        statisticsLimit,
        &isLimitExceeded);

    if (isLimitExceeded) {
        SetOperationAlert(EOperationAlertType::CustomStatisticsLimitExceeded,
            TError("Limit for number of custom statistics exceeded for operation, so they are truncated")
                << TErrorAttribute("limit", statisticsLimit));
    }

    joblet->Task->UpdateAggregatedFinishedJobStatistics(joblet, jobSummary);
}

void TOperationControllerBase::UpdateJobMetrics(const TJobletPtr& joblet, const TJobSummary& jobSummary, bool isJobFinished)
{
    YT_LOG_TRACE("Updating job metrics (JobId: %v)", joblet->JobId);

    auto delta = joblet->UpdateJobMetrics(jobSummary, isJobFinished);
    {
        auto guard = Guard(JobMetricsDeltaPerTreeLock_);

        auto it = JobMetricsDeltaPerTree_.find(joblet->TreeId);
        if (it == JobMetricsDeltaPerTree_.end()) {
            YT_VERIFY(JobMetricsDeltaPerTree_.emplace(joblet->TreeId, delta).second);
        } else {
            it->second += delta;
        }

        TotalTimePerTree_[joblet->TreeId] += delta.Values()[EJobMetricName::TotalTime];
        MainResourceConsumptionPerTree_[joblet->TreeId] += delta.Values()[EJobMetricName::TotalTime] *
            GetResource(joblet->ResourceLimits, PoolTreeControllerSettingsMap_[joblet->TreeId].MainResource);
    }
}

void TOperationControllerBase::LogProgress(bool force)
{
    if (!HasProgress()) {
        return;
    }

    auto now = GetCpuInstant();
    if (force || now > NextLogProgressDeadline_) {
        NextLogProgressDeadline_ = now + LogProgressBackoff_;
        YT_LOG_DEBUG("Operation progress (Progress: %v)", GetLoggingProgress());
    }
}

ui64 TOperationControllerBase::NextJobIndex()
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    return JobIndexGenerator_.Next();
}

TOperationId TOperationControllerBase::GetOperationId() const
{
    return OperationId_;
}

EOperationType TOperationControllerBase::GetOperationType() const
{
    return OperationType_;
}

TInstant TOperationControllerBase::GetStartTime() const
{
    return StartTime_;
}

const std::string& TOperationControllerBase::GetAuthenticatedUser() const
{
    return AuthenticatedUser_;
}

const TChunkListPoolPtr& TOperationControllerBase::GetOutputChunkListPool() const
{
    return OutputChunkListPool_;
}

const TControllerAgentConfigPtr& TOperationControllerBase::GetConfig() const
{
    return Config_;
}

const TOperationSpecBasePtr& TOperationControllerBase::GetSpec() const
{
    return Spec_;
}

const TOperationOptionsPtr& TOperationControllerBase::GetOptions() const
{
    return Options_;
}

const TOutputTablePtr& TOperationControllerBase::StderrTable() const
{
    return StderrTable_;
}

const TOutputTablePtr& TOperationControllerBase::CoreTable() const
{
    return CoreTable_;
}

const std::optional<TJobResources>& TOperationControllerBase::CachedMaxAvailableExecNodeResources() const
{
    return CachedMaxAvailableExecNodeResources_;
}

TInputManagerPtr TOperationControllerBase::GetInputManager() const
{
    return InputManager_;
}

bool TOperationControllerBase::IsRowCountPreserved() const
{
    return false;
}

i64 TOperationControllerBase::GetUnavailableInputChunkCount() const
{
    if (!DataSliceFetcherChunkScrapers_.empty() && State_ == EControllerState::Preparing) {
        i64 result = 0;
        for (const auto& fetcher : DataSliceFetcherChunkScrapers_) {
            result += fetcher->GetUnavailableChunkCount();
        }
        return result;
    }
    return InputManager_->GetUnavailableInputChunkCount();
}

int TOperationControllerBase::GetTotalJobCount() const
{
    YT_ASSERT_INVOKER_POOL_AFFINITY(CancelableInvokerPool_);

    // Avoid accessing the state while not prepared.
    if (!IsPrepared()) {
        return 0;
    }

    return GetTotalJobCounter()->GetTotal();
}

i64 TOperationControllerBase::GetDataSliceCount() const
{
    i64 result = 0;
    for (const auto& task : Tasks_) {
        result += task->GetInputDataSliceCount();
    }

    return result;
}

double TOperationControllerBase::GetCpuLimit(const TUserJobSpecPtr& userJobSpec) const
{
    return std::max(userJobSpec->CpuLimit, Options_->MinCpuLimit);
}

void TOperationControllerBase::InitUserJobSpecTemplate(
    NControllerAgent::NProto::TUserJobSpec* jobSpec,
    const TUserJobSpecPtr& jobSpecConfig,
    const std::vector<TUserFile>& files,
    const std::string& debugArtifactsAccount)
{
    const auto& userJobOptions = Options_->UserJob;

    jobSpec->set_shell_command(jobSpecConfig->Command);
    if (jobSpecConfig->JobTimeLimit) {
        jobSpec->set_job_time_limit(ToProto(*jobSpecConfig->JobTimeLimit));
    }
    jobSpec->set_prepare_time_limit(ToProto(jobSpecConfig->PrepareTimeLimit));
    jobSpec->set_memory_limit(jobSpecConfig->MemoryLimit);
    jobSpec->set_include_memory_mapped_files(jobSpecConfig->IncludeMemoryMappedFiles);
    jobSpec->set_use_yamr_descriptors(jobSpecConfig->UseYamrDescriptors);
    jobSpec->set_check_input_fully_consumed(jobSpecConfig->CheckInputFullyConsumed);
    jobSpec->set_max_stderr_size(jobSpecConfig->MaxStderrSize);
    jobSpec->set_custom_statistics_count_limit(jobSpecConfig->CustomStatisticsCountLimit);
    jobSpec->set_copy_files(jobSpecConfig->CopyFiles);
    jobSpec->set_debug_artifacts_account(debugArtifactsAccount);
    jobSpec->set_set_container_cpu_limit(jobSpecConfig->SetContainerCpuLimit || Options_->SetContainerCpuLimit);
    jobSpec->set_redirect_stdout_to_stderr(jobSpecConfig->RedirectStdoutToStderr);

    auto specifiedCpuLimit = GetCpuLimit(jobSpecConfig);
    // This is common policy for all operations of given type.
    if (Options_->SetContainerCpuLimit) {
        double cpuLimit;
        switch (Options_->CpuLimitOvercommitMode) {
            case ECpuLimitOvercommitMode::Linear:
                cpuLimit = Options_->CpuLimitOvercommitMultiplier * specifiedCpuLimit + Options_->InitialCpuLimitOvercommit;
                break;
            case ECpuLimitOvercommitMode::Minimum:
                cpuLimit = std::min(
                    specifiedCpuLimit * Options_->CpuLimitOvercommitMultiplier,
                    specifiedCpuLimit + Options_->InitialCpuLimitOvercommit);
                break;
        }
        jobSpec->set_container_cpu_limit(cpuLimit);
    }

    // This is common policy for all operations of given type.
    i64 threadLimit = ceil(userJobOptions->InitialThreadLimit + userJobOptions->ThreadLimitMultiplier * specifiedCpuLimit);
    jobSpec->set_thread_limit(threadLimit);

    // Option in task spec overrides value in operation options.
    if (jobSpecConfig->SetContainerCpuLimit) {
        jobSpec->set_container_cpu_limit(specifiedCpuLimit);
    }

    jobSpec->set_force_core_dump(jobSpecConfig->ForceCoreDump);

    jobSpec->set_port_count(jobSpecConfig->PortCount);
    jobSpec->set_use_porto_memory_tracking(jobSpecConfig->UsePortoMemoryTracking);

    if (Config_->EnableTmpfs) {
        for (const auto& volume : jobSpecConfig->TmpfsVolumes) {
            ToProto(jobSpec->add_tmpfs_volumes(), *volume);
        }
    }

    if (auto& diskRequest = jobSpecConfig->DiskRequest) {
        ToProto(jobSpec->mutable_disk_request(), *diskRequest);
        if (diskRequest->InodeCount) {
            jobSpec->set_inode_limit(*diskRequest->InodeCount);
        }
    }
    if (jobSpecConfig->InterruptionSignal) {
        jobSpec->set_interruption_signal(*jobSpecConfig->InterruptionSignal);
        jobSpec->set_signal_root_process_only(jobSpecConfig->SignalRootProcessOnly);
    }
    if (jobSpecConfig->RestartExitCode) {
        jobSpec->set_restart_exit_code(*jobSpecConfig->RestartExitCode);
    }

    if (Config_->IopsThreshold) {
        jobSpec->set_iops_threshold(*Config_->IopsThreshold);
        if (Config_->IopsThrottlerLimit) {
            jobSpec->set_iops_throttler_limit(*Config_->IopsThrottlerLimit);
        }
    }

    {
        // Set input and output format.
        TFormat inputFormat(EFormatType::Yson);
        TFormat outputFormat(EFormatType::Yson);

        if (jobSpecConfig->Format) {
            inputFormat = outputFormat = *jobSpecConfig->Format;
        }

        if (jobSpecConfig->InputFormat) {
            inputFormat = *jobSpecConfig->InputFormat;
        }

        if (jobSpecConfig->OutputFormat) {
            outputFormat = *jobSpecConfig->OutputFormat;
        }

        jobSpec->set_input_format(ToProto(ConvertToYsonString(inputFormat)));
        jobSpec->set_output_format(ToProto(ConvertToYsonString(outputFormat)));
    }

    jobSpec->set_enable_gpu_layers(jobSpecConfig->EnableGpuLayers);

    if (jobSpecConfig->CudaToolkitVersion) {
        jobSpec->set_cuda_toolkit_version(*jobSpecConfig->CudaToolkitVersion);
    }

    if (const auto& options = Options_->GpuCheck; options->UseSeparateRootVolume) {
        if (jobSpecConfig->EnableGpuCheck && jobSpecConfig->GpuLimit > 0) {
            jobSpec->set_gpu_check_binary_path(options->BinaryPath);
            ToProto(jobSpec->mutable_gpu_check_binary_args(), options->BinaryArgs);
            if (options->NetworkProject) {
                auto networkProject = GetNetworkProject(Host_->GetClient(), AuthenticatedUser_, *(options->NetworkProject));
                ToProto(jobSpec->mutable_gpu_check_network_project(), networkProject);
            }

            auto* protoEnvironment = jobSpec->mutable_gpu_check_environment();
            (*protoEnvironment)["YT_OPERATION_ID"] = ToString(OperationId_);
        }
    } else {
        // COMPAT(ignat)
        if (Config_->GpuCheckLayerDirectoryPath &&
            jobSpecConfig->GpuCheckBinaryPath &&
            jobSpecConfig->GpuCheckLayerName &&
            jobSpecConfig->EnableGpuLayers)
        {
            jobSpec->set_gpu_check_binary_path(*jobSpecConfig->GpuCheckBinaryPath);
            if (auto gpuCheckBinaryArgs = jobSpecConfig->GpuCheckBinaryArgs) {
                ToProto(jobSpec->mutable_gpu_check_binary_args(), *gpuCheckBinaryArgs);
            }
        }
    }

    if (jobSpecConfig->NetworkProject) {
        auto networkProject = GetNetworkProject(Host_->GetClient(), AuthenticatedUser_, *jobSpecConfig->NetworkProject);
        ToProto(jobSpec->mutable_network_project(), networkProject);

        // COMPAT(ignat)
        jobSpec->set_network_project_id(networkProject.Id);
        jobSpec->set_enable_nat64(networkProject.EnableNat64);
        jobSpec->set_disable_network(networkProject.DisableNetwork);
    }

    jobSpec->set_enable_porto(ToProto(jobSpecConfig->EnablePorto.value_or(Config_->DefaultEnablePorto)));
    jobSpec->set_fail_job_on_core_dump(jobSpecConfig->FailJobOnCoreDump);
    jobSpec->set_enable_cuda_gpu_core_dump(GetEnableCudaGpuCoreDump());

    jobSpec->set_enable_fuse(jobSpecConfig->EnableFuse);

    jobSpec->set_use_smaps_memory_tracker(jobSpecConfig->UseSMapsMemoryTracker);

    auto fillEnvironment = [&] (THashMap<TString, TString>& env) {
        for (const auto& [key, value] : env) {
            jobSpec->add_environment(Format("%v=%v", key, value));
        }
    };

    // Global environment.
    fillEnvironment(Config_->Environment);

    // Local environment.
    fillEnvironment(jobSpecConfig->Environment);

    jobSpec->add_environment(Format("YT_OPERATION_ID=%v", OperationId_));

    if (jobSpecConfig->ExtraEnvironment.contains(EExtraEnvironment::DiscoveryServerAddresses)) {
        auto addresses = ConvertToYsonString(Client_->GetNativeConnection()->GetDiscoveryServerAddresses(), EYsonFormat::Text);
        jobSpec->add_environment(Format("YT_DISCOVERY_ADDRESSES=%v", addresses));
    }

    if (jobSpecConfig->DockerImage) {
        jobSpec->add_environment(Format("YT_JOB_DOCKER_IMAGE=%v", *jobSpecConfig->DockerImage));
    }

    BuildFileSpecs(jobSpec, files, jobSpecConfig, Config_->EnableBypassArtifactCache);

    if (jobSpecConfig->Monitoring->Enable) {
        ToProto(jobSpec->mutable_monitoring_config()->mutable_sensor_names(), jobSpecConfig->Monitoring->SensorNames);
        jobSpec->mutable_monitoring_config()->set_request_gpu_monitoring(jobSpecConfig->Monitoring->RequestGpuMonitoring);
    }

    if (Config_->EnableJobArchiveTtl && jobSpecConfig->ArchiveTtl) {
        jobSpec->set_archive_ttl(ToProto(*jobSpecConfig->ArchiveTtl));
    }

    jobSpec->set_enable_rpc_proxy_in_job_proxy(jobSpecConfig->EnableRpcProxyInJobProxy);
    jobSpec->set_enable_shuffle_service_in_job_proxy(jobSpecConfig->EnableShuffleServiceInJobProxy);
    jobSpec->set_rpc_proxy_worker_thread_pool_size(jobSpecConfig->RpcProxyWorkerThreadPoolSize);

    jobSpec->set_start_queue_consumer_registration_manager(jobSpecConfig->StartQueueConsumerRegistrationManager);

    // Pass normalized docker image reference into job spec.
    if (jobSpecConfig->DockerImage) {
        TDockerImageSpec dockerImageSpec(*jobSpecConfig->DockerImage, Config_->DockerRegistry);
        if (!dockerImageSpec.IsInternal || Config_->DockerRegistry->ForwardInternalImagesToJobSpecs) {
            jobSpec->set_docker_image(dockerImageSpec.GetDockerImage());
        }
        if (dockerImageSpec.IsInternal && Config_->DockerRegistry->UseYtTokenForInternalRegistry) {
            GenerateDockerAuthFromToken(SecureVault_, AuthenticatedUser_, jobSpec);
        }
    }
}

const std::vector<TUserFile>& TOperationControllerBase::GetUserFiles(const TUserJobSpecPtr& userJobSpec) const
{
    return GetOrCrash(UserJobFiles_, userJobSpec);
}

void TOperationControllerBase::InitUserJobSpec(
    NControllerAgent::NProto::TUserJobSpec* jobSpec,
    const TJobletPtr& joblet) const
{
    YT_ASSERT_INVOKER_AFFINITY(JobSpecBuildInvoker_);

    ToProto(
        jobSpec->mutable_debug_transaction_id(),
        DebugTransaction_ ? DebugTransaction_->GetId() : NullTransactionId);

    ToProto(
        jobSpec->mutable_input_transaction_id(),
        InputTransactions_->GetLocalInputTransactionId());

    jobSpec->set_memory_reserve(joblet->UserJobMemoryReserve);
    jobSpec->set_job_proxy_memory_reserve(
        joblet->EstimatedResourceUsage.GetFootprintMemory() +
        joblet->EstimatedResourceUsage.GetJobProxyMemory() * joblet->JobProxyMemoryReserveFactor.value());

    if (Options_->SetSlotContainerMemoryLimit) {
        jobSpec->set_slot_container_memory_limit(
            jobSpec->memory_limit() +
            joblet->EstimatedResourceUsage.GetJobProxyMemory() +
            joblet->EstimatedResourceUsage.GetFootprintMemory() +
            Options_->SlotContainerMemoryOverhead);
    }

    auto fillEnvironment = [&] (const auto& setEnvironmentVariable) {
        setEnvironmentVariable("YT_JOB_INDEX", ToString(joblet->JobIndex));
        setEnvironmentVariable("YT_TASK_JOB_INDEX", ToString(joblet->TaskJobIndex));
        setEnvironmentVariable("YT_JOB_ID", ToString(joblet->JobId));
        setEnvironmentVariable("YT_JOB_COOKIE", ToString(joblet->OutputCookie));

        for (const auto& [key, value] : joblet->Task->BuildJobEnvironment()) {
            setEnvironmentVariable(key, value);
        }
    };

    fillEnvironment([&jobSpec] (TStringBuf key, TStringBuf value) {
        jobSpec->add_environment(Format("%v=%v", key, value));
    });

    if (joblet->StartRowIndex >= 0) {
        jobSpec->add_environment(Format("YT_START_ROW_INDEX=%v", joblet->StartRowIndex));
    }

    if (const auto& options = Options_->GpuCheck; options->UseSeparateRootVolume && jobSpec->has_gpu_check_binary_path()) {
        auto* protoEnvironment = jobSpec->mutable_gpu_check_environment();
        fillEnvironment([protoEnvironment] (TStringBuf key, TStringBuf value) {
            (*protoEnvironment)[key] = value;
        });
    }

    if (joblet->EnabledJobProfiler && joblet->EnabledJobProfiler->Type == EProfilerType::Cuda) {
        auto cudaProfilerEnvironment = !Spec_->CudaProfilerEnvironmentVariables.empty()
            ? Spec_->CudaProfilerEnvironmentVariables
            : Config_->CudaProfilerEnvironmentVariables;

        for (const auto& [name, value] : cudaProfilerEnvironment) {
            jobSpec->add_environment(
                Format("%v=%v", name, value));
        }
    }

    if (SecureVault_) {
        // NB: These environment variables should be added to user job spec, not to the user job spec template.
        // They may contain sensitive information that should not be persisted with a controller.

        // We add a single variable storing the whole secure vault and all top-level scalar values.
        jobSpec->add_environment(Format("%v=%v",
            SecureVaultEnvPrefix,
            ConvertToYsonString(SecureVault_, EYsonFormat::Text)));

        for (const auto& [key, node] : SecureVault_->GetChildren()) {
            std::optional<TString> value;
            switch (node->GetType()) {
                #define XX(type, cppType) \
                case ENodeType::type: \
                    value = ToString(node->As ## type()->GetValue()); \
                    break;
                ITERATE_SCALAR_YTREE_NODE_TYPES(XX)
                #undef XX
                case ENodeType::Entity:
                    break;
                default:
                    value = ConvertToYsonString(node, EYsonFormat::Text).ToString();
                    break;
            }
            if (value) {
                jobSpec->add_environment(Format("%v_%v=%v", SecureVaultEnvPrefix, key, *value));
            }
        }

        jobSpec->set_enable_secure_vault_variables_in_job_shell(Spec_->EnableSecureVaultVariablesInJobShell);
    }

    if (RetainedJobWithStderrCount_ >= Spec_->MaxStderrCount) {
        jobSpec->set_upload_stderr_if_completed(false);
    }

    if (joblet->StderrTableChunkListId) {
        AddStderrOutputSpecs(jobSpec, joblet);
    }
    if (joblet->CoreTableChunkListId) {
        AddCoreOutputSpecs(jobSpec, joblet);
    }

    if (joblet->UserJobMonitoringDescriptor) {
        auto* monitoringConfig = jobSpec->mutable_monitoring_config();
        monitoringConfig->set_enable(true);
        monitoringConfig->set_job_descriptor(ToString(*joblet->UserJobMonitoringDescriptor));
    }

    joblet->Task->PatchUserJobSpec(jobSpec, joblet);
}

void TOperationControllerBase::AddStderrOutputSpecs(
    NControllerAgent::NProto::TUserJobSpec* jobSpec,
    TJobletPtr joblet) const
{
    YT_ASSERT_INVOKER_AFFINITY(JobSpecBuildInvoker_);

    auto* stderrTableSpec = jobSpec->mutable_stderr_table_spec();
    auto* outputSpec = stderrTableSpec->mutable_output_table_spec();
    outputSpec->set_table_writer_options(ToProto(ConvertToYsonString(StderrTable_->TableWriterOptions)));
    outputSpec->set_table_schema(SerializeToWireProto(StderrTable_->TableUploadOptions.TableSchema.Get()));
    ToProto(outputSpec->mutable_chunk_list_id(), joblet->StderrTableChunkListId);

    auto writerConfig = GetStderrTableWriterConfig();
    YT_VERIFY(writerConfig);
    stderrTableSpec->set_blob_table_writer_config(ToProto(ConvertToYsonString(writerConfig)));
}

void TOperationControllerBase::AddCoreOutputSpecs(
    NControllerAgent::NProto::TUserJobSpec* jobSpec,
    TJobletPtr joblet) const
{
    YT_ASSERT_INVOKER_AFFINITY(JobSpecBuildInvoker_);

    auto* coreTableSpec = jobSpec->mutable_core_table_spec();
    auto* outputSpec = coreTableSpec->mutable_output_table_spec();
    outputSpec->set_table_writer_options(ToProto(ConvertToYsonString(CoreTable_->TableWriterOptions)));
    outputSpec->set_table_schema(SerializeToWireProto(CoreTable_->TableUploadOptions.TableSchema.Get()));
    ToProto(outputSpec->mutable_chunk_list_id(), joblet->CoreTableChunkListId);

    auto writerConfig = GetCoreTableWriterConfig();
    YT_VERIFY(writerConfig);
    coreTableSpec->set_blob_table_writer_config(ToProto(ConvertToYsonString(writerConfig)));
}

i64 TOperationControllerBase::GetFinalOutputIOMemorySize(
    TJobIOConfigPtr ioConfig,
    bool useEstimatedBufferSize) const
{
    i64 result = 0;
    for (const auto& outputTable : OutputTables_) {
        auto bufferSize = useEstimatedBufferSize
            ? GetWriteBufferSize(ioConfig->TableWriter, outputTable->TableWriterOptions)
            : ioConfig->TableWriter->MaxBufferSize;

        if (outputTable->TableWriterOptions->ErasureCodec == NErasure::ECodec::None) {
            i64 maxBufferSize = std::max(
                ioConfig->TableWriter->MaxRowWeight,
                bufferSize);
            result += GetOutputWindowMemorySize(ioConfig) + maxBufferSize;
        } else {
            auto* codec = NErasure::GetCodec(outputTable->TableWriterOptions->ErasureCodec);

            if (outputTable->TableWriterOptions->EnableStripedErasure) {
                // Table writer buffers.
                result += std::max(
                    ioConfig->TableWriter->MaxRowWeight,
                    ioConfig->TableWriter->MaxBufferSize);
                // Erasure writer buffer.
                result += ioConfig->TableWriter->ErasureWindowSize;
                // Encoding writer buffer.
                result += ioConfig->TableWriter->EncodeWindowSize;
                // Part writer buffers.
                result += ioConfig->TableWriter->SendWindowSize * codec->GetTotalPartCount();
            } else {
                double replicationFactor = (double) codec->GetTotalPartCount() / codec->GetDataPartCount();
                result += static_cast<i64>(ioConfig->TableWriter->DesiredChunkSize * replicationFactor);
            }
        }
    }
    return result;
}

void TOperationControllerBase::UpdateWriteBufferMemoryAlert(TJobId jobId, i64 curentMemoryLimit, i64 previousMemory)
{
    constexpr int subAlertCount = 5;
    constexpr int alertLimit = 5;

    TOverrunTableWriteBufferMemoryInfo overrunTableWriteBufferMemoryInfo(
        jobId,
        previousMemory,
        curentMemoryLimit);

    if (overrunTableWriteBufferMemoryInfo.GetRelativeDifference() > Config_->WriteBufferMemoryOverrunAlertFactor)
    {
        auto guard = Guard(OverrunWriteBufferMemoryPerJobLock_);
        OverrunWriteBufferMemoryPerJob_.emplace(std::move(overrunTableWriteBufferMemoryInfo));

        if (std::size(OverrunWriteBufferMemoryPerJob_) > alertLimit) {
            OverrunWriteBufferMemoryPerJob_.erase(std::prev(OverrunWriteBufferMemoryPerJob_.end()));
        }

        auto alert = TError("Some jobs began to consume more memory to write tables");

        std::for_each_n(
            OverrunWriteBufferMemoryPerJob_.begin(),
            std::min<int>(subAlertCount, std::ssize(OverrunWriteBufferMemoryPerJob_)),
            [&] (const TOverrunTableWriteBufferMemoryInfo& info) {
                auto subAlert = TError("More memory was allocated for write buffers with an estimated size than for buffers with a fixed size")
                    << TErrorAttribute("job_id", ToString(info.GetJobId()))
                    << TErrorAttribute("difference", info.GetRelativeDifference())
                    << TErrorAttribute("memory_with_fixed_buffers", info.GetReservedMemoryForJobProxyWithFixedBuffer())
                    << TErrorAttribute("memory_with_estimated_buffers", info.GetReservedMemoryForJobProxyWithEstimatedBuffer());
                alert.MutableInnerErrors()->emplace_back(std::move(subAlert));
            });

        SetOperationAlert(EOperationAlertType::WriteBufferMemoryOverrun, std::move(alert));
    }
}

i64 TOperationControllerBase::GetFinalIOMemorySize(
    TJobIOConfigPtr ioConfig,
    bool useEstimatedBufferSize,
    const TChunkStripeStatisticsVector& stripeStatistics) const
{
    i64 result = 0;
    for (const auto& stat : stripeStatistics) {
        result += GetInputIOMemorySize(ioConfig, stat);
    }
    result += GetFinalOutputIOMemorySize(ioConfig, useEstimatedBufferSize);
    return result;
}

void TOperationControllerBase::ValidateUserFileCount(TUserJobSpecPtr spec, const TString& operation)
{
    if (std::ssize(spec->FilePaths) > Config_->MaxUserFileCount) {
        THROW_ERROR_EXCEPTION("Too many user files in %v: maximum allowed %v, actual %v",
            operation,
            Config_->MaxUserFileCount,
            spec->FilePaths.size());
    }
}

void TOperationControllerBase::OnExecNodesUpdated()
{ }

int TOperationControllerBase::GetAvailableExecNodeCount()
{
    return AvailableExecNodeCount_;
}

const TExecNodeDescriptorMap& TOperationControllerBase::GetOnlineExecNodeDescriptors()
{
    return *OnlineExecNodesDescriptors_;
}

const TExecNodeDescriptorMap& TOperationControllerBase::GetExecNodeDescriptors()
{
    return *ExecNodesDescriptors_;
}

void TOperationControllerBase::UpdateExecNodes()
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    TSchedulingTagFilter filter(Spec_->SchedulingTagFilter);

    auto onlineExecNodeCount = Host_->GetAvailableExecNodeCount();
    auto execNodeDescriptors = Host_->GetExecNodeDescriptors(filter, /*onlineOnly*/ false);
    auto onlineExecNodeDescriptors = Host_->GetExecNodeDescriptors(filter, /*onlineOnly*/ true);
    auto maxAvailableResources = Host_->GetMaxAvailableResources(filter);

    const auto& controllerInvoker = GetCancelableInvoker();
    controllerInvoker->Invoke(
        BIND([=, this, weakThis = MakeWeak(this)] {
            auto this_ = weakThis.Lock();
            if (!this_) {
                return;
            }

            AvailableExecNodeCount_ = onlineExecNodeCount;
            CachedMaxAvailableExecNodeResources_ = maxAvailableResources;

            auto assign = []<class T, class U>(T* variable, U value) {
                auto oldValue = *variable;
                *variable = std::move(value);

                // Offload old value destruction to a large thread pool.
                NRpc::TDispatcher::Get()->GetCompressionPoolInvoker()->Invoke(
                    BIND([value = std::move(oldValue)] { Y_UNUSED(value); }));
            };

            assign(&ExecNodesDescriptors_, std::move(execNodeDescriptors));
            assign(&OnlineExecNodesDescriptors_, std::move(onlineExecNodeDescriptors));

            OnExecNodesUpdated();

            YT_LOG_DEBUG("Exec nodes information updated (SuitableExecNodeCount: %v, OnlineExecNodeCount: %v)",
                ExecNodesDescriptors_->size(),
                AvailableExecNodeCount_);
        }));
}

bool TOperationControllerBase::ShouldSkipSanityCheck()
{
    if (GetAvailableExecNodeCount() < Config_->SafeOnlineNodeCount) {
        return true;
    }

    if (TInstant::Now() < Host_->GetConnectionTime() + Config_->SafeSchedulerOnlineTime) {
        return true;
    }

    if (!CachedMaxAvailableExecNodeResources_) {
        return true;
    }

    if (TInstant::Now() < StartTime_ + Spec_->SanityCheckDelay) {
        return true;
    }

    return false;
}

void TOperationControllerBase::InferSchemaFromInput(const TSortColumns& sortColumns)
{
    // We infer schema only for operations with one output table.
    YT_VERIFY(OutputTables_.size() == 1);
    YT_VERIFY(InputManager_->GetInputTables().size() >= 1);

    OutputTables_[0]->TableUploadOptions.SchemaMode = InputManager_->GetInputTables()[0]->SchemaMode;
    for (const auto& table : InputManager_->GetInputTables()) {
        if (table->SchemaMode != OutputTables_[0]->TableUploadOptions.SchemaMode) {
            THROW_ERROR_EXCEPTION("Cannot infer output schema from input, tables have different schema modes")
                << TErrorAttribute("input_table1_path", table->GetPath())
                << TErrorAttribute("input_table1_schema_mode", table->SchemaMode)
                << TErrorAttribute("input_table2_path", InputManager_->GetInputTables()[0]->GetPath())
                << TErrorAttribute("input_table2_schema_mode", InputManager_->GetInputTables()[0]->SchemaMode);
        }
    }

    auto replaceStableNamesWithNames = [] (const TTableSchemaPtr& schema) {
        auto newColumns = schema->Columns();
        for (auto& newColumn : newColumns) {
            newColumn.SetStableName(TColumnStableName(newColumn.Name()));
        }
        return New<TTableSchema>(std::move(newColumns), schema->IsStrict(), schema->IsUniqueKeys());
    };

    if (OutputTables_[0]->TableUploadOptions.SchemaMode == ETableSchemaMode::Weak) {
        OutputTables_[0]->TableUploadOptions.TableSchema = TTableSchema::FromSortColumns(sortColumns);
    } else {
        auto resultSchema = replaceStableNamesWithNames(InputManager_->GetInputTables()[0]->Schema)
            ->ToSortedStrippedColumnAttributes();
        auto canonizedResultSchema = resultSchema
            ->ToStrippedColumnAttributes()
            ->ToCanonical();

        for (const auto& table : InputManager_->GetInputTables()) {
            auto currentSchema = replaceStableNamesWithNames(table->Schema)
                ->ToSortedStrippedColumnAttributes();
            auto currentCanonizedSchema = currentSchema
                ->ToStrippedColumnAttributes()
                ->ToCanonical();

            if (*canonizedResultSchema != *currentCanonizedSchema) {
                if (Config_->EnableMergeSchemasDuringSchemaInfer) {
                    try {
                        resultSchema = MergeTableSchemas(
                            resultSchema,
                            currentSchema);
                    } catch (const std::exception& ex) {
                        THROW_ERROR_EXCEPTION(
                            NTableClient::EErrorCode::IncompatibleSchemas,
                            "Cannot infer output schema from input in strong schema mode, "
                            "tables have incompatible schemas")
                            << ex;
                    }
                    canonizedResultSchema = resultSchema
                        ->ToStrippedColumnAttributes()
                        ->ToCanonical();
                } else {
                    THROW_ERROR_EXCEPTION(
                        NTableClient::EErrorCode::IncompatibleSchemas,
                        "Cannot infer output schema from input in strong schema mode, "
                        "because the option enable_merge_schemas_during_schema_infer is disabled")
                        << TErrorAttribute("lhs_schema", InputManager_->GetInputTables()[0]->Schema)
                        << TErrorAttribute("rhs_schema", table->Schema);
                }
            }
        }

        OutputTables_[0]->TableUploadOptions.TableSchema = replaceStableNamesWithNames(resultSchema)
            ->ToSorted(sortColumns)
            ->ToSortedStrippedColumnAttributes()
            ->ToCanonical();

        if (InputManager_->GetInputTables()[0]->Schema->HasNontrivialSchemaModification()) {
            OutputTables_[0]->TableUploadOptions.TableSchema =
                OutputTables_[0]->TableUploadOptions.TableSchema->SetSchemaModification(
                    InputManager_->GetInputTables()[0]->Schema->GetSchemaModification());
        }
    }

    FilterOutputSchemaByInputColumnSelectors(sortColumns);
}

void TOperationControllerBase::InferSchemaFromInputOrdered()
{
    // We infer schema only for operations with one output table.
    YT_VERIFY(OutputTables_.size() == 1);
    YT_VERIFY(InputManager_->GetInputTables().size() >= 1);

    auto& outputUploadOptions = OutputTables_[0]->TableUploadOptions;

    if (InputManager_->GetInputTables().size() == 1 && outputUploadOptions.UpdateMode == EUpdateMode::Overwrite) {
        // If only one input table given, we inherit the whole schema including column attributes.
        outputUploadOptions.SchemaMode = InputManager_->GetInputTables()[0]->SchemaMode;
        outputUploadOptions.TableSchema = InputManager_->GetInputTables()[0]->Schema;
        FilterOutputSchemaByInputColumnSelectors(/*sortColumns*/{});
        return;
    }

    InferSchemaFromInput();
}

void TOperationControllerBase::FilterOutputSchemaByInputColumnSelectors(const TSortColumns& sortColumns)
{
    THashSet<std::string> selectedColumns;
    for (const auto& table : InputManager_->GetInputTables()) {
        if (auto selectors = table->Path.GetColumns()) {
            for (const auto& column : *selectors) {
                selectedColumns.insert(column);
            }
        } else {
            return;
        }
    }

    auto& outputSchema = OutputTables_[0]->TableUploadOptions.TableSchema;

    for (const auto& sortColumn : sortColumns) {
        if (!selectedColumns.contains(sortColumn.Name)) {
            THROW_ERROR_EXCEPTION("Sort column %Qv is discarded by input column selectors", sortColumn.Name)
                << TErrorAttribute("sort_columns", sortColumns)
                << TErrorAttribute("selected_columns", selectedColumns);
        }
    }

    outputSchema = outputSchema->Filter(selectedColumns);
}

void TOperationControllerBase::ValidateOutputSchemaOrdered() const
{
    YT_VERIFY(OutputTables_.size() == 1);
    YT_VERIFY(InputManager_->GetInputTables().size() >= 1);

    if (InputManager_->GetInputTables().size() > 1 && OutputTables_[0]->TableUploadOptions.TableSchema->IsSorted()) {
        THROW_ERROR_EXCEPTION("Cannot generate sorted output for ordered operation with multiple input tables")
            << TErrorAttribute("output_schema", *OutputTables_[0]->TableUploadOptions.TableSchema);
    }
}

void TOperationControllerBase::ValidateOutputSchemaCompatibility(TTableSchemaCompatibilityOptions options) const
{
    YT_VERIFY(OutputTables_.size() == 1);

    for (const auto& inputTable : InputManager_->GetInputTables()) {
        if (inputTable->SchemaMode == ETableSchemaMode::Strong) {
            const auto& [compatibility, error] = CheckTableSchemaCompatibility(
                *inputTable->Schema->Filter(inputTable->Path.GetColumns()),
                *OutputTables_[0]->TableUploadOptions.GetUploadSchema(),
                options);
            if (compatibility < ESchemaCompatibility::RequireValidation) {
                // NB for historical reasons we consider optional<T> to be compatible with T when T is simple
                // check is performed during operation.
                THROW_ERROR_EXCEPTION(error);
            }
        }
    }
}

void TOperationControllerBase::ValidateSchemaInferenceMode(ESchemaInferenceMode schemaInferenceMode) const
{
    YT_VERIFY(OutputTables_.size() == 1);
    if (OutputTables_[0]->Dynamic && schemaInferenceMode != ESchemaInferenceMode::Auto) {
        THROW_ERROR_EXCEPTION("Only schema inference mode %Qlv is allowed for dynamic table in output",
            ESchemaInferenceMode::Auto);
    }
}

void TOperationControllerBase::ValidateOutputSchemaComputedColumnsCompatibility() const
{
    YT_VERIFY(OutputTables_.size() == 1);

    if (!OutputTables_[0]->TableUploadOptions.TableSchema->HasComputedColumns()) {
        return;
    }

    for (const auto& inputTable : InputManager_->GetInputTables()) {
        if (inputTable->SchemaMode == ETableSchemaMode::Strong) {
            auto filteredInputTableSchema = inputTable->Schema->Filter(inputTable->Path.GetColumns());
            ValidateComputedColumnsCompatibility(
                *filteredInputTableSchema,
                *OutputTables_[0]->TableUploadOptions.TableSchema.Get())
                .ThrowOnError();
            ValidateComputedColumns(*filteredInputTableSchema, /*isTableDynamic*/ false);
        } else {
            THROW_ERROR_EXCEPTION("Schemas of input tables must be strict "
                "if output table has computed columns");
        }
    }
}

void TOperationControllerBase::RegisterMetadata(auto&& registrar)
{
    PHOENIX_REGISTER_FIELD(1, SnapshotIndex_);
    PHOENIX_REGISTER_FIELD(8, UnavailableIntermediateChunkCount_);

    // COMPAT(yuryalekseev)
    PHOENIX_REGISTER_FIELD(9, TeleportedOutputRowCount_,
        .SinceVersion(ESnapshotVersion::TeleportedOutputRowCount));

    // COMPAT(coteeq)
    PHOENIX_REGISTER_FIELD(10, InputManager_,
        .SinceVersion(ESnapshotVersion::InputManagerIntroduction)
        .WhenMissing([] (TThis* this_, auto& context) {
            this_->InputManager_->PrepareToBeLoadedFromAncientVersion();
            this_->InputManager_->InitializeClients(this_->InputClient_);
            this_->InputManager_->LoadInputNodeDirectory(context);
            this_->OutputNodeDirectory_ = this_->InputManager_->GetNodeDirectory(LocalClusterName);
            this_->InputManager_->LoadInputTables(context);
        }));
    PHOENIX_REGISTER_FIELD(11, OutputNodeDirectory_,
        .SinceVersion(ESnapshotVersion::OutputNodeDirectory)
        .WhenMissing([] (TThis* this_, auto& /*context*/) {
            this_->OutputNodeDirectory_ = this_->InputManager_->GetNodeDirectory(LocalClusterName);
        }));
    PHOENIX_REGISTER_FIELD(12, InputStreamDirectory_);
    PHOENIX_REGISTER_FIELD(13, OutputTables_);
    PHOENIX_REGISTER_FIELD(14, StderrTable_);
    PHOENIX_REGISTER_FIELD(15, CoreTable_);
    PHOENIX_REGISTER_FIELD(16, OutputNodeDirectory_,
        .SinceVersion(ESnapshotVersion::RemoteInputForOperations));
    PHOENIX_REGISTER_FIELD(17, IntermediateTable_);
    PHOENIX_REGISTER_FIELD(18, UserJobFiles_,
        .template Serializer<TMapSerializer<TDefaultSerializer, TDefaultSerializer, TUnsortedTag>>());
    PHOENIX_REGISTER_FIELD(19, LivePreviewChunks_,
        .template Serializer<TMapSerializer<TDefaultSerializer, TDefaultSerializer, TUnsortedTag>>());
    PHOENIX_REGISTER_FIELD(20, Tasks_);
    registrar
        .template VirtualField<21>("InputChunkMap_", [] (TThis* this_, auto& context) {
            this_->InputManager_->LoadInputChunkMap(context);
        })
        .BeforeVersion(ESnapshotVersion::InputManagerIntroduction)();
    PHOENIX_REGISTER_FIELD(22, IntermediateOutputCellTagList_);
    PHOENIX_REGISTER_FIELD(23, CellTagToRequiredOutputChunkListCount_);
    PHOENIX_REGISTER_FIELD(24, CellTagToRequiredDebugChunkListCount_);
    registrar.template VirtualField<25>("PendingJobCount_", [] (TThis* this_, auto& context) {
        auto pendingJobCount = Load<TCompositePendingJobCount>(context);
        this_->CachedPendingJobCount_.Store(pendingJobCount);
    }, [] (const TThis* this_, auto& context) {
        auto pendingJobCount = this_->CachedPendingJobCount_.Load();
        NYT::Save(context, pendingJobCount);
    })();
    PHOENIX_REGISTER_FIELD(26, CachedNeededResources_);
    PHOENIX_REGISTER_FIELD(27, ChunkOriginMap_);

    // COMPAT(pogorelov)
    PHOENIX_REGISTER_FIELD(28, AllocationMap_);
    PHOENIX_REGISTER_FIELD(29, RunningJobCount_,
        .SinceVersion(ESnapshotVersion::AllocationMap));

    PHOENIX_REGISTER_FIELD(30, JobIndexGenerator_);
    PHOENIX_REGISTER_FIELD(31, AggregatedFinishedJobStatistics_);
    PHOENIX_REGISTER_FIELD(32, ScheduleAllocationStatistics_);
    PHOENIX_REGISTER_FIELD(33, RowCountLimitTableIndex_);
    PHOENIX_REGISTER_FIELD(34, RowCountLimit_);
    PHOENIX_REGISTER_FIELD(35, EstimatedInputDataSizeHistogram_);
    PHOENIX_REGISTER_FIELD(36, InputDataSizeHistogram_);
    PHOENIX_REGISTER_FIELD(37, RetainedJobWithStderrCount_);
    PHOENIX_REGISTER_FIELD(38, RetainedJobsCoreInfoCount_);
    PHOENIX_REGISTER_FIELD(39, RetainedJobCount_);
    PHOENIX_REGISTER_FIELD(40, JobSpecCompletedArchiveCount_);
    PHOENIX_REGISTER_FIELD(41, FailedJobCount_);
    PHOENIX_REGISTER_FIELD(42, Sink_);
    PHOENIX_REGISTER_FIELD(43, AutoMergeTask_);
    PHOENIX_REGISTER_FIELD(44, AutoMergeDirector_,
        .template Serializer<TUniquePtrSerializer<>>());
    PHOENIX_REGISTER_FIELD(45, DataFlowGraph_);
    // COMPAT(galtsev)
    registrar.template VirtualField<46>("LivePreviews_", [] (TThis* this_, auto& context) {
        NYT::Load(context, *this_->LivePreviews_);
    }, [] (const TThis* this_, auto& context) {
        NYT::Save(context, *this_->LivePreviews_);
    })();
    PHOENIX_REGISTER_FIELD(47, AvailableExecNodesObserved_);
    PHOENIX_REGISTER_FIELD(48, BannedNodeIds_);
    PHOENIX_REGISTER_FIELD(49, PathToOutputTable_);
    PHOENIX_REGISTER_FIELD(50, Acl_);
    // COMPAT(omgronny)
    PHOENIX_REGISTER_FIELD(51, AcoName_,
        .SinceVersion(ESnapshotVersion::AcoName));
    PHOENIX_REGISTER_FIELD(52, BannedTreeIds_);
    registrar
        .template VirtualField<53>("PathToInputTables_", [] (TThis* this_, auto& context) {
            this_->InputManager_->LoadPathToInputTables(context);
        })
        .BeforeVersion(ESnapshotVersion::InputManagerIntroduction)();
    PHOENIX_REGISTER_FIELD(54, JobMetricsDeltaPerTree_);
    PHOENIX_REGISTER_FIELD(55, TotalTimePerTree_);
    PHOENIX_REGISTER_FIELD(56, CompletedRowCount_);
    PHOENIX_REGISTER_FIELD(57, AutoMergeEnabled_);
    registrar
        .template VirtualField<58>("InputHasOrderedDynamicStores_", [] (TThis* this_, auto& context) {
            this_->InputManager_->LoadInputHasOrderedDynamicStores(context);
        })
        .BeforeVersion(ESnapshotVersion::InputManagerIntroduction)();
    PHOENIX_REGISTER_FIELD(59, StandardStreamDescriptors_);
    PHOENIX_REGISTER_FIELD(60, MainResourceConsumptionPerTree_);
    PHOENIX_REGISTER_FIELD(61, EnableMasterResourceUsageAccounting_);
    PHOENIX_REGISTER_FIELD(62, AccountResourceUsageLeaseMap_);
    PHOENIX_REGISTER_FIELD(63, TotalJobCounter_);

    PHOENIX_REGISTER_FIELD(64, AlertManager_);

    PHOENIX_REGISTER_FIELD(65, FastIntermediateMediumLimit_);

    PHOENIX_REGISTER_FIELD(66, BaseLayer_);
    PHOENIX_REGISTER_FIELD(67, JobExperiment_);

    // COMPAT(eshcherbin): Field index 68 was taken by InitialMinNeededResources_ which is removed since ESnapshotVersion::GroupedNeededResources.

    PHOENIX_REGISTER_FIELD(69, JobAbortsUntilOperationFailure_);

    PHOENIX_REGISTER_FIELD(70, MonitoredUserJobCount_,
        .SinceVersion(ESnapshotVersion::PersistMonitoringCounts));
    PHOENIX_REGISTER_FIELD(71, MonitoredUserJobAttemptCount_,
        .SinceVersion(ESnapshotVersion::PersistMonitoringCounts));
    PHOENIX_REGISTER_FIELD(72, RegisteredMonitoringDescriptorCount_,
        .SinceVersion(ESnapshotVersion::PersistMonitoringCounts));

    PHOENIX_REGISTER_FIELD(78, MonitoringDescriptorPool_,
        .SinceVersion(ESnapshotVersion::MonitoringDescriptorsPreserving));

    PHOENIX_REGISTER_FIELD(73, UnknownExitCodeFailCount_,
        .SinceVersion(ESnapshotVersion::JobFailTolerance));
    PHOENIX_REGISTER_FIELD(74, NoExitCodeFailCount_,
        .SinceVersion(ESnapshotVersion::JobFailTolerance));
    PHOENIX_REGISTER_FIELD(75, FailCountsPerKnownExitCode_,
        .SinceVersion(ESnapshotVersion::JobFailTolerance));

    PHOENIX_REGISTER_FIELD(76, OverrunWriteBufferMemoryPerJob_,
        .SinceVersion(ESnapshotVersion::TableWriteBufferEstimation));

    PHOENIX_REGISTER_FIELD(77, InitialGroupedNeededResources_,
        .SinceVersion(ESnapshotVersion::GroupedNeededResources));

    PHOENIX_REGISTER_FIELD(79, EstimatedInputStatistics_);

    // NB: Keep this at the end of persist as it requires some of the previous
    // fields to be already initialized.
    registrar.AfterLoad([] (TThis* this_, auto& /*context*/) {
        this_->InputManager_->InitializeClients(this_->InputClient_);
        this_->InitUpdatingTables();
    });
}

void TOperationControllerBase::ValidateRevivalAllowed() const
{
    if (HasJobUniquenessRequirements()) {
        THROW_ERROR_EXCEPTION(
            NScheduler::EErrorCode::OperationFailedOnJobRestart,
            "Cannot revive operation when spec option \"fail_on_job_restart\" is set")
                << TErrorAttribute("operation_type", OperationType_)
                << TErrorAttribute("reason", EFailOnJobRestartReason::RevivalIsForbidden);
    }
}

void TOperationControllerBase::ValidateSnapshot() const
{ }

std::vector<TUserJobSpecPtr> TOperationControllerBase::GetUserJobSpecs() const
{
    return {};
}

EIntermediateChunkUnstageMode TOperationControllerBase::GetIntermediateChunkUnstageMode() const
{
    return EIntermediateChunkUnstageMode::OnSnapshotCompleted;
}

TBlobTableWriterConfigPtr TOperationControllerBase::GetStderrTableWriterConfig() const
{
    return nullptr;
}

std::optional<TRichYPath> TOperationControllerBase::GetStderrTablePath() const
{
    return std::nullopt;
}

TBlobTableWriterConfigPtr TOperationControllerBase::GetCoreTableWriterConfig() const
{
    return nullptr;
}

std::optional<TRichYPath> TOperationControllerBase::GetCoreTablePath() const
{
    return std::nullopt;
}

bool TOperationControllerBase::GetEnableCudaGpuCoreDump() const
{
    return false;
}

void TOperationControllerBase::OnChunksReleased(int /*chunkCount*/)
{ }

TTableWriterOptionsPtr TOperationControllerBase::GetIntermediateTableWriterOptions() const
{
    auto options = New<NTableClient::TTableWriterOptions>();
    options->Account = Spec_->IntermediateDataAccount;
    options->ChunksVital = false;
    options->ChunksMovable = false;
    options->ReplicationFactor = Spec_->IntermediateDataReplicationFactor;
    options->MediumName = Spec_->IntermediateDataMediumName;
    options->CompressionCodec = Spec_->IntermediateCompressionCodec;
    // Distribute intermediate chunks uniformly across storage locations.
    options->PlacementId = GetOperationId().Underlying();
    // NB(levysotsky): Don't set table_index for intermediate streams
    // as we store table indices directly in rows of intermediate chunk.
    return options;
}

TOutputStreamDescriptorPtr TOperationControllerBase::GetIntermediateStreamDescriptorTemplate() const
{
    auto descriptor = New<TOutputStreamDescriptor>();
    descriptor->CellTags = IntermediateOutputCellTagList_;

    descriptor->TableWriterOptions = GetIntermediateTableWriterOptions();

    bool fastIntermediateMediumEnabled = Spec_->IntermediateDataAccount == NSecurityClient::IntermediateAccountName &&
        GetFastIntermediateMediumLimit() > 0;

    if (fastIntermediateMediumEnabled) {
        descriptor->SlowMedium = descriptor->TableWriterOptions->MediumName;
        descriptor->TableWriterOptions->MediumName = Config_->FastIntermediateMedium;
        if (auto tableWriterConfig = Spec_->FastIntermediateMediumTableWriterConfig) {
            descriptor->TableWriterOptions->ReplicationFactor = tableWriterConfig->UploadReplicationFactor;
            descriptor->TableWriterOptions->ErasureCodec = tableWriterConfig->ErasureCodec;
            descriptor->TableWriterOptions->EnableStripedErasure = false; // TODO(galtsev): use tableWriterConfig->EnableStripedErasure when the striped erasure reader is implemented, see YT-24207
        }
        YT_LOG_INFO("Fast intermediate medium enabled (FastMedium: %v, SlowMedium: %v, FastMediumLimit: %v, "
            "ReplicationFactor: %v, ErasureCodec: %v, EnableStripedErasure: %v)",
            descriptor->TableWriterOptions->MediumName,
            descriptor->SlowMedium,
            GetFastIntermediateMediumLimit(),
            descriptor->TableWriterOptions->ReplicationFactor,
            descriptor->TableWriterOptions->ErasureCodec,
            descriptor->TableWriterOptions->EnableStripedErasure);
    }

    descriptor->TableWriterConfig = MakeIntermediateTableWriterConfig(Spec_, fastIntermediateMediumEnabled);

    descriptor->RequiresRecoveryInfo = true;
    return descriptor;
}

void TOperationControllerBase::ReleaseIntermediateStripeList(const NChunkPools::TChunkStripeListPtr& stripeList)
{
    switch (GetIntermediateChunkUnstageMode()) {
        case EIntermediateChunkUnstageMode::OnJobCompleted: {
            auto chunks = GetStripeListChunks(stripeList);
            AddChunksToUnstageList(std::move(chunks));
            OnChunksReleased(stripeList->TotalChunkCount);
            break;
        }
        case EIntermediateChunkUnstageMode::OnSnapshotCompleted: {
            IntermediateStripeListReleaseQueue_.Push(stripeList);
            break;
        }
        default:
            YT_ABORT();
    }
}

const TDataFlowGraphPtr& TOperationControllerBase::GetDataFlowGraph() const
{
    return DataFlowGraph_;
}

void TOperationControllerBase::TLivePreviewChunkDescriptor::RegisterMetadata(auto&& registrar)
{
    PHOENIX_REGISTER_FIELD(1, VertexDescriptor);
    PHOENIX_REGISTER_FIELD(2, LivePreviewIndex);
}

void TOperationControllerBase::TResourceUsageLeaseInfo::RegisterMetadata(auto&& registrar)
{
    PHOENIX_REGISTER_FIELD(1, LeaseId);
    PHOENIX_REGISTER_FIELD(2, DiskQuota);
}

void TOperationControllerBase::RegisterLivePreviewChunk(
    const TDataFlowGraph::TVertexDescriptor& vertexDescriptor,
    int index,
    const TInputChunkPtr& chunk)
{
    YT_VERIFY(LivePreviewChunks_.emplace(
        chunk,
        TLivePreviewChunkDescriptor{vertexDescriptor, index}).second);

    auto result = DataFlowGraph_->TryRegisterLivePreviewChunk(vertexDescriptor, index, chunk);
    if (!result.IsOK()) {
        static constexpr auto message = "Error registering a chunk in a live preview";
        auto tableName = "output_" + ToString(index);
        if (Config_->FailOperationOnErrorsInLivePreview) {
            THROW_ERROR_EXCEPTION(message)
                << TErrorAttribute("table_name", tableName)
                << TErrorAttribute("chunk_id", chunk->GetChunkId());
        } else {
            YT_LOG_WARNING(result, "%v (TableName: %v, Chunk: %v)",
                message,
                tableName,
                chunk);
        }
    }

    if (vertexDescriptor == GetOutputLivePreviewVertexDescriptor()) {
        auto tableName = "output_" + ToString(index);
        AttachToLivePreview(tableName, chunk);
    }
}

const IThroughputThrottlerPtr& TOperationControllerBase::GetJobSpecSliceThrottler() const
{
    return Host_->GetJobSpecSliceThrottler();
}

// TODO(gritukan): Should this method exist?
void TOperationControllerBase::FinishTaskInput(const TTaskPtr& task)
{
    task->FinishInput();
    task->RegisterInGraph(TDataFlowGraph::SourceDescriptor);
}

const std::vector<TTaskPtr>& TOperationControllerBase::GetTasks() const
{
    return Tasks_;
}

void TOperationControllerBase::SetOperationAlert(EOperationAlertType alertType, const TError& alert)
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    auto guard = Guard(AlertsLock_);

    auto& existingAlert = Alerts_[alertType];
    if (alert.IsOK() && !existingAlert.IsOK()) {
        YT_LOG_DEBUG("Alert reset (Type: %v)",
            alertType);
    } else if (!alert.IsOK() && existingAlert.IsOK()) {
        YT_LOG_DEBUG(alert, "Alert set (Type: %v)",
            alertType);
    } else if (!alert.IsOK() && !existingAlert.IsOK()) {
        YT_LOG_DEBUG(alert, "Alert updated (Type: %v)",
            alertType);
    }

    Alerts_[alertType] = alert;
}

bool TOperationControllerBase::IsCompleted() const
{
    if (AutoMergeTask_ && !AutoMergeTask_->IsCompleted()) {
        return false;
    }
    return true;
}

TString TOperationControllerBase::WriteCoreDump() const
{
    // Save `this` explicitly to simplify debugging a core dump in GDB.
    auto this_ = this;
    DoNotOptimizeAway(this_);

    const auto& coreDumper = Host_->GetCoreDumper();
    if (!coreDumper) {
        THROW_ERROR_EXCEPTION("Core dumper is not set up");
    }
    return coreDumper->WriteCoreDump(CoreNotes_, "rpc_call").Path;
}

void TOperationControllerBase::RegisterOutputRows(i64 count, int tableIndex)
{
    if (RowCountLimitTableIndex_ && *RowCountLimitTableIndex_ == tableIndex && !IsFinished()) {
        CompletedRowCount_ += count;
        if (CompletedRowCount_ >= RowCountLimit_) {
            YT_LOG_INFO("Row count limit is reached (CompletedRowCount: %v, RowCountLimit: %v).",
                CompletedRowCount_,
                RowCountLimit_);
            OnOperationCompleted(/*interrupted*/ true);
        }
    }
}

std::optional<int> TOperationControllerBase::GetRowCountLimitTableIndex()
{
    return RowCountLimitTableIndex_;
}

void TOperationControllerBase::LoadSnapshot(const NYT::NControllerAgent::TOperationSnapshot& snapshot)
{
    DoLoadSnapshot(snapshot);
}

void TOperationControllerBase::RegisterOutputTables(const std::vector<TRichYPath>& outputTablePaths)
{
    std::vector<IPersistentChunkPoolInputPtr> sinks;
    sinks.reserve(outputTablePaths.size());
    for (const auto& outputTablePath : outputTablePaths) {
        auto it = PathToOutputTable_.find(outputTablePath.GetPath());
        if (it != PathToOutputTable_.end()) {
            const auto& lhsAttributes = it->second->Path.Attributes();
            const auto& rhsAttributes = outputTablePath.Attributes();
            if (lhsAttributes != rhsAttributes) {
                THROW_ERROR_EXCEPTION("Output table %v appears twice with different attributes", outputTablePath.GetPath())
                    << TErrorAttribute("lhs_attributes", lhsAttributes)
                    << TErrorAttribute("rhs_attributes", rhsAttributes);
            }
            continue;
        }
        auto table = New<TOutputTable>(outputTablePath, EOutputTableType::Output);
        table->TableIndex = OutputTables_.size();
        auto rowCountLimit = table->Path.GetRowCountLimit();
        if (rowCountLimit) {
            if (RowCountLimitTableIndex_) {
                THROW_ERROR_EXCEPTION("Only one output table with row_count_limit is supported");
            }
            RowCountLimitTableIndex_ = table->TableIndex;
            RowCountLimit_ = *rowCountLimit;
        }

        sinks.emplace_back(New<TSink>(this, table->TableIndex));
        OutputTables_.emplace_back(table);
        PathToOutputTable_[outputTablePath.GetPath()] = table;
    }

    Sink_ = CreateMultiChunkPoolInput(std::move(sinks));
}

void TOperationControllerBase::DoAbortJob(
    TJobletPtr joblet,
    EAbortReason abortReason,
    bool requestJobTrackerJobAbortion,
    bool force)
{
    // NB(renadeen): there must be no context switches before call OnJobAborted.

    auto jobId = joblet->JobId;

    if (!force && !ShouldProcessJobEvents()) {
        YT_LOG_DEBUG(
            "Job events processing disabled, abort skipped (JobId: %v, OperationState: %v)",
            joblet->JobId,
            State_.load());
        return;
    }

    if (requestJobTrackerJobAbortion) {
        Host_->AbortJob(joblet->JobId, abortReason, /*requestNewJob*/ false);
    }

    OnJobAborted(std::move(joblet), std::make_unique<TAbortedJobSummary>(jobId, abortReason));
}

void TOperationControllerBase::AbortJob(TJobId jobId, EAbortReason abortReason)
{
    auto joblet = FindJoblet(jobId);
    if (!joblet) {
        YT_LOG_DEBUG(
            "Ignore stale job abort request (JobId: %v, AbortReason: %v)",
            jobId,
            abortReason);

        return;
    }

    YT_LOG_DEBUG(
        "Aborting job by controller request (JobId: %v, AbortReason: %v)",
        jobId,
        abortReason);

    DoAbortJob(std::move(joblet), abortReason, /*requestJobTrackerJobAbortion*/ true, /*force*/ false);
}

bool TOperationControllerBase::CanInterruptJobs() const
{
    return Config_->EnableJobInterrupts && InputManager_->CanInterruptJobs();
}

void TOperationControllerBase::InterruptJob(TJobId jobId, EInterruptionReason reason)
{
    InterruptJob(
        jobId,
        reason,
        /*timeout*/ TDuration::Zero());
}

void TOperationControllerBase::HandleJobReport(const TJobletPtr& joblet, TControllerJobReport&& jobReport) const
{
    Host_->GetJobReporter()->HandleJobReport(
        jobReport
            .OperationId(OperationId_)
            .JobId(joblet->JobId)
            .Address(NNodeTrackerClient::GetDefaultAddress(joblet->NodeDescriptor.Addresses))
            .Addresses(joblet->NodeDescriptor.Addresses)
            .Ttl(joblet->ArchiveTtl)
            .AllocationId(AllocationIdFromJobId(joblet->JobId)));
}

void TOperationControllerBase::OnCompetitiveJobScheduled(const TJobletPtr& joblet, EJobCompetitionType competitionType)
{
    ReportJobHasCompetitors(joblet, competitionType);
    // Original job could be finished and another speculative still running.
    if (auto originalJob = FindJoblet(joblet->CompetitionIds[competitionType])) {
        ReportJobHasCompetitors(originalJob, competitionType);
    }
}

void TOperationControllerBase::ReportJobHasCompetitors(const TJobletPtr& joblet, EJobCompetitionType competitionType)
{
    if (!joblet->HasCompetitors[competitionType]) {
        joblet->HasCompetitors[competitionType] = true;

        HandleJobReport(joblet, TControllerJobReport()
            .HasCompetitors(/*hasCompetitors*/ true, competitionType));
    }
}

void TOperationControllerBase::RegisterTestingSpeculativeJobIfNeeded(TTask& task, TAllocationId allocationId)
{
    //! NB(arkady-e1ppa): we always have one joblet per allocation.
    const auto& joblet = GetJoblet(allocationId);

    bool needLaunchSpeculativeJob;
    switch (Spec_->TestingOperationOptions->TestingSpeculativeLaunchMode) {
        case ETestingSpeculativeLaunchMode::None:
            needLaunchSpeculativeJob = false;
            break;
        case ETestingSpeculativeLaunchMode::Once:
            needLaunchSpeculativeJob = joblet->JobIndex == 0;
            break;
        case ETestingSpeculativeLaunchMode::Always:
            needLaunchSpeculativeJob = !joblet->CompetitionType;
            break;
        default:
            YT_ABORT();
    }
    if (needLaunchSpeculativeJob) {
        task.TryRegisterSpeculativeJob(joblet);
    }
}

std::vector<TRichYPath> TOperationControllerBase::GetLayerPaths(
    const NYT::NScheduler::TUserJobSpecPtr& userJobSpec) const
{
    if (!Config_->TestingOptions->RootfsTestLayers.empty()) {
        return Config_->TestingOptions->RootfsTestLayers;
    }
    std::vector<TRichYPath> layerPaths;
    if (userJobSpec->DockerImage) {
        TDockerImageSpec dockerImage(*userJobSpec->DockerImage, Config_->DockerRegistry);

        // External docker images are not compatible with any additional layers.
        if (!dockerImage.IsInternal || !Config_->DockerRegistry->TranslateInternalImagesIntoLayers) {
            return {};
        }

        // Resolve internal docker image into base layers.
        layerPaths = GetLayerPathsFromDockerImage(Host_->GetClient(), dockerImage);
    }
    std::copy(userJobSpec->LayerPaths.begin(), userJobSpec->LayerPaths.end(), std::back_inserter(layerPaths));
    if (layerPaths.empty() && Spec_->DefaultBaseLayerPath) {
        layerPaths.insert(layerPaths.begin(), *Spec_->DefaultBaseLayerPath);
    }
    if (Config_->DefaultLayerPath && layerPaths.empty()) {
        // If no layers were specified, we insert the default one.
        layerPaths.insert(layerPaths.begin(), *Config_->DefaultLayerPath);
    }
    if (Config_->CudaToolkitLayerDirectoryPath &&
        !layerPaths.empty() &&
        userJobSpec->CudaToolkitVersion &&
        userJobSpec->EnableGpuLayers)
    {
        // If cuda toolkit is requested, add the layer as the topmost user layer.
        auto path = *Config_->CudaToolkitLayerDirectoryPath + "/" + *userJobSpec->CudaToolkitVersion;
        layerPaths.insert(layerPaths.begin(), path);
    }
    // COMPAT(ignat)
    if (!Options_->GpuCheck->UseSeparateRootVolume &&
        Config_->GpuCheckLayerDirectoryPath &&
        userJobSpec->GpuCheckLayerName &&
        userJobSpec->GpuCheckBinaryPath &&
        !layerPaths.empty() &&
        userJobSpec->EnableGpuLayers)
    {
        // If cuda toolkit is requested, add the layer as the topmost user layer.
        auto path = *Config_->GpuCheckLayerDirectoryPath + "/" + *userJobSpec->GpuCheckLayerName;
        layerPaths.insert(layerPaths.begin(), path);
    }
    if (userJobSpec->Profilers) {
        for (const auto& profilerSpec : *userJobSpec->Profilers) {
            auto cudaProfilerLayerPath = Spec_->CudaProfilerLayerPath
                ? Spec_->CudaProfilerLayerPath
                : Config_->CudaProfilerLayerPath;

            if (cudaProfilerLayerPath && profilerSpec->Type == EProfilerType::Cuda) {
                layerPaths.insert(layerPaths.begin(), *cudaProfilerLayerPath);
                break;
            }
        }
    }
    if (!layerPaths.empty()) {
        auto systemLayerPath = userJobSpec->SystemLayerPath
            ? userJobSpec->SystemLayerPath
            : Config_->SystemLayerPath;
        if (systemLayerPath) {
            // This must be the top layer, so insert in the beginning.
            layerPaths.insert(layerPaths.begin(), *systemLayerPath);
        }
    }
    return layerPaths;
}

const TThrottlerManagerPtr& TOperationControllerBase::GetChunkLocationThrottlerManager() const
{
    return Host_->GetChunkLocationThrottlerManager();
}

void TOperationControllerBase::MaybeCancel(ECancelationStage cancelationStage)
{
    if (Spec_->TestingOperationOptions->CancelationStage &&
        cancelationStage == *Spec_->TestingOperationOptions->CancelationStage)
    {
        YT_LOG_INFO("Making test operation failure (CancelationStage: %v)", cancelationStage);
        GetInvoker()->Invoke(BIND(
            &TOperationControllerBase::DoFailOperation,
            MakeWeak(this),
            TError("Test operation failure"),
            /*flush*/ false,
            /*abortAllJoblets*/ false));
        YT_LOG_INFO("Making test cancelation (CancelationStage: %v)", cancelationStage);
        Cancel();
    }
}

const NChunkClient::TMediumDirectoryPtr& TOperationControllerBase::GetMediumDirectory() const
{
    return MediumDirectory_;
}

TJobSplitterConfigPtr TOperationControllerBase::GetJobSplitterConfigTemplate() const
{
    auto config = CloneYsonStruct(Options_->JobSplitter);

    if (!Spec_->EnableJobSplitting || !Config_->EnableJobSplitting) {
        config->EnableJobSplitting = false;
    }

    const auto& specConfig = Spec_->JobSplitter;

    if (specConfig->MinJobTime) {
        config->MinJobTime = *(specConfig->MinJobTime);
    }
    if (specConfig->MinTotalDataWeight) {
        config->MinTotalDataWeight = *(specConfig->MinTotalDataWeight);
    }
    if (specConfig->ExecToPrepareTimeRatio) {
        config->ExecToPrepareTimeRatio = *(specConfig->ExecToPrepareTimeRatio);
    }
    if (specConfig->NoProgressJobTimeToAveragePrepareTimeRatio) {
        config->NoProgressJobTimeToAveragePrepareTimeRatio = *(specConfig->NoProgressJobTimeToAveragePrepareTimeRatio);
    }

    if (specConfig->MaxJobsPerSplit) {
        config->MaxJobsPerSplit = *(specConfig->MaxJobsPerSplit);
    }
    if (specConfig->MaxInputTableCount) {
        config->MaxInputTableCount = *(specConfig->MaxInputTableCount);
    }

    if (specConfig->ResidualJobFactor) {
        config->ResidualJobFactor = *(specConfig->ResidualJobFactor);
    }
    if (specConfig->ResidualJobCountMinThreshold) {
        config->ResidualJobCountMinThreshold = *(specConfig->ResidualJobCountMinThreshold);
    }

    if (!specConfig->EnableJobSplitting) {
        config->EnableJobSplitting = false;
    }
    if (!specConfig->EnableJobSpeculation) {
        config->EnableJobSpeculation = false;
    }

    // It should be checked after update of config->MaxInputTableCount.
    if (std::ssize(InputManager_->GetInputTables()) > config->MaxInputTableCount) {
        config->EnableJobSplitting = false;
    }

    return config;
}

const TInputTablePtr& TOperationControllerBase::GetInputTable(int tableIndex) const
{
    return InputManager_->GetInputTables()[tableIndex];
}

const TOutputTablePtr& TOperationControllerBase::GetOutputTable(int tableIndex) const
{
    return OutputTables_[tableIndex];
}

int TOperationControllerBase::GetOutputTableCount() const
{
    return std::ssize(OutputTables_);
}

std::vector<TTaskPtr> TOperationControllerBase::GetTopologicallyOrderedTasks() const
{
    THashMap<TDataFlowGraph::TVertexDescriptor, int> vertexDescriptorToIndex;
    auto topologicalOrdering = DataFlowGraph_->GetTopologicalOrdering();
    for (int index = 0; index < std::ssize(topologicalOrdering); ++index) {
        YT_VERIFY(vertexDescriptorToIndex.emplace(topologicalOrdering[index], index).second);
    }

    std::vector<std::pair<int, TTaskPtr>> tasksWithIndices;
    tasksWithIndices.reserve(Tasks_.size());
    for (const auto& task : Tasks_) {
        for (const auto& vertex : task->GetAllVertexDescriptors()) {
            auto iterator = vertexDescriptorToIndex.find(vertex);
            if (iterator != vertexDescriptorToIndex.end()) {
                tasksWithIndices.emplace_back(iterator->second, task);
                break;
            }
        }
    }
    std::sort(tasksWithIndices.begin(), tasksWithIndices.end());

    std::vector<TTaskPtr> tasks;
    tasks.reserve(tasksWithIndices.size());
    for (auto& [index, task] : tasksWithIndices) {
        Y_UNUSED(index);
        tasks.push_back(std::move(task));
    }
    return tasks;
}

void TOperationControllerBase::AccountExternalScheduleAllocationFailures() const
{
    YT_ASSERT_INVOKER_POOL_AFFINITY(CancelableInvokerPool_);

    for (const auto& reason : TEnumTraits<EScheduleFailReason>::GetDomainValues()) {
        auto count = ExternalScheduleAllocationFailureCounts_[reason].exchange(0);
        ScheduleAllocationStatistics_->Failed()[reason] += count;
    }
}

void TOperationControllerBase::UpdatePeakMemoryUsage()
{
    YT_ASSERT_INVOKER_POOL_AFFINITY(CancelableInvokerPool_);

    auto memoryUsage = GetMemoryUsage();

    PeakMemoryUsage_ = std::max(memoryUsage, PeakMemoryUsage_);
}

void TOperationControllerBase::OnMemoryLimitExceeded(const TError& error)
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    MemoryLimitExceeded_ = true;

    GetInvoker()->Invoke(BIND(
        &TOperationControllerBase::DoFailOperation,
        MakeWeak(this),
        error,
        /*flush*/ true,
        /*abortAllJoblets*/ true));
}

bool TOperationControllerBase::IsMemoryLimitExceeded() const
{
    YT_ASSERT_THREAD_AFFINITY_ANY();

    return MemoryLimitExceeded_;
}

void TOperationControllerBase::ReportJobCookieToArchive(const TJobletPtr& joblet) const
{
    HandleJobReport(joblet, TControllerJobReport()
        .JobCookie(joblet->OutputCookie));
}

void TOperationControllerBase::ReportControllerStateToArchive(const TJobletPtr& joblet, EJobState state) const
{
    HandleJobReport(joblet, TControllerJobReport()
        .ControllerState(state));
}

void TOperationControllerBase::ReportStartTimeToArchive(const TJobletPtr& joblet) const
{
    HandleJobReport(joblet, TControllerJobReport()
        .StartTime(joblet->StartTime));
}

void TOperationControllerBase::ReportFinishTimeToArchive(const TJobletPtr& joblet) const
{
    HandleJobReport(joblet, TControllerJobReport()
        .FinishTime(joblet->FinishTime));
}

void TOperationControllerBase::SendRunningAllocationTimeStatisticsUpdates()
{
    YT_ASSERT_INVOKER_AFFINITY(GetCancelableInvoker(EOperationControllerQueue::JobEvents));

    std::vector<TAgentToSchedulerRunningAllocationStatistics> runningAllocationTimeStatisticsUpdates;
    runningAllocationTimeStatisticsUpdates.reserve(std::size(RunningAllocationPreemptibleProgressStartTimes_));

    for (auto [allocationId, preemptibleProgressStartTime] : RunningAllocationPreemptibleProgressStartTimes_) {
        runningAllocationTimeStatisticsUpdates.push_back({
            .AllocationId = allocationId,
            .PreemptibleProgressStartTime = preemptibleProgressStartTime});
    }

    if (std::empty(runningAllocationTimeStatisticsUpdates)) {
        YT_LOG_DEBUG("No running allocation statistics received since last sending");
        return;
    }

    YT_LOG_DEBUG("Send running allocation statistics updates (UpdateCount: %v)", std::size(runningAllocationTimeStatisticsUpdates));

    Host_->UpdateRunningAllocationsStatistics(std::move(runningAllocationTimeStatisticsUpdates));
    RunningAllocationPreemptibleProgressStartTimes_.clear();
}

void TOperationControllerBase::RemoveRemainingJobsOnOperationFinished()
{
    YT_ASSERT_INVOKER_POOL_AFFINITY(CancelableInvokerPool_);

    EAbortReason jobAbortReason = [this] {
        auto operationControllerState = State_.load();

        switch (operationControllerState) {
            case EControllerState::Aborted:
                return EAbortReason::OperationAborted;
            case EControllerState::Completed:
                return EAbortReason::OperationCompleted;
            case EControllerState::Failed:
                return EAbortReason::OperationFailed;
            default:
                YT_LOG_DEBUG(
                    "Operation controller is not in finished state (State: %v)",
                    operationControllerState);
                return EAbortReason::OperationFailed;
        }
    }();

    // NB(pogorelov): We should not abort jobs honestly when operation is failed because invariants may be violated.
    // Also there is no meaning to abort jobs honestly when operation is finished.
    AbortAllJoblets(jobAbortReason, /*honestly*/ false);

    auto headCookie = CompletedJobIdsReleaseQueue_.Checkpoint();
    YT_LOG_INFO(
        "Releasing jobs on controller finish (HeadCookie: %v)",
        headCookie);
    auto jobIdsToRelease = CompletedJobIdsReleaseQueue_.Release();
    ReleaseJobs(jobIdsToRelease);
}

void TOperationControllerBase::OnOperationReady() const
{
    YT_ASSERT_INVOKER_POOL_AFFINITY(InvokerPool_);

    std::vector<TStartedAllocationInfo> revivedAllocations;
    revivedAllocations.reserve(size(AllocationMap_));

    for (const auto& [allocationId, allocation] : AllocationMap_) {
        TStartedAllocationInfo revivedAllocationInfo{
            .AllocationId = allocationId,
            .NodeAddress = NNodeTrackerClient::GetDefaultAddress(allocation.Joblet->NodeDescriptor.Addresses),
        };

        if (allocation.Joblet) {
            revivedAllocationInfo.StartedJobInfo = TStartedJobInfo{
                .JobId = allocation.Joblet->JobId,
            };

            GetJobProfiler()->ProfileRevivedJob(*allocation.Joblet);
        }

        revivedAllocations.push_back(std::move(revivedAllocationInfo));
    }

    YT_LOG_DEBUG("Registering revived allocations and jobs in job tracker (AllocationCount: %v)", std::size(revivedAllocations));

    Host_->Revive(std::move(revivedAllocations));
}

void TOperationControllerBase::OnOperationRevived()
{
    YT_ASSERT_INVOKER_POOL_AFFINITY(InvokerPool_);
}

void TOperationControllerBase::BuildControllerInfoYson(TFluentMap fluent) const
{
    YT_ASSERT_INVOKER_POOL_AFFINITY(InvokerPool_);

    std::set<TClusterName> networkBandwidthAvailability;
    if (Spec_->UseClusterThrottlers) {
        for (const auto& task : GetTasks()) {
            auto availability = task->GetClusterToNetworkBandwidthAvailability();
            for (const auto& [cluster, isAvailable] : availability) {
                // Do not overwrite false by true from another task. We need only false values here any way.
                if (isAvailable) {
                    continue;
                }
                networkBandwidthAvailability.insert(cluster);
            }
        }
    }

    fluent
        .Item("network_bandwidth_availability")
            .DoMapFor(networkBandwidthAvailability, [] (TFluentMap fluent, const TClusterName& clusterName) {
                fluent.Item(clusterName.Underlying()).Value(false);
            });
}

bool TOperationControllerBase::ShouldProcessJobEvents() const
{
    return State_ == EControllerState::Running || State_ == EControllerState::Failing;
}

void TOperationControllerBase::InterruptJob(TJobId jobId, EInterruptionReason interruptionReason, TDuration timeout)
{
    Host_->InterruptJob(jobId, interruptionReason, timeout);
}

std::unique_ptr<TAbortedJobSummary> TOperationControllerBase::RegisterOutputChunkReplicas(
    const TJobSummary& jobSummary,
    const NChunkClient::NProto::TChunkSpec& chunkSpec)
{
    YT_ASSERT_INVOKER_POOL_AFFINITY(CancelableInvokerPool_);

    const auto& globalNodeDirectory = Host_->GetNodeDirectory();

    auto replicas = GetReplicasFromChunkSpec(chunkSpec);
    for (auto replica : replicas) {
        auto nodeId = replica.GetNodeId();
        if (OutputNodeDirectory_->FindDescriptor(nodeId)) {
            continue;
        }

        const auto* descriptor = globalNodeDirectory->FindDescriptor(nodeId);
        if (!descriptor) {
            YT_LOG_DEBUG("Job is considered aborted since its output contains unresolved node id "
                "(JobId: %v, NodeId: %v)",
                jobSummary.Id,
                nodeId);
            return std::make_unique<TAbortedJobSummary>(jobSummary, EAbortReason::UnresolvedNodeId);
        }

        OutputNodeDirectory_->AddDescriptor(nodeId, *descriptor);
    }

    return nullptr;
}

PHOENIX_DEFINE_TYPE(TOperationControllerBase);
PHOENIX_DEFINE_TYPE(TOperationControllerBase::TLivePreviewChunkDescriptor);
PHOENIX_DEFINE_TYPE(TOperationControllerBase::TResourceUsageLeaseInfo);

////////////////////////////////////////////////////////////////////////////////

TOperationControllerBase::TCachedYsonCallback::TCachedYsonCallback(TDuration period, TCallback callback)
    : UpdatePeriod_(period)
    , Callback_(std::move(callback))
{ }

const NYson::TYsonString& TOperationControllerBase::TCachedYsonCallback::GetValue()
{
    auto now = GetInstant();
    if (UpdateTime_ + UpdatePeriod_ < now) {
        Value_ = Callback_();
        UpdateTime_ = now;
    }
    return Value_;
}

void TOperationControllerBase::TCachedYsonCallback::Flush()
{
    UpdateTime_ = TInstant::Zero();
}

////////////////////////////////////////////////////////////////////////////////

int TOperationControllerBase::GetYsonNestingLevelLimit() const
{
    return Host_
        ->GetClient()
        ->GetNativeConnection()
        ->GetConfig()
        ->CypressWriteYsonNestingLevelLimit;
}

template <typename T>
TYsonString TOperationControllerBase::ConvertToYsonStringNestingLimited(const T& value) const
{
    return NYson::ConvertToYsonStringNestingLimited(value, GetYsonNestingLevelLimit());
}

i64 TOperationControllerBase::GetFastIntermediateMediumLimit() const
{
    return FastIntermediateMediumLimit_;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NControllerAgent::NControllers
