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

#ifndef DATABASESQLITE_H
#define DATABASESQLITE_H

#if defined(__USE_SQLITE__) || defined(__ALLDB__)

#include "database.h"
#include <sqlite3.h>

class DatabaseSQLite : public Database
{
	public:
		DatabaseSQLite();
		virtual ~DatabaseSQLite() {sqlite3_close(m_handle);}

		bool getParam(DBParam_t param);

		bool beginTransaction() {return query("BEGIN");}
		bool rollback() {return query("ROLLBACK");}
		bool commit() {return query("COMMIT");}

		bool query(const std::string& query);
		DBResult_ptr storeQuery(const std::string& query);

		std::string escapeString(const std::string& s);
		std::string escapeBlob(const char* s, uint32_t length);

		uint64_t getLastInsertId() {return (uint64_t)sqlite3_last_insert_rowid(m_handle);}

		std::string getStringComparer() {return "LIKE ";}
		std::string getUpdateLimiter() {return ";";}
		DatabaseEngine_t getDatabaseEngine() {return DATABASE_ENGINE_SQLITE;}

	protected:
		std::string _parse(const std::string& s);

		std::recursive_mutex sqliteLock;
		sqlite3* m_handle;
};

class SQLiteResult : public DBResult
{
	friend class DatabaseSQLite;

	public:
		int32_t getDataInt(const std::string& s);
		int64_t getDataLong(const std::string& s);
		std::string getDataString(const std::string& s);
		const char* getDataStream(const std::string& s, uint64_t& size);

		void free();
		bool next() {return sqlite3_step(m_handle) == SQLITE_ROW;}

		SQLiteResult(sqlite3_stmt* stmt);
		~SQLiteResult();
	protected:

		std::map<const std::string, uint32_t> m_listNames;

		sqlite3_stmt* m_handle;
};
#endif

#endif
