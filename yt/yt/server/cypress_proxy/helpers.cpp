#include "helpers.h"

#include "path_resolver.h"

#include <yt/yt/ytlib/cypress_client/rpc_helpers.h>

#include <yt/yt/ytlib/cypress_client/proto/cypress_ypath.pb.h>

#include <yt/yt/ytlib/object_client/master_ypath_proxy.h>

#include <yt/yt/ytlib/object_client/proto/object_ypath.pb.h>

#include <yt/yt/ytlib/sequoia_client/client.h>
#include <yt/yt/ytlib/sequoia_client/transaction.h>
#include <yt/yt/ytlib/sequoia_client/ypath_detail.h>

#include <yt/yt/ytlib/sequoia_client/records/transactions.record.h>

#include <yt/yt/client/object_client/helpers.h>

#include <yt/yt/core/yson/attribute_consumer.h>

#include <yt/yt/core/ytree/ypath_detail.h>

#include <library/cpp/yt/misc/variant.h>

#include <library/cpp/iterator/zip.h>

namespace NYT::NCypressProxy {

using namespace NApi;
using namespace NConcurrency;
using namespace NCypressClient;
using namespace NCypressClient::NProto;
using namespace NObjectClient;
using namespace NRpc;
using namespace NSequoiaClient;
using namespace NYPath;
using namespace NYTree;

using NYT::FromProto;

////////////////////////////////////////////////////////////////////////////////

TError WrapCypressProxyRegistrationError(TError error)
{
    if (error.IsOK()) {
        return error;
    }

    return TError(NRpc::EErrorCode::Unavailable, "Cypress proxy is not registered")
        << std::move(error);
}

////////////////////////////////////////////////////////////////////////////////

void SetAccessTrackingOptions(
    const IClientRequestPtr& request,
    const TSuppressableAccessTrackingOptions& commandOptions)
{
    if (commandOptions.SuppressAccessTracking) {
        NCypressClient::SetSuppressAccessTracking(request, true);
    }
    if (commandOptions.SuppressModificationTracking) {
        NCypressClient::SetSuppressModificationTracking(request, true);
    }
    if (commandOptions.SuppressExpirationTimeoutRenewal) {
        NCypressClient::SetSuppressExpirationTimeoutRenewal(request, true);
    }
}

////////////////////////////////////////////////////////////////////////////////

namespace {

TAbsolutePath GetTargetPathOrThrow(const TResolveResult& resolveResult)
{
    return Visit(resolveResult,
        [] (const TCypressResolveResult& resolveResult) -> TAbsolutePath {
            // NB: Cypress resolve result doesn't contain unresolved links.
            return TAbsolutePath::MakeCanonicalPathOrThrow(resolveResult.Path);
        },
        [] (const TMasterResolveResult& /*resolveResult*/) -> TAbsolutePath {
            // NB: Master resolve result is uncurated, it's unwise to attempt to parse it.
            Y_UNREACHABLE();
        },
        [] (const TSequoiaResolveResult& resolveResult) -> TAbsolutePath {
            // We don't want to distinguish "//tmp/a&/my-link" from
            // "//tmp/a/my-link".
            return PathJoin(
                resolveResult.Path,
                TRelativePath::MakeCanonicalPathOrThrow(resolveResult.UnresolvedSuffix));
        });
}

} // namespace

void ValidateLinkNodeCreation(
    const TSequoiaSessionPtr& session,
    const TYPath& targetPath,
    const TResolveResult& resolveResult)
{
    // TODO(danilalexeev): In case of a master-object root designator the
    // following resolve will not produce a meaningful result. Such YPath has to
    // be resolved by master first.
    // TODO(kvk1920): probably works (since links are stored in both resolve
    // tables now), but has to be tested.
    auto linkPath = GetTargetPathOrThrow(resolveResult);

    auto checkAcyclicity = [&] (
        TYPath pathToResolve,
        const TAbsolutePath& forbiddenPrefix)
    {
        std::vector<TSequoiaResolveIterationResult> history;
        auto resolveResult = ResolvePath(
            session,
            std::move(pathToResolve),
            /*service*/ {},
            /*method*/ {},
            &history);

        for (const auto& [id, path] : history) {
            if (IsLinkType(TypeFromId(id)) && path == forbiddenPrefix) {
                return false;
            }
        }

        return GetTargetPathOrThrow(resolveResult) != forbiddenPrefix;
    };

    if (!checkAcyclicity(targetPath, linkPath)) {
        THROW_ERROR_EXCEPTION("Failed to create link: link is cyclic")
            << TErrorAttribute("target_path", targetPath)
            << TErrorAttribute("path", linkPath);
    }
}

std::vector<TTransactionId> ParsePrerequisiteTransactionIds(const NRpc::NProto::TRequestHeader& header)
{
    const auto prerequisitesExt = NObjectClient::NProto::TPrerequisitesExt::prerequisites_ext;
    if (!header.HasExtension(prerequisitesExt)) {
        return {};
    }

    auto prerequisites = header.GetExtension(prerequisitesExt);
    std::vector<TTransactionId> prerequisiteTransactionIds;
    prerequisiteTransactionIds.reserve(prerequisites.transactions_size());
    for (const auto& protoTransaction : prerequisites.transactions()) {
        auto transactionId = FromProto<TTransactionId>(protoTransaction.transaction_id());
        prerequisiteTransactionIds.push_back(transactionId);
    }
    return prerequisiteTransactionIds;
}

void ValidatePrerequisiteTransactions(
    const ISequoiaClientPtr& sequoiaClient,
    const std::vector<TTransactionId>& prerequisiteTransactionIds)
{
    // Fast path.
    if (prerequisiteTransactionIds.empty()) {
        return;
    }

    std::vector<NRecords::TTransactionKey> transactionKeys;
    transactionKeys.reserve(prerequisiteTransactionIds.size());
    for (const auto& transactionId : prerequisiteTransactionIds) {
        if (!IsCypressTransactionMirroredToSequoia(transactionId)) {
            THROW_ERROR_EXCEPTION("Non-mirrored transaction %v found in prerequisites", transactionId);
        }
        transactionKeys.push_back({.TransactionId = transactionId});
    }

    auto transactionRowsOrError = WaitFor(sequoiaClient->LookupRows(transactionKeys));
    THROW_ERROR_EXCEPTION_IF_FAILED(transactionRowsOrError, "Failed to check prerequisite transactions")

    auto transactionRows = transactionRowsOrError.Value();
    for (const auto& [key, row] : Zip(transactionKeys, transactionRows)) {
        if (!row.has_value()) {
            THROW_ERROR_EXCEPTION(
                NObjectClient::EErrorCode::PrerequisiteCheckFailed,
                "Prerequisite check failed: transaction %v is missing in Sequoia",
                key.TransactionId);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

std::pair<TRootDesignator, TYPathBuf> GetRootDesignator(TYPathBuf path)
{
    NYPath::TTokenizer tokenizer(path);
    tokenizer.Advance();
    switch (tokenizer.GetType()) {
        case NYPath::ETokenType::Slash:
            return {TSlashRootDesignatorTag{}, TYPathBuf(tokenizer.GetSuffix())};
        case NYPath::ETokenType::Literal: {
            auto token = tokenizer.GetToken();
            if (!token.StartsWith(NObjectClient::ObjectIdPathPrefix)) {
                tokenizer.ThrowUnexpected();
            }

            TStringBuf objectIdString(token.begin() + 1, token.end());
            NCypressClient::TObjectId objectId;
            if (!NCypressClient::TObjectId::FromString(objectIdString, &objectId)) {
                THROW_ERROR_EXCEPTION(
                    NYTree::EErrorCode::ResolveError,
                    "Error parsing object id %Qv in path %v",
                    objectIdString,
                    path);
            }
            return {objectId, TYPathBuf(tokenizer.GetSuffix())};
        }
        default:
            tokenizer.ThrowUnexpected();
    }
    Y_UNREACHABLE();
}

////////////////////////////////////////////////////////////////////////////////

std::vector<std::string> TokenizeUnresolvedSuffix(TYPathBuf unresolvedSuffix)
{
    constexpr auto TypicalPathTokenCount = 3;
    std::vector<std::string> pathTokens;
    pathTokens.reserve(TypicalPathTokenCount);

    TTokenizer tokenizer(unresolvedSuffix);
    tokenizer.Advance();

    while (tokenizer.GetType() != ETokenType::EndOfStream) {
        tokenizer.Expect(ETokenType::Slash);
        tokenizer.Advance();
        tokenizer.Expect(ETokenType::Literal);
        pathTokens.push_back(tokenizer.GetLiteralValue());
        tokenizer.Advance();
    }

    return pathTokens;
}

TAbsolutePath JoinNestedNodesToPath(
    const TAbsolutePath& parentPath,
    const std::vector<std::string>& childKeys)
{
    auto result = parentPath;
    for (const auto& key : childKeys) {
        result.Append(key);
    }
    return result;
}

////////////////////////////////////////////////////////////////////////////////

bool IsSupportedSequoiaType(EObjectType type)
{
    return IsSequoiaCompositeNodeType(type) ||
        IsScalarType(type) ||
        IsChunkOwnerType(type) ||
        type == EObjectType::SequoiaLink ||
        type == EObjectType::Document ||
        type == EObjectType::Orchid;
}

bool IsSequoiaCompositeNodeType(EObjectType type)
{
    return type == EObjectType::SequoiaMapNode || type == EObjectType::Scion;
}

void ValidateSupportedSequoiaType(EObjectType type)
{
    if (!IsSupportedSequoiaType(type)) {
        THROW_ERROR_EXCEPTION(
            "Object type %Qlv is not supported in Sequoia yet",
            type);
    }
}

void ThrowAlreadyExists(const TAbsolutePath& path)
{
    THROW_ERROR_EXCEPTION(
        NYTree::EErrorCode::AlreadyExists,
        "Node %v already exists",
        path);
}

void ThrowCannotHaveChildren(const TAbsolutePath& path)
{
    THROW_ERROR_EXCEPTION("%v cannot have children", path);
}

void ThrowCannotReplaceNode(const TAbsolutePath& path)
{
    THROW_ERROR_EXCEPTION("%v cannot be replaced", path);
}

void ThrowNoSuchChild(const TAbsolutePath& existingPath, TStringBuf missingPath)
{
    THROW_ERROR_EXCEPTION(
        NYTree::EErrorCode::ResolveError,
        "Node %v has no child with key %Qv",
        existingPath,
        missingPath);
}

////////////////////////////////////////////////////////////////////////////////

std::optional<TParsedReqCreate> TryParseReqCreate(const ISequoiaServiceContextPtr& context)
{
    YT_VERIFY(context->GetRequestHeader().method() == "Create");

    auto typedContext = New<NRpc::TGenericTypedServiceContext<
        IServiceContext,
        TServiceContextWrapper,
        TReqCreate,
        TRspCreate>>(
        std::move(context),
        THandlerInvocationOptions{});

    // NB: This replies to underlying context on error.
    if (!typedContext->DeserializeRequest()) {
        return std::nullopt;
    }

    const auto& request = typedContext->Request();

    try {
        return TParsedReqCreate{
            .Type = FromProto<EObjectType>(request.type()),
            .ExplicitAttributes = request.has_node_attributes()
                ? NYTree::FromProto(request.node_attributes())
                : CreateEphemeralAttributes(),
        };
    } catch (const std::exception& ex) {
        typedContext->Reply(ex);
        return std::nullopt;
    }
}

////////////////////////////////////////////////////////////////////////////////

void ConsumeAttributes(NYson::IAsyncYsonConsumer* consumer, const IAttributeDictionaryPtr& attributes)
{
    NYson::TAttributeFragmentConsumer attributeConsumer(consumer);
    for (const auto& [key, value] : attributes->ListPairs()) {
        attributeConsumer.OnKeyedItem(key);
        attributeConsumer.OnRaw(value);
    }
    attributeConsumer.Finish();
}

////////////////////////////////////////////////////////////////////////////////

void FromProto(TCopyOptions* options, const TReqCopy& protoOptions)
{
    options->Mode = FromProto<ENodeCloneMode>(protoOptions.mode());
    options->PreserveAcl = protoOptions.preserve_acl();
    options->PreserveAccount = protoOptions.preserve_account();
    options->PreserveOwner = protoOptions.preserve_owner();
    options->PreserveCreationTime = protoOptions.preserve_creation_time();
    options->PreserveModificationTime = protoOptions.preserve_modification_time();
    options->PreserveExpirationTime = protoOptions.preserve_expiration_time();
    options->PreserveExpirationTimeout = protoOptions.preserve_expiration_timeout();
    options->PessimisticQuotaCheck = protoOptions.pessimistic_quota_check();
}

void FromProto(
    TMultisetAttributesSubrequest* subrequest,
    const NYTree::NProto::TReqMultisetAttributes::TSubrequest& protoSubrequest)
{
    subrequest->AttributeKey = protoSubrequest.attribute();
    subrequest->Value = NYson::TYsonString(protoSubrequest.value());
}

////////////////////////////////////////////////////////////////////////////////

TFuture<NYTree::INodePtr> FetchSingleObject(
    const NNative::IClientPtr& client,
    TVersionedObjectId objectId,
    const TAttributeFilter& attributeFilter)
{
    auto request = TYPathProxy::Get();

    if (attributeFilter) {
        ToProto(request->mutable_attributes(), attributeFilter);
    }

    auto batcher = TMasterYPathProxy::CreateGetBatcher(client, request, {objectId.ObjectId}, objectId.TransactionId);

    return batcher.Invoke().Apply(BIND([=] (const TMasterYPathProxy::TVectorizedGetBatcher::TVectorizedResponse& rsp) {
        return ConvertToNode(NYson::TYsonString(rsp.at(objectId.ObjectId).ValueOrThrow()->value()));
    }));
}

////////////////////////////////////////////////////////////////////////////////

std::string GetRequestQueueNameForKey(const std::pair<std::string, EUserWorkloadType>& userNameAndWorkloadType)
{
    return Format(
        "%v_%v",
        userNameAndWorkloadType.first,
        CamelCaseToUnderscoreCase(TEnumTraits<EUserWorkloadType>::ToString(userNameAndWorkloadType.second)));
}

std::string GetDistributedWeightThrottlerId(const std::string& prefix)
{
    return prefix + "_weight_throttler";
}

////////////////////////////////////////////////////////////////////////////////

std::string BuildMultipleTransactionSelectCondition(TRange<TTransactionId> transactionIds)
{
    YT_VERIFY(!transactionIds.Empty());

    // NB: Null GUIDs may be stored as null instead of "0-0-0-0".
    auto formatTransactionId = [] (TStringBuilderBase* builder, TTransactionId transactionId) {
        if (!transactionId) {
            builder->AppendString("null, ");
        }
        builder->AppendFormat("%Qv", transactionId);
    };

    return Format("transaction_id in (%v)", MakeFormatterWrapper([&] (TStringBuilderBase* builder) {
        formatTransactionId(builder, transactionIds.Front());
        for (int i = 1; i < std::ssize(transactionIds); ++i) {
            builder->AppendString(", ");
            formatTransactionId(builder, transactionIds[i]);
        }
    }));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCypressProxy
