/*
 * Unit tests for RBO parallel rewrite functionality using hybrid real/mock approach
 * Uses real MariaDB ColumnStore classes where practical, minimal mocks where system infrastructure is
 * required
 */

#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <string>

#include <dbcon/rbo/rbo_apply_parallel_ces.h>
#include <dbcon/rbo/rbo_or_to_in.h>

#include <dbcon/execplan/calpontselectexecutionplan.h>
#include <dbcon/execplan/simplecolumn.h>
#include <dbcon/execplan/constantcolumn.h>
#include <dbcon/execplan/constantfilter.h>
#include <dbcon/execplan/predicateoperator.h>
#include <dbcon/execplan/logicoperator.h>
#include <dbcon/execplan/parsetree.h>
#include <dbcon/mysql/ha_mcs_impl_if.h>

class RBOHybridTest : public ::testing::Test
{
 protected:
  void SetUp() override
  {
  }

  void TearDown() override
  {
  }

  // Mock SimpleColumn that doesn't require Syscat infrastructure
  class MockSimpleColumn : public execplan::SimpleColumn
  {
   public:
    MockSimpleColumn(const std::string& schema, const std::string& table, const std::string& column)
     : execplan::SimpleColumn()
    {
      // Set basic properties without requiring Syscat
      schemaName(schema);
      tableName(table);
      columnName(column);
      // Note: OID and other system-dependent properties are not set
    }
  };

  // Helper to create a mock SimpleColumn
  boost::shared_ptr<MockSimpleColumn> createMockSimpleColumn(const std::string& schema,
                                                             const std::string& table,
                                                             const std::string& column)
  {
    return boost::shared_ptr<MockSimpleColumn>(new MockSimpleColumn(schema, table, column));
  }

  // Helper to create a real CalpontSelectExecutionPlan
  boost::shared_ptr<execplan::CalpontSelectExecutionPlan> createCSEP()
  {
    return boost::shared_ptr<execplan::CalpontSelectExecutionPlan>(
        new execplan::CalpontSelectExecutionPlan());
  }

  // Helper to create a real TableAliasName (this is just a struct, no system dependencies)
  execplan::CalpontSystemCatalog::TableAliasName createTableAlias(const std::string& schema,
                                                                  const std::string& table,
                                                                  const std::string& alias = "",
                                                                  bool isColumnStore = true)
  {
    execplan::CalpontSystemCatalog::TableAliasName tableAlias;
    tableAlias.schema = schema;
    tableAlias.table = table;
    tableAlias.alias = alias.empty() ? table : alias;
    tableAlias.view = "";
    tableAlias.fisColumnStore = isColumnStore;
    return tableAlias;
  }

  // Mock structures needed for RBOptimizerContext
  struct MockTHD
  {
    // Minimal THD mock for testing
    uint64_t thread_id = 1;
    // Add other fields as needed
  };

  struct MockGatewayInfo
  {
    std::unordered_map<
        cal_impl_if::SchemaAndTableName,
        std::map<std::string, std::pair<execplan::SimpleColumn, std::vector<Histogram_json_hb*>>>,
        cal_impl_if::SchemaAndTableNameHash>
        tableStatistics;

    // Helper method to find statistics for a table
    std::map<std::string, std::pair<execplan::SimpleColumn, std::vector<Histogram_json_hb*>>>*
    findStatisticsForATable(const cal_impl_if::SchemaAndTableName& schemaAndTable)
    {
      auto it = tableStatistics.find(schemaAndTable);
      return (it != tableStatistics.end()) ? &(it->second) : nullptr;
    }
  };

  // Create a simplified mock approach since RBOptimizerContext is complex to mock properly
  // We'll create a wrapper that provides the interface we need for testing
  class MockRBOptimizerContextWrapper
  {
   private:
    MockTHD mockTHD;
    MockGatewayInfo mockGWI;

   public:
    MockRBOptimizerContextWrapper()
    {
    }

    // Helper to add test statistics
    void addTableStatistics(const std::string& schema, const std::string& table, const std::string& column,
                            Histogram_json_hb* histogram)
    {
      cal_impl_if::SchemaAndTableName schemaAndTable = {schema, table};
      execplan::SimpleColumn simpleCol;  // Mock column
      std::vector<Histogram_json_hb*> histograms = {histogram};
      mockGWI.tableStatistics[schemaAndTable][column] = std::make_pair(simpleCol, histograms);
    }

    // Get the mock gateway info for testing helper functions
    MockGatewayInfo& getGWI()
    {
      return mockGWI;
    }
  };

