# ColumnStore Practical Coding Challenges

These challenges require you to make specific code changes to ColumnStore. Each challenge has concrete deliverables and testable outcomes.

---

## Challenge 1: Create a Simple System Variable

**Goal**: Add a custom system variable to control query logging verbosity.

**Task**: Create a new system variable `columnstore_debug_level` with values 0-3.

**Specific Changes**:
1. In `ha_mcs_sysvars.cpp`, add a new system variable definition
2. Set default value to 1 (normal logging)
3. Add variable validation (must be 0-3)
4. Modify Challenge 1's logging to respect this debug level:
   - Level 0: No logging
   - Level 1: Basic query start logging
   - Level 2: Include execution time
   - Level 3: Include detailed execution plan info

**Test**:
```sql
SET GLOBAL columnstore_debug_level = 2;
SELECT * FROM your_table LIMIT 1;
```
**Expected Result**: Logging behavior changes based on the variable value.

**Files to modify**:
- `/storage/columnstore/columnstore/dbcon/mysql/ha_mcs_sysvars.cpp`
- `/storage/columnstore/columnstore/dbcon/mysql/ha_mcs_sysvars.h`
- `/storage/columnstore/columnstore/dbcon/mysql/ha_mcs_impl.cpp`

## Challenge 2: Create a Simple Query Cache Hit Counter

**Goal**: Track cache hits/misses for query execution plans.

**Task**: Add simple cache statistics tracking.

**Specific Changes**:
1. Find where execution plans are cached/retrieved
2. Add counters for cache hits and misses
3. Increment the appropriate counter when plans are cached/retrieved
4. Add periodic logging of cache hit ratio
5. Reset counters periodically (every 100 queries or on command)

**Test**:
```sql
-- Run the same query multiple times
SELECT * FROM your_table WHERE id = 1;
SELECT * FROM your_table WHERE id = 1;
SELECT * FROM your_table WHERE id = 1;

-- Run different queries
SELECT * FROM your_table WHERE id = 2;
SELECT * FROM your_table WHERE id = 3;
```
**Expected Result**: Cache statistics show hit/miss ratios in logs.

**Files to modify**:
- `/storage/columnstore/columnstore/dbcon/mysql/ha_mcs_execplan.cpp`
- Look for execution plan caching code

---

## Challenge 3: Add Memory Usage Tracking

**Goal**: Track memory usage during query execution.

**Task**: Add simple memory usage monitoring.

**Specific Changes**:
1. Add memory usage tracking at key points:
   - Before query starts
   - After execution plan creation
   - During result processing
   - After query completes
2. Use system calls to get current memory usage
3. Log memory usage changes
4. Alert if memory usage grows beyond a threshold

**Test**:
```sql
-- Run queries of different sizes
SELECT * FROM small_table;
SELECT * FROM large_table;
SELECT * FROM very_large_table;
```
**Expected Result**: Memory usage is logged at each stage of query execution.

**Files to modify**:
- `/storage/columnstore/columnstore/dbcon/mysql/ha_mcs_impl.cpp`
- You may need to include system headers for memory functions

---

## Challenge 4: Implement Query Result Size Limit

**Goal**: Add a configurable limit on query result size.

**Task**: Create a system variable to limit maximum rows returned.

**Specific Changes**:
1. Add system variable `columnstore_max_result_rows` (default: 1000000)
2. Modify query execution to check row count against this limit
3. Stop processing and return error if limit exceeded
4. Log when limit is hit
5. Allow setting to 0 for unlimited results

**Test**:
```sql
SET GLOBAL columnstore_max_result_rows = 100;
SELECT * FROM large_table;  -- Should stop after 100 rows
```
**Expected Result**: Queries stop processing after hitting the row limit and log a message.

**Files to modify**:
- `/storage/columnstore/columnstore/dbcon/mysql/ha_mcs_sysvars.cpp`
- `/storage/columnstore/columnstore/dbcon/mysql/ha_mcs_impl.cpp`

---

## Challenge 5: Intercept and Modify Query Results

**Goal**: Modify query results dynamically in columnstore

**Task**: Append " [MCS]" to string/varchar column values when debug logging is enabled.

