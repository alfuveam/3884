////////////////////////////////////////////////////////////////////////
// OpenTibia - an opensource roleplaying game
////////////////////////////////////////////////////////////////////////
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
////////////////////////////////////////////////////////////////////////

#include "otpch.h"
#include "database.h"

#if !defined(__ALLDB__)
	#if !defined(__USE_MYSQL__) && !defined(__USE_PGSQL__) && !defined(__USE_SQLITE__) && !defined(__USE_ODBC__)
		#error You must define one Database.
	#endif
// #else
// 	#if defined(__USE_MYSQL__) && defined(__USE_PGSQL__) 
// 		#error To all database use __ALLDB__ in preprocessor.
// 	#endif	
#endif

#if defined(__USE_MYSQL__) || defined(__ALLDB__)
	#include "databasemysql.h"
#endif

#if defined(__USE_ODBC__) || defined(__ALLDB__)
	#include "databaseodbc.h"
#endif

#if defined(__USE_SQLITE__) || defined(__ALLDB__)
	#include "databasesqlite.h"
#endif

#if defined(__USE_PGSQL__) || defined(__ALLDB__)
	#include "databasepgsql.h"
#endif

#include "configmanager.h"
extern ConfigManager g_config;

std::recursive_mutex DBQuery::databaseLock;
Database* Database::_instance = NULL;

Database* Database::getInstance()
{
	if(!_instance)
	{
	#ifdef __USE_MYSQL__
			if(g_config.getString(ConfigManager::SQL_TYPE) == "mysql")
				_instance = new DatabaseMySQL;
	#endif
	#ifdef __USE_ODBC__
		if(g_config.getString(ConfigManager::SQL_TYPE) == "odbc")
		_instance = new DatabaseODBC;
	#endif
	#ifdef __USE_SQLITE__
			if(g_config.getString(ConfigManager::SQL_TYPE) == "sqlite")
				_instance = new DatabaseSQLite;
	#endif
	#ifdef __USE_PGSQL__
			if(g_config.getString(ConfigManager::SQL_TYPE) == "pgsql")
				_instance = new DatabasePgSQL;
	#endif
	}

	_instance->use();
	return _instance;
}

DBResult_ptr Database::verifyResult(DBResult_ptr result)
{
	if(result->next())
		return result;

	result->free();
	result = NULL;
	return NULL;
}

DBInsert::DBInsert(Database* db)
{
	m_db = db;
	m_rows = 0;
	// checks if current database engine supports multiline INSERTs
	m_multiLine = m_db->getParam(DBPARAM_MULTIINSERT);
}

void DBInsert::setQuery(const std::string& query)
{
	m_query = query;
	m_buf = "";
	m_rows = 0;
}

bool DBInsert::addRow(const std::string& row)
{
	if(!m_multiLine) // executes INSERT for current row
		return m_db->query(m_query + "(" + row + ")");

	m_rows++;
	int32_t size = m_buf.length();
	// adds new row to buffer
	if(!size)
		m_buf = "(" + row + ")";
	else if(size > 8192)
	{
		if(!execute())
			return false;

		m_buf = "(" + row + ")";
	}
	else
		m_buf += ",(" + row + ")";

	return true;
}

bool DBInsert::addRow(std::stringstream& row)
{
	bool ret = addRow(row.str());
	row.str("");
	return ret;
}

bool DBInsert::execute()
{
	if(!m_multiLine || m_buf.length() < 1 || !m_rows) // INSERTs were executed on-fly or there's no rows to execute
		return true;

	m_rows = 0;
	// executes buffer
	bool ret = m_db->query(m_query + m_buf);
	m_buf = "";
	return ret;
}
