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
#include "raids.h"

#include "player.h"

#include "game.h"
#include "configmanager.h"

extern Game g_game;
extern ConfigManager g_config;

Raids::Raids()
{
	running = NULL;
	loaded = started = false;
	lastRaidEnd = checkRaidsEvent = 0;
}

bool Raids::parseRaidNode(pugi::xml_node& raidNode, bool checkDuplicate, FileType_t pathing)
{	
	if(strcasecmp(raidNode.name(),"raid") == 0)
		return false;

	int32_t intValue;
	std::string strValue;
	pugi::xml_attribute attr;
	if((attr = raidNode.attribute("name")))
	{
		strValue = attr.as_string();
	} else {
		std::clog << "[Error - Raids::parseRaidNode] name tag missing for raid." << std::endl;
		return false;
	}

	std::string name = strValue;
	if((attr = raidNode.attribute("interval2")))
	{
		intValue = attr.as_int();
	}else{
		if(pugi::cast<int32_t>(attr.value()) < 0)
		std::clog << "[Error - Raids::parseRaidNode] interval2 tag missing or divided by 0 for raid " << name << std::endl;
		return false;
	}

	uint32_t interval = intValue * 60;
	std::string file;
	if((attr = raidNode.attribute("file")))
	{
		file = pugi::cast<std::string>(attr.value());
	} else {
		file = name + ".xml";
		std::clog << "[Warning - Raids::parseRaidNode] file tag missing for raid " << name << ", using default: " << file << std::endl;
	}

	file = getFilePath(pathing, "raids/" + file);
	uint64_t margin = 0;
	if((attr = raidNode.attribute("margin"))){
		margin = pugi::cast<int32_t>(attr.value()) * 60 * 1000;
	} else{
		std::clog << "[Warning - Raids::parseRaidNode] margin tag missing for raid " << name << ", using default: " << margin << std::endl;
	}

	RefType_t refType = REF_NONE;
	if((attr = raidNode.attribute("reftype")) || (attr = raidNode.attribute("refType")))
	{
		std::string tmpStrValue = asLowerCaseString(pugi::cast<std::string>(attr.value()));
		if(tmpStrValue == "single")
			refType = REF_SINGLE;
		else if(tmpStrValue == "block")
			refType = REF_BLOCK;
		else if(tmpStrValue != "none")
			std::clog << "[Warning - Raids::parseRaidNode] Unknown reftype \"" << pugi::cast<std::string>(attr.value()) << "\" for raid " << name << std::endl;
	}

	bool ref = false;
	if((attr = raidNode.attribute("ref")))
		ref = booleanString(pugi::cast<std::string>(attr.value()));

	bool enabled = true;
	if((attr = raidNode.attribute("enabled")))
		enabled = booleanString(pugi::cast<std::string>(attr.value()));

	Raid* raid = new Raid(name, interval, margin, refType, ref, enabled);
	if(!raid || !raid->loadFromXml(file))
	{
		delete raid;
		std::clog << "[Fatal - Raids::parseRaidNode] failed to load raid " << name << std::endl;
		return false;
	}

	if(checkDuplicate)
	{
		for(RaidList::iterator it = raidList.begin(); it != raidList.end(); ++it)
		{
			if((*it)->getName() == name)
				delete *it;
		}
	}

	raidList.push_back(raid);
	return true;
}

bool Raids::loadFromXml()
{
	if(isLoaded())
		return true;
		
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(getFilePath(FILE_TYPE_OTHER, "raids/raids.xml").c_str());	
	if(!result)
	{
		std::clog << "[Warning - Raids::loadFromXml] Could not load raids file." << std::endl;		
		return false;
	}

	if(strcasecmp(doc.name(),"raid") == 0)
	{
		std::clog << "[Error - Raids::loadFromXml] Malformed raids file." << std::endl;		
		return false;
	}

	for(auto raidNode : doc.child("raid").children())
	{
		parseRaidNode(raidNode, false, FILE_TYPE_OTHER);		
	}

	loaded = true;
	return true;
}

