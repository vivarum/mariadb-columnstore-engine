/* Copyright (C) 2025 MariaDB Corporation

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; version 2 of
   the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA. */

#include "rbo_or_to_in.h"

#include "execplan/calpontselectexecutionplan.h"
#include "execplan/simplecolumn.h"
#include "constantcolumn.h"
#include "constantfilter.h"
#include "logicoperator.h"
#include "operator.h"
#include "parsetree.h"
#include "simplefilter.h"

namespace optimizer
{

using namespace execplan;

// Extract the SimpleColumn from a SimpleFilter that is an equality predicate
// with a ConstantColumn on one side. Returns nullptr if the filter doesn't match.
static SimpleColumn* getEqColumnSide(SimpleFilter* sf)
{
  if (!sf || sf->op()->op() != OP_EQ)
    return nullptr;

  auto* lsc = dynamic_cast<SimpleColumn*>(sf->lhs());
  auto* rcc = dynamic_cast<ConstantColumn*>(sf->rhs());

  if (lsc && rcc)
    return lsc;

  // Check the reversed case: constant on the left
  auto* lcc = dynamic_cast<ConstantColumn*>(sf->lhs());
  auto* rsc = dynamic_cast<SimpleColumn*>(sf->rhs());

  if (lcc && rsc)
    return rsc;

  return nullptr;
}

// Check if two SimpleColumns reference the same column
static bool sameColumn(const SimpleColumn* a, const SimpleColumn* b)
{
  return a->schemaName() == b->schemaName() && a->tableName() == b->tableName() &&
         a->columnName() == b->columnName() && a->tableAlias() == b->tableAlias();
}

// Recursive post-order walk of the filter tree, rewriting OR-chains into ConstantFilters.
// Returns true if any rewrite was performed in this subtree.
static bool rewriteOrSubtree(ParseTree* node)
{
  if (!node)
    return false;

  bool changed = false;

  // Post-order: process children first so chains build bottom-up
  changed |= rewriteOrSubtree(node->left());
  changed |= rewriteOrSubtree(node->right());

  // This node must be a LogicOperator(OR) with both children present
  auto* logicOp = dynamic_cast<LogicOperator*>(node->data());

  if (!logicOp || logicOp->op() != OP_OR)
    return changed;

  if (!node->left() || !node->right())
    return changed;

  // Case 1: Both children are SimpleFilter leaves
  auto* leftSF = dynamic_cast<SimpleFilter*>(node->left()->data());
  auto* rightSF = dynamic_cast<SimpleFilter*>(node->right()->data());

  if (leftSF && rightSF)
  {
    SimpleColumn* leftCol = getEqColumnSide(leftSF);
    SimpleColumn* rightCol = getEqColumnSide(rightSF);

    if (!leftCol || !rightCol || !sameColumn(leftCol, rightCol))
      return changed;

    // Build a ConstantFilter from the two SimpleFilters
    ConstantFilter* cf = new ConstantFilter();
    cf->op(SOP(new LogicOperator("or")));
    cf->col(SRCP(leftCol->clone()));
    cf->pushFilter(leftSF->clone());
    cf->pushFilter(rightSF->clone());

    // Replace this ParseTree node: delete old children and data, set new data
    delete node->left();
    delete node->right();
    node->nullLeft();
    node->nullRight();
    delete node->data();
    node->data(cf);

    return true;
  }

  // Case 2: Left child was already rewritten to a ConstantFilter, right is a SimpleFilter
  auto* leftCF = dynamic_cast<ConstantFilter*>(node->left()->data());

  if (leftCF && rightSF)
  {
    SimpleColumn* rightCol = getEqColumnSide(rightSF);
    auto* cfCol = dynamic_cast<SimpleColumn*>(leftCF->col().get());

    if (rightCol && cfCol && sameColumn(cfCol, rightCol) && leftCF->op()->op() == OP_OR)
    {
      // Merge: add the right SimpleFilter into the existing ConstantFilter
      leftCF->pushFilter(rightSF->clone());

      // Move the ConstantFilter up to this node
      // Steal data from left child, delete both children
      node->left()->data(nullptr);  // prevent left's destructor from deleting the CF
      delete node->left();
      delete node->right();
      node->nullLeft();
      node->nullRight();
      delete node->data();  // delete the LogicOperator
      node->data(leftCF);

      return true;
    }
  }

  // Case 3: Right child is a ConstantFilter, left is a SimpleFilter
  auto* rightCF = dynamic_cast<ConstantFilter*>(node->right()->data());

  if (rightCF && leftSF)
  {
    SimpleColumn* leftCol = getEqColumnSide(leftSF);
    auto* cfCol = dynamic_cast<SimpleColumn*>(rightCF->col().get());

    if (leftCol && cfCol && sameColumn(cfCol, leftCol) && rightCF->op()->op() == OP_OR)
    {
      rightCF->pushFilter(leftSF->clone());

      node->right()->data(nullptr);
      delete node->left();
      delete node->right();
      node->nullLeft();
      node->nullRight();
      delete node->data();
      node->data(rightCF);

      return true;
    }
  }

  return changed;
}

bool orToInFilter(execplan::CalpontSelectExecutionPlan& csep, optimizer::RBOptimizerContext& /*ctx*/)
{
  return csep.filters() != nullptr;
}

bool applyOrToIn(execplan::CalpontSelectExecutionPlan& csep, optimizer::RBOptimizerContext& /*ctx*/)
{
  ParseTree* pt = csep.filters();

  if (!pt)
    return false;

  return rewriteOrSubtree(pt);
}

}  // namespace optimizer
