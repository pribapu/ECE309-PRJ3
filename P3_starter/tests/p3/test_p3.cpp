// tests/p3/test_p3.cpp — P3 TEST SUITE.
//
// Build & run:   cmake --build build && ./build/test_p3
// One area:      ./build/test_p3 reg_        (names starting with "reg_")
// One test:      ./build/test_p3 =reg_add_and_find

#include "../mini_test.h"
#include "../test_helpers.h"     // TempDir, write_text, args1, ...

#include "tools/calculator.h"
#include "tools/tool_registry.h"
#include "tools/workspace.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// A tool whose name you choose — use it to fill the registry in tests.
class NamedTool : public Tool {
public:
    explicit NamedTool(std::string n) : n_(std::move(n)), schema_(JsonValue::object()) {}
    std::string name() const override { return n_; }
    std::string description() const override { return "test tool"; }
    const JsonValue& schema() const override { return schema_; }
    ToolResult invoke(const JsonValue&, ToolContext&) override { return ToolResult::success(n_); }
private:
    std::string n_;
    JsonValue   schema_;   // an empty JSON object: {}
};

// Tokenize with the provided tokenizer, then run YOUR calc_evaluate.
bool eval(const std::string& expr, double* value, std::string* error) {
    std::vector<CalcToken> tokens;
    if (!calc_tokenize(expr, &tokens, error)) return false;
    return calc_evaluate(tokens, value, error);
}

}  // namespace

// =================================================================== reg_

TEST(reg_add_and_find) {
    ToolRegistry r;
    auto t = std::make_unique<NamedTool>("alpha");
    Tool* raw = t.get();                        // remember the address before moving
    REQUIRE(r.add(std::move(t)));
    CHECK(r.size() == 1);
    CHECK(r.find("alpha") == raw);              // same object, not a copy
    CHECK(r.find("beta") == nullptr);
}

TEST(reg_duplicate_keeps_original) {
    ToolRegistry r;
    auto first = std::make_unique<NamedTool>("alpha");
    Tool* original = first.get();
    REQUIRE(r.add(std::move(first)));
    CHECK(!r.add(std::make_unique<NamedTool>("alpha")));
    CHECK(r.find("alpha") == original);
    CHECK(r.size() == 1);
}

TEST(reg_null_rejected) {
    ToolRegistry r;
    CHECK(!r.add(nullptr));
    CHECK(r.size() == 0);
}

TEST(reg_find_missing_returns_null) {
    ToolRegistry r;
    CHECK(r.find("anything") == nullptr);        // empty table
    REQUIRE(r.add(std::make_unique<NamedTool>("alpha")));
    CHECK(r.find("beta") == nullptr);             // non-empty, still absent
}

TEST(reg_size_tracks_additions) {
    ToolRegistry r;
    CHECK(r.size() == 0);
    REQUIRE(r.add(std::make_unique<NamedTool>("alpha")));
    CHECK(r.size() == 1);
    REQUIRE(r.add(std::make_unique<NamedTool>("beta")));
    CHECK(r.size() == 2);
    CHECK(!r.add(std::make_unique<NamedTool>("alpha")));   // duplicate: no change
    CHECK(r.size() == 2);
    CHECK(!r.add(nullptr));                                // null: no change
    CHECK(r.size() == 2);
}

TEST(reg_for_each_visits_each_once) {
    ToolRegistry r;
    const std::vector<std::string> names = {"alpha", "beta", "gamma", "delta"};
    for (const auto& n : names) REQUIRE(r.add(std::make_unique<NamedTool>(n)));

    std::vector<std::string> seen;
    int visits = 0;
    r.for_each([&](const Tool& t) {               // a lambda: runs once per tool
        seen.push_back(t.name());
        ++visits;                                  // [&] lets it change `visits`
    });

    CHECK(visits == static_cast<int>(names.size()));
    std::sort(seen.begin(), seen.end());
    std::vector<std::string> expected = names;
    std::sort(expected.begin(), expected.end());
    CHECK(seen == expected);
}

// Required: insert tool_0..tool_9999 and check scale behavior and pointer
// stability across the resizes that happen along the way.
TEST(reg_10000_tools) {
    ToolRegistry r;
    constexpr int kCount = 10000;

    auto first = std::make_unique<NamedTool>("tool_0");
    Tool* first_ptr = first.get();                 // address right after the first insert
    REQUIRE(r.add(std::move(first)));
    CHECK(r.find("tool_0") == first_ptr);

    for (int i = 1; i < kCount; ++i) {
        REQUIRE(r.add(std::make_unique<NamedTool>("tool_" + std::to_string(i))));
        CHECK(r.load_factor() <= 1.0);             // must hold after every add()
    }

    // Pointer stability: still the same address after 9999 more inserts,
    // which forced several rehashes.
    CHECK(r.find("tool_0") == first_ptr);

    for (int i = 0; i < kCount; ++i) {
        CHECK(r.find("tool_" + std::to_string(i)) != nullptr);
    }

    CHECK(r.size() == static_cast<std::size_t>(kCount));
    CHECK(r.max_probe_length() <= 64);

    std::printf("    [reg_10000_tools] bucket_count=%zu load_factor=%.4f max_probe_length=%zu\n",
                r.bucket_count(), r.load_factor(), r.max_probe_length());
}

// Required: show what the weak hash s[0] + s.size() would have done to the
// same 10,000 names, at the same final bucket count our real hash reached.
TEST(reg_alternative_hash) {
    ToolRegistry r;
    constexpr int kCount = 10000;
    for (int i = 0; i < kCount; ++i)
        REQUIRE(r.add(std::make_unique<NamedTool>("tool_" + std::to_string(i))));

    const std::size_t bucket_count = r.bucket_count();

    std::vector<int> weak_bucket_counts(bucket_count, 0);
    for (int i = 0; i < kCount; ++i) {
        const std::string name = "tool_" + std::to_string(i);
        const std::size_t weak_hash = static_cast<unsigned char>(name[0]) + name.size();
        ++weak_bucket_counts[weak_hash % bucket_count];
    }
    int weak_longest_chain = 0;
    for (int count : weak_bucket_counts) weak_longest_chain = std::max(weak_longest_chain, count);

    std::printf("    [reg_alternative_hash] bucket_count=%zu  "
                "FNV-1a max_probe_length=%zu  weak-hash longest chain=%d\n",
                bucket_count, r.max_probe_length(), weak_longest_chain);

    // The lesson the spec is after: a hash that looks at only the first
    // character and the length clusters badly on names like these, while
    // FNV-1a, at the very same bucket count, stays small.
    CHECK(static_cast<std::size_t>(weak_longest_chain) > r.max_probe_length());
}

// ==================================================================== ws_

TEST(ws_rejects_parent_escape) {
    TempDir d;
    Workspace ws(d.path());
    fs::path out;
    std::string reason;
    REQUIRE(ws.resolve("notes/todo.txt", &out, &reason));   // sanity: accepts *something*
    CHECK(out == ws.root() / "notes" / "todo.txt");
    CHECK(!ws.resolve("../etc/passwd", &out, &reason));
    CHECK(!reason.empty());
}

TEST(ws_accepts_dot_and_double_slash) {
    TempDir d;
    Workspace ws(d.path());
    fs::path out;
    std::string reason;
    REQUIRE(ws.resolve("./notes//todo.txt", &out, &reason));
    CHECK(out == ws.root() / "notes" / "todo.txt");
}

TEST(ws_accepts_dot_as_root) {
    TempDir d;
    Workspace ws(d.path());
    fs::path out;
    std::string reason;
    REQUIRE(ws.resolve(".", &out, &reason));
    CHECK(out == ws.root());
}

TEST(ws_accepts_dotdot_in_middle) {
    TempDir d;
    Workspace ws(d.path());
    fs::path out;
    std::string reason;
    REQUIRE(ws.resolve("a/../b", &out, &reason));
    CHECK(out == ws.root() / "b");
}

TEST(ws_accepts_trailing_dotdot) {
    TempDir d;
    Workspace ws(d.path());
    fs::path out;
    std::string reason;
    REQUIRE(ws.resolve("a/..", &out, &reason));
    CHECK(out == ws.root());
}

TEST(ws_accepts_nonexistent_new_path) {
    TempDir d;
    Workspace ws(d.path());
    fs::path out;
    std::string reason;
    REQUIRE(ws.resolve("new/dir/file.txt", &out, &reason));
    CHECK(out == ws.root() / "new" / "dir" / "file.txt");
}

TEST(ws_accepts_name_with_embedded_dots) {
    TempDir d;
    Workspace ws(d.path());
    fs::path out;
    std::string reason;
    REQUIRE(ws.resolve("my..notes.txt", &out, &reason));
    CHECK(out == ws.root() / "my..notes.txt");
}

TEST(ws_rejects_empty_path) {
    TempDir d;
    Workspace ws(d.path());
    fs::path out;
    std::string reason;
    CHECK(!ws.resolve("", &out, &reason));
    CHECK(!reason.empty());
}