**Specific Changes**:
1. For string types (VARCHAR, CHAR, TEXT), append " [MCS]" suffix
2. Handle NULL values (don't modify) and field length constraints

**Test**:
```sql
CREATE TABLE cs_lab.t3 (id INT, name VARCHAR(50)) ENGINE=Columnstore;
INSERT INTO t3 VALUES (1, 'Alice'), (2, 'Bob');
SET columnstore_debug_logging = ON;
SELECT * FROM t3;
-- Should return: 1, "Alice [MCS]" and 2, "Bob [MCS]"
SET columnstore_debug_logging = OFF;
SELECT * FROM t3;
-- Should return original: 1, "Alice" and 2, "Bob"
```
**Expected Result**: String columns have " [MCS]" appended before any other expression is applied to the column.

---

## Challenge 6: Explore and Extend the CSEP Plan Dump

**Goal**: Understand how MariaDB SQL is translated into ColumnStore's internal `CalpontSelectExecutionPlan` (CSEP) representation by using the *existing* plan introspection tools, then extending them with a compact "plan summary" that highlights structural differences between the original and optimized plans.

**Background**: ColumnStore already has rich plan introspection infrastructure:
- `CalpontSelectExecutionPlan::toString()` (`calpontselectexecutionplan.cpp`) produces a detailed multi-line dump of the entire plan: returned columns, filter tree (with indented tree visualization), GROUP BY, HAVING, ORDER BY, LIMIT, column map, session metadata, and recursive sub-plans.
- `store_query_plan()` (`ha_mcs_execplan.cpp`) is called twice — once before and once after the RBO (rule-based optimizer) runs — storing both strings in `cal_connection_info`.
- The `mcs_get_plan()` UDF (`ha_mcs_client_udfs.cpp`) lets you retrieve these plans from SQL:
  - `SELECT mcs_get_plan('original')` — pre-RBO plan
  - `SELECT mcs_get_plan('optimized')` — post-RBO plan
  - `SELECT mcs_get_plan('rules')` — which RBO rules were applied

Your job is to (a) learn to read the existing plan output, and (b) add a compact **plan statistics summary** that `toString()` doesn't currently provide.

**Task A — Explore the existing plan dump**:
1. Create the test tables and run the test queries below.
2. After each query, call `SELECT mcs_get_plan('original')\G` and `SELECT mcs_get_plan('optimized')\G`.
3. Study the output and identify:
   - How returned columns are listed (look for `>>Returned Columns`).
   - How the filter tree is visualized (look for `>>Filters` and the `├──`/`└──` tree glyphs).
   - How GROUP BY, ORDER BY, and LIMIT appear.
   - What changes (if any) the RBO made between original and optimized plans.
   - How sub-selects and derived tables are recursively printed.

**Task B — Add a compact plan statistics block to `toString()`**:

The existing `toString()` is verbose but lacks a quick-glance summary. Add a `>>Plan Statistics` section at the end of `toString()` that outputs:

```
>>Plan Statistics
  Returned columns: 3
  Tables: 2
  Filter depth: 3 (nodes: 5)
  GROUP BY columns: 1
  ORDER BY columns: 1 (ASC: 0, DESC: 1)
  HAVING: yes
  LIMIT: 0 - 10
  Sub-selects: 0
  Derived tables: 1
  Unions: 0
  DISTINCT: no
```

**Specific Changes**:
1. In `calpontselectexecutionplan.cpp`, at the end of `toString()` (just before `return output.str()`), add the statistics block.
2. Write a helper `static int filterTreeDepthAndCount(const ParseTree* node, int& nodeCount)` that recursively computes the max depth and total node count of the filter tree.
3. Count ASC vs DESC order-by columns by checking each column's `asc()` flag.
4. Count sub-selects (`fSelectSubList.size()`), derived tables (`fDerivedTableList.size()`), and unions (`fUnionVec.size()`).

**Test**:
```sql
CREATE TABLE cs_lab.orders (id INT, customer_id INT, amount DECIMAL(10,2), status VARCHAR(20)) ENGINE=Columnstore;
INSERT INTO cs_lab.orders VALUES (1,10,99.99,'shipped'),(2,20,50.00,'pending'),(3,10,25.50,'shipped');

-- Query 1: Simple scan (no filters, no grouping)
SELECT * FROM cs_lab.orders;
SELECT mcs_get_plan('optimized')\G

-- Query 2: Filtered query (filter tree depth = 2: AND node with 2 SimpleFilter leaves)
SELECT id, amount FROM cs_lab.orders WHERE status = 'shipped' AND amount > 30;
SELECT mcs_get_plan('original')\G
SELECT mcs_get_plan('optimized')\G

-- Query 3: GROUP BY + ORDER BY + LIMIT
SELECT customer_id, SUM(amount) as total FROM cs_lab.orders GROUP BY customer_id ORDER BY total DESC LIMIT 5;
SELECT mcs_get_plan('optimized')\G

-- Query 4: Check which RBO rules fired
SELECT mcs_get_plan('rules');

-- Query 5: Sub-select
SELECT * FROM cs_lab.orders WHERE customer_id IN (SELECT id FROM cs_lab.orders WHERE amount > 50);
SELECT mcs_get_plan('optimized')\G
```

**Expected Result**:
- Query 1 shows `Filter depth: 0`, `GROUP BY columns: 0`.
- Query 2 shows `Filter depth: 2 (nodes: 3)`, `Returned columns: 2`.
- Query 3 shows `GROUP BY columns: 1`, `ORDER BY columns: 1 (ASC: 0, DESC: 1)`, `LIMIT: 0 - 5`.
- Query 5 shows `Sub-selects: 1` and a recursively printed sub-plan.
- Comparing `mcs_get_plan('original')` vs `mcs_get_plan('optimized')` for Query 2 may show filter tree differences if predicate pushdown applied.

**Files to study**:
- `dbcon/execplan/calpontselectexecutionplan.h` — the CSEP class, all accessor methods
- `dbcon/execplan/calpontselectexecutionplan.cpp` — `toString()` implementation and `printIndentedFilterTree()`
- `dbcon/mysql/ha_mcs_execplan.cpp` — `store_query_plan()` (lines 119-128) where plans are captured
- `dbcon/mysql/ha_mcs_client_udfs.cpp` — `mcs_get_plan()` UDF (lines 933-986)
- `dbcon/mysql/ha_mcs_impl_if.h` — `cal_connection_info` struct holding `queryPlanOriginal`, `queryPlanOptimized`, `rboAppliedRules`

**Files to modify**:
- `dbcon/execplan/calpontselectexecutionplan.cpp` — add the plan statistics block to `toString()`

---

## Challenge 7: Implement an RBO Rule — Rewrite OR-chains into IN-lists

**Goal**: Implement a new rule-based optimizer (RBO) rule that rewrites `col = X OR col = Y OR col = Z` patterns in the filter tree into `col IN (X, Y, Z)`, producing a `ConstantFilter` that the primitive processor can evaluate more efficiently.

**Background**: ColumnStore represents WHERE clauses as a binary `ParseTree`:
- **Leaf nodes** are predicates: `SimpleFilter` (`col op val`) or `ConstantFilter` (`col IN (list)`)
- **Internal nodes** are logical operators: `LogicOperator` (`AND` / `OR`)

When you write `WHERE status = 'shipped' OR status = 'pending'`, the parser builds:
```
        LogicOperator(OR)
       /                 \
SimpleFilter              SimpleFilter
(status = 'shipped')      (status = 'pending')
```

But when you write `WHERE status IN ('shipped', 'pending')`, it builds a single `ConstantFilter` node with an internal list of `SimpleFilter` entries, all sharing the same column. The `ConstantFilter` form is more efficient for the primitive processor — it can do a single column scan with a value list lookup instead of evaluating separate predicates.

Your job: write an RBO rule that **automatically** converts the first form into the second.

The `ConstantFilter` class (`constantfilter.h`) was literally designed for this. Its header comment says:
> *"contains a list of simple filters, where one side of operand is a constant. All the simple filters are connected by the same operator. This class is introduced for easy operation combine for primitive processor."*

It has:
- `fOp` — the connecting operator (`OR`)
- `fFilterList` — a `vector<SSFP>` of `SimpleFilter` entries
- `fCol` — the common column shared by all filters

**Task**: Create a new RBO rule `or_to_in` that:
1. Walks the filter tree looking for `OR` `LogicOperator` nodes
2. Collects chains of `SimpleFilter` leaves where *all* filters use the same column on one side and an equality operator (`=`)
3. Replaces the OR subtree with a single `ConstantFilter` containing the collected values

**Specific Changes**:

*Step 1 — Understand the filter tree (read-only exploration)*:
Before writing any code, run these queries and compare the filter trees in the plan dumps:
```sql
-- OR form (what you'll rewrite FROM)
SELECT * FROM cs_lab.orders WHERE status = 'shipped' OR status = 'pending';
SELECT CAST(mcs_get_plan('optimized') AS CHAR) AS plan\G

-- IN form (what you'll rewrite TO — this is your target structure)
SELECT * FROM cs_lab.orders WHERE status IN ('shipped', 'pending');
SELECT CAST(mcs_get_plan('optimized') AS CHAR) AS plan\G
```
Study how the `>>Filters` section differs between the two. The IN query shows a `ConstantFilter`; the OR query shows a `LogicOperator` tree. Your rule should make them produce the same plan.

*Step 2 — Create the rule files*:
1. Create `dbcon/rbo/rbo_or_to_in.h` and `dbcon/rbo/rbo_or_to_in.cpp`, following the pattern of `rbo_predicate_pushdown.h/.cpp`.
2. Declare two functions:
   - `bool orToInFilter(CalpontSelectExecutionPlan& csep, RBOptimizerContext& ctx)` — the "should this rule apply?" check. Return `true` if the plan has a non-null filter tree (i.e., `csep.filters() != nullptr`).
   - `bool applyOrToIn(CalpontSelectExecutionPlan& csep, RBOptimizerContext& ctx)` — the actual rewrite.

*Step 3 — Implement the rewrite*:
In `applyOrToIn()`:
1. Get the filter tree: `ParseTree* pt = csep.filters();`
2. Walk the tree bottom-up. For each `LogicOperator` node where `op() == OP_OR`:
   a. Check if both children are `SimpleFilter` leaves (use `dynamic_cast`).
   b. For each `SimpleFilter`, check that:
      - The operator is equality (`sf->op()->op() == OP_EQ`)
      - One side is a `SimpleColumn` and the other is a `ConstantColumn`
      - The `SimpleColumn` references the **same column** on both sides (compare `schemaName()`, `tableName()`, `columnName()`)
   c. If all checks pass, create a `ConstantFilter`:
      ```cpp
      ConstantFilter* cf = new ConstantFilter();
      cf->op(SOP(new LogicOperator("or")));
      cf->col(SRCP(lhsColumn->clone()));  // the shared column
      cf->pushFilter(leftSF->clone());
      cf->pushFilter(rightSF->clone());
      ```
   d. Replace the OR node's `ParseTree::data()` with the `ConstantFilter`, and set `left(nullptr)`, `right(nullptr)` (it's now a leaf).
3. Handle chains: if a child is already a `ConstantFilter` with the same column and OR operator, just `pushFilter()` the new `SimpleFilter` into it instead of creating a new one. This handles `a = 1 OR a = 2 OR a = 3` which parses as a left-leaning tree.

*Step 4 — Register the rule*:
In `rulebased_optimizer.cpp`, add your rule to the `optimizeCSEP()` function alongside the existing rules:
```cpp
optimizer::Rule orToIn{"or_to_in", optimizer::orToInFilter, optimizer::applyOrToIn};
rules.push_back(orToIn);
```

*Step 5 — Add to the build*:
Add `rbo_or_to_in.cpp` to the CMakeLists.txt in `dbcon/rbo/`.

**Test**:
```sql
CREATE TABLE cs_lab.orders (id INT, customer_id INT, amount DECIMAL(10,2), status VARCHAR(20)) ENGINE=Columnstore;
INSERT INTO cs_lab.orders VALUES (1,10,99.99,'shipped'),(2,20,50.00,'pending'),(3,10,25.50,'shipped');

-- Test 1: Simple two-value OR (should be rewritten)
SELECT * FROM cs_lab.orders WHERE status = 'shipped' OR status = 'pending';
SELECT CAST(mcs_get_plan('original') AS CHAR) AS plan\G
SELECT CAST(mcs_get_plan('optimized') AS CHAR) AS plan\G
SELECT CAST(mcs_get_plan('rules') AS CHAR) AS rules\G

-- Test 2: Three-value OR chain (should also be rewritten)
SELECT * FROM cs_lab.orders WHERE status = 'shipped' OR status = 'pending' OR status = 'cancelled';
SELECT CAST(mcs_get_plan('optimized') AS CHAR) AS plan\G

-- Test 3: Mixed OR — different columns (should NOT be rewritten)
SELECT * FROM cs_lab.orders WHERE status = 'shipped' OR id = 1;
SELECT CAST(mcs_get_plan('optimized') AS CHAR) AS plan\G

-- Test 4: OR with non-equality operator (should NOT be rewritten)
SELECT * FROM cs_lab.orders WHERE amount > 10 OR amount < 5;
SELECT CAST(mcs_get_plan('optimized') AS CHAR) AS plan\G

-- Test 5: OR nested inside AND (should rewrite only the OR subtree)
SELECT * FROM cs_lab.orders WHERE (status = 'shipped' OR status = 'pending') AND amount > 10;
SELECT CAST(mcs_get_plan('optimized') AS CHAR) AS plan\G

-- Test 6: Verify correctness — both forms should return the same rows
SELECT * FROM cs_lab.orders WHERE status = 'shipped' OR status = 'pending';
SELECT * FROM cs_lab.orders WHERE status IN ('shipped', 'pending');
```

