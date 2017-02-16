//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// binder_test.cpp
//
// Identification: test/binder/binder_test.cpp
//
// Copyright (c) 2015-16, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "catalog/catalog.h"
#include "common/harness.h"
#include "common/statement.h"
#include "expression/tuple_value_expression.h"
#include "binder/bind_node_visitor.h"
#include "parser/parser.h"
#include "optimizer/simple_optimizer.h"
#include "tcop/tcop.h"

#include <memory>

using std::string;
using std::unique_ptr;
using std::vector;
using std::make_tuple;

namespace peloton {
namespace test {

class BinderCorrectnessTest : public PelotonTest {};

TEST_F(BinderCorrectnessTest, SelectStatementTest) {
  LOG_INFO("Creating default database");
  catalog::Catalog::GetInstance()->CreateDatabase(DEFAULT_DB_NAME, nullptr);
  LOG_INFO("Default database created!");

  auto& txn_manager = concurrency::TransactionManagerFactory::GetInstance();
  auto& parser = parser::Parser::GetInstance();
  auto& traffic_cop = tcop::TrafficCop::GetInstance();
  catalog::Catalog* catalog_ptr = catalog::Catalog::GetInstance();
  optimizer::SimpleOptimizer optimizer;
  

  vector<string> createTableSQLs{"CREATE TABLE A(A1 int, a2 varchar)",
                                  "CREATE TABLE b(B1 int, b2 varchar)"};
  auto txn = txn_manager.BeginTransaction();
  for (auto& sql : createTableSQLs) {
    LOG_INFO("%s", sql.c_str());
    vector<type::Value> params;
    vector<StatementResult> result;
    vector<int> result_format;
    unique_ptr<Statement> statement(new Statement("CREATE", sql));
    auto parse_tree = parser.BuildParseTree(sql);
    statement->SetPlanTree(optimizer.BuildPelotonPlanTree(parse_tree));
    auto status = traffic_cop.ExecuteStatementPlan(statement->GetPlanTree().get(), params, result, result_format);
    LOG_INFO("Table create result: %s", ResultTypeToString(status.m_result).c_str());
  }
  txn_manager.CommitTransaction(txn);
  
  // Test regular table name
  LOG_INFO("Parsing sql query");
  unique_ptr<binder::BindNodeVisitor> binder(new binder::BindNodeVisitor());
  string selectSQL = "SELECT A.a1, B.b2 FROM A INNER JOIN b ON a.a1 = b.b1 "
                     "WHERE a1 < 100 GROUP BY A.a1, B.b2 HAVING a1 > 50 "
                     "ORDER BY a1";

  auto parser_tree = parser.BuildParseTree(selectSQL);
  auto selectStmt = dynamic_cast<parser::SelectStatement*>(parser_tree->GetStatements().at(0));
  binder->BindNameToNode(selectStmt);
  
  oid_t db_oid = catalog_ptr->GetDatabaseWithName(DEFAULT_DB_NAME)->GetOid();
  oid_t tableA_oid = catalog_ptr->GetTableWithName(DEFAULT_DB_NAME, "a")->GetOid();
  oid_t tableB_oid = catalog_ptr->GetTableWithName(DEFAULT_DB_NAME, "b")->GetOid();
  
  // Check select_list
  LOG_INFO("Checking select list");
  auto tupleExpr = (expression::TupleValueExpression*)(*selectStmt->select_list)[0];
  EXPECT_EQ(tupleExpr->BoundObjectId, make_tuple(db_oid, tableA_oid, 0)); // A.a1
  tupleExpr = (expression::TupleValueExpression*)(*selectStmt->select_list)[1];
  EXPECT_EQ(tupleExpr->BoundObjectId, make_tuple(db_oid, tableB_oid, 1)); // B.b2


  // Check join condition
  LOG_INFO("Checking join condition");
  tupleExpr = (expression::TupleValueExpression*)selectStmt->from_table->join->
    condition->GetChild(0);
  EXPECT_EQ(tupleExpr->BoundObjectId, make_tuple(db_oid, tableA_oid, 0)); // a.a1
  tupleExpr = (expression::TupleValueExpression*)selectStmt->from_table->join->
    condition->GetChild(1);
  EXPECT_EQ(tupleExpr->BoundObjectId, make_tuple(db_oid, tableB_oid, 0)); // b.b1
  
  // Check Where clause
  LOG_INFO("Checking where clause");
  tupleExpr = (expression::TupleValueExpression*)selectStmt->where_clause->GetChild(0);
  EXPECT_EQ(tupleExpr->BoundObjectId, make_tuple(db_oid, tableA_oid, 0)); // a1
  
  // Check Group By and Having
  LOG_INFO("Checking group by");
  tupleExpr = (expression::TupleValueExpression*)selectStmt->group_by->columns->at(0);
  EXPECT_EQ(tupleExpr->BoundObjectId, make_tuple(db_oid, tableA_oid, 0)); // A.a1
  tupleExpr = (expression::TupleValueExpression*)selectStmt->group_by->columns->at(1);
  EXPECT_EQ(tupleExpr->BoundObjectId, make_tuple(db_oid, tableB_oid, 1)); // B.b2
  tupleExpr = (expression::TupleValueExpression*)selectStmt->group_by->having->GetChild(0);
  EXPECT_EQ(tupleExpr->BoundObjectId, make_tuple(db_oid, tableA_oid, 0)); // a1

  // Check Order By
  LOG_INFO("Checking order by");
  tupleExpr = (expression::TupleValueExpression*)selectStmt->order->expr;
  EXPECT_EQ(tupleExpr->BoundObjectId, make_tuple(db_oid, tableA_oid, 0)); // a1
  
  // TODO: Test alias ambiguous
  // TODO: Test alias and select_list
  
  // Delete the test database
  catalog_ptr->DropDatabaseWithName(DEFAULT_DB_NAME, nullptr);
}
} // End test namespace
} // End peloton namespace