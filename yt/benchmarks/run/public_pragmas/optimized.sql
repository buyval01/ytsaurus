pragma AnsiInForEmptyOrNullableItemsCollections;
pragma AnsiOptionalAs;
--pragma CompactGroupBy;
pragma TablePathPrefix = "home/tpcds/3Tb";
pragma EmitUnionMerge;
pragma yt.UseIntermediateStreams;
pragma yt.EnableFuseMapToMapReduce = 'true';
pragma config.flags(
    "OptimizerFlags",
    -- "EmitPruneKeys",
    "FilterPushdownEnableMultiusage",
    "EarlyExpandSkipNull",
    "PushdownComplexFiltersOverAggregate",
    "ExtractCommonPredicatesFromLogicalOps",
    "PullUpExtendOverEquiJoin",
    "DisableEmitSkipNullOnPushDown");
pragma yt.AutoMerge = "disabled";
pragma yt.DataSizePerPartition = "64M";
pragma yt.DataSizePerJob = "64M";
pragma yt.DataSizePerMapJob = "64M";
pragma yt.HybridDqExecution = "false";
pragma dq.AnalyzeQuery = "false";
pragma yt.MapJoinLimit = "4G";
pragma yt.MaxReplicationFactorToFuseOperations="100";
pragma yt.PartitionByConstantKeysViaMap;
pragma yt.Pool = "tpcds";
pragma yt.QueryCacheMode = "disable";
pragma yt.SchedulingTagFilter = "%true";
pragma yt.TableContentLocalExecution;
pragma yt.UseNewPredicateExtraction="true";
pragma yt.LookupJoinMaxRows="2000";
pragma yt.MaxKeyRangeCount="2000";
pragma yt.MaxExtraJobMemoryToFuseOperations="4G";
pragma yt.JobBlockInput;
pragma yt.JobBlockTableContent;
pragma BlockEngine='auto';
pragma yt.BlockMapJoin;
pragma FilterPushdownOverJoinOptionalSide;
pragma yt.TableContentMinAvgChunkSize="0";
