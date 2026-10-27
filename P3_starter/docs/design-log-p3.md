# Project 3 Design Log
**Author:** Prithu Bapu


## 1. Hash table

`ToolRegistry` uses **separate chaining**: `buckets_` is a `std::vector` of
`std::forward_list<std::unique_ptr<Tool>>`. I picked chaining over open addressing because
a bucket's list can only ever grow to the table's own load factor, with no need to worry about
a probe sequence wrapping a full table or needing tombstones (irrelevant here anyway, since P3
never removes a tool). Open addressing is more cache-friendly, but the spec's own reference
numbers (Appendix A) show it costing roughly 5x the probe length of chaining with the same
hash at the same load factor, and chaining made pointer stability (§2) simpler to reason about.

The hash function is 64-bit **FNV-1a** straight from Appendix A: it folds in every byte of the
key and multiplies by a large prime each step, so `"ab"` and `"ba"` hash to different values
and a name's length alone can't dominate the result. The table starts at 8 buckets (a power of
two) and doubles whenever the *next* insert would push `size() / bucket_count()` above
**0.75**. The check happens before the insert, not after — `add()` computes
`(size_ + 1) > 0.75 * bucket_count()` and rehashes first, so `load_factor()` can never be
caught above the threshold even transiently.

Measured after inserting `tool_0` … `tool_9999`:

| | FNV-1a (chaining) | weak hash `s[0] + s.size()` |
|---|---|---|
| bucket_count | 16384 | 16384 (same table size, for comparison) |
| load_factor | 0.6104 | — |
| longest chain / max_probe_length | **6** | **9000** |

The weak hash collapses almost the entire 10,000-name set into a handful of buckets, because
every `tool_N` of the same digit-length and leading character maps to the same value — it never
looks past the first character. FNV-1a, using the whole string, spreads them out close to
evenly.

`add()` is **amortized O(1)** by the same argument as `Conversation::append` in P2: a rehash
from *n* to *2n* buckets costs O(n) (every existing entry is reinserted once), but it only
happens after roughly *n*/2 cheap O(1) inserts since the previous rehash. Spread over those
inserts, the O(n) cost adds O(1) to each one on average, so the amortized cost per `add()` stays
O(1) even though any single call might trigger a resize.

## 2. Pointer stability

`find()` returns a raw `Tool*` that must stay valid across later `add()` calls, including ones
that trigger a rehash. Because each bucket holds `std::unique_ptr<Tool>`, the `Tool` object
itself lives in one heap allocation that the `unique_ptr` owns. A rehash moves `unique_ptr`s
between `forward_list` nodes (`std::move`), which only copies the *pointer value* — it never
touches, copies, or relocates the `Tool` the pointer points to. The address a caller got from an
earlier `find()` is therefore still the address of the live object after any number of rehashes.

## 3. Calculator trace

For `2 * (3 + 4) - 5`: push `2` onto `values`; `ops` is empty, so push `*`; push `(` onto `ops`;
push `3` onto `values`; `+` sees `(` on top of `ops` (not an operator), so it's pushed too; push
`4` onto `values`. On `)`, apply operators until `(` is on top: apply `+` (3+4=7, `values` is now
`2 7`), then pop the `(`. Now `-` arrives: the top of `ops` is `*`, precedence 2 ≥ 1, so apply it
first (2×7=14, `values` is `14`), then push `-`. Push `5` onto `values` (`14 5`). Tokens are
exhausted, so apply everything left on `ops`: apply `-` (14−5=9). One value remains: **9**.

## 4. Errors as values

A tool call, a path, or a division is something the *model* chose, not something my code
controls, so failing is an expected outcome the model is meant to read and react to — the same
way a nonzero shell exit code isn't a crash. Throwing would force every caller up the stack to
either catch it or propagate it past code with no way to turn it into a useful message for the
model; returning `ToolResult::failure(...)` (or `false` plus a reason string) keeps the error
traveling the exact path the success value would have taken. I'd reserve an actual exception for
something that should never happen regardless of model input — e.g. calling `as_string()` on a
JSON number, which is a bug in the C++ code calling `JsonValue`, not a bad request from the
model.

## 5. What I would design differently

`add()` calls `find(name)` to check for a duplicate (hashing `name` and walking its bucket),
then, on success, hashes `name` again to compute the insertion index. The duplicate work is
cheap given the chain lengths involved, but reusing one hash value for both the lookup and the
insert would be the tidier design — I'd factor `find()` into a private helper returning both the
bucket index and the matching entry (if any), called by both `add()` and the public `find()`.