bool Raids::startup()
{
	if(!isLoaded() || isStarted())
		return false;

	setLastRaidEnd(OTSYS_TIME());
	checkRaidsEvent = Scheduler::getInstance().addEvent(createSchedulerTask(
		CHECK_RAIDS_INTERVAL * 1000, boost::bind(&Raids::checkRaids, this)));

	started = true;
	return true;
}

void Raids::checkRaids()
{
	checkRaidsEvent = Scheduler::getInstance().addEvent(createSchedulerTask(
		CHECK_RAIDS_INTERVAL * 1000, boost::bind(&Raids::checkRaids, this)));
	if(running)
		return;

	uint64_t now = OTSYS_TIME();
	for(RaidList::iterator it = raidList.begin(); it != raidList.end(); ++it)
	{
		if((*it)->isEnabled() && !(*it)->hasRef() && now > (lastRaidEnd + (*it)->getMargin()) &&
			(MAX_RAND_RANGE * CHECK_RAIDS_INTERVAL / (*it)->getInterval()) >= (
			uint32_t)random_range(0, MAX_RAND_RANGE) && (*it)->startRaid())
			break;
	}
}

void Raids::clear()
{
	Scheduler::getInstance().stopEvent(checkRaidsEvent);
	checkRaidsEvent = lastRaidEnd = 0;
	loaded = started = false;

	running = NULL;
	for(RaidList::iterator it = raidList.begin(); it != raidList.end(); ++it)
		delete (*it);

	raidList.clear();
	ScriptEvent::m_interface.reInitState();
}

bool Raids::reload()
{
	clear();
	return loadFromXml();
}

Raid* Raids::getRaidByName(const std::string& name)
{
	RaidList::iterator it;
	for(it = raidList.begin(); it != raidList.end(); it++)
	{
		if(!strcasecmp((*it)->getName().c_str(), name.c_str()))
			return (*it);
	}

	return NULL;
}

Raid::Raid(const std::string& _name, uint32_t _interval, uint64_t _margin,
	RefType_t _refType, bool _ref, bool _enabled)
{
	name = _name;
	interval = _interval;
	margin = _margin;
	refType = _refType;
	ref = _ref;
	enabled = _enabled;

	loaded = false;
	refCount = eventCount = nextEvent = 0;
}

Raid::~Raid()
{
	stopEvents();
	for(RaidEventVector::iterator it = raidEvents.begin(); it != raidEvents.end(); it++)
		delete (*it);

	raidEvents.clear();
}

bool Raid::loadFromXml(const std::string& _filename)
{
	if(isLoaded())
		return true;

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(_filename.c_str());	
	if(!result)
	{
		std::clog << "[Error - Raid::loadFromXml] Could not load raid file (" << _filename << ")." << std::endl;		
		return false;
	}
	
	if(strcasecmp(doc.name(),"raid") == 0)
	{
		std::clog << "[Error - Raid::loadFromXml] Malformed raid file (" << _filename << ")." << std::endl;		
		return false;
	}

	std::string strValue;
	for(auto eventNode : doc.children())
	{
		RaidEvent* event;
		if(strcasecmp(eventNode.name(), "announce") != 0)
			event = new AnnounceEvent(this, ref);
		else if(strcasecmp(eventNode.name(), "effect") != 0)
			event = new EffectEvent(this, ref);
		else if(strcasecmp(eventNode.name(), "itemspawn") != 0)
			event = new ItemSpawnEvent(this, ref);
		else if(strcasecmp(eventNode.name(), "singlespawn") != 0)
			event = new SingleSpawnEvent(this, ref);
		else if(strcasecmp(eventNode.name(), "areaspawn") != 0)
			event = new AreaSpawnEvent(this, ref);
		else if(strcasecmp(eventNode.name(), "script") != 0)
			event = new ScriptEvent(this, ref);
		else
		{
			continue;
		}

		if(event->configureRaidEvent(eventNode))
		{
			raidEvents.push_back(event);
		} else {
			std::clog << "[Error - Raid::loadFromXml] Could not configure raid in file: " << _filename << ", eventNode: " << eventNode.name() << std::endl;
			delete event;
		}
	}

	//sort by delay time
	std::sort(raidEvents.begin(), raidEvents.end(), RaidEvent::compareEvents);	

	loaded = true;
	return true;
}