  // Helper to create a mock optimizer context
  std::unique_ptr<MockRBOptimizerContextWrapper> createMockOptimizerContext()
  {
    return std::make_unique<MockRBOptimizerContextWrapper>();
  }

  // Mock histogram for testing (Histogram_json_hb is final, so we can't inherit)
  // We'll use a simple wrapper approach instead
  struct MockHistogramData
  {
    std::vector<uint32_t> testValues;

    MockHistogramData(const std::vector<uint32_t>& values) : testValues(values)
    {
    }
  };

  boost::shared_ptr<MockHistogramData> createMockHistogram(const std::vector<uint32_t>& values)
  {
    return boost::shared_ptr<MockHistogramData>(new MockHistogramData(values));
  }
};

// Test helper functions that work with real data structures
TEST_F(RBOHybridTest, HelperFunctionsWithRealStructures)
{
  // Test someAreForeignTables with real CSEP and real TableAliasName structures
  auto csep = createCSEP();

  // Initially empty, should return false
  EXPECT_FALSE(optimizer::details::someAreForeignTables(*csep));

  // Create table lists using real TableAliasName structures
  execplan::CalpontSelectExecutionPlan::TableList tables;

  // Add ColumnStore table (real structure)
  auto csTable = createTableAlias("test_schema", "cs_table", "", true);
  tables.push_back(csTable);
  csep->tableList(tables);

  EXPECT_FALSE(optimizer::details::someAreForeignTables(*csep));

  // Add foreign table (real structure)
  auto foreignTable = createTableAlias("test_schema", "foreign_table", "", false);
  tables.push_back(foreignTable);
  csep->tableList(tables);

  EXPECT_TRUE(optimizer::details::someAreForeignTables(*csep));
}

// Test tableIsInUnion with real CSEP and TableAliasName
TEST_F(RBOHybridTest, TableIsInUnionWithRealStructures)
{
  auto csep = createCSEP();
  auto testTable = createTableAlias("test_schema", "test_table");

  // Test with no unions
  EXPECT_FALSE(optimizer::details::tableIsInUnion(testTable, *csep));

  // Create a union subquery using real CSEP
  auto unionPlan = createCSEP();
  execplan::CalpontSelectExecutionPlan::TableList unionTables;
  unionTables.push_back(testTable);
  unionPlan->tableList(unionTables);

  // Add to union vector
  execplan::CalpontSelectExecutionPlan::SelectList unions;
  boost::shared_ptr<execplan::CalpontExecutionPlan> unionPlanBase = unionPlan;
  unions.push_back(unionPlanBase);
  csep->unionVec(unions);

  // Test with table present in union
  EXPECT_TRUE(optimizer::details::tableIsInUnion(testTable, *csep));

  // Test with table not present in union
  auto otherTable = createTableAlias("test_schema", "other_table");
  EXPECT_FALSE(optimizer::details::tableIsInUnion(otherTable, *csep));
}

// Test real TableAliasName structure functionality
TEST_F(RBOHybridTest, RealTableAliasNameBasics)
{
  // Test creating and manipulating real TableAliasName structures
  auto table1 = createTableAlias("schema1", "table1", "alias1", true);
  auto table2 = createTableAlias("schema2", "table2", "", false);

  EXPECT_EQ("schema1", table1.schema);
  EXPECT_EQ("table1", table1.table);
  EXPECT_EQ("alias1", table1.alias);
  EXPECT_TRUE(table1.fisColumnStore);

  EXPECT_EQ("schema2", table2.schema);
  EXPECT_EQ("table2", table2.table);
  EXPECT_EQ("table2", table2.alias);  // Should default to table name
  EXPECT_FALSE(table2.fisColumnStore);
}

// Test real CalpontSelectExecutionPlan functionality
TEST_F(RBOHybridTest, RealCSEPBasics)
{
  auto csep = createCSEP();

  // Test table list operations with real structures
  execplan::CalpontSelectExecutionPlan::TableList tables;
  auto table1 = createTableAlias("schema1", "table1");
  auto table2 = createTableAlias("schema2", "table2");
  tables.push_back(table1);
  tables.push_back(table2);
  csep->tableList(tables);

  const auto& retrievedTables = csep->tableList();
  EXPECT_EQ(2u, retrievedTables.size());
  EXPECT_EQ("schema1", retrievedTables[0].schema);
  EXPECT_EQ("table1", retrievedTables[0].table);
  EXPECT_EQ("schema2", retrievedTables[1].schema);
  EXPECT_EQ("table2", retrievedTables[1].table);
}