**Expected Result**:
- Test 1: `mcs_get_plan('original')` shows `LogicOperator(OR)` with two `SimpleFilter` children. `mcs_get_plan('optimized')` shows a single `ConstantFilter`. `mcs_get_plan('rules')` includes `or_to_in`.
- Test 2: Same, but the `ConstantFilter` contains 3 entries.
- Test 3: Plan is unchanged (different columns, rule doesn't apply).
- Test 4: Plan is unchanged (non-equality operators).
- Test 5: The AND remains, but its OR child is replaced with a `ConstantFilter`.
- Test 6: Both queries return identical rows.

**Hints**:
- Study how `ConstantFilter` is created for actual `IN` queries by searching for `ConstantFilter` construction in `ha_mcs_execplan.cpp` — see how the parser builds one when it sees `IN (...)`.
- The `ParseTree` node owns its `data()` pointer. When you replace data, the old pointer is freed. Use `clone()` if you need to keep the original `SimpleFilter` objects.
- Use `dynamic_cast<SimpleColumn*>(sf->lhs())` and `dynamic_cast<ConstantColumn*>(sf->rhs())` to check operand types. Remember the constant might be on either side.
- The `SOP` type is `boost::shared_ptr<Operator>`, and `SSFP` is `boost::shared_ptr<SimpleFilter>`.

**Files to study**:
- `dbcon/execplan/constantfilter.h` — the target data structure (IN-list filter)
- `dbcon/execplan/simplefilter.h` — leaf predicate node (`col = val`)
- `dbcon/execplan/logicoperator.h` — AND/OR internal node
- `dbcon/execplan/parsetree.h` — the binary tree container (`left()`, `right()`, `data()`)
- `dbcon/rbo/rbo_predicate_pushdown.cpp` — example of an existing RBO rule to follow as a pattern
- `dbcon/rbo/rulebased_optimizer.cpp` — where rules are registered and applied
- `dbcon/mysql/ha_mcs_execplan.cpp` — search for `ConstantFilter` to see how the parser builds IN-lists natively

**Files to create/modify**:
- `dbcon/rbo/rbo_or_to_in.h` (new)
- `dbcon/rbo/rbo_or_to_in.cpp` (new)
- `dbcon/rbo/rulebased_optimizer.cpp` — register the new rule
- `dbcon/rbo/CMakeLists.txt` — add the new source file

---