bool Raid::startRaid()
{
	if(refCount)
		return true;

	RaidEvent* raidEvent = getNextRaidEvent();
	if(!raidEvent)
		return false;

	nextEvent = Scheduler::getInstance().addEvent(createSchedulerTask(
		raidEvent->getDelay(), boost::bind(&Raid::executeRaidEvent, this, raidEvent)));
	Raids::getInstance()->setRunning(this);
	return true;
}

bool Raid::executeRaidEvent(RaidEvent* raidEvent)
{
	if(!raidEvent->executeEvent())
		return !resetRaid(false);

	RaidEvent* newRaidEvent = getNextRaidEvent();
	if(!newRaidEvent)
		return !resetRaid(false);

	nextEvent = Scheduler::getInstance().addEvent(createSchedulerTask(
		std::max(RAID_MINTICKS, (int32_t)(newRaidEvent->getDelay() - raidEvent->getDelay())),
		boost::bind(&Raid::executeRaidEvent, this, newRaidEvent)));
	return true;
}

bool Raid::resetRaid(bool checkExecution)
{
	if(checkExecution && nextEvent)
		return true;

	stopEvents();
	if(refType == REF_BLOCK && refCount > 0)
		return false;

	if(refType != REF_SINGLE || refCount <= 0)
		eventCount = 0;

	if(Raids::getInstance()->getRunning() == this)
	{
		Raids::getInstance()->setRunning(NULL);
		Raids::getInstance()->setLastRaidEnd(OTSYS_TIME());
	}

	return true;
}

void Raid::stopEvents()
{
	if(!nextEvent)
		return;

	Scheduler::getInstance().stopEvent(nextEvent);
	nextEvent = 0;
}

RaidEvent* Raid::getNextRaidEvent()
{
	if(eventCount < raidEvents.size())
		return raidEvents[eventCount++];

	return NULL;
}

bool RaidEvent::configureRaidEvent(pugi::xml_node& eventNode)
{
	pugi::xml_attribute attr;
	if((attr = eventNode.attribute("ref")))
		m_ref = booleanString(pugi::cast<std::string>(attr.value()));

	if((attr = eventNode.attribute("delay")))
		m_delay = std::max((int32_t)m_delay, pugi::cast<int>(attr.value()));

	return true;
}

bool AnnounceEvent::configureRaidEvent(pugi::xml_node& eventNode)
{
	if(!RaidEvent::configureRaidEvent(eventNode))
		return false;

	std::string strValue;
	pugi::xml_attribute attr;
	if((attr = eventNode.attribute("message")))
	{
		strValue = pugi::cast<std::string>(attr.value());
	}else{
		std::clog << "[Error - AnnounceEvent::configureRaidEvent] Message tag missing for announce event." << std::endl;
		return false;
	}

	m_message = strValue;
	if((attr = eventNode.attribute("type")))
	{
		std::string tmpStrValue = asLowerCaseString(strValue);
		if(tmpStrValue == "warning")
			m_messageType = MSG_STATUS_WARNING;
		else if(tmpStrValue == "event")
			m_messageType = MSG_EVENT_ADVANCE;
		else if(tmpStrValue == "default")
			m_messageType = MSG_EVENT_DEFAULT;
		else if(tmpStrValue == "description")
			m_messageType = MSG_INFO_DESCR;
		else if(tmpStrValue == "status")
			m_messageType = MSG_STATUS_SMALL;
		else if(tmpStrValue == "blue")
			m_messageType = MSG_STATUS_CONSOLE_BLUE;
		else if(tmpStrValue == "red")
			m_messageType = MSG_STATUS_CONSOLE_RED;
		else
			std::clog << "[Notice - AnnounceEvent::configureRaidEvent] Unknown type tag for announce event, using default: "
				<< (int32_t)m_messageType << std::endl;
	}
	else
		std::clog << "[Notice - AnnounceEvent::configureRaidEvent] Missing type tag for announce event. Using default: "
			<< (int32_t)m_messageType << std::endl;

	return true;
}