// Test with mock SimpleColumn (since real one requires Syscat)
TEST_F(RBOHybridTest, MockSimpleColumnBasics)
{
  auto column = createMockSimpleColumn("test_schema", "test_table", "test_column");

  EXPECT_EQ("test_schema", column->schemaName());
  EXPECT_EQ("test_table", column->tableName());
  EXPECT_EQ("test_column", column->columnName());

  // Test that we can modify column properties
  column->schemaName("new_schema");
  column->tableName("new_table");
  column->columnName("new_column");

  EXPECT_EQ("new_schema", column->schemaName());
  EXPECT_EQ("new_table", column->tableName());
  EXPECT_EQ("new_column", column->columnName());
}

// Test integration with real CSEP and mock columns
TEST_F(RBOHybridTest, CSEPWithMockColumns)
{
  auto csep = createCSEP();

  // Test returned columns operations with mock SimpleColumns
  execplan::CalpontSelectExecutionPlan::ReturnedColumnList cols;
  auto column1 = createMockSimpleColumn("schema1", "table1", "col1");
  auto column2 = createMockSimpleColumn("schema2", "table2", "col2");

  boost::shared_ptr<execplan::ReturnedColumn> col1Base = column1;
  boost::shared_ptr<execplan::ReturnedColumn> col2Base = column2;

  cols.push_back(col1Base);
  cols.push_back(col2Base);
  csep->returnedCols(cols);

  const auto& retrievedCols = csep->returnedCols();
  EXPECT_EQ(2u, retrievedCols.size());

  // Test casting back to MockSimpleColumn
  auto mockCol1 = boost::dynamic_pointer_cast<MockSimpleColumn>(retrievedCols[0]);
  EXPECT_NE(nullptr, mockCol1);
  if (mockCol1)
  {
    EXPECT_EQ("schema1", mockCol1->schemaName());
    EXPECT_EQ("table1", mockCol1->tableName());
    EXPECT_EQ("col1", mockCol1->columnName());
  }
}

// Test helper functions that can work with mock context
TEST_F(RBOHybridTest, HelperFunctionsWithMockContext)
{
  auto csep = createCSEP();
  auto mockCtx = createMockOptimizerContext();

  // Add mixed table types
  execplan::CalpontSelectExecutionPlan::TableList tables;
  auto csTable = createTableAlias("test_schema", "cs_table", "", true);
  auto foreignTable = createTableAlias("test_schema", "foreign_table", "", false);
  tables.push_back(csTable);
  tables.push_back(foreignTable);
  csep->tableList(tables);

  // Test someAreForeignTables (doesn't need context)
  EXPECT_TRUE(optimizer::details::someAreForeignTables(*csep));

  // Note: Functions that require real RBOptimizerContext would need to be tested
  // with actual system infrastructure or more sophisticated mocking
}

// Test edge cases with real structures
TEST_F(RBOHybridTest, EdgeCasesWithRealStructures)
{
  // Test with empty execution plan
  auto emptyCSEP = createCSEP();
  EXPECT_FALSE(optimizer::details::someAreForeignTables(*emptyCSEP));

  // Test with empty table alias
  auto emptyTable = createTableAlias("", "", "");
  EXPECT_TRUE(emptyTable.schema.empty());
  EXPECT_TRUE(emptyTable.table.empty());

  // Test TableAliasName comparison (if TableAliasLessThan is accessible)
  auto table1 = createTableAlias("schema1", "table1");
  auto table2 = createTableAlias("schema2", "table2");

  // These are real structures that can be compared
  EXPECT_NE(table1.schema, table2.schema);
  EXPECT_NE(table1.table, table2.table);
}

// Test parallelCESFilter logic through helper functions (since direct testing requires complex
// RBOptimizerContext)
TEST_F(RBOHybridTest, ParallelCESFilterLogicTesting)
{
  auto csep = createCSEP();

  // Test the first condition: someAreForeignTables
  // Test 1: All ColumnStore tables - should return false
  execplan::CalpontSelectExecutionPlan::TableList csOnlyTables;
  auto csTable1 = createTableAlias("test_schema", "cs_table1", "", true);
  auto csTable2 = createTableAlias("test_schema", "cs_table2", "", true);
  csOnlyTables.push_back(csTable1);
  csOnlyTables.push_back(csTable2);
  csep->tableList(csOnlyTables);

  EXPECT_FALSE(optimizer::details::someAreForeignTables(*csep));

  // Test 2: Mixed tables with foreign tables
  execplan::CalpontSelectExecutionPlan::TableList mixedTables;
  auto foreignTable = createTableAlias("test_schema", "foreign_table", "", false);
  mixedTables.push_back(csTable1);
  mixedTables.push_back(foreignTable);
  csep->tableList(mixedTables);

  EXPECT_TRUE(optimizer::details::someAreForeignTables(*csep));

  // Test 3: Only foreign tables
  execplan::CalpontSelectExecutionPlan::TableList foreignOnlyTables;
  auto foreignTable1 = createTableAlias("test_schema", "foreign_table1", "", false);
  auto foreignTable2 = createTableAlias("test_schema", "foreign_table2", "", false);
  foreignOnlyTables.push_back(foreignTable1);
  foreignOnlyTables.push_back(foreignTable2);
  csep->tableList(foreignOnlyTables);

  EXPECT_TRUE(optimizer::details::someAreForeignTables(*csep));

  // Note: Testing the full parallelCESFilter function would require a real RBOptimizerContext
  // with proper statistics setup, which is not feasible in unit tests without system infrastructure
}

