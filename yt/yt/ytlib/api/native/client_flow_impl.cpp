#include "client_impl.h"

#include "config.h"

#include <yt/yt/ytlib/node_tracker_client/channel.h>

#include <yt/yt/flow/lib/client/public.h>

#include <yt/yt/core/ytree/convert.h>

#include <yt/yt/core/yson/protobuf_helpers.h>

namespace NYT::NApi::NNative {

using namespace NConcurrency;
using namespace NYPath;
using namespace NYTree;
using namespace NYson;
using namespace NFlow;
using namespace NFlow::NController;

////////////////////////////////////////////////////////////////////////////////

TString TClient::DiscoverPipelineControllerLeader(const TYPath& pipelinePath)
{
    YT_LOG_DEBUG("Started discovering pipeline controller leader (PipelinePath: %v)",
        pipelinePath);

    TGetNodeOptions options{
        .Attributes = TAttributeFilter(
            {
                PipelineFormatVersionAttribute,
                LeaderControllerAddressAttribute,
            }),
    };

    auto str = WaitFor(GetNode(pipelinePath, options))
        .ValueOrThrow();

    auto node = ConvertToNode(str);
    const auto& attributes = node->Attributes();

    if (!attributes.Contains(PipelineFormatVersionAttribute)) {
        THROW_ERROR_EXCEPTION("%v is not a valid pipeline; missing attribute %Qv",
            pipelinePath,
            PipelineFormatVersionAttribute);
    }

    if (auto version = attributes.Get<int>(PipelineFormatVersionAttribute); version != CurrentPipelineFormatVersion) {
        THROW_ERROR_EXCEPTION("Invalid pipeline format version: expected %v, got %v",
            CurrentPipelineFormatVersion,
            version);
    }

    auto address = attributes.Get<TString>(LeaderControllerAddressAttribute);

    YT_LOG_DEBUG("Finished discovering pipeline controller leader (PipelinePath: %v, Address: %v)",
        pipelinePath,
        address);

    return address;
}

TControllerServiceProxy TClient::CreatePipelineControllerLeaderProxy(const TYPath& pipelinePath)
{
    auto address = DiscoverPipelineControllerLeader(pipelinePath);
    auto channel = ChannelFactory_->CreateChannel(address);
    TControllerServiceProxy proxy(std::move(channel));
    proxy.SetDefaultTimeout(Connection_->GetConfig()->FlowPipelineControllerRpcTimeout);
    return proxy;
}

TGetPipelineSpecResult TClient::DoGetPipelineSpec(
    const NYPath::TYPath& pipelinePath,
    const TGetPipelineSpecOptions& options)
{
    auto proxy = CreatePipelineControllerLeaderProxy(pipelinePath);
    auto req = proxy.GetSpec();
    if (options.Timeout) {
        req->SetTimeout(options.Timeout);
    }
    auto rsp = WaitFor(req->Invoke())
        .ValueOrThrow();
    return {
        .Version = FromProto<TVersion>(rsp->version()),
        .Spec = TYsonString(rsp->spec()),
    };
}

TSetPipelineSpecResult TClient::DoSetPipelineSpec(
    const NYPath::TYPath& pipelinePath,
    const NYson::TYsonString& spec,
    const TSetPipelineSpecOptions& options)
{
    auto proxy = CreatePipelineControllerLeaderProxy(pipelinePath);
    auto req = proxy.SetSpec();
    if (options.Timeout) {
        req->SetTimeout(options.Timeout);
    }
    req->set_spec(ToProto(spec));
    req->set_force(options.Force);
    if (options.ExpectedVersion) {
        req->set_expected_version(ToProto(*options.ExpectedVersion));
    }
    auto rsp = WaitFor(req->Invoke())
        .ValueOrThrow();
    return {
        .Version = FromProto<TVersion>(rsp->version()),
    };
}

TGetPipelineDynamicSpecResult TClient::DoGetPipelineDynamicSpec(
    const NYPath::TYPath& pipelinePath,
    const TGetPipelineDynamicSpecOptions& options)
{
    auto proxy = CreatePipelineControllerLeaderProxy(pipelinePath);
    auto req = proxy.GetDynamicSpec();
    if (options.Timeout) {
        req->SetTimeout(options.Timeout);
    }
    auto rsp = WaitFor(req->Invoke())
        .ValueOrThrow();
    return {
        .Version = FromProto<TVersion>(rsp->version()),
        .Spec = TYsonString(rsp->spec()),
    };
}

TSetPipelineDynamicSpecResult TClient::DoSetPipelineDynamicSpec(
    const NYPath::TYPath& pipelinePath,
    const NYson::TYsonString& spec,
    const TSetPipelineDynamicSpecOptions& options)
{
    auto proxy = CreatePipelineControllerLeaderProxy(pipelinePath);
    auto req = proxy.SetDynamicSpec();
    if (options.Timeout) {
        req->SetTimeout(options.Timeout);
    }
    req->set_spec(ToProto(spec));
    if (options.ExpectedVersion) {
        req->set_expected_version(ToProto(*options.ExpectedVersion));
    }
    auto rsp = WaitFor(req->Invoke())
        .ValueOrThrow();
    return {
        .Version = FromProto<TVersion>(rsp->version()),
    };
}

void TClient::DoStartPipeline(
    const TYPath& pipelinePath,
    const TStartPipelineOptions& options)
{
    auto proxy = CreatePipelineControllerLeaderProxy(pipelinePath);
    auto req = proxy.StartPipeline();
    if (options.Timeout) {
        req->SetTimeout(options.Timeout);
    }
    WaitFor(req->Invoke())
        .ThrowOnError();
}

void TClient::DoStopPipeline(
    const TYPath& pipelinePath,
    const TStopPipelineOptions& options)
{
    auto proxy = CreatePipelineControllerLeaderProxy(pipelinePath);
    auto req = proxy.StopPipeline();
    if (options.Timeout) {
        req->SetTimeout(options.Timeout);
    }
    WaitFor(req->Invoke())
        .ThrowOnError();
}

void TClient::DoPausePipeline(
    const TYPath& pipelinePath,
    const TPausePipelineOptions& options)
{
    auto proxy = CreatePipelineControllerLeaderProxy(pipelinePath);
    auto req = proxy.PausePipeline();
    if (options.Timeout) {
        req->SetTimeout(options.Timeout);
    }
    WaitFor(req->Invoke())
        .ThrowOnError();
}

TPipelineState TClient::DoGetPipelineState(
    const TYPath& pipelinePath,
    const TGetPipelineStateOptions& options)
{
    auto proxy = CreatePipelineControllerLeaderProxy(pipelinePath);
    auto req = proxy.GetPipelineState();
    if (options.Timeout) {
        req->SetTimeout(options.Timeout);
    }
    auto rsp = WaitFor(req->Invoke())
        .ValueOrThrow();
    return {
        .State = FromProto<EPipelineState>(rsp->state()),
    };
}

TGetFlowViewResult TClient::DoGetFlowView(
    const TYPath& pipelinePath,
    const TYPath& viewPath,
    const TGetFlowViewOptions& options)
{
    auto proxy = CreatePipelineControllerLeaderProxy(pipelinePath);
    auto req = proxy.GetFlowView();
    if (options.Timeout) {
        req->SetTimeout(options.Timeout);
    }
    req->set_path(viewPath);
    req->set_cache(options.Cache);
    auto rsp = WaitFor(req->Invoke())
        .ValueOrThrow();
    return {
        .FlowViewPart = TYsonString(rsp->flow_view_part()),
    };
}

TFlowExecuteResult TClient::DoFlowExecute(
    const NYPath::TYPath& pipelinePath,
    const TString& command,
    const NYson::TYsonString& argument,
    const TFlowExecuteOptions& options)
{
    auto proxy = CreatePipelineControllerLeaderProxy(pipelinePath);
    auto req = proxy.FlowExecute();
    if (options.Timeout) {
        req->SetTimeout(options.Timeout);
    }
    req->set_command(command);
    if (argument) {
        req->set_argument(ToProto(argument));
    }
    auto rsp = WaitFor(req->Invoke())
        .ValueOrThrow();
    return {
        .Result = rsp->has_result() ? TYsonString(rsp->result()) : TYsonString{},
    };
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi::NNative