bool AnnounceEvent::executeEvent() const
{
	g_game.broadcastMessage(m_message, m_messageType);
	return true;
}

bool EffectEvent::configureRaidEvent(pugi::xml_node& eventNode)
{
	if(!RaidEvent::configureRaidEvent(eventNode))
		return false;

	std::string strValue;
	pugi::xml_attribute attr;
	if((attr = eventNode.attribute("id")))
	{
		m_effect = (MagicEffect_t)pugi::cast<int32_t>(attr.value());		
	} else {
		if((attr = eventNode.attribute("name")))
		{
			m_effect = getMagicEffect(pugi::cast<std::string>(attr.value()));
		} else {
			std::clog << "[Error - EffectEvent::configureRaidEvent] id (or name) tag missing for effect event." << std::endl;
			return false;
		}
	}

	if((attr = eventNode.attribute("pos")))
	{		
		IntegerVec posList = vectorAtoi(explodeString(pugi::cast<std::string>(eventNode.value()), ";"));
		if(posList.size() < 3)
		{
			std::clog << "[Error - EffectEvent::configureRaidEvent] Malformed pos tag for effect event." << std::endl;
			return false;
		}

		m_position = Position(posList[0], posList[1], posList[2]);

	} else {
		if((attr = eventNode.attribute("x")))
		{
			m_position.x = pugi::cast<int32_t>(attr.value());
		} else {
			std::clog << "[Error - EffectEvent::configureRaidEvent] x tag missing for effect event." << std::endl;
			return false;
		}

		if((attr = eventNode.attribute("y")))
		{
			m_position.y = pugi::cast<int32_t>(attr.value());
		} else {
			std::clog << "[Error - EffectEvent::configureRaidEvent] y tag missing for effect event." << std::endl;
			return false;
		}

		if((attr = eventNode.attribute("z")))
		{
			m_position.z = pugi::cast<int32_t>(attr.value());
		} else {
			std::clog << "[Error - EffectEvent::configureRaidEvent] z tag missing for effect event." << std::endl;
			return false;
		}
	}

	return true;
}

bool EffectEvent::executeEvent() const
{
	g_game.addMagicEffect(m_position, m_effect);
	return true;
}

bool ItemSpawnEvent::configureRaidEvent(pugi::xml_node& eventNode)
{
	if(!RaidEvent::configureRaidEvent(eventNode))
		return false;

	std::string strValue;
	pugi::xml_attribute attr;
	if((attr = eventNode.attribute("id")))
	{
		m_itemId = pugi::cast<int32_t>(attr.value());
	} else {
		if((attr = eventNode.attribute("name")))
		{
			m_itemId = Item::items.getItemIdByName(pugi::cast<std::string>(attr.value()));
		} else {
			std::clog << "[Error - ItemSpawnEvent::configureRaidEvent] id (or name) tag missing for itemspawn event." << std::endl;
			return false;
		}		
	}

	if((attr = eventNode.attribute("chance")))
		m_chance = pugi::cast<int32_t>(attr.value());

	if((attr = eventNode.attribute("subType")))
		m_subType = pugi::cast<int32_t>(attr.value());

	if((attr = eventNode.attribute("pos")))
	{
		IntegerVec posList = vectorAtoi(explodeString(pugi::cast<std::string>(attr.value()), ";"));
		if(posList.size() < 3)
		{
			std::clog << "[Error - ItemSpawnEvent::configureRaidEvent] Malformed pos tag for itemspawn event." << std::endl;
			return false;
		}

		m_position = Position(posList[0], posList[1], posList[2]);

	} else {
		if((attr = eventNode.attribute("x")))
		{
			m_position.x = pugi::cast<int32_t>(attr.value());
		} else {
			std::clog << "[Error - ItemSpawnEvent::configureRaidEvent] x tag missing for itemspawn event." << std::endl;
			return false;
		}

		if((attr = eventNode.attribute("y")))
		{
			m_position.y = pugi::cast<int32_t>(attr.value());
		} else {
			std::clog << "[Error - ItemSpawnEvent::configureRaidEvent] y tag missing for itemspawn event." << std::endl;
			return false;
		}
		
		if((attr = eventNode.attribute("z")))
		{
			m_position.z = pugi::cast<int32_t>(attr.value());
		} else {
			std::clog << "[Error - ItemSpawnEvent::configureRaidEvent] z tag missing for itemspawn event." << std::endl;
			return false;
		}
		
	}

	return true;
}