// Test applyParallelCES prerequisites and data structure setup
TEST_F(RBOHybridTest, ApplyParallelCESPrerequisites)
{
  auto csep = createCSEP();

  // Set up a realistic test scenario that would be suitable for parallel rewrite
  execplan::CalpontSelectExecutionPlan::TableList tables;
  auto csTable = createTableAlias("test_schema", "cs_table", "", true);
  auto foreignTable = createTableAlias("test_schema", "foreign_table", "", false);
  tables.push_back(csTable);
  tables.push_back(foreignTable);
  csep->tableList(tables);

  // Add some mock columns
  execplan::CalpontSelectExecutionPlan::ReturnedColumnList cols;
  auto column = createMockSimpleColumn("test_schema", "foreign_table", "id");
  boost::shared_ptr<execplan::ReturnedColumn> columnBase = column;
  cols.push_back(columnBase);
  csep->returnedCols(cols);

  // Verify the setup meets the basic requirements for parallel rewrite consideration
  EXPECT_TRUE(optimizer::details::someAreForeignTables(*csep));  // Has foreign tables
  EXPECT_EQ(2u, csep->tableList().size());                       // Has multiple tables
  EXPECT_EQ(1u, csep->returnedCols().size());                    // Has columns
  EXPECT_EQ(0u, csep->unionVec().size());                        // No existing unions

  // Verify table types are correctly identified
  const auto& retrievedTables = csep->tableList();
  EXPECT_TRUE(retrievedTables[0].fisColumnStore);   // cs_table
  EXPECT_FALSE(retrievedTables[1].fisColumnStore);  // foreign_table

  // Verify column can be cast back to SimpleColumn
  const auto& retrievedCols = csep->returnedCols();
  auto simpleCol = boost::dynamic_pointer_cast<MockSimpleColumn>(retrievedCols[0]);
  EXPECT_NE(nullptr, simpleCol);
  if (simpleCol)
  {
    EXPECT_EQ("test_schema", simpleCol->schemaName());
    EXPECT_EQ("foreign_table", simpleCol->tableName());
    EXPECT_EQ("id", simpleCol->columnName());
  }

  // Note: Testing the actual applyParallelCES function would require a real RBOptimizerContext
  // with proper statistics setup, which is not feasible in unit tests without system infrastructure
}

// Test parallelCESFilter edge cases through helper functions
TEST_F(RBOHybridTest, ParallelCESFilterEdgeCases)
{
  // Test 1: Empty execution plan
  auto emptyCSEP = createCSEP();
  EXPECT_FALSE(optimizer::details::someAreForeignTables(*emptyCSEP));  // Should return false for empty plan

  // Test 2: Only foreign tables (no ColumnStore tables)
  auto foreignOnlyCSEP = createCSEP();
  execplan::CalpontSelectExecutionPlan::TableList foreignTables;
  auto foreignTable1 = createTableAlias("test_schema", "foreign_table1", "", false);
  auto foreignTable2 = createTableAlias("test_schema", "foreign_table2", "", false);
  foreignTables.push_back(foreignTable1);
  foreignTables.push_back(foreignTable2);
  foreignOnlyCSEP->tableList(foreignTables);

  // Verify that someAreForeignTables returns true
  EXPECT_TRUE(optimizer::details::someAreForeignTables(*foreignOnlyCSEP));

  // Test 3: Tables with union subqueries
  auto unionCSEP = createCSEP();
  auto mainTable = createTableAlias("test_schema", "main_table", "", false);
  execplan::CalpontSelectExecutionPlan::TableList mainTables;
  mainTables.push_back(mainTable);
  unionCSEP->tableList(mainTables);

  // Add union subquery
  auto unionSubquery = createCSEP();
  auto unionTable = createTableAlias("test_schema", "union_table", "", true);
  execplan::CalpontSelectExecutionPlan::TableList unionTables;
  unionTables.push_back(unionTable);
  unionSubquery->tableList(unionTables);

  execplan::CalpontSelectExecutionPlan::SelectList unions;
  boost::shared_ptr<execplan::CalpontExecutionPlan> unionBase = unionSubquery;
  unions.push_back(unionBase);
  unionCSEP->unionVec(unions);

  // Test tableIsInUnion functionality
  EXPECT_TRUE(optimizer::details::tableIsInUnion(unionTable, *unionCSEP));
  EXPECT_FALSE(optimizer::details::tableIsInUnion(mainTable, *unionCSEP));
}

