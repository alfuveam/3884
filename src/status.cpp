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

#include "status.h"

#include "connection.h"
#include "networkmessage.h"
#include "outputmessage.h"

#include "configmanager.h"
#include "game.h"

extern ConfigManager g_config;
extern Game g_game;

#ifdef __ENABLE_SERVER_DIAGNOSTIC__
uint32_t ProtocolStatus::protocolStatusCount = 0;
#endif
IpConnectMap ProtocolStatus::ipConnectMap;

void ProtocolStatus::onRecvFirstMessage(NetworkMessage& msg)
{
	IpConnectMap::const_iterator it = ipConnectMap.find(getIP());
	if(it != ipConnectMap.end() && OTSYS_TIME() < it->second + g_config.getNumber(ConfigManager::STATUSQUERY_TIMEOUT))
	{
		getConnection()->close();
		return;
	}

	ipConnectMap[getIP()] = OTSYS_TIME();
	uint8_t type = msg.get<char>();
	switch(type)
	{
		case 0xFF:
		{
			if(msg.getString(false, 4) == "info")
			{
				if(OutputMessage_ptr output = OutputMessagePool::getInstance()->getOutputMessage(this, false))
				{
					TRACK_MESSAGE(output);
					if(Status* status = Status::getInstance())
					{
						bool sendPlayers = false;
						if(msg.size() > msg.position())
							sendPlayers = msg.get<char>() == 0x01;

						output->putString(status->getStatusString(sendPlayers), false);
					}

					setRawMessages(true); // we dont want the size header, nor encryption
					OutputMessagePool::getInstance()->send(output);
				}
			}

			break;
		}

		case 0x01:
		{
			uint32_t requestedInfo = msg.get<uint16_t>(); //Only a Byte is necessary, though we could add new infos here
			if(OutputMessage_ptr output = OutputMessagePool::getInstance()->getOutputMessage(this, false))
			{
				TRACK_MESSAGE(output);
				if(Status* status = Status::getInstance())
					status->getInfo(requestedInfo, output, msg);

				OutputMessagePool::getInstance()->send(output);
			}

			break;
		}

		default:
			break;
	}

	getConnection()->close();
}

void ProtocolStatus::deleteProtocolTask()
{
#ifdef __DEBUG_NET_DETAIL__
	std::clog << "Deleting ProtocolStatus" << std::endl;
#endif
	Protocol::deleteProtocolTask();
}

std::string Status::getStatusString(bool sendPlayers) const
{
	pugi::xml_document doc;

	pugi::xml_node decl = doc.prepend_child(pugi::node_declaration);
	decl.apprend_attribute("version") = "1.0";
	
	pugi::xml_node tsqp = doc.append_child("tsqp");
	tsqp.apprend_attribute("version") = "1.0";

	pugi::xml_node serverinfo = tsqp.append_child("serverinfo");
	serverinfo.append_attribute("uptime") = getUptime();
	serverinfo.append_attribute("ip") = g_config.getString(ConfigManager::IP).c_str();
	serverinfo.append_attribute("servername") = g_config.getString(ConfigManager::SERVER_NAME).c_str();	
	serverinfo.append_attribute("port") = g_config.getNumber(ConfigManager::LOGIN_PORT);
	serverinfo.append_attribute("location") = g_config.getString(ConfigManager::LOCATION).c_str();
	serverinfo.append_attribute("url") = g_config.getString(ConfigManager::URL).c_str();
	serverinfo.append_attribute("server") = SOFTWARE_NAME;
	serverinfo.append_attribute("version") = SOFTWARE_VERSION;
	serverinfo.append_attribute("client") = SOFTWARE_PROTOCOL)	

	pugi::xml_node owner = tsqp.append_child("owner");
	owner.append_attribute("name") = g_config.getString(ConfigManager::OWNER_NAME).c_str();
	owner.append_attribute("email") = g_config.getString(ConfigManager::OWNER_EMAIL).c_str();

	pugi::xml_node players = tsqp.append_child("players");
	players.append_attribute("online") = g_game.getPlayersOnline())
	players.append_attribute("max") = g_config.getNumber(ConfigManager::MAX_PLAYERS))
	players.append_attribute("peak") = g_game.getPlayersRecord())
	
	if(sendPlayers)
	{
		pugi::xml_node onlineplayers = tsqp.append_child("onlineplayers");
		std::stringstream ss;
		for(AutoList<Player>::iterator it = Player::autoList.begin(); it != Player::autoList.end(); ++it)
		{
			if(it->second->isRemoved() || it->second->isGhost())
				continue;

			if(!ss.str().empty())
				ss << ";";

			ss << it->second->getName() << "," << it->second->getVocationId() << "," << it->second->getLevel();
		}
		onlineplayers.append_attribute("list") = ss.str().c_str();		
	}

	pugi::xml_node monsters = tsqp.append_child("monsters");	
	monsters.append_attribute("total") = g_game.getMonstersOnline();
	
	pugi::xml_node npcs = tsqp.append_child("npcs");		
	npcs.append_attribute("total") = g_game.getNpcsOnline());
	
	pugi::xml_node map = tsqp.append_child("map");
	map.append_attribute("name") = g_config.getString(ConfigManager::MAP_NAME).c_str());
	map.append_attribute("author") = g_config.getString(ConfigManager::MAP_AUTHOR).c_str());

	uint32_t mapWidth, mapHeight;
	g_game.getMapDimensions(mapWidth, mapHeight);
	map.append_attribute("width") = mapWidth;	
	map.append_attribute("height") = mapHeight;

	pugi::xml_node motd = tsqp.append_child("motd");
	motd.text() = g_config.getString(ConfigManager::MOTD).c_str();

	return doc;
}

