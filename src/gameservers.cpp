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

#include "gameservers.h"
#include "tools.h"

void GameServers::clear()
{
	for(GameServersMap::iterator it = serverList.begin(); it != serverList.end(); ++it)
		delete it->second;

	serverList.clear();
}

bool GameServers::reload()
{
	clear();
	return loadFromXml(false);
}

bool GameServers::loadFromXml(bool result)
{
	pugi::xml_attribute attr;
	pugi::xml_document doc;
	pugi::xml_parse_result _result = doc.load_file(getFilePath(FILE_TYPE_XML, "servers.xml").c_str());
	
	if(!_result)
	{
		printXMLError("[Warning - GameServers::loadFromXml] Cannot load servers file.", "servers.xml", _result);
		return false;
	}

	if(!doc.child("servers"))
	{
		printXMLError("[Error - GameServers::loadFromXml] Malformed servers file.", "servers.xml", _result);
		return false;
	}

	std::string strValue;	
	for(auto serverNode : doc.child("servers").children())
	{
		std::string name, address;
		uint32_t id, versionMin, versionMax, port;
		if((attr = serverNode.attribute("id"))){
			id = attr.as_uint();
		}
		else
		{
			std::clog << "[Error - GameServers::loadFromXml] Missing id, skipping" << std::endl;			
			continue;
		}

		if(getServerById(id))
		{
			std::clog << "[Error - GameServers::loadFromXml] Duplicate server id " << id << ", skipping" << std::endl;			
			continue;
		}

		if((attr = serverNode.attribute("name"))){
			name = attr.as_string();
		}
		else
		{
			name = "Server #" + std::to_string(id);
			std::clog << "[Warning - GameServers::loadFromXml] Missing name for server " << id << ", using default" << std::endl;
		}

		if((attr = serverNode.attribute("versionMin")))
		{
			versionMin = attr.as_uint();
		}
		else
		{
			versionMin = CLIENT_VERSION_MIN;
			std::clog << "[Warning - GameServers::loadFromXml] Missing versionMin for server " << id << ", using default" << std::endl;
		}

		if((attr = serverNode.attribute("versionMax"))){
			versionMax = attr.as_uint();
		}
		else
		{
			versionMax = CLIENT_VERSION_MAX;
			std::clog << "[Warning - GameServers::loadFromXml] Missing versionMax for server " << id << ", using default" << std::endl;
		}

		if((attr = serverNode.attribute("address")) || (attr = serverNode.attribute("ip")))
		{
			address = attr.as_string();
		}
		else
		{
			address = "localhost";
			std::clog << "[Warning - GameServers::loadFromXml] Missing address for server " << id << ", using default" << std::endl;
		}

		if((attr = serverNode.attribute("port")))
		{	
			port = attr.as_uint();
		}
		else
		{
			port = 7171;
			std::clog << "[Warning - GameServers::loadFromXml] Missing port for server " << id << ", using default" << std::endl;
		}

		if(GameServer* server = new GameServer(name, versionMin, versionMax, inet_addr(address.c_str()), port))
			serverList[id] = server;
		else
			std::clog << "[Error - GameServers::loadFromXml] Couldn't add server " << name << std::endl;
	}

	if(result)
	{
		std::clog << "> Servers loaded:" << std::endl;
		for(GameServersMap::iterator it = serverList.begin(); it != serverList.end(); it++)
			std::clog << it->second->getName() << " (" << it->second->getAddress() << ":" << it->second->getPort() << ")" << std::endl;
	}	
	return true;
}

GameServer* GameServers::getServerById(uint32_t id) const
{
	GameServersMap::const_iterator it = serverList.find(id);
	if(it != serverList.end())
		return it->second;

	return NULL;
}

GameServer* GameServers::getServerByName(std::string name) const
{
	for(GameServersMap::const_iterator it = serverList.begin(); it != serverList.end(); ++it)
	{
		if(it->second->getName() == name)
			return it->second;
	}

	return NULL;
}

GameServer* GameServers::getServerByAddress(uint32_t address) const
{
	for(GameServersMap::const_iterator it = serverList.begin(); it != serverList.end(); ++it)
	{
		if(it->second->getAddress() == address)
			return it->second;
	}

	return NULL;
}

GameServer* GameServers::getServerByPort(uint32_t port) const
{
	for(GameServersMap::const_iterator it = serverList.begin(); it != serverList.end(); ++it)
	{
		if(it->second->getPort() == port)
			return it->second;
	}

	return NULL;
}