// Test applyParallelCES scenarios through data structure validation
TEST_F(RBOHybridTest, ApplyParallelCESScenarios)
{
  // Scenario 1: Query that should not be rewritten (all ColumnStore tables)
  auto csOnlyCSEP = createCSEP();
  execplan::CalpontSelectExecutionPlan::TableList csTables;
  auto csTable1 = createTableAlias("test_schema", "cs_table1", "", true);
  auto csTable2 = createTableAlias("test_schema", "cs_table2", "", true);
  csTables.push_back(csTable1);
  csTables.push_back(csTable2);
  csOnlyCSEP->tableList(csTables);

  // Should not apply parallel rewrite
  EXPECT_FALSE(optimizer::details::someAreForeignTables(*csOnlyCSEP));

  // Scenario 2: Query with foreign tables but no statistics
  auto noStatsCSEP = createCSEP();
  execplan::CalpontSelectExecutionPlan::TableList mixedTables;
  auto foreignTable = createTableAlias("test_schema", "foreign_table", "", false);
  mixedTables.push_back(csTable1);
  mixedTables.push_back(foreignTable);
  noStatsCSEP->tableList(mixedTables);

  // Has foreign tables but no statistics in mock context
  EXPECT_TRUE(optimizer::details::someAreForeignTables(*noStatsCSEP));

  // Scenario 3: Complex query with multiple foreign tables and columns
  auto complexCSEP = createCSEP();
  execplan::CalpontSelectExecutionPlan::TableList complexTables;
  auto foreignTable1 = createTableAlias("schema1", "foreign_table1", "ft1", false);
  auto foreignTable2 = createTableAlias("schema2", "foreign_table2", "ft2", false);
  auto csTable = createTableAlias("schema1", "cs_table", "ct", true);
  complexTables.push_back(foreignTable1);
  complexTables.push_back(foreignTable2);
  complexTables.push_back(csTable);
  complexCSEP->tableList(complexTables);

  // Add multiple columns
  execplan::CalpontSelectExecutionPlan::ReturnedColumnList complexCols;
  auto col1 = createMockSimpleColumn("schema1", "foreign_table1", "id");
  auto col2 = createMockSimpleColumn("schema1", "foreign_table1", "name");
  auto col3 = createMockSimpleColumn("schema2", "foreign_table2", "value");
  auto col4 = createMockSimpleColumn("schema1", "cs_table", "cs_id");

  boost::shared_ptr<execplan::ReturnedColumn> col1Base = col1;
  boost::shared_ptr<execplan::ReturnedColumn> col2Base = col2;
  boost::shared_ptr<execplan::ReturnedColumn> col3Base = col3;
  boost::shared_ptr<execplan::ReturnedColumn> col4Base = col4;

  complexCols.push_back(col1Base);
  complexCols.push_back(col2Base);
  complexCols.push_back(col3Base);
  complexCols.push_back(col4Base);
  complexCSEP->returnedCols(complexCols);

  // Verify the setup
  EXPECT_TRUE(optimizer::details::someAreForeignTables(*complexCSEP));
  EXPECT_EQ(3u, complexCSEP->tableList().size());
  EXPECT_EQ(4u, complexCSEP->returnedCols().size());

  // Test that we can identify foreign vs ColumnStore tables
  const auto& retrievedTables = complexCSEP->tableList();
  EXPECT_FALSE(retrievedTables[0].fisColumnStore);  // foreign_table1
  EXPECT_FALSE(retrievedTables[1].fisColumnStore);  // foreign_table2
  EXPECT_TRUE(retrievedTables[2].fisColumnStore);   // cs_table
}

// ============================================================================
// or_to_in RBO rule tests
// ============================================================================

