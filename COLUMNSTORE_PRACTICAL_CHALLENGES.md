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