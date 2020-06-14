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

#ifndef __DATABASE_PGSQL__
#define __DATABASE_PGSQL__

#if defined(__USE_PGSQL__) || defined(__ALLDB__)

#include "database.h"
#include "libpq-fe.h"

class DatabasePgSQL : public Database
{
	public:
		DatabasePgSQL();
		virtual ~DatabasePgSQL() {PQfinish(m_handle);}

		bool getParam(DBParam_t param);

		bool beginTransaction() {return query("BEGIN");}
		bool rollback() {return query("ROLLBACK");}
		bool commit() {return query("COMMIT");}

		bool query(const std::string& query);
		DBResult_ptr storeQuery(const std::string& query);

		std::string escapeString(const std::string& s);
		std::string escapeBlob(const char *s, uint32_t length);

		uint64_t getLastInsertId();
		DatabaseEngine_t getDatabaseEngine() {return DATABASE_ENGINE_POSTGRESQL;}

	protected:
		std::string _parse(const std::string& s);
		PGconn* m_handle;
};

class PgSQLResult : public DBResult
{
	friend class DatabasePgSQL;

	public:
		int32_t getDataInt(const std::string& s) {return atoi(
			PQgetvalue(m_handle, m_cursor, PQfnumber(m_handle, s.c_str())));}
		int64_t getDataLong(const std::string& s) {return atoll(
			PQgetvalue(m_handle, m_cursor, PQfnumber(m_handle, s.c_str())));}
		std::string getDataString(const std::string& s) {return std::string(
			PQgetvalue(m_handle, m_cursor, PQfnumber(m_handle, s.c_str())));}
		const char* getDataStream(const std::string& s, uint64_t& size);

		void free();
		bool next();

		PgSQLResult(PGresult* results);
		~PgSQLResult();
	protected:

		PGresult* m_handle;
		int32_t m_rows, m_cursor;
};
#endif

#endif