TEST(ws_rejects_nul_byte) {
    TempDir d;
    Workspace ws(d.path());
    fs::path out;
    std::string reason;
    // Built explicitly: a string literal would be cut off at the NUL byte
    // and resolve() would never see it.
    const std::string bad = std::string("notes") + '\0' + "/x";
    CHECK(!ws.resolve(bad, &out, &reason));
    CHECK(!reason.empty());
}

TEST(ws_rejects_absolute_path) {
    TempDir d;
    Workspace ws(d.path());
    fs::path out;
    std::string reason;
    CHECK(!ws.resolve("/etc/passwd", &out, &reason));
    CHECK(!reason.empty());
}

TEST(ws_rejects_bare_dotdot) {
    TempDir d;
    Workspace ws(d.path());
    fs::path out;
    std::string reason;
    CHECK(!ws.resolve("..", &out, &reason));
    CHECK(!reason.empty());
}

TEST(ws_rejects_dotdot_climbs_above_root) {
    TempDir d;
    Workspace ws(d.path());
    fs::path out;
    std::string reason;
    CHECK(!ws.resolve("a/../../b", &out, &reason));
    CHECK(!reason.empty());
}

TEST(ws_rejects_round_trip_above_root) {
    TempDir d;
    Workspace ws(d.path());
    fs::path out;
    std::string reason;
    CHECK(!ws.resolve("notes/../../ws/notes/todo.txt", &out, &reason));
    CHECK(!reason.empty());
}

// ================================================================== calc_

TEST(calc_precedence) {
    double v = 0;
    std::string err;
    REQUIRE(eval("2 + 3 * 4", &v, &err));
    CHECK(v == 14);
}

TEST(calc_division_by_zero) {
    double v = 0;
    std::string err;
    CHECK(!eval("1 / 0", &v, &err));
    CHECK(err == "division by zero");
}

TEST(calc_left_to_right_subtraction) {
    double v = 0;
    std::string err;
    REQUIRE(eval("10 - 4 - 3", &v, &err));
    CHECK(v == 3);
}

TEST(calc_left_to_right_division) {
    double v = 0;
    std::string err;
    REQUIRE(eval("100 / 10 / 5", &v, &err));
    CHECK(v == 2);
}

TEST(calc_parentheses_simple) {
    double v = 0;
    std::string err;
    REQUIRE(eval("(2 + 3) * 4", &v, &err));
    CHECK(v == 20);
}

TEST(calc_mixed_precedence_and_parens) {
    double v = 0;
    std::string err;
    REQUIRE(eval("2 * (3 + 4) - 5", &v, &err));
    CHECK(v == 9);
}

TEST(calc_negative_numbers) {
    double v = 0;
    std::string err;
    REQUIRE(eval("-3 * -3", &v, &err));
    CHECK(v == 9);
}

TEST(calc_fractional_result) {
    double v = 0;
    std::string err;
    REQUIRE(eval("7 / 2", &v, &err));
    CHECK(v == 3.5);
}

TEST(calc_division_by_zero_in_parens) {
    double v = 0;
    std::string err;
    CHECK(!eval("1 / (2 - 2)", &v, &err));
    CHECK(err == "division by zero");
}

// -------- my own expressions --------

TEST(calc_nested_parens_custom) {
    double v = 0;
    std::string err;
    REQUIRE(eval("((1 + 2) * (3 + 4))", &v, &err));
    CHECK(v == 21);
}

TEST(calc_decimal_arithmetic) {
    double v = 0;
    std::string err;
    REQUIRE(eval("2.5 * 4 + 0.5", &v, &err));
    CHECK(v == 10.5);
}

// Stress test: a loop-based evaluator handles this in O(n)

TEST(calc_long_addition_chain) {
    constexpr int kTerms = 20000;
    std::string expr = "0";
    for (int i = 0; i < kTerms; ++i) expr += "+1";
    double v = 0;
    std::string err;
    REQUIRE(eval(expr, &v, &err));
    CHECK(v == static_cast<double>(kTerms));
}

TEST(calc_deep_nesting) {
    constexpr int kDepth = 5000;
    std::string expr(static_cast<std::size_t>(kDepth), '(');
    expr += "1";
    expr.append(static_cast<std::size_t>(kDepth), ')');
    double v = 0;
    std::string err;
    REQUIRE(eval(expr, &v, &err));
    CHECK(v == 1);
}

int main(int argc, char** argv) { return mt::run_all(argc, argv); }
