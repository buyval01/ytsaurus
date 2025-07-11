LIBRARY()

GENERATE_ENUM_SERIALIZATION(private/raw_transform.h)

SRCS(
    private/attributes.cpp
    private/co_group_by_key.cpp
    private/combine.cpp
    private/dummy_pipeline.cpp
    private/fn_attributes_ops.cpp
    private/group_by_key.cpp
    private/merge_par_dos.cpp
    private/raw_transform.cpp
    private/raw_coder.cpp
    private/raw_multi_write.cpp
    private/raw_data_flow.cpp
    private/raw_par_do.cpp
    private/raw_pipeline.cpp
    private/row_vtable.cpp
    private/stateful_par_do.cpp
    private/stateful_timer_par_do.cpp
    private/tags.cpp
    private/par_do_tree.cpp
    private/save_loadable_logicaltype_wrapper.cpp
    private/save_loadable_tableschema_wrapper.cpp

    coder.cpp
    fns.cpp
    input.cpp
    output.cpp
    roren.cpp
    transforms.cpp
    type_tag.cpp
    execution_context.cpp
)

PEERDIR(
    library/cpp/iterator
    library/cpp/yson/node
    library/cpp/yt/string
    yt/yt/flow/lib/serializer
    yt/cpp/roren/library/bind
    yt/cpp/roren/library/timers/timer
    yt/yt/core
    yt/yt/library/profiling
    yt/yt/library/profiling/sensors_owner
)

END()

RECURSE_FOR_TESTS(ut)
