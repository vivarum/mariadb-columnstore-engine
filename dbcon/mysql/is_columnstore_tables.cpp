/* c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil
 * vi: set shiftwidth=4 tabstop=4 expandtab:
 *  :indentSize=4:tabSize=4:noTabs=true:
 *
 * Copyright (C) 2016 MariaDB Corporation
 *
 * This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of
 * the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#define PREFER_MY_CONFIG_H
#include "idb_mysql.h"
#include <vector>

#include <boost/shared_ptr.hpp>
#include "calpontsystemcatalog.h"
#include "dataconvert.h"
#include "is_columnstore.h"
#include "dbrm.h"

// Required declaration as it isn't in a MairaDB include
bool schema_table_store_record(THD* thd, TABLE* table);

const int BYTE_PER_BLOCK = 8192;

ST_FIELD_INFO is_columnstore_tables_fields[] = {
    Show::Column("TABLE_SCHEMA", Show::Varchar(64), NOT_NULL),
    Show::Column("TABLE_NAME", Show::Varchar(64), NOT_NULL),
    Show::Column("OBJECT_ID", Show::SLong(0), NOT_NULL),
    Show::Column("CREATION_DATE", Show::Datetime(0), NOT_NULL),  // TODO: Make a date if possible
    Show::Column("COLUMN_COUNT", Show::SLong(0), NOT_NULL),
    Show::Column("AUTOINCREMENT", Show::SLong(0), NULLABLE),
    Show::Column("ROW_COUNT_ESTIMATE", Show::ULonglong(0), NULLABLE),
    Show::CEnd()};

static uint64_t calculateRowCountEstimate(BRM::DBRM* emp, BRM::OID_t firstColumnOid)
{
  if (!emp || firstColumnOid == 0)
    return 0;

  std::vector<struct BRM::EMEntry> entries;
  int rc = emp->getExtents(firstColumnOid, entries, false, false, true);

  if (rc != 0 || entries.empty())
    return 0;

  uint64_t totalRows = 0;

  for (const auto& entry : entries)
  {
    if (entry.colWid > 0)
    {
      uint64_t rowsPerBlock = BYTE_PER_BLOCK / entry.colWid;
      totalRows += (entry.HWM + 1) * rowsPerBlock;
    }
  }

  return totalRows;
}

static int is_columnstore_tables_fill(THD* thd, TABLE_LIST* tables, COND* cond)
{
  CHARSET_INFO* cs = system_charset_info;
  TABLE* table = tables->table;
  InformationSchemaCond isCond;

  execplan::CalpontSystemCatalog csc;
  csc.identity(execplan::CalpontSystemCatalog::FE);

  BRM::DBRM::refreshShmWithLock();
  std::unique_ptr<BRM::DBRM> emp(new BRM::DBRM());

  if (!emp || !emp->isDBRMReady())
  {
    return 1;
  }

  if (cond)
  {
    isCond.getCondItems(cond);
  }

  const std::vector<
      std::pair<execplan::CalpontSystemCatalog::OID, execplan::CalpontSystemCatalog::TableName> >
      catalog_tables = csc.getTables();

  for (std::vector<std::pair<execplan::CalpontSystemCatalog::OID,
                             execplan::CalpontSystemCatalog::TableName> >::const_iterator it =
           catalog_tables.begin();
       it != catalog_tables.end(); ++it)
  {
    if (!isCond.match((*it).second.schema, (*it).second.table))
      continue;

    try
    {
      execplan::CalpontSystemCatalog::TableInfo tb_info = csc.tableInfo((*it).second);
      std::string create_date = dataconvert::DataConvert::dateToString((*it).second.create_date);
      table->field[0]->store((*it).second.schema.c_str(), (*it).second.schema.length(), cs);
      table->field[1]->store((*it).second.table.c_str(), (*it).second.table.length(), cs);
      table->field[2]->store((*it).first);
      table->field[3]->store(create_date.c_str(), create_date.length(), cs);
      table->field[4]->store(tb_info.numOfCols);

      if (tb_info.tablewithautoincr)
      {
        table->field[5]->set_notnull();
        table->field[5]->store(csc.nextAutoIncrValue((*it).second));
      }
      else
      {
        table->field[5]->set_null();
      }

      table->field[5]->store(tb_info.tablewithautoincr);

      if (tb_info.numOfCols > 0)
      {
        execplan::CalpontSystemCatalog::RIDList ridList = csc.columnRIDs(it->second);
        if (!ridList.empty())
        {
          BRM::OID_t firstColumnOid = ridList[0].objnum;
          uint64_t rowCount = calculateRowCountEstimate(emp.get(), firstColumnOid);
          if (rowCount > 0)
          {
            table->field[6]->set_notnull();
            table->field[6]->store(rowCount, true);
          }
          else
          {
            table->field[6]->set_null();
          }
        }
        else
        {
          table->field[6]->set_null();
        }
      }
      else
      {
        table->field[6]->set_null();
      }

      if (schema_table_store_record(thd, table))
        return 1;
    }
    catch (std::runtime_error& e)
    {
      std::cerr << e.what() << std::endl;
    }
  }

  return 0;
}

int is_columnstore_tables_plugin_init(void* p)
{
  ST_SCHEMA_TABLE* schema = (ST_SCHEMA_TABLE*)p;
  schema->fields_info = is_columnstore_tables_fields;
  schema->fill_table = is_columnstore_tables_fill;
  return 0;
}
