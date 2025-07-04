LIBRARY()

PEERDIR(
    contrib/ydb/library/actors/core
    contrib/ydb/library/yql/dq/actors/compute
    contrib/ydb/library/yql/dq/runtime
    yql/essentials/minikql/comp_nodes
    yql/essentials/minikql/computation
    yql/essentials/utils
)

SRCS(
    yql_common_dq_factory.cpp
    dq_hash_aggregate.cpp
    dq_hash_combine.cpp
    dq_hash_operator_common.cpp
    dq_hash_operator_serdes.cpp
    dq_program_builder.cpp
)

YQL_LAST_ABI_VERSION()


END()