class OrToInTest : public RBOHybridTest
{
 protected:
  execplan::SimpleColumn* newMockCol(const std::string& schema, const std::string& table,
                                     const std::string& column, const std::string& alias = "")
  {
    auto* sc = new MockSimpleColumn(schema, table, column);
    sc->tableAlias(alias.empty() ? table : alias);
    return sc;
  }

  // Helper: create a SimpleFilter representing col = 'value'
  execplan::SimpleFilter* newEqFilter(const std::string& schema, const std::string& table,
                                      const std::string& column, const std::string& value,
                                      const std::string& alias = "")
  {
    return new execplan::SimpleFilter(execplan::SOP(new execplan::PredicateOperator("=")),
                                      newMockCol(schema, table, column, alias),
                                      new execplan::ConstantColumn(value));
  }

  // Helper: create a SimpleFilter representing col > value (non-equality)
  execplan::SimpleFilter* newGtFilter(const std::string& schema, const std::string& table,
                                      const std::string& column, const std::string& value,
                                      const std::string& alias = "")
  {
    return new execplan::SimpleFilter(execplan::SOP(new execplan::PredicateOperator(">")),
                                      newMockCol(schema, table, column, alias),
                                      new execplan::ConstantColumn(value));
  }

  // Helper: build OR(left, right) ParseTree node
  execplan::ParseTree* newOrNode(execplan::ParseTree* left, execplan::ParseTree* right)
  {
    return new execplan::ParseTree(new execplan::LogicOperator("or"), left, right);
  }

  // Helper: build AND(left, right) ParseTree node
  execplan::ParseTree* newAndNode(execplan::ParseTree* left, execplan::ParseTree* right)
  {
    return new execplan::ParseTree(new execplan::LogicOperator("and"), left, right);
  }

  // applyOrToIn completely ignores the RBOptimizerContext parameter, so we
  // provide a zeroed-memory stand-in.  Constructing a real gp_walk_info would
  // pull in server symbols (end_of_list, gp_walk_info dtor) that the test
  // target doesn't link against.
  struct DummyContext
  {
    alignas(optimizer::RBOptimizerContext) char buf[sizeof(optimizer::RBOptimizerContext)];
    optimizer::RBOptimizerContext& ref()
    {
      return *reinterpret_cast<optimizer::RBOptimizerContext*>(buf);
    }
    DummyContext()
    {
      memset(buf, 0, sizeof(buf));
    }
  };

  DummyContext dummyCtx;
};

// Test 1: Simple two-value OR → ConstantFilter with 2 entries
TEST_F(OrToInTest, SimpleTwoValueOR)
{
  execplan::CalpontSelectExecutionPlan csep;
  auto* tree = newOrNode(new execplan::ParseTree(newEqFilter("s", "t", "status", "shipped")),
                         new execplan::ParseTree(newEqFilter("s", "t", "status", "pending")));
  csep.filters(tree);

  bool applied = optimizer::applyOrToIn(csep, dummyCtx.ref());
  EXPECT_TRUE(applied);

  auto* cf = dynamic_cast<execplan::ConstantFilter*>(csep.filters()->data());
  ASSERT_NE(nullptr, cf);
  EXPECT_EQ(2u, cf->filterList().size());
  EXPECT_EQ(execplan::OP_OR, cf->op()->op());

  // Verify it's now a leaf (no children)
  EXPECT_EQ(nullptr, csep.filters()->left());
  EXPECT_EQ(nullptr, csep.filters()->right());

  // Verify the column
  auto* col = dynamic_cast<execplan::SimpleColumn*>(cf->col().get());
  ASSERT_NE(nullptr, col);
  EXPECT_EQ("status", col->columnName());
}

// Test 2: Three-value OR chain (left-leaning tree) → ConstantFilter with 3 entries
TEST_F(OrToInTest, ThreeValueORChain)
{
  // Parser builds left-leaning: OR( OR(a=1, a=2), a=3 )
  auto* innerOr = newOrNode(new execplan::ParseTree(newEqFilter("s", "t", "status", "a")),
                            new execplan::ParseTree(newEqFilter("s", "t", "status", "b")));
  auto* tree = newOrNode(innerOr, new execplan::ParseTree(newEqFilter("s", "t", "status", "c")));
  execplan::CalpontSelectExecutionPlan csep;
  csep.filters(tree);

  bool applied = optimizer::applyOrToIn(csep, dummyCtx.ref());
  EXPECT_TRUE(applied);

  auto* cf = dynamic_cast<execplan::ConstantFilter*>(csep.filters()->data());
  ASSERT_NE(nullptr, cf);
  EXPECT_EQ(3u, cf->filterList().size());
  EXPECT_EQ(nullptr, csep.filters()->left());
  EXPECT_EQ(nullptr, csep.filters()->right());
}