bool ItemSpawnEvent::executeEvent() const
{
	if(m_chance < (uint32_t)random_range(0, (int32_t)MAX_ITEM_CHANCE))
		return true;

	Tile* tile = g_game.getTile(m_position);
	if(!tile)
	{
		std::clog << "[Fatal - ItemSpawnEvent::executeEvent] Missing tile at position " << m_position << std::endl;
		return false;
	}

	const ItemType& it = Item::items[m_itemId];
	if(it.stackable && m_subType > 100)
	{
		int32_t subCount = m_subType;
		while(subCount > 0)
		{
			int32_t stackCount = std::min(100, (int32_t)subCount);
			subCount -= stackCount;

			Item* newItem = Item::CreateItem(m_itemId, stackCount);
			if(!newItem)
			{
				std::clog << "[Error - ItemSpawnEvent::executeEvent] Cannot create item with id " << m_itemId << std::endl;
				return false;
			}

			ReturnValue ret = g_game.internalAddItem(NULL, tile, newItem, INDEX_WHEREEVER, FLAG_NOLIMIT);
			if(ret != RET_NOERROR)
			{
				std::clog << "[Error - ItemSpawnEvent::executeEvent] Cannot spawn item with id " << m_itemId << std::endl;
				return false;
			}

			if(m_raid->usesRef() && m_ref)
			{
				newItem->setRaid(m_raid);
				m_raid->addRef();
			}
		}
	}
	else
	{
		Item* newItem = Item::CreateItem(m_itemId, m_subType);
		if(!newItem)
		{
			std::clog << "[Error - ItemSpawnEvent::executeEvent] Cannot create item with id " << m_itemId << std::endl;
			return false;
		}

		ReturnValue ret = g_game.internalAddItem(NULL, tile, newItem, INDEX_WHEREEVER, FLAG_NOLIMIT);
		if(ret != RET_NOERROR)
		{
			std::clog << "[Error - ItemSpawnEvent::executeEvent] Cannot spawn item with id " << m_itemId << std::endl;
			return false;
		}

		if(m_raid->usesRef() && m_ref)
		{
			newItem->setRaid(m_raid);
			m_raid->addRef();
		}
	}

	return true;
}

bool SingleSpawnEvent::configureRaidEvent(pugi::xml_node& eventNode)
{
	if(!RaidEvent::configureRaidEvent(eventNode))
		return false;

	std::string strValue;
	pugi::xml_attribute attr;
	if((attr = eventNode.attribute("name")))
	{
		m_monsterName = pugi::cast<std::string>(attr.value());
	}else{
		std::clog << "[Error - SingleSpawnEvent::configureRaidEvent] name tag missing for singlespawn event." << std::endl;
		return false;
	}
	
	if((attr = eventNode.attribute("pos")))
	{
		IntegerVec posList = vectorAtoi(explodeString(pugi::cast<std::string>(attr.value()), ";"));
		if(posList.size() < 3)
		{
			std::clog << "[Error - SingleSpawnEvent::configureRaidEvent] Malformed pos tag for singlespawn event." << std::endl;
			return false;
		}

		m_position = Position(posList[0], posList[1], posList[2]);			
		
	} else {
		if((attr = eventNode.attribute("x")))
		{
			m_position.x = pugi::cast<int32_t>(attr.value());
		} else {
			std::clog << "[Error - SingleSpawnEvent::configureRaidEvent] x tag missing for singlespawn event." << std::endl;
			return false;
		}
		
		if((attr = eventNode.attribute("y")))
		{
			m_position.y = pugi::cast<int32_t>(attr.value());
		} else {
			std::clog << "[Error - SingleSpawnEvent::configureRaidEvent] y tag missing for singlespawn event." << std::endl;
			return false;
		}
		
		if((attr = eventNode.attribute("z")))
		{
			m_position.z = pugi::cast<int32_t>(attr.value());
		} else {
			std::clog << "[Error - SingleSpawnEvent::configureRaidEvent] z tag missing for singlespawn event." << std::endl;
			return false;
		}
	}

	return true;
}