void Status::getInfo(uint32_t requestedInfo, OutputMessage_ptr output, NetworkMessage& msg) const
{
	if(requestedInfo & REQUEST_BASIC_SERVER_INFO)
	{
		output->put<char>(0x10);
		output->putString(g_config.getString(ConfigManager::SERVER_NAME).c_str());
		output->putString(g_config.getString(ConfigManager::IP).c_str());

		char buffer[10];
		sprintf(buffer, "%d", g_config.getNumber(ConfigManager::LOGIN_PORT));
		output->putString(buffer);
	}

	if(requestedInfo & REQUEST_SERVER_OWNER_INFO)
	{
		output->put<char>(0x11);
		output->putString(g_config.getString(ConfigManager::OWNER_NAME).c_str());
		output->putString(g_config.getString(ConfigManager::OWNER_EMAIL).c_str());
	}

	if(requestedInfo & REQUEST_MISC_SERVER_INFO)
	{
		output->put<char>(0x12);
		output->putString(g_config.getString(ConfigManager::MOTD).c_str());
		output->putString(g_config.getString(ConfigManager::LOCATION).c_str());
		output->putString(g_config.getString(ConfigManager::URL).c_str());

		uint64_t uptime = getUptime();
		output->put<uint32_t>((uint32_t)(uptime >> 32));
		output->put<uint32_t>((uint32_t)(uptime));
	}

	if(requestedInfo & REQUEST_PLAYERS_INFO)
	{
		output->put<char>(0x20);
		output->put<uint32_t>(g_game.getPlayersOnline());
		output->put<uint32_t>(g_config.getNumber(ConfigManager::MAX_PLAYERS));
		output->put<uint32_t>(g_game.getPlayersRecord());
	}

	if(requestedInfo & REQUEST_SERVER_MAP_INFO)
	{
		output->put<char>(0x30);
		output->putString(g_config.getString(ConfigManager::MAP_NAME).c_str());
		output->putString(g_config.getString(ConfigManager::MAP_AUTHOR).c_str());

		uint32_t mapWidth, mapHeight;
		g_game.getMapDimensions(mapWidth, mapHeight);
		output->put<uint16_t>(mapWidth);
		output->put<uint16_t>(mapHeight);
	}

	if(requestedInfo & REQUEST_EXT_PLAYERS_INFO)
	{
		output->put<char>(0x21);
		std::list<std::pair<std::string, uint32_t> > players;
		for(AutoList<Player>::iterator it = Player::autoList.begin(); it != Player::autoList.end(); ++it)
		{
			if(!it->second->isRemoved() && !it->second->isGhost())
				players.push_back(std::make_pair(it->second->getName(), it->second->getLevel()));
		}

		output->put<uint32_t>(players.size());
		for(std::list<std::pair<std::string, uint32_t> >::iterator it = players.begin(); it != players.end(); ++it)
		{
			output->putString(it->first);
			output->put<uint32_t>(it->second);
		}
	}

	if(requestedInfo & REQUEST_PLAYER_STATUS_INFO)
	{
		output->put<char>(0x22);
		const std::string name = msg.getString();

		Player* p = NULL;
		if(g_game.getPlayerByNameWildcard(name, p) == RET_NOERROR && !p->isGhost())
			output->put<char>(0x01);
		else
			output->put<char>(0x00);
	}

	if(requestedInfo & REQUEST_SERVER_SOFTWARE_INFO)
	{
		output->put<char>(0x23);
		output->putString(SOFTWARE_NAME);
		output->putString(SOFTWARE_VERSION);
		output->putString(SOFTWARE_PROTOCOL);
	}
}