// Test 3: Different columns → no rewrite
TEST_F(OrToInTest, DifferentColumnsNoRewrite)
{
  auto* tree = newOrNode(new execplan::ParseTree(newEqFilter("s", "t", "status", "shipped")),
                         new execplan::ParseTree(newEqFilter("s", "t", "id", "1")));
  execplan::CalpontSelectExecutionPlan csep;
  csep.filters(tree);

  bool applied = optimizer::applyOrToIn(csep, dummyCtx.ref());
  EXPECT_FALSE(applied);

  // Tree should be unchanged — root is still a LogicOperator
  auto* logicOp = dynamic_cast<execplan::LogicOperator*>(csep.filters()->data());
  ASSERT_NE(nullptr, logicOp);
  EXPECT_EQ(execplan::OP_OR, logicOp->op());
  EXPECT_NE(nullptr, csep.filters()->left());
  EXPECT_NE(nullptr, csep.filters()->right());
}

// Test 4: Non-equality operator → no rewrite
TEST_F(OrToInTest, NonEqualityOperatorNoRewrite)
{
  auto* tree = newOrNode(new execplan::ParseTree(newGtFilter("s", "t", "amount", "10")),
                         new execplan::ParseTree(newGtFilter("s", "t", "amount", "5")));
  execplan::CalpontSelectExecutionPlan csep;
  csep.filters(tree);

  bool applied = optimizer::applyOrToIn(csep, dummyCtx.ref());
  EXPECT_FALSE(applied);

  auto* logicOp = dynamic_cast<execplan::LogicOperator*>(csep.filters()->data());
  ASSERT_NE(nullptr, logicOp);
}

// Test 5: OR nested inside AND — only OR subtree is rewritten
TEST_F(OrToInTest, ORNestedInsideAND)
{
  // AND( OR(status='a', status='b'), amount > 10 )
  auto* orSubtree = newOrNode(new execplan::ParseTree(newEqFilter("s", "t", "status", "a")),
                              new execplan::ParseTree(newEqFilter("s", "t", "status", "b")));
  auto* tree = newAndNode(orSubtree, new execplan::ParseTree(newGtFilter("s", "t", "amount", "10")));
  execplan::CalpontSelectExecutionPlan csep;
  csep.filters(tree);

  bool applied = optimizer::applyOrToIn(csep, dummyCtx.ref());
  EXPECT_TRUE(applied);

  // Root should still be AND
  auto* rootOp = dynamic_cast<execplan::LogicOperator*>(csep.filters()->data());
  ASSERT_NE(nullptr, rootOp);
  EXPECT_EQ(execplan::OP_AND, rootOp->op());

  // Left child should now be a ConstantFilter
  ASSERT_NE(nullptr, csep.filters()->left());
  auto* cf = dynamic_cast<execplan::ConstantFilter*>(csep.filters()->left()->data());
  ASSERT_NE(nullptr, cf);
  EXPECT_EQ(2u, cf->filterList().size());

  // Right child should still be a SimpleFilter (amount > 10)
  ASSERT_NE(nullptr, csep.filters()->right());
  auto* sf = dynamic_cast<execplan::SimpleFilter*>(csep.filters()->right()->data());
  ASSERT_NE(nullptr, sf);
}

// Test 6: Null filter tree → returns false
TEST_F(OrToInTest, NullFilterTree)
{
  execplan::CalpontSelectExecutionPlan csep;
  // filters() is nullptr by default

  bool applied = optimizer::applyOrToIn(csep, dummyCtx.ref());
  EXPECT_FALSE(applied);
}

// Test 7: Single SimpleFilter leaf (no OR) → no rewrite
TEST_F(OrToInTest, SingleSimpleFilterNoRewrite)
{
  execplan::CalpontSelectExecutionPlan csep;
  csep.filters(new execplan::ParseTree(newEqFilter("s", "t", "status", "shipped")));

  bool applied = optimizer::applyOrToIn(csep, dummyCtx.ref());
  EXPECT_FALSE(applied);

  // Should still be a SimpleFilter
  auto* sf = dynamic_cast<execplan::SimpleFilter*>(csep.filters()->data());
  ASSERT_NE(nullptr, sf);
}