bool SingleSpawnEvent::executeEvent() const
{
	Monster* monster = Monster::createMonster(m_monsterName);
	if(!monster)
	{
		std::clog << "[Error - SingleSpawnEvent::executeEvent] Cannot create monster " << m_monsterName << std::endl;
		return false;
	}

	if(!g_game.placeCreature(monster, m_position, false, true))
	{
		delete monster;
		std::clog << "[Error - SingleSpawnEvent::executeEvent] Cannot spawn monster " << m_monsterName << std::endl;
		return false;
	}

	if(m_raid->usesRef() && m_ref)
	{
		monster->setRaid(m_raid);
		m_raid->addRef();
	}

	return true;
}

bool AreaSpawnEvent::configureRaidEvent(pugi::xml_node& eventNode)
{
	if(!RaidEvent::configureRaidEvent(eventNode))
		return false;

	std::string strValue;
	pugi::xml_attribute attr;
	if((attr = eventNode.attribute("radius")))
	{
		int32_t radius = pugi::cast<int32_t>(attr.value());
		Position centerPos;
		if((attr = eventNode.attribute("centerPosition")) || (attr = eventNode.attribute("centerpos")))
		{
			IntegerVec posList = vectorAtoi(explodeString(pugi::cast<std::string>(attr.value()), ";"));
			if(posList.size() < 3)
			{
				std::clog << "[Error - AreaSpawnEvent::configureRaidEvent] Malformed centerPosition tag for areaspawn event." << std::endl;
				return false;
			}

			centerPos = Position(posList[0], posList[1], posList[2]);
		} else {
			if((attr = eventNode.attribute("centerx")))
			{
				centerPos.x = pugi::cast<int32_t>(attr.value());
			} else { 
				std::clog << "[Error - AreaSpawnEvent::configureRaidEvent] centerx tag missing for areaspawn event." << std::endl;
				return false;				
			}
			
			if((attr = eventNode.attribute("centery")))
			{
				centerPos.y = pugi::cast<int32_t>(attr.value());
			} else { 
				std::clog << "[Error - AreaSpawnEvent::configureRaidEvent] centery tag missing for areaspawn event." << std::endl;
				return false;				
			}
			
			if((attr = eventNode.attribute("centerz")))
			{
				centerPos.z = pugi::cast<int32_t>(attr.value());
			} else { 
				std::clog << "[Error - AreaSpawnEvent::configureRaidEvent] centerz tag missing for areaspawn event." << std::endl;
				return false;				
			}
			
		}

		m_fromPos.x = centerPos.x - radius;
		m_fromPos.y = centerPos.y - radius;
		m_fromPos.z = centerPos.z;

		m_toPos.x = centerPos.x + radius;
		m_toPos.y = centerPos.y + radius;
		m_toPos.z = centerPos.z;
	}
	else
	{
		if((attr = eventNode.attribute("fromPosition")) || (attr = eventNode.attribute("frompos")))
		{
			IntegerVec posList = vectorAtoi(explodeString(pugi::cast<std::string>(attr.value()), ";"));
			if(posList.size() < 3)
			{
				std::clog << "[Error - AreaSpawnEvent::configureRaidEvent] Malformed fromPosition tag for areaspawn event." << std::endl;
				return false;
			}

			m_fromPos = Position(posList[0], posList[1], posList[2]);
		} else {
			if((attr = eventNode.attribute("fromx")))
			{
				m_fromPos.x = pugi::cast<int32_t>(attr.value());
			} else {
				std::clog << "[Error - AreaSpawnEvent::configureRaidEvent] fromx tag missing for areaspawn event." << std::endl;
				return false;
			}			
			if((attr = eventNode.attribute("fromy")))
			{
				m_fromPos.y = pugi::cast<int32_t>(attr.value());
			} else {
				std::clog << "[Error - AreaSpawnEvent::configureRaidEvent] fromy tag missing for areaspawn event." << std::endl;
				return false;
			}			
			if((attr = eventNode.attribute("fromz")))
			{
				m_fromPos.z = pugi::cast<int32_t>(attr.value());
			} else {
				std::clog << "[Error - AreaSpawnEvent::configureRaidEvent] fromz tag missing for areaspawn event." << std::endl;
				return false;
			}			
		}

		if((attr = eventNode.attribute("toPosition")) || (attr = eventNode.attribute("topos")))
		{
			IntegerVec posList = vectorAtoi(explodeString(pugi::cast<std::string>(attr.value()), ";"));
			if(posList.size() < 3)
			{
				std::clog << "[Error - AreaSpawnEvent::configureRaidEvent] Malformed toPosition tag for areaspawn event." << std::endl;
				return false;
			}

			m_toPos = Position(posList[0], posList[1], posList[2]);
		}
		else
		{
			if((attr = eventNode.attribute("tox")))
			{
				m_toPos.x = pugi::cast<int32_t>(attr.value());				
			} else {
				std::clog << "[Error - AreaSpawnEvent::configureRaidEvent] tox tag missing for areaspawn event." << std::endl;
				return false;
			}

			if((attr = eventNode.attribute("toy")))
			{
				m_toPos.y = pugi::cast<int32_t>(attr.value());				
			} else {
				std::clog << "[Error - AreaSpawnEvent::configureRaidEvent] toy tag missing for areaspawn event." << std::endl;
				return false;
			}

			if((attr = eventNode.attribute("toz")))
			{
				m_toPos.z = pugi::cast<int32_t>(attr.value());				
			} else {
				std::clog << "[Error - AreaSpawnEvent::configureRaidEvent] toz tag missing for areaspawn event." << std::endl;
				return false;
			}

		}
	}

	for(auto monsterNode : eventNode.children())
	{
		if(strcasecmp(monsterNode.name(), "monster") != 0)
		{
			if((attr = monsterNode.attribute("name")))
			{
				strValue = pugi::cast<std::string>(attr.value());
			} else {
				std::clog << "[Error - AreaSpawnEvent::configureRaidEvent] name tag missing for monster node." << std::endl;
				return false;
			}

			std::string name = strValue;
			int32_t min = 0, max = 0;
			if((attr = monsterNode.attribute("min")) || (attr = monsterNode.attribute("minamount")))
				min = pugi::cast<int32_t>(attr.value());

			if((attr = monsterNode.attribute("max")) || (attr = monsterNode.attribute("maxamount")))
				max = pugi::cast<int32_t>(attr.value());

			if(!min && !max)
			{
				if((attr = monsterNode.attribute("amount")))
				{
					min = max = pugi::cast<int32_t>(attr.value());
				} else {
					std::clog << "[Error - AreaSpawnEvent::configureRaidEvent] amount tag missing for monster node." << std::endl;
					return false;
				}

			}

			addMonster(name, min, max);
		}
	}

	return true;
}

