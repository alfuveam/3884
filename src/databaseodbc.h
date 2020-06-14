//////////////////////////////////////////////////////////////////////
// OpenTibia - an opensource roleplaying game
//////////////////////////////////////////////////////////////////////
//
//////////////////////////////////////////////////////////////////////
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//////////////////////////////////////////////////////////////////////


#ifndef __OTSERV_DATABASEODBC_H__
#define __OTSERV_DATABASEODBC_H__

#if defined(__USE_ODBC__) || defined(__ALLDB__)

#include "database.h"
#ifdef WIN32
	#include <windows.h>
#else
	#include <sqltypes.h>
#endif

#include <sql.h>
#include <sqlext.h>

class DatabaseODBC : public Database
{
public:
  DatabaseODBC();
  virtual ~DatabaseODBC();

  bool getParam(DBParam_t param);

  bool beginTransaction();
  bool rollback();
  bool commit();

  bool executeQuery(const std::string &query) { return false; }; // todo
  
  uint64_t getLastInsertedRowID(){ return 0; }; // todo

  std::string escapeString(const std::string &s);
  std::string escapeBlob(const char* s, uint32_t length);

protected:
  bool internalQuery(const std::string &query);

  std::string _parse(const std::string &s);

  SQLHDBC m_handle;
  SQLHENV m_env;
};

class ODBCResult : public DBResult
{
  friend class DatabaseODBC;

public:
  int32_t getDataInt(const std::string &s);
  uint32_t getDataUInt(const std::string &s);
  int64_t getDataLong(const std::string &s);
  std::string getDataString(const std::string &s);
  const char* getDataStream(const std::string &s, uint64_t &size);

  bool empty();

  ODBCResult(SQLHSTMT stmt);
  ~ODBCResult();
protected:

  std::map<const std::string, uint32_t> m_listNames;

  bool m_rowAvailable;

  SQLHSTMT m_handle;
};

#endif

#endif