// Test 8: Constant on the left side (reversed operands) → still rewritten
TEST_F(OrToInTest, ConstantOnLeftSide)
{
  // Build: OR( 'shipped'=status, 'pending'=status )
  auto* leftSF = new execplan::SimpleFilter(execplan::SOP(new execplan::PredicateOperator("=")),
                                            new execplan::ConstantColumn("shipped"),
                                            newMockCol("s", "t", "status"));
  auto* rightSF = new execplan::SimpleFilter(execplan::SOP(new execplan::PredicateOperator("=")),
                                             new execplan::ConstantColumn("pending"),
                                             newMockCol("s", "t", "status"));
  auto* tree = newOrNode(new execplan::ParseTree(leftSF), new execplan::ParseTree(rightSF));
  execplan::CalpontSelectExecutionPlan csep;
  csep.filters(tree);

  bool applied = optimizer::applyOrToIn(csep, dummyCtx.ref());
  EXPECT_TRUE(applied);

  auto* cf = dynamic_cast<execplan::ConstantFilter*>(csep.filters()->data());
  ASSERT_NE(nullptr, cf);
  EXPECT_EQ(2u, cf->filterList().size());
}

// Test: orToInFilter returns true when filters exist, false otherwise
TEST_F(OrToInTest, FilterCheckFunction)
{
  execplan::CalpontSelectExecutionPlan csepNoFilters;
  EXPECT_FALSE(optimizer::orToInFilter(csepNoFilters, dummyCtx.ref()));

  execplan::CalpontSelectExecutionPlan csepWithFilters;
  csepWithFilters.filters(new execplan::ParseTree(newEqFilter("s", "t", "status", "shipped")));
  EXPECT_TRUE(optimizer::orToInFilter(csepWithFilters, dummyCtx.ref()));
}

// Test: AND of two ORs on different columns — both rewritten independently
TEST_F(OrToInTest, ANDOfTwoORsDifferentColumns)
{
  // AND( OR(status='a', status='b'), OR(id=1, id=2) )
  auto* orLeft = newOrNode(new execplan::ParseTree(newEqFilter("s", "t", "status", "a")),
                           new execplan::ParseTree(newEqFilter("s", "t", "status", "b")));
  auto* orRight = newOrNode(new execplan::ParseTree(newEqFilter("s", "t", "id", "1")),
                            new execplan::ParseTree(newEqFilter("s", "t", "id", "2")));
  auto* tree = newAndNode(orLeft, orRight);
  execplan::CalpontSelectExecutionPlan csep;
  csep.filters(tree);

  bool applied = optimizer::applyOrToIn(csep, dummyCtx.ref());
  EXPECT_TRUE(applied);

  // Root is still AND
  auto* rootOp = dynamic_cast<execplan::LogicOperator*>(csep.filters()->data());
  ASSERT_NE(nullptr, rootOp);
  EXPECT_EQ(execplan::OP_AND, rootOp->op());

  // Left child: ConstantFilter on status
  auto* cfLeft = dynamic_cast<execplan::ConstantFilter*>(csep.filters()->left()->data());
  ASSERT_NE(nullptr, cfLeft);
  EXPECT_EQ(2u, cfLeft->filterList().size());
  auto* colLeft = dynamic_cast<execplan::SimpleColumn*>(cfLeft->col().get());
  ASSERT_NE(nullptr, colLeft);
  EXPECT_EQ("status", colLeft->columnName());

  // Right child: ConstantFilter on id
  auto* cfRight = dynamic_cast<execplan::ConstantFilter*>(csep.filters()->right()->data());
  ASSERT_NE(nullptr, cfRight);
  EXPECT_EQ(2u, cfRight->filterList().size());
  auto* colRight = dynamic_cast<execplan::SimpleColumn*>(cfRight->col().get());
  ASSERT_NE(nullptr, colRight);
  EXPECT_EQ("id", colRight->columnName());
}

// Test: Four-value OR chain → ConstantFilter with 4 entries
TEST_F(OrToInTest, FourValueORChain)
{
  // Left-leaning: OR( OR( OR(a, b), c), d)
  auto* or1 = newOrNode(new execplan::ParseTree(newEqFilter("s", "t", "status", "a")),
                        new execplan::ParseTree(newEqFilter("s", "t", "status", "b")));
  auto* or2 = newOrNode(or1, new execplan::ParseTree(newEqFilter("s", "t", "status", "c")));
  auto* or3 = newOrNode(or2, new execplan::ParseTree(newEqFilter("s", "t", "status", "d")));
  execplan::CalpontSelectExecutionPlan csep;
  csep.filters(or3);

  bool applied = optimizer::applyOrToIn(csep, dummyCtx.ref());
  EXPECT_TRUE(applied);

  auto* cf = dynamic_cast<execplan::ConstantFilter*>(csep.filters()->data());
  ASSERT_NE(nullptr, cf);
  EXPECT_EQ(4u, cf->filterList().size());
}
