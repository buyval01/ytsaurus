#include "global.h"

#include <library/cpp/testing/unittest/registar.h>

using namespace NSQLComplete;

Y_UNIT_TEST_SUITE(GlobalAnalysisTests) {

    Y_UNIT_TEST(TopLevelNamesCollected) {
        IGlobalAnalysis::TPtr global = MakeGlobalAnalysis();

        TString query = R"(
            DECLARE $cluster_name AS String;

            IMPORT math SYMBOLS $sqrt, $pow;

            $sqrt = 0;

            DEFINE ACTION $hello_world($name, $suffix?) AS
                $name = $name ?? ($suffix ?? "world");
                SELECT "Hello, " || $name || "!";
            END DEFINE;

            $first, $second, $_ = AsTuple(1, 2, 3);
        )";

        TGlobalContext ctx = global->Analyze({query}, {});
        Sort(ctx.Names);

        TVector<TString> expected = {
            "cluster_name",
            "first",
            "hello_world",
            "pow",
            "second",
            "sqrt",
        };
        UNIT_ASSERT_VALUES_EQUAL(ctx.Names, expected);
    }

    Y_UNIT_TEST(LocalNamesCollected) {
        IGlobalAnalysis::TPtr global = MakeGlobalAnalysis();

        TString query = R"(
            DEFINE ACTION $sum($x, $y) AS
                $acc = 0;
                EVALUATE FOR $i IN AsList($x, $y) DO BEGIN
                    $plus = ($a, $b) -> (#);
                    $acc = $plus($acc, $i);
                END DO;
            END DEFINE;
        )";

        TCompletionInput input = SharpedInput(query);

        TGlobalContext ctx = global->Analyze(input, {});
        Sort(ctx.Names);

        TVector<TString> expected = {
            "a",
            "acc",
            "b",
            "i",
            "plus",
            "sum",
            "x",
            "y",
        };
        UNIT_ASSERT_VALUES_EQUAL(ctx.Names, expected);
    }

    Y_UNIT_TEST(EnclosingFunctionName) {
        IGlobalAnalysis::TPtr global = MakeGlobalAnalysis();
        {
            TString query = "SELECT * FROM Concat(#)";
            TGlobalContext ctx = global->Analyze(SharpedInput(query), {});
            UNIT_ASSERT_VALUES_EQUAL(ctx.EnclosingFunction, "Concat");
        }
        {
            TString query = "SELECT * FROM Concat(a, #)";
            TGlobalContext ctx = global->Analyze(SharpedInput(query), {});
            UNIT_ASSERT_VALUES_EQUAL(ctx.EnclosingFunction, "Concat");
        }
        {
            TString query = "SELECT * FROM Concat(a#)";
            TGlobalContext ctx = global->Analyze(SharpedInput(query), {});
            UNIT_ASSERT_VALUES_EQUAL(ctx.EnclosingFunction, "Concat");
        }
        {
            TString query = "SELECT * FROM Concat(#";
            TGlobalContext ctx = global->Analyze(SharpedInput(query), {});
            UNIT_ASSERT_VALUES_EQUAL(ctx.EnclosingFunction, Nothing());
        }
        {
            TString query = "SELECT * FROM (#)";
            TGlobalContext ctx = global->Analyze(SharpedInput(query), {});
            UNIT_ASSERT_VALUES_EQUAL(ctx.EnclosingFunction, Nothing());
        }
    }

    Y_UNIT_TEST(SimpleSelectFrom) {
        IGlobalAnalysis::TPtr global = MakeGlobalAnalysis();
        {
            TString query = "SELECT # FROM plato.Input";

            TGlobalContext ctx = global->Analyze(SharpedInput(query), {});

            TColumnContext expected = {.Tables = {TTableId{"plato", "Input"}}};
            UNIT_ASSERT_VALUES_EQUAL(ctx.Column, expected);
        }
        {
            TString query = "SELECT # FROM plato.`//home/input`";

            TGlobalContext ctx = global->Analyze(SharpedInput(query), {});

            TColumnContext expected = {.Tables = {TTableId{"plato", "//home/input"}}};
            UNIT_ASSERT_VALUES_EQUAL(ctx.Column, expected);
        }
        {
            TString query = "SELECT # FROM plato.Input AS x";

            TGlobalContext ctx = global->Analyze(SharpedInput(query), {});

            TColumnContext expected = {.Tables = {TAliased<TTableId>("x", TTableId{"plato", "Input"})}};
            UNIT_ASSERT_VALUES_EQUAL(ctx.Column, expected);
        }
    }

    Y_UNIT_TEST(Join) {
        IGlobalAnalysis::TPtr global = MakeGlobalAnalysis();
        {
            TString query = R"(
                SELECT #
                FROM q.a AS x, p.b, c
                JOIN p.d AS y ON x.key = y.key
            )";

            TGlobalContext ctx = global->Analyze(SharpedInput(query), {});

            TColumnContext expected = {
                .Tables = {
                    TAliased<TTableId>("", {"", "c"}),
                    TAliased<TTableId>("", {"p", "b"}),
                    TAliased<TTableId>("x", {"q", "a"}),
                    TAliased<TTableId>("y", {"p", "d"}),
                },
            };
            UNIT_ASSERT_VALUES_EQUAL(ctx.Column, expected);
        }
    }

    Y_UNIT_TEST(Subquery) {
        IGlobalAnalysis::TPtr global = MakeGlobalAnalysis();
        {
            TString query = "SELECT # FROM (SELECT * FROM x)";

            TGlobalContext ctx = global->Analyze(SharpedInput(query), {});

            TColumnContext expected = {.Tables = {TAliased<TTableId>("", {"", "x"})}};
            UNIT_ASSERT_VALUES_EQUAL(ctx.Column, expected);
        }
        {
            TString query = "SELECT # FROM (SELECT a, b FROM x)";

            TGlobalContext ctx = global->Analyze(SharpedInput(query), {});

            TColumnContext expected = {.Columns = {{.Name = "a"}, {.Name = "b"}}};
            UNIT_ASSERT_VALUES_EQUAL(ctx.Column, expected);
        }
        {
            TString query = "SELECT # FROM (SELECT 1 AS a, 2 AS b)";

            TGlobalContext ctx = global->Analyze(SharpedInput(query), {});

            TColumnContext expected = {.Columns = {{.Name = "a"}, {.Name = "b"}}};
            UNIT_ASSERT_VALUES_EQUAL(ctx.Column, expected);
        }
        {
            TString query = "SELECT # FROM (SELECT 1 AS a, 2 AS b) AS x";

            TGlobalContext ctx = global->Analyze(SharpedInput(query), {});

            TColumnContext expected = {.Columns = {{"x", "a"}, {"x", "b"}}};
            UNIT_ASSERT_VALUES_EQUAL(ctx.Column, expected);
        }
        {
            TString query = R"(
                SELECT #
                FROM (SELECT * FROM example.`/people`) AS ep
                JOIN (SELECT room AS Room, time FROM example.`/yql/tutorial`) AS et ON 1 = 1
            )";

            TGlobalContext ctx = global->Analyze(SharpedInput(query), {});

            TColumnContext expected = {
                .Tables = {
                    TAliased<TTableId>("ep", {"example", "/people"}),
                },
                .Columns = {
                    {.TableAlias = "et", .Name = "Room"},
                    {.TableAlias = "et", .Name = "time"},
                },
            };
            UNIT_ASSERT_VALUES_EQUAL(ctx.Column, expected);
        }
    }

    Y_UNIT_TEST(Projection) {
        IGlobalAnalysis::TPtr global = MakeGlobalAnalysis();
        {
            TString query = "SELECT a, b, # FROM x";

            TGlobalContext ctx = global->Analyze(SharpedInput(query), {});

            TColumnContext expected = {.Tables = {TAliased<TTableId>("", {"", "x"})}};
            UNIT_ASSERT_VALUES_EQUAL(ctx.Column, expected);
        }
    }

} // Y_UNIT_TEST_SUITE(GlobalAnalysisTests)
