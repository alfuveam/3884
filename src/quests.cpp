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

#include "quests.h"


bool Mission::isStarted(Player* player)
{
	if(!player)
		return false;

	std::string value;
	player->getStorage(storageId, value);
	return atoi(value.c_str()) >= startValue;
}

bool Mission::isCompleted(Player* player)
{
	if(!player)
		return false;

	std::string value;
	player->getStorage(storageId, value);
	return atoi(value.c_str()) >= endValue;
}

std::string Mission::parseStorages(std::string state, std::string value)
{
	/*std::string::size_type start, end;
	while((start = state.find("|STORAGE:")) != std::string::npos)
	{
		if((end = state.find("|", start)) = std::string::npos)
			continue;

		std::string value, storage = state.substr(start, end - start)
		player->getStorage(storage, value);
		state.replace(start, end, value);
	} requires testing and probably fixing, inspired by QuaS code*/

	replaceString(state, "|STATE|", value);
	return state;
}

std::string Mission::getDescription(Player* player)
{
	std::string value;
	player->getStorage(storageId, value);
	if(state.size())
		return parseStorages(state, value);

	if(atoi(value.c_str()) >= endValue)
		return parseStorages(states.rbegin()->second, value);

	for(int32_t i = endValue; i >= startValue; --i)
	{
		player->getStorage(storageId, value);
		if(atoi(value.c_str()) == i)
			return parseStorages(states[i - startValue], value);
	}

	return "Couldn't retrieve any mission description, please report to a gamemaster.";
}

Quest::~Quest()
{
	for(MissionList::iterator it = missions.begin(); it != missions.end(); it++)
		delete (*it);

	missions.clear();
}

bool Quest::isStarted(Player* player)
{
	if(!player)
		return false;

	std::string value;
	player->getStorage(storageId, value);
	return atoi(value.c_str()) >= storageValue;
}

bool Quest::isCompleted(Player* player) const
{
	for(MissionList::const_iterator it = missions.begin(); it != missions.end(); it++)
	{
		if(!(*it)->isCompleted(player))
			return false;
	}

	return true;
}

uint16_t Quest::getMissionCount(Player* player)
{
	uint16_t count = 0;
	for(MissionList::iterator it = missions.begin(); it != missions.end(); it++)
	{
		if((*it)->isStarted(player))
			count++;
	}

	return count;
}

void Quests::clear()
{
	for(QuestList::iterator it = quests.begin(); it != quests.end(); it++)
		delete (*it);

	quests.clear();
}

bool Quests::reload()
{
	clear();
	return loadFromXml();
}

bool Quests::loadFromXml()
{
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(getFilePath(FILE_TYPE_XML, "quests.xml").c_str());	
	if(!result)
	{
		std::clog << "[Warning - Quests::loadFromXml] Cannot load quests file." << std::endl;		
		return false;
	}

	if(strcasecmp(doc.name(),"quests") == 0)
	{
		std::clog << "[Error - Quests::loadFromXml] Malformed quests file." << std::endl;
		return false;
	}

	for(auto p : doc.children())
	{
		parseQuestNode(p, false);		
	}

	return true;
}

bool Quests::parseQuestNode(pugi::xml_node& p, bool checkDuplicate)
{	
	if(strcasecmp(p.name(),"quest") == 0)
		return false;

	pugi::xml_attribute attr;
	
	uint32_t id = m_lastId;
	if((attr = p.attribute("id")))
	{		
		id = attr.as_int();
		if(id > m_lastId)
			m_lastId = id;
	}

	std::string name;
	if((attr = p.attribute("name")))
		name = attr.as_string();

	std::string startStorageId;
	if((attr = p.attribute("startstorageid")) || (attr = p.attribute("storageId")))
		startStorageId = attr.as_string();

	int32_t startStorageValue = 0;
	if((attr = p.attribute("startstoragevalue")) || ((attr = p.attribute("storageValue"))))
		startStorageValue = attr.as_int();

	Quest* quest = new Quest(name, id, startStorageId, startStorageValue);
	if(!quest)
		return false;
	
	for(auto missionNode : p.children())
	{
		if(strcasecmp(missionNode.name(),"mission") == 0)
			continue;

		std::string missionName, missionState, storageId;
		if((attr = missionNode.attribute("name")))
			missionName = attr.as_string();

		if((attr = missionNode.attribute("state")) || (attr = missionNode.attribute("description")))
			missionState = attr.as_string();

		if((attr = missionNode.attribute("storageid")) || (attr = missionNode.attribute("storageId")))
			storageId = attr.as_string();

		int32_t startValue = 0, endValue = 0;
		if((attr = missionNode.attribute("startvalue")) || ((attr = missionNode.attribute("startValue"))))
			startValue = attr.as_int();

		if((attr = missionNode.attribute("endvalue")) || ((attr = missionNode.attribute("endValue"))))
			endValue = attr.as_int();

		if(Mission* mission = new Mission(missionName, missionState, storageId, startValue, endValue))
		{
			if(missionState.empty())
			{
				// parse sub-states only if main is not set				
				for(auto stateNode : missionNode.children())
				{
					if(strcasecmp(stateNode.name(),"missionstate") == 0)
						continue;

					uint32_t missionId;
					if((attr = stateNode.attribute("id")))
					{
						missionId = attr.as_int();
					} else {
						std::clog << "[Warning - Quests::parseQuestNode] Missing missionId for mission state" << std::endl;
						continue;
					 }

					std::string description;
					if((attr = stateNode.attribute("description")))
						description = attr.as_string();

					mission->newState(missionId, description);
				}
			}

			quest->newMission(mission);
		}
	}

	if(checkDuplicate)
	{
		for(QuestList::iterator it = quests.begin(); it != quests.end(); ++it)
		{
			if((*it)->getName() == name)
				delete *it;
		}
	}

	m_lastId++;
	quests.push_back(quest);
	return true;
}

uint16_t Quests::getQuestCount(Player* player)
{
	uint16_t count = 0;
	for(QuestList::iterator it = quests.begin(); it != quests.end(); it++)
	{
		if((*it)->isStarted(player))
			count++;
	}

	return count;
}

Quest* Quests::getQuestById(uint16_t id) const
{
	for(QuestList::const_iterator it = quests.begin(); it != quests.end(); it++)
	{
		if((*it)->getId() == id)
			return (*it);
	}

	return NULL;
}
