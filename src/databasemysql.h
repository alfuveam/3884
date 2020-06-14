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

#ifndef __DATABASEMYSQL__
#define __DATABASEMYSQL__

#if defined(__USE_MYSQL__) || defined(__ALLDB__)

#include "database.h"
#include <mysql/mysql.h>

class DatabaseMySQL : public Database
{
	public:
		DatabaseMySQL();
		virtual ~DatabaseMySQL();

		bool getParam(DBParam_t param);

		bool beginTransaction() {return query("BEGIN");}
		bool rollback();
		bool commit();

		bool query(const std::string& query);
		DBResult_ptr storeQuery(const std::string& query);

		std::string escapeString(const std::string &s) {return escapeBlob(s.c_str(), s.length());}
		std::string escapeBlob(const char* s, uint32_t length);

		uint64_t getLastInsertId() {return (uint64_t)mysql_insert_id(&m_handle);}
		DatabaseEngine_t getDatabaseEngine() {return DATABASE_ENGINE_MYSQL;}

	protected:
		void keepAlive();

		MYSQL m_handle;
		uint32_t m_timeoutTask;
};

class MySQLResult : public DBResult
{
	friend class DatabaseMySQL;
	public:
		int32_t getDataInt(const std::string& s);
		int64_t getDataLong(const std::string& s);
		std::string getDataString(const std::string& s);
		const char* getDataStream(const std::string& s, uint64_t& size);

		void free();
		bool next();

		MySQLResult(MYSQL_RES* result);
		~MySQLResult();
	protected:

		std::map<const std::string, uint32_t> m_listNames;

		MYSQL_RES* m_handle;
		MYSQL_ROW m_row;
};
#endif
#endif