AreaSpawnEvent::~AreaSpawnEvent()
{
	for(MonsterSpawnList::iterator it = m_spawnList.begin(); it != m_spawnList.end(); it++)
		delete (*it);

	m_spawnList.clear();
}

void AreaSpawnEvent::addMonster(MonsterSpawn* _spawn)
{
	m_spawnList.push_back(_spawn);
}

void AreaSpawnEvent::addMonster(const std::string& name, uint32_t min, uint32_t max)
{
	MonsterSpawn* monsterSpawn = new MonsterSpawn();
	monsterSpawn->min = min;
	monsterSpawn->max = max;

	monsterSpawn->name = name;
	addMonster(monsterSpawn);
}

bool AreaSpawnEvent::executeEvent() const
{
	for(MonsterSpawnList::const_iterator it = m_spawnList.begin(); it != m_spawnList.end(); it++)
	{
		MonsterSpawn* spawn = *it;
		uint32_t amount = (uint32_t)random_range(spawn->min, spawn->max);
		for(uint32_t i = 0; i < amount; ++i)
		{
			Monster* monster = Monster::createMonster(spawn->name);
			if(!monster)
			{
				std::clog << "[Error - AreaSpawnEvent::executeEvent] Cannot create monster " << spawn->name << std::endl;
				return false;
			}

			bool success = false;
			for(int32_t t = 0; t < MAXIMUM_TRIES_PER_MONSTER; ++t)
			{
				if(!g_game.placeCreature(monster, Position(random_range(m_fromPos.x, m_toPos.x),
					random_range(m_fromPos.y, m_toPos.y), random_range(m_fromPos.z, m_toPos.z)), true))
					continue;

				if(m_raid->usesRef() && m_ref)
				{
					monster->setRaid(m_raid);
					m_raid->addRef();
				}

				success = true;
				break;
			}

			if(!success)
				delete monster;
		}
	}

	return true;
}

LuaInterface ScriptEvent::m_interface("Raid Interface");

bool ScriptEvent::configureRaidEvent(pugi::xml_node& eventNode)
{
	if(!RaidEvent::configureRaidEvent(eventNode))
		return false;

	std::string scriptsName = Raids::getInstance()->getScriptBaseName();
	if(!m_interface.loadDirectory(getFilePath(FILE_TYPE_OTHER, std::string(scriptsName + "/lib/"))))
		std::clog << "[Warning - ScriptEvent::configureRaidEvent] Cannot load " << scriptsName << "/lib/" << std::endl;

	std::string strValue;
	pugi::xml_attribute attr;
	if((attr = eventNode.attribute("file")))
	{
		if(!loadScript(getFilePath(FILE_TYPE_OTHER, std::string(scriptsName + "/scripts/" + pugi::cast<std::string>(attr.value()))), true))
		{
			std::clog << "[Error - ScriptEvent::configureRaidEvent] Cannot load raid script file (" << pugi::cast<std::string>(attr.value()) << ")." << std::endl;
			return false;
		}
	} else if(loadBuffer(eventNode.attribute("file").as_string())){
		return true;
	}

	std::clog << "[Error - ScriptEvent::configureRaidEvent] Cannot load raid script buffer." << std::endl;
	return false;
}

bool ScriptEvent::executeEvent() const
{
	//onRaid()
	if(m_interface.reserveEnv())
	{
		ScriptEnviroment* env = m_interface.getEnv();
		if(m_scripted == EVENT_SCRIPT_BUFFER)
		{
			bool result = true;
			if(m_interface.loadBuffer(m_scriptData))
			{
				lua_State* L = m_interface.getState();
				result = m_interface.getGlobalBool(L, "_result", true);
			}

			m_interface.releaseEnv();
			return result;
		}
		else
		{
			#ifdef __DEBUG_LUASCRIPTS__
			env->setEvent("Raid event");
			#endif
			env->setScriptId(m_scriptId, &m_interface);
			m_interface.pushFunction(m_scriptId);

			bool result = m_interface.callFunction(0);
			m_interface.releaseEnv();
			return result;
		}
	}
	else
	{
		std::clog << "[Error - ScriptEvent::executeEvent] Call stack overflow." << std::endl;
		return false;
	}
}
