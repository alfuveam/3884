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
#include "luascript.h"
#include "scriptmanager.h"

#include "player.h"
#include "item.h"
#include "teleport.h"
#include "beds.h"

#include "town.h"
#include "house.h"
#include "housetile.h"

#include "database.h"
#include "iologindata.h"
#include "ioban.h"
#include "iomap.h"
#include "iomapserialize.h"

#include "talkaction.h"
#include "spells.h"
#include "combat.h"
#include "condition.h"

#include "baseevents.h"
#include "monsters.h"
#include "raids.h"

#include "configmanager.h"
#include "vocation.h"
#include "status.h"
#include "game.h"
#include "chat.h"

#include "movement.h"
#include "globalevent.h"
#include "weapons.h"
#include "monster.h"

#if LUA_VERSION_NUM >= 502
	#undef lua_strlen
	#define lua_strlen lua_rawlen
#endif

extern Game g_game;
extern Monsters g_monsters;
extern Chat g_chat;
extern ConfigManager g_config;
extern Spells* g_spells;
extern TalkActions* g_talkActions;
extern Actions* g_actions;
extern CreatureEvents* g_creatureEvents;
extern MoveEvents* g_moveEvents;
extern GlobalEvents* g_globalEvents;
extern Weapons* g_weapons;
enum
{
	EVENT_ID_LOADING = 1,
	EVENT_ID_USER = 1000,
};

LuaEnvironment::AreaMap LuaEnvironment::m_areaMap;
uint32_t LuaEnvironment::m_lastAreaId = 0;
LuaEnvironment::CombatMap LuaEnvironment::m_combatMap;
uint32_t LuaEnvironment::m_lastCombatId = 0;
LuaEnvironment::ConditionMap LuaEnvironment::m_conditionMap;
uint32_t LuaEnvironment::m_lastConditionId = 0;
LuaEnvironment::ConditionMap LuaEnvironment::m_tempConditionMap;

LuaEnvironment::ThingMap LuaEnvironment::m_globalMap;
LuaEnvironment::StorageMap LuaEnvironment::m_storageMap;
LuaEnvironment::TempItemListMap LuaEnvironment::m_tempItems;

LuaEnvironment::LuaEnvironment()
{
	m_lastUID = 70000;
	m_loaded = true;
	reset();
}

LuaEnvironment::~LuaEnvironment()
{
	for(CombatMap::iterator it = m_combatMap.begin(); it != m_combatMap.end(); ++it)
		delete it->second;

	m_combatMap.clear();
	for(AreaMap::iterator it = m_areaMap.begin(); it != m_areaMap.end(); ++it)
		delete it->second;

	m_areaMap.clear();
	for(ConditionMap::iterator it = m_conditionMap.begin(); it != m_conditionMap.end(); ++it)
		delete it->second;

	m_conditionMap.clear();
	reset();
}

void LuaEnvironment::reset()
{
	m_scriptId = m_callbackId = 0;
	m_timerEvent = false;

	m_realPos = Position();
	m_interface = NULL;
	for(TempItemListMap::iterator mit = m_tempItems.begin(); mit != m_tempItems.end(); ++mit)
	{
		ItemList itemList = mit->second;
		for(ItemList::iterator it = itemList.begin(); it != itemList.end(); ++it)
		{
			if((*it)->getParent() == VirtualCylinder::virtualCylinder)
				g_game.freeThing(*it);
		}
	}

	m_tempItems.clear();
	for(DBResultMap::iterator it = m_tempResults.begin(); it != m_tempResults.end(); ++it)
	{
		if(it->second)
			it->second->free();
	}

	m_tempResults.clear();
	for(ConditionMap::iterator it = m_tempConditionMap.begin(); it != m_tempConditionMap.end(); ++it)
		delete it->second;

	m_tempConditionMap.clear();
	m_localMap.clear();
}

bool LuaEnvironment::saveGameState()
{
	if(!g_config.getBool(ConfigManager::SAVE_GLOBAL_STORAGE))
		return true;

	Database* db = Database::getInstance();
	DBQuery query;

	query << "DELETE FROM `global_storage` WHERE `world_id` = " << g_config.getNumber(ConfigManager::WORLD_ID) << ";";
	if(!db->query(query.str()))
		return false;

	DBInsert query_insert(db);
	query_insert.setQuery("INSERT INTO `global_storage` (`key`, `world_id`, `value`) VALUES ");
	for(StorageMap::const_iterator it = m_storageMap.begin(); it != m_storageMap.end(); ++it)
	{
		char* buffer = new char[25 + it->second.length()];
		sprintf(buffer, "%s, %u, %s", db->escapeString(it->first).c_str(), g_config.getNumber(ConfigManager::WORLD_ID), db->escapeString(it->second).c_str());
		if(!query_insert.addRow(buffer))
			return false;
	}

	return query_insert.execute();
}

bool LuaEnvironment::loadGameState()
{
	Database* db = Database::getInstance();
	DBResult* result;

	DBQuery query;
	query << "SELECT `key`, `value` FROM `global_storage` WHERE `world_id` = " << g_config.getNumber(ConfigManager::WORLD_ID) << ";";
	if((result = db->storeQuery(query.str())))
	{
		do
			m_storageMap[result->getDataString("key")] = result->getDataString("value");
		while(result->next());
		result->free();
	}

	query.str("");
	return true;
}

bool LuaEnvironment::setCallbackId(int32_t callbackId, LuaScriptInterface* interface)
{
	if(!m_callbackId)
	{
		m_callbackId = callbackId;
		m_interface = interface;
		return true;
	}

	//nested callbacks are not allowed
	if(m_interface)
		m_interface->errorEx("Nested callbacks!");

	return false;
}

void LuaEnvironment::getInfo(int32_t& scriptId, std::string& desc, LuaScriptInterface*& interface, int32_t& callbackId, bool& timerEvent)
{
	scriptId = m_scriptId;
	desc = m_event;
	interface = m_interface;
	callbackId = m_callbackId;
	timerEvent = m_timerEvent;
}

void LuaEnvironment::addUniqueThing(Thing* thing)
{
	Item* item = thing->getItem();
	if(!item || !item->getUniqueId())
		return;

	/*if(m_globalMap[item->getUniqueId()])
	{
		if(item->getActionId() != 2000) //scripted quest system
			std::clog << "Duplicate uniqueId " << item->getUniqueId() << std::endl;
	}
	else
		m_globalMap[item->getUniqueId()] = thing;*/
}

void LuaEnvironment::removeUniqueThing(Thing* thing)
{
	Item* item = thing->getItem();
	if(!item || !item->getUniqueId())
		return;

	ThingMap::iterator it = m_globalMap.find(item->getUniqueId());
	if(it != m_globalMap.end())
		m_globalMap.erase(it);
}

uint32_t LuaEnvironment::addThing(Thing* thing)
{
	if(!thing || thing->isRemoved())
		return 0;

	for(ThingMap::iterator it = m_localMap.begin(); it != m_localMap.end(); ++it)
	{
		if(it->second == thing)
			return it->first;
	}

	if(Creature* creature = thing->getCreature())
	{
		m_localMap[creature->getID()] = thing;
		return creature->getID();
	}

	if(Item* item = thing->getItem())
	{
		uint32_t tmp = item->getUniqueId();
		if(tmp)
		{
			m_localMap[tmp] = thing;
			return tmp;
		}
	}

	while(m_localMap.find(m_lastUID) != m_localMap.end())
		++m_lastUID;

	m_localMap[m_lastUID] = thing;
	return m_lastUID;
}

void LuaEnvironment::insertThing(uint32_t uid, Thing* thing)
{
	if(!m_localMap[uid])
		m_localMap[uid] = thing;
	else
		std::clog << "[Error - LuaEnvironment::insertThing] Thing uid already taken" << std::endl;
}

Thing* LuaEnvironment::getThingByUID(uint32_t uid)
{
	Thing* tmp = m_localMap[uid];
	if(tmp && !tmp->isRemoved())
		return tmp;

	tmp = m_globalMap[uid];
	if(tmp && !tmp->isRemoved())
		return tmp;

	if(uid >= 0x10000000)
	{
		tmp = g_game.getCreatureByID(uid);
		if(tmp && !tmp->isRemoved())
		{
			m_localMap[uid] = tmp;
			return tmp;
		}
	}

	return NULL;
}

Item* LuaEnvironment::getItemByUID(uint32_t uid)
{
	if(Thing* tmp = getThingByUID(uid))
	{
		if(Item* item = tmp->getItem())
			return item;
	}

	return NULL;
}

Container* LuaEnvironment::getContainerByUID(uint32_t uid)
{
	if(Item* tmp = getItemByUID(uid))
	{
		if(Container* container = tmp->getContainer())
			return container;
	}

	return NULL;
}

Creature* LuaEnvironment::getCreatureByUID(uint32_t uid)
{
	if(Thing* tmp = getThingByUID(uid))
	{
		if(Creature* creature = tmp->getCreature())
			return creature;
	}

	return NULL;
}

Player* LuaEnvironment::getPlayerByUID(uint32_t uid)
{
	if(Thing* tmp = getThingByUID(uid))
	{
		if(Creature* creature = tmp->getCreature())
		{
			if(Player* player = creature->getPlayer())
				return player;
		}
	}

	return NULL;
}

void LuaEnvironment::removeThing(uint32_t uid)
{
	ThingMap::iterator it;
	it = m_localMap.find(uid);
	if(it != m_localMap.end())
		m_localMap.erase(it);

	it = m_globalMap.find(uid);
	if(it != m_globalMap.end())
		m_globalMap.erase(it);
}

uint32_t LuaEnvironment::addCombatArea(CombatArea* area)
{
	uint32_t newAreaId = m_lastAreaId + 1;
	m_areaMap[newAreaId] = area;

	m_lastAreaId++;
	return newAreaId;
}

CombatArea* LuaEnvironment::getCombatArea(uint32_t areaId)
{
	AreaMap::const_iterator it = m_areaMap.find(areaId);
	if(it != m_areaMap.end())
		return it->second;

	return NULL;
}

uint32_t LuaEnvironment::addCombatObject(Combat* combat)
{
	uint32_t newCombatId = m_lastCombatId + 1;
	m_combatMap[newCombatId] = combat;

	m_lastCombatId++;
	return newCombatId;
}

Combat* LuaEnvironment::getCombatObject(uint32_t combatId)
{
	CombatMap::iterator it = m_combatMap.find(combatId);
	if(it != m_combatMap.end())
		return it->second;

	return NULL;
}

uint32_t LuaEnvironment::addConditionObject(Condition* condition)
{
	m_conditionMap[++m_lastConditionId] = condition;
	return m_lastConditionId;
}

uint32_t LuaEnvironment::addTempConditionObject(Condition* condition)
{
	m_conditionMap[++m_lastConditionId] = condition;
	return m_lastConditionId;
}

Condition* LuaEnvironment::getConditionObject(uint32_t conditionId)
{
	ConditionMap::iterator it = m_conditionMap.find(conditionId);
	if(it != m_conditionMap.end())
		return it->second;

	it = m_tempConditionMap.find(conditionId);
	if(it != m_tempConditionMap.end())
		return it->second;

	return NULL;
}

void LuaEnvironment::addTempItem(LuaEnvironment* env, Item* item)
{
	m_tempItems[env].push_back(item);
}

void LuaEnvironment::removeTempItem(LuaEnvironment* env, Item* item)
{
	ItemList itemList = m_tempItems[env];
	ItemList::iterator it = std::find(itemList.begin(), itemList.end(), item);
	if(it != itemList.end())
		itemList.erase(it);
}

void LuaEnvironment::removeTempItem(Item* item)
{
	for(TempItemListMap::iterator mit = m_tempItems.begin(); mit != m_tempItems.end(); ++mit)
	{
		ItemList itemList = mit->second;
		ItemList::iterator it = std::find(itemList.begin(), itemList.end(), item);
		if(it != itemList.end())
			itemList.erase(it);
	}
}

uint32_t LuaEnvironment::addResult(DBResult* res)
{
	uint32_t lastId = 0;
	while(m_tempResults.find(lastId) != m_tempResults.end())
		lastId++;

	m_tempResults[lastId] = res;
	return lastId;
}

bool LuaEnvironment::removeResult(uint32_t id)
{
	DBResultMap::iterator it = m_tempResults.find(id);
	if(it == m_tempResults.end())
		return false;

	if(it->second)
		it->second->free();

	m_tempResults.erase(it);
	return true;
}

DBResult* LuaEnvironment::getResultByID(uint32_t id)
{
	DBResultMap::iterator it = m_tempResults.find(id);
	if(it != m_tempResults.end())
		return it->second;

	return NULL;
}

bool LuaEnvironment::getStorage(const std::string& key, std::string& value) const
{
	StorageMap::const_iterator it = m_storageMap.find(key);
	if(it != m_storageMap.end())
	{
		value = it->second;
		return true;
	}

	value = "-1";
	return false;
}

void LuaEnvironment::streamVariant(std::stringstream& stream, const std::string& local, const LuaVariant& var)
{
	if(!local.empty())
		stream << "local " << local << " = {" << std::endl;

	stream << "type = " << var.type;
	switch(var.type)
	{
		case VARIANT_NUMBER:
			stream << "," << std::endl << "number = " << var.number;
			break;
		case VARIANT_STRING:
			stream << "," << std::endl << "string = \"" << var.text << "\"";
			break;
		case VARIANT_TARGETPOSITION:
		case VARIANT_POSITION:
		{
			stream << "," << std::endl;
			streamPosition(stream, "pos", var.pos);
			break;
		}
		case VARIANT_NONE:
		default:
			break;
	}

	if(!local.empty())
		stream << std::endl << "}" << std::endl;
}

void LuaEnvironment::streamThing(std::stringstream& stream, const std::string& local, Thing* thing, uint32_t id/* = 0*/)
{
	if(!local.empty())
		stream << "local " << local << " = {" << std::endl;

	if(thing && thing->getItem())
	{
		const Item* item = thing->getItem();
		if(!id)
			id = addThing(thing);

		stream << "uid = " << id << "," << std::endl;
		stream << "itemid = " << item->getID() << "," << std::endl;
		if(item->hasSubType())
			stream << "type = " << item->getSubType() << "," << std::endl;
		else
			stream << "type = 0," << std::endl;

		stream << "actionid = " << item->getActionId() << std::endl;
	}
	else if(thing && thing->getCreature())
	{
		const Creature* creature = thing->getCreature();
		if(!id)
			id = creature->getID();

		stream << "uid = " << id << "," << std::endl;
		stream << "itemid = 1," << std::endl;
		if(creature->getPlayer())
			stream << "type = 1," << std::endl;
		else if(creature->getMonster())
			stream << "type = 2," << std::endl;
		else
			stream << "type = 3," << std::endl;

		if(const Player* player = creature->getPlayer())
			stream << "actionid = " << player->getGUID() << "," << std::endl;
		else
			stream << "actionid = 0" << std::endl;
	}
	else
	{
		stream << "uid = 0," << std::endl;
		stream << "itemid = 0," << std::endl;
		stream << "type = 0," << std::endl;
		stream << "actionid = 0" << std::endl;
	}

	if(!local.empty())
		stream << "}" << std::endl;
}

void LuaEnvironment::streamPosition(std::stringstream& stream, const std::string& local, const Position& position, uint32_t stackpos)
{
	if(!local.empty())
		stream << "local " << local << " = {" << std::endl;

	stream << "x = " << position.x << "," << std::endl;
	stream << "y = " << position.y << "," << std::endl;
	stream << "z = " << position.z << "," << std::endl;

	stream << "stackpos = " << stackpos << std::endl;
	if(!local.empty())
		stream << "}" << std::endl;
}

void LuaEnvironment::streamOutfit(std::stringstream& stream, const std::string& local, const Outfit_t& outfit)
{
	if(!local.empty())
		stream << "local " << local << " = {" << std::endl;

	stream << "lookType = " << outfit.lookType << "," << std::endl;
	stream << "lookTypeEx = " << outfit.lookTypeEx << "," << std::endl;

	stream << "lookHead = " << outfit.lookHead << "," << std::endl;
	stream << "lookBody = " << outfit.lookBody << "," << std::endl;
	stream << "lookLegs = " << outfit.lookLegs << "," << std::endl;
	stream << "lookFeet = " << outfit.lookFeet << "," << std::endl;

	stream << "lookAddons = " << outfit.lookAddons << std::endl;
	if(!local.empty())
		stream << "}" << std::endl;
}

std::string LuaScriptInterface::getError(ErrorCode_t code)
{
	switch(code)
	{
		case LUA_ERROR_PLAYER_NOT_FOUND:
			return "Player not found";
		case LUA_ERROR_MONSTER_NOT_FOUND:
			return "Monster not found";
		case LUA_ERROR_NPC_NOT_FOUND:
			return "NPC not found";
		case LUA_ERROR_CREATURE_NOT_FOUND:
			return "Creature not found";
		case LUA_ERROR_ITEM_NOT_FOUND:
			return "Item not found";
		case LUA_ERROR_THING_NOT_FOUND:
			return "Thing not found";
		case LUA_ERROR_TILE_NOT_FOUND:
			return "Tile not found";
		case LUA_ERROR_HOUSE_NOT_FOUND:
			return "House not found";
		case LUA_ERROR_COMBAT_NOT_FOUND:
			return "Combat not found";
		case LUA_ERROR_CONDITION_NOT_FOUND:
			return "Condition not found";
		case LUA_ERROR_AREA_NOT_FOUND:
			return "Area not found";
		case LUA_ERROR_CONTAINER_NOT_FOUND:
			return "Container not found";
		case LUA_ERROR_VARIANT_NOT_FOUND:
			return "Variant not found";
		case LUA_ERROR_VARIANT_UNKNOWN:
			return "Unknown variant type";
		case LUA_ERROR_SPELL_NOT_FOUND:
			return "Spell not found";
		default:
			break;
	}

	return "Invalid error code!";
}

LuaEnvironment LuaScriptInterface::m_scriptEnv[21];
int32_t LuaScriptInterface::m_scriptEnvIndex = -1;

LuaScriptInterface::LuaScriptInterface(std::string interfaceName)
{
	m_luaState = NULL;
	m_interfaceName = interfaceName;
	m_lastTimer = 1000;
	m_errors = true;
}

LuaScriptInterface::~LuaScriptInterface()
{
	for(LuaTimerEvents::iterator it = m_timerEvents.begin(); it != m_timerEvents.end(); ++it)
		Scheduler::getInstance().stopEvent(it->second.eventId);

	closeState();
}

bool LuaScriptInterface::reInitState()
{
	closeState();
	return initState();
}

bool LuaScriptInterface::loadBuffer(const std::string& text, Npc* npc/* = NULL*/)
{
	//loads buffer as a chunk at stack top
	int32_t ret = luaL_loadbuffer(m_luaState, text.c_str(), text.length(), "LuaScriptInterface::loadBuffer");
	if(ret)
	{
		m_lastError = popString(m_luaState);
		error(NULL, m_lastError);
		return false;
	}

	//check that it is loaded as a function
	if(!isFunction(m_luaState, -1))
		return false;

	m_loadingFile = text;
	reserveEnv();

	LuaEnvironment* env = getScriptEnv();
	env->setScriptId(EVENT_ID_LOADING, this);
	env->setNpc(npc);

	//execute it
	ret = lua_pcall(m_luaState, 0, 0, 0);
	if(ret)
	{
		error(NULL, popString(m_luaState));
		releaseEnv();
		return false;
	}

	releaseEnv();
	return true;
}

bool LuaScriptInterface::loadFile(const std::string& file, Npc* npc/* = NULL*/)
{
	//loads file as a chunk at stack top
	int32_t ret = luaL_loadfile(m_luaState, file.c_str());
	if(ret)
	{
		m_lastError = popString(m_luaState);
		std::clog << "[Error - LuaScriptInterface::loadFile] " << m_lastError << std::endl;
		return false;
	}

	//check that it is loaded as a function
	if(!isFunction(m_luaState, -1))
		return false;

	m_loadingFile = file;
	reserveEnv();

	LuaEnvironment* env = getScriptEnv();
	env->setScriptId(EVENT_ID_LOADING, this);
	env->setNpc(npc);

	//execute it
	ret = lua_pcall(m_luaState, 0, 0, 0);
	if(ret)
	{
		error(NULL, popString(m_luaState));
		releaseEnv();
		return false;
	}

	releaseEnv();
	return true;
}

bool LuaScriptInterface::loadDirectory(const std::string& dir, Npc* npc/* = NULL*/, bool recursively/* = false*/)
{
	StringVec files;
	for(boost::filesystem::directory_iterator it(dir), end; it != end; ++it)
	{
		std::string s = BOOST_DIR_ITER_FILENAME(it);
		if(!boost::filesystem::is_directory(it->status()) && (s.size() > 4 ? s.substr(s.size() - 4) : "") == ".lua")
			files.push_back(s);
	}

	std::sort(files.begin(), files.end());
	for(StringVec::iterator it = files.begin(); it != files.end(); ++it)
	{
		if(!loadFile(dir + (*it), npc))
			return false;
	}

	return true;
}

int32_t LuaScriptInterface::getEvent()
{
	//check if function is on the stack
	if (!isFunction(m_luaState, -1)) {
		return -1;
	}

	//get our events table
	lua_rawgeti(m_luaState, LUA_REGISTRYINDEX, m_eventTableRef);
	if (!lua_istable(m_luaState, -1)) {
		lua_pop(m_luaState, 1);
		return -1;
	}

	//save in our events table
	lua_pushvalue(m_luaState, -2);
	lua_rawseti(m_luaState, -2, m_runningEvent);
	lua_pop(m_luaState, 2);

	m_cacheFiles[m_runningEvent] = m_loadingFile + ":callback";
	return m_runningEvent++;
}

int32_t LuaScriptInterface::getEvent(const std::string& eventName)
{
	//get our events table
	lua_rawgeti(m_luaState, LUA_REGISTRYINDEX, m_eventTableRef);
	if (!isTable(m_luaState, -1)) {
		lua_pop(m_luaState, 1);
		return -1;
	}

	//get current event function pointer
	lua_getglobal(m_luaState, eventName.c_str());
	if (!isFunction(m_luaState, -1)) {
		lua_pop(m_luaState, 2);
		return -1;
	}

	//save in our events table
	lua_pushvalue(m_luaState, -1);
	lua_rawseti(m_luaState, -3, m_runningEvent);
	lua_pop(m_luaState, 2);

	//reset global value of this event
	lua_pushnil(m_luaState);
	lua_setglobal(m_luaState, eventName.c_str());

	m_cacheFiles[m_runningEvent] = m_loadingFile + ":" + eventName;
	return m_runningEvent++;
}

std::string LuaScriptInterface::getScript(int32_t scriptId)
{
	const static std::string tmp = "(Unknown script file)";
	if(scriptId != EVENT_ID_LOADING)
	{
		ScriptsCache::iterator it = m_cacheFiles.find(scriptId);
		if(it != m_cacheFiles.end())
			return it->second;

		return tmp;
	}

	return m_loadingFile;
}

void LuaScriptInterface::error(const char* function, const std::string& desc)
{
	int32_t script, callback;
	bool timer;
	std::string event;

	LuaScriptInterface* interface;
	getScriptEnv()->getInfo(script, event, interface, callback, timer);
	if(interface)
	{
		if(!interface->m_errors)
			return;

		std::clog << std::endl << "[Error - " << interface->getName() << "] " << std::endl;
		if(callback)
			std::clog << "In a callback: " << interface->getScript(callback) << std::endl;

		if(timer)
			std::clog << (callback ? "from" : "In") << " a timer event called from: " << std::endl;

		std::clog << interface->getScript(script) << std::endl << "Description: ";
	}
	else
		std::clog << std::endl << "[Lua Error] ";

	std::clog << event << std::endl;
	if(function)
		std::clog << "(" << function << ") ";

	std::clog << desc << std::endl;
}

bool LuaScriptInterface::pushFunction(int32_t functionId)
{
	lua_rawgeti(m_luaState, LUA_REGISTRYINDEX, m_eventTableRef);
	if (!isTable(m_luaState, -1)) {
		return false;
	}

	lua_rawgeti(m_luaState, -1, functionId);
	lua_replace(m_luaState, -2);
	return isFunction(m_luaState, -1);
}

bool LuaScriptInterface::initState()
{
	m_luaState = luaL_newstate();
	if(!m_luaState)
		return false;

	luaL_openlibs(m_luaState);
#ifdef __LUAJIT__
	luaJIT_setmode(m_luaState, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_ON);
#endif

	registerFunctions();
	if(!loadDirectory(getFilePath(FILE_TYPE_OTHER, "lib/"), NULL))
		std::clog << "[Warning - LuaScriptInterface::initState] Cannot load " << getFilePath(FILE_TYPE_OTHER, "lib/") << std::endl;

	lua_newtable(m_luaState);
	m_eventTableRef = luaL_ref(m_luaState, LUA_REGISTRYINDEX);
	m_runningEvent = EVENT_ID_USER;
	return true;
}

bool LuaScriptInterface::closeState()
{
	if(!m_luaState)
		return false;

	m_cacheFiles.clear();
	for(LuaTimerEvents::iterator it = m_timerEvents.begin(); it != m_timerEvents.end(); ++it)
	{
		for(std::list<int32_t>::iterator lt = it->second.parameters.begin(); lt != it->second.parameters.end(); ++lt)
			luaL_unref(m_luaState, LUA_REGISTRYINDEX, *lt);

		it->second.parameters.clear();
		luaL_unref(m_luaState, LUA_REGISTRYINDEX, it->second.function);
	}

	m_timerEvents.clear();
	lua_close(m_luaState);

	if (m_eventTableRef != -1) {
		luaL_unref(m_luaState, LUA_REGISTRYINDEX, m_eventTableRef);
		m_eventTableRef = -1;
	}	

	return true;
}

void LuaScriptInterface::executeTimer(uint32_t eventIndex)
{
	LuaTimerEvents::iterator it = m_timerEvents.find(eventIndex);
	if(it != m_timerEvents.end())
	{
		//push function
		lua_rawgeti(m_luaState, LUA_REGISTRYINDEX, it->second.function);

		//push parameters
		for(std::list<int32_t>::reverse_iterator rt = it->second.parameters.rbegin(); rt != it->second.parameters.rend(); ++rt)
			lua_rawgeti(m_luaState, LUA_REGISTRYINDEX, *rt);

		//CASTll the function
		if(reserveEnv())
		{
			LuaEnvironment* env = getScriptEnv();
			env->setTimerEvent();
			env->setScriptId(it->second.scriptId, this);

			callFunction(it->second.parameters.size());
			releaseEnv();
		}
		else
			std::clog << "[Error - LuaScriptInterface::executeTimer] Call stack overflow." << std::endl;

		//free resources
		for(std::list<int32_t>::iterator lt = it->second.parameters.begin(); lt != it->second.parameters.end(); ++lt)
			luaL_unref(m_luaState, LUA_REGISTRYINDEX, *lt);

		it->second.parameters.clear();
		luaL_unref(m_luaState, LUA_REGISTRYINDEX, it->second.function);
		m_timerEvents.erase(it);
	}
}

std::string LuaScriptInterface::getString(lua_State* L, int32_t arg)
{
	size_t len;
	const char* c_str = lua_tolstring(L, arg, &len);
	if (!c_str || len == 0) {
		return std::string();
	}
	return std::string(c_str, len);
}

std::string LuaScriptInterface::getStackTrace(const std::string& error_desc)
{
	lua_getglobal(m_luaState, "debug");
	if (!lua_istable(m_luaState, -1)) {
		lua_pop(m_luaState, 1);
		return error_desc;
	}

	lua_getfield(m_luaState, -1, "traceback");
	if (!isFunction(m_luaState, -1)) {
		lua_pop(m_luaState, 2);
		return error_desc;
	}

	lua_replace(m_luaState, -2);
	pushString(m_luaState, error_desc);
	lua_call(m_luaState, 1, 1);
	return popString(m_luaState);
}

int LuaScriptInterface::luaErrorHandler(lua_State* L)
{
	const std::string& errorMessage = popString(L);
	auto interface = getScriptEnv()->getScriptInterface();
	assert(interface); //This fires if the ScriptEnvironment hasn't been setup
	lua_pushstring(L, interface->getStackTrace(errorMessage).c_str());
	return 1;
}

/// Same as lua_pcall, but adds stack trace to error strings in called function.
int LuaScriptInterface::protectedCall(lua_State* L, int nargs, int nresults)
{
	int error_index = lua_gettop(L) - nargs;
	lua_pushcfunction(L, luaErrorHandler);
	lua_insert(L, error_index);

	int ret = lua_pcall(L, nargs, nresults, error_index);	
	lua_remove(L, error_index);
	return ret;
}

bool LuaScriptInterface::callFunction(uint32_t params)
{
	bool result = false;
	uint32_t size = lua_gettop(m_luaState);
	if (protectedCall(m_luaState, params, 1) != 0) {
		LuaScriptInterface::error(nullptr, LuaScriptInterface::getString(m_luaState, -1));
	} else {
		result = lua_toboolean(m_luaState, -1) != 0;
	}

	lua_pop(m_luaState, 1);
	if ((lua_gettop(m_luaState) + params + 1) != size) {
		LuaScriptInterface::error(nullptr, "Stack size changed!");
	}

	releaseEnv();
	return result;
}

void LuaScriptInterface::dumpStack(lua_State* L/* = NULL*/)
{
	if(!L)
		L = m_luaState;

	int32_t stack = lua_gettop(L);
	if(!stack)
		return;

	std::clog << "Stack size: " << stack << std::endl;
	for(int32_t i = 1; i <= stack ; ++i)
		std::clog << lua_typename(m_luaState, isNumber(m_luaState, -i)) << " " << lua_topointer(m_luaState, -i) << std::endl;
}
//	Push
void LuaScriptInterface::pushVariant(lua_State* L, const LuaVariant& var)
{
	lua_newtable(L);
	setField(L, "type", var.type);
	switch(var.type)
	{
		case VARIANT_NUMBER:
			setField(L, "number", var.number);
			break;
		case VARIANT_STRING:
			setField(L, "string", var.text);
			break;
		case VARIANT_TARGETPOSITION:
		case VARIANT_POSITION:
		{
			lua_pushstring(L, "pos");
			pushPosition(L, var.pos);
			pushTable(L);
			break;
		}
		case VARIANT_NONE:
			break;
	}
}

void LuaScriptInterface::pushBoolean(lua_State* L, bool value)
{
	lua_pushboolean(L, value ? 1 : 0);
}


void LuaScriptInterface::pushThing(lua_State* L, Thing* thing, uint32_t id/* = 0*/)
{
	lua_newtable(L);
	if(thing && thing->getItem())
	{
		const Item* item = thing->getItem();
		if(!id)
			id = getScriptEnv()->addThing(thing);

		setField(L, "uid", id);
		setField(L, "itemid", item->getID());
		if(item->hasSubType())
			setField(L, "type", item->getSubType());
		else
			setField(L, "type", 0);

		setField(L, "actionid", item->getActionId());
	}
	else if(thing && thing->getCreature())
	{
		const Creature* creature = thing->getCreature();
		if(!id)
			id = creature->getID();

		setField(L, "uid", id);
		setField(L, "itemid", 1);
		if(creature->getPlayer())
			setField(L, "type", 1);
		else if(creature->getMonster())
			setField(L, "type", 2);
		else
			setField(L, "type", 3);

		if(const Player* player = creature->getPlayer())
			setField(L, "actionid", player->getGUID());
		else
			setField(L, "actionid", 0);
	}
	else
	{
		setField(L, "uid", 0);
		setField(L, "itemid", 0);
		setField(L, "type", 0);
		setField(L, "actionid", 0);
	}
}

void LuaScriptInterface::pushPosition(lua_State* L, const Position& position, uint32_t stackpos)
{
	lua_newtable(L);
	setField(L, "x", position.x);
	setField(L, "y", position.y);
	setField(L, "z", position.z);
	setField(L, "stackpos", stackpos);
}

void LuaScriptInterface::pushOutfit(lua_State* L, const Outfit_t& outfit)
{
	lua_newtable(L);
	setField(L, "lookType", outfit.lookType);
	setField(L, "lookTypeEx", outfit.lookTypeEx);
	setField(L, "lookHead", outfit.lookHead);
	setField(L, "lookBody", outfit.lookBody);
	setField(L, "lookLegs", outfit.lookLegs);
	setField(L, "lookFeet", outfit.lookFeet);
	setField(L, "lookAddons", outfit.lookAddons);
}

void LuaScriptInterface::pushString(lua_State* L, const std::string& value)
{
	lua_pushlstring(L, value.c_str(), value.length());
}

void LuaScriptInterface::pushCallback(lua_State* L, int32_t callback)
{
	lua_rawgeti(L, LUA_REGISTRYINDEX, callback);
}

LuaVariant LuaScriptInterface::popVariant(lua_State* L)
{
	LuaVariant var;
	var.type = (LuaVariantType_t)getField<uint16_t>(L, 1, "type");
	switch(var.type)
	{
		case VARIANT_NUMBER:
			var.number = getFieldUnsigned(L, "number");
			break;
		case VARIANT_STRING:
			var.text = getFieldString(L, "string");
			break;
		case VARIANT_POSITION:
		case VARIANT_TARGETPOSITION:
		{
			lua_pushstring(L, "pos");
			lua_gettable(L, -2);
			var.pos = getPosition(L, lua_gettop(L));
			break;
		}
		default:
			var.type = VARIANT_NONE;
			break;
	}

	lua_pop(L, 1); //table
	return var;
}

Position LuaScriptInterface::getPosition(lua_State* L, int32_t arg)
{
	Position position;
	position.x = getField<uint16_t>(L, arg, "x");
	position.y = getField<uint16_t>(L, arg, "y");
	position.z = getField<uint8_t>(L, arg, "z");

	lua_pop(L, 3);
	return position;
}

Position LuaScriptInterface::getPosition(lua_State* L, int32_t arg, int32_t& stackpos)
{
	Position position;
	position.x = getField<uint16_t>(L, arg, "x");
	position.y = getField<uint16_t>(L, arg, "y");
	position.z = getField<uint8_t>(L, arg, "z");

	lua_getfield(L, arg, "stackpos");
	if (lua_isnil(L, -1) == 1) {
		stackpos = 0;
	} else {
		stackpos = getNumber<int32_t>(L, -1);
	}

	lua_pop(L, 4);
	return position;
}

Thing* LuaScriptInterface::getThing(lua_State* L, int32_t arg)
{
	Thing* thing;
	if (lua_getmetatable(L, arg) != 0) {
		lua_rawgeti(L, -1, 't');
		switch(getNumber<uint32_t>(L, -1)) {
			case LuaData_Item:
				thing = getUserdata<Item>(L, arg);
				break;
			case LuaData_Container:
				thing = getUserdata<Container>(L, arg);
				break;
			case LuaData_Teleport:
				thing = getUserdata<Teleport>(L, arg);
				break;
			case LuaData_Player:
				thing = getUserdata<Player>(L, arg);
				break;
			case LuaData_Monster:
				thing = getUserdata<Monster>(L, arg);
				break;
			case LuaData_Npc:
				thing = getUserdata<Npc>(L, arg);
				break;
			default:
				thing = nullptr;
				break;
		}
		lua_pop(L, 2);
	} else {
		thing = getScriptEnv()->getThingByUID(getNumber<uint32_t>(L, arg));
	}
	return thing;
}

bool LuaScriptInterface::popBoolean(lua_State* L)
{
	lua_pop(L, 1);
	return lua_toboolean(L, 0);
}

int64_t LuaScriptInterface::popNumber(lua_State* L)
{
	lua_pop(L, 1);
	if(isBoolean(L, 0))
		return (int64_t)lua_toboolean(L, 0);

	return (int64_t)lua_tonumber(L, 0);
}

double LuaScriptInterface::popFloatNumber(lua_State* L)
{
	lua_pop(L, 1);
	return lua_tonumber(L, 0);
}

std::string LuaScriptInterface::popString(lua_State* L)
{
	lua_pop(L, 1);
	const char* str = lua_tostring(L, 0);
	if(!str || !strlen(str))
		return std::string();

	return str;
}

int32_t LuaScriptInterface::popCallback(lua_State* L)
{
	return luaL_ref(L, LUA_REGISTRYINDEX);
}

Outfit_t LuaScriptInterface::getOutfit(lua_State* L, int arg)
{
	Outfit_t outfit;
	outfit.lookAddons = getField<uint16_t>(L, arg, "lookAddons");

	outfit.lookFeet = getField<uint16_t>(L, arg, "lookFeet");
	outfit.lookLegs = getField<uint16_t>(L, arg, "lookLegs");
	outfit.lookBody = getField<uint16_t>(L, arg, "lookBody");
	outfit.lookHead = getField<uint16_t>(L, arg, "lookHead");

	outfit.lookTypeEx = getField<uint16_t>(L, arg, "lookTypeEx");
	outfit.lookType = getField<uint16_t>(L, arg, "lookType");

	lua_pop(L, 1); //table
	return outfit;
}

void LuaScriptInterface::setField(lua_State* L, const char* index, int32_t val)
{
	lua_pushstring(L, index);
	lua_pushnumber(L, val);
	pushTable(L);
}

void LuaScriptInterface::setField(lua_State* L, const char* index, const std::string& val)
{
	lua_pushstring(L, index);
	lua_pushstring(L, val.c_str());
	pushTable(L);
}

void LuaScriptInterface::setFieldBool(lua_State* L, const char* index, bool val)
{
	lua_pushstring(L, index);
	pushBoolean(L, val);
	pushTable(L);
}

void LuaScriptInterface::setFieldFloat(lua_State* L, const char* index, double val)
{
	lua_pushstring(L, index);
	lua_pushnumber(L, val);
	pushTable(L);
}

void LuaScriptInterface::createTable(lua_State* L, const char* index)
{
	lua_pushstring(L, index);
	lua_newtable(L);
}

void LuaScriptInterface::createTable(lua_State* L, const char* index, int32_t narr, int32_t nrec)
{
	lua_pushstring(L, index);
	lua_createtable(L, narr, nrec);
}

void LuaScriptInterface::createTable(lua_State* L, int32_t index)
{
	lua_pushnumber(L, index);
	lua_newtable(L);
}

void LuaScriptInterface::createTable(lua_State* L, int32_t index, int32_t narr, int32_t nrec)
{
	lua_pushnumber(L, index);
	lua_createtable(L, narr, nrec);
}

void LuaScriptInterface::pushTable(lua_State* L)
{
	lua_settable(L, -3);
}

uint64_t LuaScriptInterface::getFieldUnsigned(lua_State* L, const char* key)
{
	lua_pushstring(L, key);
	lua_gettable(L, -2); // get table[key]

	uint64_t result = (uint64_t)lua_tonumber(L, -1);
	lua_pop(L, 1); // remove number and key
	return result;
}

bool LuaScriptInterface::getFieldBool(lua_State* L, const char* key)
{
	lua_pushstring(L, key);
	lua_gettable(L, -2); // get table[key]

	bool result = lua_toboolean(L, -1);
	lua_pop(L, 1); // remove number and key
	return result;
}

std::string LuaScriptInterface::getFieldString(lua_State* L, const char* key)
{
	lua_pushstring(L, key);
	lua_gettable(L, -2); // get table[key]

	std::string result = lua_tostring(L, -1);
	lua_pop(L, 1); // remove number and key
	return result;
}

LuaDataType LuaScriptInterface::getUserdataType(lua_State* L, int32_t arg)
{
	if (lua_getmetatable(L, arg) == 0) {
		return LuaData_Unknown;
	}
	lua_rawgeti(L, -1, 't');

	LuaDataType type = getNumber<LuaDataType>(L, -1);
	lua_pop(L, 2);

	return type;
}

std::string LuaScriptInterface::getGlobalString(lua_State* L, const std::string& _identifier, const std::string& _default/* = ""*/)
{
	lua_getglobal(L, _identifier.c_str());
	if(!isString(L, -1))
	{
		lua_pop(L, 1);
		return _default;
	}

	int32_t len = (int32_t)lua_strlen(L, -1);
	std::string ret(lua_tostring(L, -1), len);

	lua_pop(L, 1);
	return ret;
}

bool LuaScriptInterface::getGlobalBool(lua_State* L, const std::string& _identifier, bool _default/* = false*/)
{
	lua_getglobal(L, _identifier.c_str());
	if(!isBoolean(L, -1))
	{
		lua_pop(L, 1);
		return booleanString(LuaScriptInterface::getGlobalString(L, _identifier, _default ? "yes" : "no"));
	}

	bool val = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return val;
}

int32_t LuaScriptInterface::getGlobalNumber(lua_State* L, const std::string& _identifier, const int32_t _default/* = 0*/)
{
	return (int32_t)LuaScriptInterface::getGlobalDouble(L, _identifier, _default);
}

double LuaScriptInterface::getGlobalDouble(lua_State* L, const std::string& _identifier, const double _default/* = 0*/)
{
	lua_getglobal(L, _identifier.c_str());
	if(!lua_isnumber(L, -1))
	{
		lua_pop(L, 1);
		return _default;
	}

	double val = lua_tonumber(L, -1);
	lua_pop(L, 1);
	return val;
}

void LuaScriptInterface::getValue(const std::string& key, lua_State* L, lua_State* _L)
{
	lua_getglobal(L, key.c_str());
	moveValue(L, _L);
}

void LuaScriptInterface::moveValue(lua_State* from, lua_State* to)
{
	switch(lua_type(from, -1))
	{
		case LUA_TNIL:
			lua_pushnil(to);
			break;
		case LUA_TBOOLEAN:
			pushBoolean(to, lua_toboolean(from, -1));
			break;
		case LUA_TNUMBER:
			lua_pushnumber(to, lua_tonumber(from, -1));
			break;
		case LUA_TSTRING:
		{
			size_t len;
			const char* str = lua_tolstring(from, -1, &len);

			pushString(to, str);
			break;
		}
		case LUA_TTABLE:
		{
			lua_newtable(to);
			lua_pushnil(from); // First key
			while(lua_next(from, -2))
			{
				// Move value to the other state
				moveValue(from, to); // Value is popped, key is left
				// Move key to the other state
				lua_pushvalue(from, -1); // Make a copy of the key to use for the next iteration
				moveValue(from, to); // Key is in other state.
				// We still have the key in the 'from' state ontop of the stack

				lua_insert(to, -2); // Move key above value
				pushTable(to); // Set the key
			}

			break;
		}
		default:
			break;
	}

	lua_pop(from, 1); // Pop the value we just read
}

void LuaScriptInterface::registerFunctions()
{
	//example(...)
	//lua_register(L, "name", C_function);
   	//registerMethod("name", "name", C_function);

	// Game

	registerTable("Game");
	//getWorldType()
	lua_register(m_luaState, "getWorldType", LuaScriptInterface::luaGetWorldType);
	registerMethod("Game", "getWorldType", LuaScriptInterface::luaGetWorldType);

	//setWorldType(type)
	lua_register(m_luaState, "setWorldType", LuaScriptInterface::luaSetWorldType);

	//getWorldTime()
	lua_register(m_luaState, "getWorldTime", LuaScriptInterface::luaGetWorldTime);

	//getWorldLight()
	lua_register(m_luaState, "getWorldLight", LuaScriptInterface::luaGetWorldLight);

	//getWorldCreatures(type)
	//0 players, 1 monsters, 2 npcs, 3 all
	lua_register(m_luaState, "getWorldCreatures", LuaScriptInterface::luaGetWorldCreatures);

	//isInArray(array, value[, caseSensitive = false])
	lua_register(m_luaState, "isInArray", LuaScriptInterface::luaIsInArray);

	//addEvent(callback, delay, ...)
	lua_register(m_luaState, "addEvent", LuaScriptInterface::luaAddEvent);

	//stopEvent(eventid)
	lua_register(m_luaState, "stopEvent", LuaScriptInterface::luaStopEvent);

	//getStorage(key)
	lua_register(m_luaState, "getStorage", LuaScriptInterface::luaGetStorage);

	//doSetStorage(key, value)
	lua_register(m_luaState, "doSetStorage", LuaScriptInterface::luaDoSetStorage);

	//getChannelUsers(channelId)
	lua_register(m_luaState, "getChannelUsers", LuaScriptInterface::luaGetChannelUsers);

	//getPlayersOnline()
	lua_register(m_luaState, "getPlayersOnline", LuaScriptInterface::luaGetPlayersOnline);

	//getWorldUpTime()
	lua_register(m_luaState, "getWorldUpTime", LuaScriptInterface::luaGetWorldUpTime);

	//getTownId(townName)
	lua_register(m_luaState, "getTownId", LuaScriptInterface::luaGetTownId);

	//getTownName(townId)
	lua_register(m_luaState, "getTownName", LuaScriptInterface::luaGetTownName);

	//getTownTemplePosition(townId[, displayError])
	lua_register(m_luaState, "getTownTemplePosition", LuaScriptInterface::luaGetTownTemplePosition);

	//getTownHouses(townId)
	lua_register(m_luaState, "getTownHouses", LuaScriptInterface::luaGetTownHouses);

	//getSpectators(centerPos, rangex, rangey[, multifloor = false])
	lua_register(m_luaState, "getSpectators", LuaScriptInterface::luaGetSpectators);

	//getVocationInfo(id)
	lua_register(m_luaState, "getVocationInfo", LuaScriptInterface::luaGetVocationInfo);

	//getGroupInfo(id[, premium = false])
	lua_register(m_luaState, "getGroupInfo", LuaScriptInterface::luaGetGroupInfo);

	//getVocationList()
	lua_register(m_luaState, "getVocationList", LuaScriptInterface::luaGetVocationList);

	//getGroupList()
	lua_register(m_luaState, "getGroupList", LuaScriptInterface::luaGetGroupList);

	//getChannelList()
	lua_register(m_luaState, "getChannelList", LuaScriptInterface::luaGetChannelList);

	//getTownList()
	lua_register(m_luaState, "getTownList", LuaScriptInterface::luaGetTownList);

	//getWaypointList()
	lua_register(m_luaState, "getWaypointList", LuaScriptInterface::luaGetWaypointList);

	//getTalkActionList()
	lua_register(m_luaState, "getTalkActionList", LuaScriptInterface::luaGetTalkActionList);

	//getExperienceStageList()
	lua_register(m_luaState, "getExperienceStageList", LuaScriptInterface::luaGetExperienceStageList);

	//isIpBanished(ip[, mask])
	lua_register(m_luaState, "isIpBanished", LuaScriptInterface::luaIsIpBanished);

	//isPlayerBanished(name/guid, type)
	lua_register(m_luaState, "isPlayerBanished", LuaScriptInterface::luaIsPlayerBanished);

	//isAccountBanished(accountId[, playerId])
	lua_register(m_luaState, "isAccountBanished", LuaScriptInterface::luaIsAccountBanished);

	//doAddIpBanishment(...)
	lua_register(m_luaState, "doAddIpBanishment", LuaScriptInterface::luaDoAddIpBanishment);

	//doAddPlayerBanishment(...)
	lua_register(m_luaState, "doAddPlayerBanishment", LuaScriptInterface::luaDoAddPlayerBanishment);

	//doAddAccountBanishment(...)
	lua_register(m_luaState, "doAddAccountBanishment", LuaScriptInterface::luaDoAddAccountBanishment);

	//doAddNotation(...)
	lua_register(m_luaState, "doAddNotation", LuaScriptInterface::luaDoAddNotation);

	//doAddStatement(...)
	lua_register(m_luaState, "doAddStatement", LuaScriptInterface::luaDoAddStatement);

	//doRemoveIpBanishment(ip[, mask])
	lua_register(m_luaState, "doRemoveIpBanishment", LuaScriptInterface::luaDoRemoveIpBanishment);

	//doRemovePlayerBanishment(name/guid, type)
	lua_register(m_luaState, "doRemovePlayerBanishment", LuaScriptInterface::luaDoRemovePlayerBanishment);

	//doRemoveAccountBanishment(accountId[, playerId])
	lua_register(m_luaState, "doRemoveAccountBanishment", LuaScriptInterface::luaDoRemoveAccountBanishment);

	//doRemoveNotations(accountId[, playerId])
	lua_register(m_luaState, "doRemoveNotations", LuaScriptInterface::luaDoRemoveNotations);

	//doRemoveStatements(name/guid[, channelId])
	lua_register(m_luaState, "doRemoveStatements", LuaScriptInterface::luaDoRemoveStatements);

	//getNotationsCount(accountId[, playerId])
	lua_register(m_luaState, "getNotationsCount", LuaScriptInterface::luaGetNotationsCount);

	//getStatementsCount(name/guid[, channelId])
	lua_register(m_luaState, "getStatementsCount", LuaScriptInterface::luaGetStatementsCount);

	//getBanData(value[, type[, param]])
	lua_register(m_luaState, "getBanData", LuaScriptInterface::luaGetBanData);

	//getBanReason(id)
	lua_register(m_luaState, "getBanReason", LuaScriptInterface::luaGetBanReason);

	//getBanAction(id[, ipBanishment = false])
	lua_register(m_luaState, "getBanAction", LuaScriptInterface::luaGetBanAction);

	//getBanList(type[, value[, param]])
	lua_register(m_luaState, "getBanList", LuaScriptInterface::luaGetBanList);

	//getExperienceStage(level)
	lua_register(m_luaState, "getExperienceStage", LuaScriptInterface::luaGetExperienceStage);

	//getDataDir()
	lua_register(m_luaState, "getDataDir", LuaScriptInterface::luaGetDataDir);

	//getLogsDir()
	lua_register(m_luaState, "getLogsDir", LuaScriptInterface::luaGetLogsDir);

	//getConfigFile()
	lua_register(m_luaState, "getConfigFile", LuaScriptInterface::luaGetConfigFile);

	//getConfigValue(key)
	lua_register(m_luaState, "getConfigValue", LuaScriptInterface::luaGetConfigValue);

	//getModList()
	lua_register(m_luaState, "getModList", LuaScriptInterface::luaGetModList);

	//getHighscoreString(skillId)
	lua_register(m_luaState, "getHighscoreString", LuaScriptInterface::luaGetHighscoreString);

	//getWaypointPosition(name)
	lua_register(m_luaState, "getWaypointPosition", LuaScriptInterface::luaGetWaypointPosition);

	//doWaypointAddTemporial(name, pos)
	lua_register(m_luaState, "doWaypointAddTemporial", LuaScriptInterface::luaDoWaypointAddTemporial);

	//getGameState()
	lua_register(m_luaState, "getGameState", LuaScriptInterface::luaGetGameState);

	//doSetGameState(id)
	lua_register(m_luaState, "doSetGameState", LuaScriptInterface::luaDoSetGameState);

	//doExecuteRaid(name)
	lua_register(m_luaState, "doExecuteRaid", LuaScriptInterface::luaDoExecuteRaid);

	//doCreatureExecuteTalkAction(cid, text[, ignoreAccess = false[, channelId = CHANNEL_DEFAULT]])
	lua_register(m_luaState, "doCreatureExecuteTalkAction", LuaScriptInterface::luaDoCreatureExecuteTalkAction);

	//doReloadInfo(id[, cid])
	lua_register(m_luaState, "doReloadInfo", LuaScriptInterface::luaDoReloadInfo);

	//doSaveServer([shallow = false])
	lua_register(m_luaState, "doSaveServer", LuaScriptInterface::luaDoSaveServer);

	//doCleanHouse(houseId)
	lua_register(m_luaState, "doCleanHouse", LuaScriptInterface::luaDoCleanHouse);

	//doCleanMap()
	lua_register(m_luaState, "doCleanMap", LuaScriptInterface::luaDoCleanMap);

	//doRefreshMap()
	lua_register(m_luaState, "doRefreshMap", LuaScriptInterface::luaDoRefreshMap);

	//doUpdateHouseAuctions()
	lua_register(m_luaState, "doUpdateHouseAuctions", LuaScriptInterface::luaDoUpdateHouseAuctions);

	//getHouseInfo(houseId[, displayError = true])
	lua_register(m_luaState, "getHouseInfo", LuaScriptInterface::luaGetHouseInfo);

	//getHouseAccessList(houseid, listId)
	lua_register(m_luaState, "getHouseAccessList", LuaScriptInterface::luaGetHouseAccessList);

	//getHouseByPlayerGUID(playerGUID)
	lua_register(m_luaState, "getHouseByPlayerGUID", LuaScriptInterface::luaGetHouseByPlayerGUID);

	//getHouseFromPos(pos)
	lua_register(m_luaState, "getHouseFromPos", LuaScriptInterface::luaGetHouseFromPos);

	//setHouseAccessList(houseid, listid, listtext)
	lua_register(m_luaState, "setHouseAccessList", LuaScriptInterface::luaSetHouseAccessList);

	//setHouseOwner(houseId, owner[, clean])
	lua_register(m_luaState, "setHouseOwner", LuaScriptInterface::luaSetHouseOwner);

   	//	Creature

	registerClass("Creature", "", LuaScriptInterface::luaCreatureCreate);
	registerMetaMethod("Creature", "__eq", LuaScriptInterface::luaUserdataCompare);

	//getCreatureHealth(cid)
	lua_register(m_luaState, "getCreatureHealth", LuaScriptInterface::luaGetCreatureHealth);
	registerMethod("Creature", "getHealth", LuaScriptInterface::luaGetCreatureHealth);

	//getCreatureMaxHealth(cid[, ignoreModifiers = false])
	lua_register(m_luaState, "getCreatureMaxHealth", LuaScriptInterface::luaGetCreatureMaxHealth);

	//getCreatureMana(cid)
	lua_register(m_luaState, "getCreatureMana", LuaScriptInterface::luaGetCreatureMana);

	//getCreatureMaxMana(cid[, ignoreModifiers = false])
	lua_register(m_luaState, "getCreatureMaxMana", LuaScriptInterface::luaGetCreatureMaxMana);

	//getCreatureHideHealth(cid)
	lua_register(m_luaState, "getCreatureHideHealth", LuaScriptInterface::luaGetCreatureHideHealth);

	//doCreatureSetHideHealth(cid, hide)
	lua_register(m_luaState, "doCreatureSetHideHealth", LuaScriptInterface::luaDoCreatureSetHideHealth);

	//getCreatureSpeakType(cid)
	lua_register(m_luaState, "getCreatureSpeakType", LuaScriptInterface::luaGetCreatureSpeakType);

	//doCreatureSetSpeakType(cid, type)
	lua_register(m_luaState, "doCreatureSetSpeakType", LuaScriptInterface::luaDoCreatureSetSpeakType);

	//getCreatureLookDirection(cid)
	lua_register(m_luaState, "getCreatureLookDirection", LuaScriptInterface::luaGetCreatureLookDirection);

   	//	Cast

    //getCastsOnline()
	lua_register(m_luaState, "getCastsOnline", LuaScriptInterface::luaGetCastsOnline);
		
	//doPlayerSetCastDescription(cid, desc)
	lua_register(m_luaState, "doPlayerSetCastDescription", LuaScriptInterface::luaDoPlayerSetCastDescription);

	//doPlayerAddCastMute(cid, ip)
	lua_register(m_luaState, "doPlayerAddCastMute", LuaScriptInterface::luaDoPlayerAddCastMute);

	//doPlayerRemoveCastMute(cidl, ip)
	lua_register(m_luaState, "doPlayerRemoveCastMute", LuaScriptInterface::luaDoPlayerRemoveCastMute);

	//doPlayerGetCastMutes(cid)
	lua_register(m_luaState, "getCastMutes", LuaScriptInterface::luaGetPlayerCastMutes);

	//doPlayerAddCastBan(cid, ip)
	lua_register(m_luaState, "doPlayerAddCastBan", LuaScriptInterface::luaDoPlayerAddCastBan);

	//doPlayerRemoveCastBan(cid, ip)
	lua_register(m_luaState, "doPlayerRemoveCastBan", LuaScriptInterface::luaDoPlayerRemoveCastBan);
	
	//doPlayerAddCastKick(cid, ip)
    lua_register(m_luaState, "doPlayerAddCastKick", LuaScriptInterface::luaDoPlayerAddCastKick);

	//doPlayerGetCastBan(cid)
	lua_register(m_luaState, "getCastBans", LuaScriptInterface::luaGetPlayerCastBans);

	//doPlayerAddCastBan(cid, ip)
	lua_register(m_luaState, "getCastViewers", LuaScriptInterface::luaGetPlayerCastViewers);

	//doPlayerSetCastPassword(cid, password)
	lua_register(m_luaState, "doPlayerSetCastPassword", LuaScriptInterface::luaDoPlayerSetCastPassword);

	//getPlayerCast(cid)
	lua_register(m_luaState, "doPlayerSetCastState", LuaScriptInterface::luaDoPlayerSetCastState);

	//getPlayerCast(cid)
	lua_register(m_luaState, "getPlayerCast", LuaScriptInterface::luaGetPlayerCast);
	
	//Player

	registerClass("Player", "Creature", LuaScriptInterface::luaPlayerCreate);
	registerMetaMethod("Player", "__eq", LuaScriptInterface::luaUserdataCompare);

    //doSendPlayerExtendedOpcode(cid, opcode, buffer)
    lua_register(m_luaState, "doSendPlayerExtendedOpcode", LuaScriptInterface::luaDoSendPlayerExtendedOpcode);

	//getPlayerLevel(cid)
	lua_register(m_luaState, "getPlayerLevel", LuaScriptInterface::luaGetPlayerLevel);
	registerMethod("Player", "getLevel", LuaScriptInterface::luaGetPlayerLevel);

	//getPlayerExperience(cid)
	lua_register(m_luaState, "getPlayerExperience", LuaScriptInterface::luaGetPlayerExperience);

	//getPlayerMagLevel(cid[, ignoreModifiers = false])
	lua_register(m_luaState, "getPlayerMagLevel", LuaScriptInterface::luaGetPlayerMagLevel);

	//getPlayerSpentMana(cid)
	lua_register(m_luaState, "getPlayerSpentMana", LuaScriptInterface::luaGetPlayerSpentMana);

	//getPlayerFood(cid)
	lua_register(m_luaState, "getPlayerFood", LuaScriptInterface::luaGetPlayerFood);

	//getPlayerAccess(cid)
	lua_register(m_luaState, "getPlayerAccess", LuaScriptInterface::luaGetPlayerAccess);

	//getPlayerGhostAccess(cid)
	lua_register(m_luaState, "getPlayerGhostAccess", LuaScriptInterface::luaGetPlayerGhostAccess);

	//getPlayerSkillLevel(cid, skill[, ignoreModifiers = false])
	lua_register(m_luaState, "getPlayerSkillLevel", LuaScriptInterface::luaGetPlayerSkillLevel);

	//getPlayerSkillTries(cid, skill)
	lua_register(m_luaState, "getPlayerSkillTries", LuaScriptInterface::luaGetPlayerSkillTries);

	//getPlayerTown(cid)
	lua_register(m_luaState, "getPlayerTown", LuaScriptInterface::luaGetPlayerTown);

	//getPlayerVocation(cid)
	lua_register(m_luaState, "getPlayerVocation", LuaScriptInterface::luaGetPlayerVocation);

	//getPlayerIp(cid)
	lua_register(m_luaState, "getPlayerIp", LuaScriptInterface::luaGetPlayerIp);

	//getPlayerRequiredMana(cid, magicLevel)
	lua_register(m_luaState, "getPlayerRequiredMana", LuaScriptInterface::luaGetPlayerRequiredMana);

	//getPlayerRequiredSkillTries(cid, skillId, skillLevel)
	lua_register(m_luaState, "getPlayerRequiredSkillTries", LuaScriptInterface::luaGetPlayerRequiredSkillTries);

	//getPlayerItemCount(cid, itemid[, subType = -1])
	lua_register(m_luaState, "getPlayerItemCount", LuaScriptInterface::luaGetPlayerItemCount);

	//getPlayerMoney(cid)
	lua_register(m_luaState, "getPlayerMoney", LuaScriptInterface::luaGetPlayerMoney);

	//getPlayerSoul(cid[, ignoreModifiers = false])
	lua_register(m_luaState, "getPlayerSoul", LuaScriptInterface::luaGetPlayerSoul);

	//getPlayerFreeCap(cid)
	lua_register(m_luaState, "getPlayerFreeCap", LuaScriptInterface::luaGetPlayerFreeCap);

	//getPlayerLight(cid)
	lua_register(m_luaState, "getPlayerLight", LuaScriptInterface::luaGetPlayerLight);

	//getPlayerSlotItem(cid, slot)
	lua_register(m_luaState, "getPlayerSlotItem", LuaScriptInterface::luaGetPlayerSlotItem);

	//getPlayerWeapon(cid[, ignoreAmmo = false])
	lua_register(m_luaState, "getPlayerWeapon", LuaScriptInterface::luaGetPlayerWeapon);

	//getPlayerItemById(cid, deepSearch, itemId[, subType = -1])
	lua_register(m_luaState, "getPlayerItemById", LuaScriptInterface::luaGetPlayerItemById);

	//getPlayerDepotItems(cid, depotid)
	lua_register(m_luaState, "getPlayerDepotItems", LuaScriptInterface::luaGetPlayerDepotItems);

	//getPlayerGuildId(cid)
	lua_register(m_luaState, "getPlayerGuildId", LuaScriptInterface::luaGetPlayerGuildId);

	//getPlayerGuildName(cid)
	lua_register(m_luaState, "getPlayerGuildName", LuaScriptInterface::luaGetPlayerGuildName);

	//getPlayerGuildRankId(cid)
	lua_register(m_luaState, "getPlayerGuildRankId", LuaScriptInterface::luaGetPlayerGuildRankId);

	//getPlayerGuildRank(cid)
	lua_register(m_luaState, "getPlayerGuildRank", LuaScriptInterface::luaGetPlayerGuildRank);

	//getPlayerGuildNick(cid)
	lua_register(m_luaState, "getPlayerGuildNick", LuaScriptInterface::luaGetPlayerGuildNick);

	//getPlayerGuildLevel(cid)
	lua_register(m_luaState, "getPlayerGuildLevel", LuaScriptInterface::luaGetPlayerGuildLevel);

	//doPlayerSetGuildId(cid, id)
	lua_register(m_luaState, "doPlayerSetGuildId", LuaScriptInterface::luaDoPlayerSetGuildId);

	//doPlayerSetGuildLevel(cid, level[, rank])
	lua_register(m_luaState, "doPlayerSetGuildLevel", LuaScriptInterface::luaDoPlayerSetGuildLevel);

	//doPlayerSetGuildNick(cid, nick)
	lua_register(m_luaState, "doPlayerSetGuildNick", LuaScriptInterface::luaDoPlayerSetGuildNick);

	//getCreatureGuildEmblem(cid[, target])
	lua_register(m_luaState, "getCreatureGuildEmblem", LuaScriptInterface::luaGetCreatureGuildEmblem);

	//doCreatureSetGuildEmblem(cid, emblem)
	lua_register(m_luaState, "doCreatureSetGuildEmblem", LuaScriptInterface::luaDoCreatureSetGuildEmblem);
	
	//getPlayerGUID(cid)
	lua_register(m_luaState, "getPlayerGUID", LuaScriptInterface::luaGetPlayerGUID);

	//getPlayerNameDescription(cid)
	lua_register(m_luaState, "getPlayerNameDescription", LuaScriptInterface::luaGetPlayerNameDescription);

	//doPlayerSetNameDescription(cid, desc)
	lua_register(m_luaState, "doPlayerSetNameDescription", LuaScriptInterface::luaDoPlayerSetNameDescription);

	//getPlayerSpecialDescription(cid)
	lua_register(m_luaState, "getPlayerSpecialDescription", LuaScriptInterface::luaGetPlayerSpecialDescription);

	//doPlayerSetSpecialDescription(cid, desc)
	lua_register(m_luaState, "doPlayerSetSpecialDescription", LuaScriptInterface::luaDoPlayerSetSpecialDescription);

	//getPlayerAccountId(cid)
	lua_register(m_luaState, "getPlayerAccountId", LuaScriptInterface::luaGetPlayerAccountId);

	//getPlayerAccount(cid)
	lua_register(m_luaState, "getPlayerAccount", LuaScriptInterface::luaGetPlayerAccount);

	//getPlayerFlagValue(cid, flag)
	lua_register(m_luaState, "getPlayerFlagValue", LuaScriptInterface::luaGetPlayerFlagValue);

	//getPlayerCustomFlagValue(cid, flag)
	lua_register(m_luaState, "getPlayerCustomFlagValue", LuaScriptInterface::luaGetPlayerCustomFlagValue);

	//getPlayerPromotionLevel(cid)
	lua_register(m_luaState, "getPlayerPromotionLevel", LuaScriptInterface::luaGetPlayerPromotionLevel);

	//doPlayerSetPromotionLevel(cid, level)
	lua_register(m_luaState, "doPlayerSetPromotionLevel", LuaScriptInterface::luaDoPlayerSetPromotionLevel);

	//getPlayerGroupId(cid)
	lua_register(m_luaState, "getPlayerGroupId", LuaScriptInterface::luaGetPlayerGroupId);

	//doPlayerSetGroupId(cid, newGroupId)
	lua_register(m_luaState, "doPlayerSetGroupId", LuaScriptInterface::luaDoPlayerSetGroupId);

	//doPlayerSendOutfitWindow(cid)
	lua_register(m_luaState, "doPlayerSendOutfitWindow", LuaScriptInterface::luaDoPlayerSendOutfitWindow);

	//doPlayerLearnInstantSpell(cid, name)
	lua_register(m_luaState, "doPlayerLearnInstantSpell", LuaScriptInterface::luaDoPlayerLearnInstantSpell);

	//doPlayerUnlearnInstantSpell(cid, name)
	lua_register(m_luaState, "doPlayerUnlearnInstantSpell", LuaScriptInterface::luaDoPlayerUnlearnInstantSpell);

	//getPlayerLearnedInstantSpell(cid, name)
	lua_register(m_luaState, "getPlayerLearnedInstantSpell", LuaScriptInterface::luaGetPlayerLearnedInstantSpell);

	//getPlayerInstantSpellCount(cid)
	lua_register(m_luaState, "getPlayerInstantSpellCount", LuaScriptInterface::luaGetPlayerInstantSpellCount);

	//getPlayerInstantSpellInfo(cid, index)
	lua_register(m_luaState, "getPlayerInstantSpellInfo", LuaScriptInterface::luaGetPlayerInstantSpellInfo);

	//getInstantSpellInfo(cid, name)
	lua_register(m_luaState, "getInstantSpellInfo", LuaScriptInterface::luaGetInstantSpellInfo);

	//getCreatureStorage(uid, key)
	lua_register(m_luaState, "getCreatureStorage", LuaScriptInterface::luaGetCreatureStorage);

	//doCreatureSetStorage(uid, key, value)
	lua_register(m_luaState, "doCreatureSetStorage", LuaScriptInterface::luaDoCreatureSetStorage);

	//doPlayerFeed(cid, food)
	lua_register(m_luaState, "doPlayerFeed", LuaScriptInterface::luaDoPlayerFeed);

	//doPlayerSendCancel(cid, text)
	lua_register(m_luaState, "doPlayerSendCancel", LuaScriptInterface::luaDoPlayerSendCancel);

	//doPlayerSendDefaultCancel(cid, ReturnValue)
	lua_register(m_luaState, "doPlayerSendDefaultCancel", LuaScriptInterface::luaDoSendDefaultCancel);

	//getSearchString(fromPosition, toPosition[, fromIsCreature = false[, toIsCreature = false]])
	lua_register(m_luaState, "getSearchString", LuaScriptInterface::luaGetSearchString);

	//getClosestFreeTile(cid, targetpos[, extended = false[, ignoreHouse = true]])
	lua_register(m_luaState, "getClosestFreeTile", LuaScriptInterface::luaGetClosestFreeTile);

	//doTeleportThing(cid, newpos[, pushmove = true[, fullTeleport = true]])
	lua_register(m_luaState, "doTeleportThing", LuaScriptInterface::luaDoTeleportThing);

	//doTransformItem(uid, newId[, count/subType])
	lua_register(m_luaState, "doTransformItem", LuaScriptInterface::luaDoTransformItem);

	//doCreatureSay(uid, text[, type = SPEAK_SAY[, ghost = false[, cid = 0[, pos]]]])
	lua_register(m_luaState, "doCreatureSay", LuaScriptInterface::luaDoCreatureSay);

	//doSendCreatureSquare(cid, color[, player])
	lua_register(m_luaState, "doSendCreatureSquare", LuaScriptInterface::luaDoSendCreatureSquare);

	//doPlayerAddSkillTry(cid, skillid, n[, useMultiplier = true])
	lua_register(m_luaState, "doPlayerAddSkillTry", LuaScriptInterface::luaDoPlayerAddSkillTry);

	//doCreatureAddHealth(cid, health[, hitEffect[, hitColor[, force]]])
	lua_register(m_luaState, "doCreatureAddHealth", LuaScriptInterface::luaDoCreatureAddHealth);

	//doCreatureAddMana(cid, mana)
	lua_register(m_luaState, "doCreatureAddMana", LuaScriptInterface::luaDoCreatureAddMana);

	//setCreatureMaxHealth(cid, health)
	lua_register(m_luaState, "setCreatureMaxHealth", LuaScriptInterface::luaSetCreatureMaxHealth);

	//setCreatureMaxMana(cid, mana)
	lua_register(m_luaState, "setCreatureMaxMana", LuaScriptInterface::luaSetCreatureMaxMana);

	//doPlayerSetMaxCapacity(cid, cap)
	lua_register(m_luaState, "doPlayerSetMaxCapacity", LuaScriptInterface::luaDoPlayerSetMaxCapacity);

	//doPlayerAddSpentMana(cid, amount[, useMultiplier = true])
	lua_register(m_luaState, "doPlayerAddSpentMana", LuaScriptInterface::luaDoPlayerAddSpentMana);

	//doPlayerAddSoul(cid, amount)
	lua_register(m_luaState, "doPlayerAddSoul", LuaScriptInterface::luaDoPlayerAddSoul);

    //doPlayerSetExtraAttackSpeed(cid, speed)
    lua_register(m_luaState, "doPlayerSetExtraAttackSpeed", LuaScriptInterface::luaDoPlayerSetExtraAttackSpeed);

	//doPlayerAddItem(cid, itemid[, count/subtype = 1[, canDropOnMap = true[, slot = 0]]])
	//doPlayerAddItem(cid, itemid[, count = 1[, canDropOnMap = true[, subtype = 1[, slot = 0]]]])
	//Returns uid of the created item
	lua_register(m_luaState, "doPlayerAddItem", LuaScriptInterface::luaDoPlayerAddItem);

	//doPlayerAddItemEx(cid, uid[, canDropOnMap = false[, slot = 0]])
	lua_register(m_luaState, "doPlayerAddItemEx", LuaScriptInterface::luaDoPlayerAddItemEx);

	//doPlayerSendTextMessage(cid, MessageClasses, message)
	lua_register(m_luaState, "doPlayerSendTextMessage", LuaScriptInterface::luaDoPlayerSendTextMessage);

	//doPlayerSendChannelMessage(cid, author, message, SpeakClasses, channel)
	lua_register(m_luaState, "doPlayerSendChannelMessage", LuaScriptInterface::luaDoPlayerSendChannelMessage);

	//doPlayerSendToChannel(cid, targetId, SpeakClasses, message, channel[, time])
	lua_register(m_luaState, "doPlayerSendToChannel", LuaScriptInterface::luaDoPlayerSendToChannel);

	//doPlayerOpenChannel(cid, channelId)
	lua_register(m_luaState, "doPlayerOpenChannel", LuaScriptInterface::luaDoPlayerOpenChannel);

	//doPlayerAddMoney(cid, money)
	lua_register(m_luaState, "doPlayerAddMoney", LuaScriptInterface::luaDoPlayerAddMoney);

	//doPlayerRemoveMoney(cid, money)
	lua_register(m_luaState, "doPlayerRemoveMoney", LuaScriptInterface::luaDoPlayerRemoveMoney);

	//doPlayerTransferMoneyTo(cid, target, money)
	lua_register(m_luaState, "doPlayerTransferMoneyTo", LuaScriptInterface::luaDoPlayerTransferMoneyTo);

	//	Item 
	registerClass("Item", "", LuaScriptInterface::luaItemCreate);
	registerMetaMethod("Item", "__eq", LuaScriptInterface::luaUserdataCompare);

	//getItemWeight(uid[, precise = true])
	lua_register(m_luaState, "getItemWeight", LuaScriptInterface::luaGetItemWeight);
	registerMethod("Item", "getWeight", LuaScriptInterface::luaGetItemWeight);

	//doShowTextDialog(cid, itemid, text)
	lua_register(m_luaState, "doShowTextDialog", LuaScriptInterface::luaDoShowTextDialog);

	//doDecayItem(uid)
	lua_register(m_luaState, "doDecayItem", LuaScriptInterface::luaDoDecayItem);

	//getItemIdByName(name[, displayError = true])
	lua_register(m_luaState, "getItemIdByName", LuaScriptInterface::luaGetItemIdByName);

	//getItemInfo(itemid)
	lua_register(m_luaState, "getItemInfo", LuaScriptInterface::luaGetItemInfo);

	//getItemAttribute(uid, key)
	lua_register(m_luaState, "getItemAttribute", LuaScriptInterface::luaGetItemAttribute);

	//doItemSetAttribute(uid, key, value)
	lua_register(m_luaState, "doItemSetAttribute", LuaScriptInterface::luaDoItemSetAttribute);

	//doItemEraseAttribute(uid, key)
	lua_register(m_luaState, "doItemEraseAttribute", LuaScriptInterface::luaDoItemEraseAttribute);

	//getItemParent(uid)
	lua_register(m_luaState, "getItemParent", LuaScriptInterface::luaGetItemParent);

	//hasItemProperty(uid, prop)
	lua_register(m_luaState, "hasItemProperty", LuaScriptInterface::luaHasItemProperty);

	//doItemRaidUnref(uid)
	lua_register(m_luaState, "doItemRaidUnref", LuaScriptInterface::luaDoItemRaidUnref);

	//doCreateItem(itemid[, type/count], pos)
	//Returns uid of the created item, only works on tiles.
	lua_register(m_luaState, "doCreateItem", LuaScriptInterface::luaDoCreateItem);

	//doCreateItemEx(itemid[, count/subType = -1])
	lua_register(m_luaState, "doCreateItemEx", LuaScriptInterface::luaDoCreateItemEx);

	//doTileAddItemEx(pos, uid)
	lua_register(m_luaState, "doTileAddItemEx", LuaScriptInterface::luaDoTileAddItemEx);

	//doAddContainerItemEx(uid, virtuid)
	lua_register(m_luaState, "doAddContainerItemEx", LuaScriptInterface::luaDoAddContainerItemEx);

	//doRelocate(pos, posTo[, creatures = true[, unmovable = true]])
	//Moves all movable objects from pos to posTo
	lua_register(m_luaState, "doRelocate", LuaScriptInterface::luaDoRelocate);

	//doCleanTile(pos[, forceMapLoaded = false])
	lua_register(m_luaState, "doCleanTile", LuaScriptInterface::luaDoCleanTile);

	//doCreateTeleport(itemid, topos, createpos)
	lua_register(m_luaState, "doCreateTeleport", LuaScriptInterface::luaDoCreateTeleport);

	//	Monster

	registerClass("Monster", "Creature", LuaScriptInterface::luaMonsterCreate);
	registerMetaMethod("Monster", "__eq", LuaScriptInterface::luaUserdataCompare);

	//doCreateMonster(name, pos[, extend = false[, force = false[, displayError = true]]])
	lua_register(m_luaState, "doCreateMonster", LuaScriptInterface::luaDoCreateMonster);

	//doCreateNpc(name, pos[, displayError = true])
	lua_register(m_luaState, "doCreateNpc", LuaScriptInterface::luaDoCreateNpc);

	//doSummonMonster(cid, name)
	lua_register(m_luaState, "doSummonMonster", LuaScriptInterface::luaDoSummonMonster);

	//doConvinceCreature(cid, target)
	lua_register(m_luaState, "doConvinceCreature", LuaScriptInterface::luaDoConvinceCreature);

	//getMonsterTargetList(cid)
	lua_register(m_luaState, "getMonsterTargetList", LuaScriptInterface::luaGetMonsterTargetList);

	//getMonsterFriendList(cid)
	lua_register(m_luaState, "getMonsterFriendList", LuaScriptInterface::luaGetMonsterFriendList);

	//doMonsterSetTarget(cid, target)
	lua_register(m_luaState, "doMonsterSetTarget", LuaScriptInterface::luaDoMonsterSetTarget);

	//doMonsterChangeTarget(cid)
	lua_register(m_luaState, "doMonsterChangeTarget", LuaScriptInterface::luaDoMonsterChangeTarget);

	//getMonsterInfo(name)
	lua_register(m_luaState, "getMonsterInfo", LuaScriptInterface::luaGetMonsterInfo);

	// Condition

	//doAddCondition(cid, condition)
	lua_register(m_luaState, "doAddCondition", LuaScriptInterface::luaDoAddCondition);

	//doRemoveCondition(cid, type[, subId])
	lua_register(m_luaState, "doRemoveCondition", LuaScriptInterface::luaDoRemoveCondition);

	//doRemoveConditions(cid[, onlyPersistent])
	lua_register(m_luaState, "doRemoveConditions", LuaScriptInterface::luaDoRemoveConditions);

	//doRemoveCreature(cid[, forceLogout = true])
	lua_register(m_luaState, "doRemoveCreature", LuaScriptInterface::luaDoRemoveCreature);

	//doMoveCreature(cid, direction[, flag = FLAG_NOLIMIT])
	lua_register(m_luaState, "doMoveCreature", LuaScriptInterface::luaDoMoveCreature);

	//doSteerCreature(cid, position)
	lua_register(m_luaState, "doSteerCreature", LuaScriptInterface::luaDoSteerCreature);

	//doPlayerSetPzLocked(cid, locked)
	lua_register(m_luaState, "doPlayerSetPzLocked", LuaScriptInterface::luaDoPlayerSetPzLocked);

	//doPlayerSetTown(cid, townid)
	lua_register(m_luaState, "doPlayerSetTown", LuaScriptInterface::luaDoPlayerSetTown);

	//doPlayerSetVocation(cid,voc)
	lua_register(m_luaState, "doPlayerSetVocation", LuaScriptInterface::luaDoPlayerSetVocation);

	//doPlayerRemoveItem(cid, itemid[, count[, subType = -1]])
	lua_register(m_luaState, "doPlayerRemoveItem", LuaScriptInterface::luaDoPlayerRemoveItem);

	//doPlayerAddExperience(cid, amount)
	lua_register(m_luaState, "doPlayerAddExperience", LuaScriptInterface::luaDoPlayerAddExperience);

	//doPlayerAddOutfit(cid, looktype, addon)
	lua_register(m_luaState, "doPlayerAddOutfit", LuaScriptInterface::luaDoPlayerAddOutfit);

	//doPlayerRemoveOutfit(cid, looktype[, addon = 0])
	lua_register(m_luaState, "doPlayerRemoveOutfit", LuaScriptInterface::luaDoPlayerRemoveOutfit);

	//doPlayerAddOutfitId(cid, outfitId, addon)
	lua_register(m_luaState, "doPlayerAddOutfitId", LuaScriptInterface::luaDoPlayerAddOutfitId);

	//doPlayerRemoveOutfitId(cid, outfitId[, addon = 0])
	lua_register(m_luaState, "doPlayerRemoveOutfitId", LuaScriptInterface::luaDoPlayerRemoveOutfitId);

	//CASTnPlayerWearOutfit(cid, looktype[, addon = 0])
	lua_register(m_luaState, "canPlayerWearOutfit", LuaScriptInterface::luaCanPlayerWearOutfit);

	//CASTnPlayerWearOutfitId(cid, outfitId[, addon = 0])
	lua_register(m_luaState, "canPlayerWearOutfitId", LuaScriptInterface::luaCanPlayerWearOutfitId);

	//getCreatureCondition(cid, condition[, subId = 0])
	lua_register(m_luaState, "getCreatureCondition", LuaScriptInterface::luaGetCreatureCondition);

	//doCreatureSetDropLoot(cid, doDrop)
	lua_register(m_luaState, "doCreatureSetDropLoot", LuaScriptInterface::luaDoCreatureSetDropLoot);

	//getPlayerLossPercent(cid, lossType)
	lua_register(m_luaState, "getPlayerLossPercent", LuaScriptInterface::luaGetPlayerLossPercent);

	//doPlayerSetLossPercent(cid, lossType, newPercent)
	lua_register(m_luaState, "doPlayerSetLossPercent", LuaScriptInterface::luaDoPlayerSetLossPercent);

	//doPlayerSetLossSkill(cid, doLose)
	lua_register(m_luaState, "doPlayerSetLossSkill", LuaScriptInterface::luaDoPlayerSetLossSkill);

	//getPlayerLossSkill(cid)
	lua_register(m_luaState, "getPlayerLossSkill", LuaScriptInterface::luaGetPlayerLossSkill);

	//doPlayerSwitchSaving(cid)
	lua_register(m_luaState, "doPlayerSwitchSaving", LuaScriptInterface::luaDoPlayerSwitchSaving);

	//doPlayerSave(cid[, shallow = false])
	lua_register(m_luaState, "doPlayerSave", LuaScriptInterface::luaDoPlayerSave);

	//isPlayerPzLocked(cid)
	lua_register(m_luaState, "isPlayerPzLocked", LuaScriptInterface::luaIsPlayerPzLocked);

	//isPlayerSaving(cid)
	lua_register(m_luaState, "isPlayerSaving", LuaScriptInterface::luaIsPlayerSaving);

	//isCreature(cid)
	lua_register(m_luaState, "isCreature", LuaScriptInterface::luaIsCreature);

	//isContainer(uid)
	lua_register(m_luaState, "isContainer", LuaScriptInterface::luaIsContainer);

	//isMovable(uid)
	lua_register(m_luaState, "isMovable", LuaScriptInterface::luaIsMovable);

	//getCreatureByName(name)
	lua_register(m_luaState, "getCreatureByName", LuaScriptInterface::luaGetCreatureByName);

	//getPlayerByGUID(guid)
	lua_register(m_luaState, "getPlayerByGUID", LuaScriptInterface::luaGetPlayerByGUID);

	//getPlayerByNameWildcard(name~[, ret = false])
	lua_register(m_luaState, "getPlayerByNameWildcard", LuaScriptInterface::luaGetPlayerByNameWildcard);

	//getPlayerGUIDByName(name[, multiworld = false])
	lua_register(m_luaState, "getPlayerGUIDByName", LuaScriptInterface::luaGetPlayerGUIDByName);

	//getPlayerNameByGUID(guid[, multiworld = false[, displayError = true]])
	lua_register(m_luaState, "getPlayerNameByGUID", LuaScriptInterface::luaGetPlayerNameByGUID);

	//registerCreatureEvent(uid, eventName)
	lua_register(m_luaState, "registerCreatureEvent", LuaScriptInterface::luaRegisterCreatureEvent);

	//unregisterCreatureEvent(uid, eventName)
	lua_register(m_luaState, "unregisterCreatureEvent", LuaScriptInterface::luaUnregisterCreatureEvent);

	//getContainerSize(uid)
	lua_register(m_luaState, "getContainerSize", LuaScriptInterface::luaGetContainerSize);

	//getContainerCap(uid)
	lua_register(m_luaState, "getContainerCap", LuaScriptInterface::luaGetContainerCap);

	//getContainerItems(uid)
	lua_register(m_luaState, "getContainerItems", LuaScriptInterface::luaGetContainerItems);

	//getContainerItem(uid, slot)
	lua_register(m_luaState, "getContainerItem", LuaScriptInterface::luaGetContainerItem);

	//doAddContainerItem(uid, itemid[, count/subType = 1])
	lua_register(m_luaState, "doAddContainerItem", LuaScriptInterface::luaDoAddContainerItem);

	//createCombatArea({area}[, {extArea}])
	lua_register(m_luaState, "createCombatArea", LuaScriptInterface::luaCreateCombatArea);

	//createConditionObject(type[, ticks[, buff[, subId]]])
	lua_register(m_luaState, "createConditionObject", LuaScriptInterface::luaCreateConditionObject);

	//setCombatArea(combat, area)
	lua_register(m_luaState, "setCombatArea", LuaScriptInterface::luaSetCombatArea);

	//setCombatCondition(combat, condition)
	lua_register(m_luaState, "setCombatCondition", LuaScriptInterface::luaSetCombatCondition);

	//setCombatParam(combat, key, value)
	lua_register(m_luaState, "setCombatParam", LuaScriptInterface::luaSetCombatParam);

	//setConditionParam(condition, key, value)
	lua_register(m_luaState, "setConditionParam", LuaScriptInterface::luaSetConditionParam);

	//addDamageCondition(condition, rounds, time, value)
	lua_register(m_luaState, "addDamageCondition", LuaScriptInterface::luaAddDamageCondition);

	//addOutfitCondition(condition, outfit)
	lua_register(m_luaState, "addOutfitCondition", LuaScriptInterface::luaAddOutfitCondition);

	//setCombatCallBack(combat, key, function_name)
	lua_register(m_luaState, "setCombatCallback", LuaScriptInterface::luaSetCombatCallBack);

	//setCombatFormula(combat, type, mina, minb, maxa, maxb[, minl, maxl[, minm, maxm[, minc[, maxc]]]])
	lua_register(m_luaState, "setCombatFormula", LuaScriptInterface::luaSetCombatFormula);

	//setConditionFormula(combat, mina, minb, maxa, maxb)
	lua_register(m_luaState, "setConditionFormula", LuaScriptInterface::luaSetConditionFormula);

	//doCombat(cid, combat, param)
	lua_register(m_luaState, "doCombat", LuaScriptInterface::luaDoCombat);

	//createCombatObject()
	lua_register(m_luaState, "createCombatObject", LuaScriptInterface::luaCreateCombatObject);

	//doCombatAreaHealth(cid, type, pos, area, min, max, effect)
	lua_register(m_luaState, "doCombatAreaHealth", LuaScriptInterface::luaDoCombatAreaHealth);

	//doTargetCombatHealth(cid, target, type, min, max, effect)
	lua_register(m_luaState, "doTargetCombatHealth", LuaScriptInterface::luaDoTargetCombatHealth);

	//doCombatAreaMana(cid, pos, area, min, max, effect)
	lua_register(m_luaState, "doCombatAreaMana", LuaScriptInterface::luaDoCombatAreaMana);

	//doTargetCombatMana(cid, target, min, max, effect)
	lua_register(m_luaState, "doTargetCombatMana", LuaScriptInterface::luaDoTargetCombatMana);

	//doCombatAreaCondition(cid, pos, area, condition, effect)
	lua_register(m_luaState, "doCombatAreaCondition", LuaScriptInterface::luaDoCombatAreaCondition);

	//doTargetCombatCondition(cid, target, condition, effect)
	lua_register(m_luaState, "doTargetCombatCondition", LuaScriptInterface::luaDoTargetCombatCondition);

	//doCombatAreaDispel(cid, pos, area, type, effect)
	lua_register(m_luaState, "doCombatAreaDispel", LuaScriptInterface::luaDoCombatAreaDispel);

	//doTargetCombatDispel(cid, target, type, effect)
	lua_register(m_luaState, "doTargetCombatDispel", LuaScriptInterface::luaDoTargetCombatDispel);

	//doChallengeCreature(cid, target)
	lua_register(m_luaState, "doChallengeCreature", LuaScriptInterface::luaDoChallengeCreature);

//	---------------------------------------------------------------------------------------------

	//hasPlayerClient(cid)
	lua_register(m_luaState, "hasPlayerClient", LuaScriptInterface::luaHasPlayerClient);

	//getPlayerSex(cid[, full = false])
	lua_register(m_luaState, "getPlayerSex", LuaScriptInterface::luaGetPlayerSex);

	//doPlayerSetSex(cid, newSex)
	lua_register(m_luaState, "doPlayerSetSex", LuaScriptInterface::luaDoPlayerSetSex);

	//doChangeSpeed(cid, delta)
	lua_register(m_luaState, "doChangeSpeed", LuaScriptInterface::luaDoChangeSpeed);

	//doCreatureChangeOutfit(cid, outfit)
	lua_register(m_luaState, "doCreatureChangeOutfit", LuaScriptInterface::luaDoCreatureChangeOutfit);

	//doSetMonsterOutfit(cid, name[, time = -1])
	lua_register(m_luaState, "doSetMonsterOutfit", LuaScriptInterface::luaSetMonsterOutfit);
    
	//doSetItemOutfit(cid, item[, time = -1])
	lua_register(m_luaState, "doSetItemOutfit", LuaScriptInterface::luaSetItemOutfit);

	//doSetCreatureOutfit(cid, outfit[, time = -1])
	lua_register(m_luaState, "doSetCreatureOutfit", LuaScriptInterface::luaSetCreatureOutfit);

	//getCreatureOutfit(cid)
	lua_register(m_luaState, "getCreatureOutfit", LuaScriptInterface::luaGetCreatureOutfit);

	//getCreatureLastPosition(cid)
	lua_register(m_luaState, "getCreatureLastPosition", LuaScriptInterface::luaGetCreatureLastPosition);

	//getCreatureName(cid)
	lua_register(m_luaState, "getCreatureName", LuaScriptInterface::luaGetCreatureName);

	//getCreaturePathTo(cid, pos, maxSearchDist)
	lua_register(m_luaState, "getCreaturePathTo", LuaScriptInterface::luaGetCreaturePathTo);

	//getCreatureSpeed(cid)
	lua_register(m_luaState, "getCreatureSpeed", LuaScriptInterface::luaGetCreatureSpeed);

	//getCreatureBaseSpeed(cid)
	lua_register(m_luaState, "getCreatureBaseSpeed", LuaScriptInterface::luaGetCreatureBaseSpeed);

	//getCreatureTarget(cid)
	lua_register(m_luaState, "getCreatureTarget", LuaScriptInterface::luaGetCreatureTarget);

	//getPlayersByAccountId(accId)
	lua_register(m_luaState, "getPlayersByAccountId", LuaScriptInterface::luaGetPlayersByAccountId);

	//getAccountIdByName(name)
	lua_register(m_luaState, "getAccountIdByName", LuaScriptInterface::luaGetAccountIdByName);

	//getAccountByName(name)
	lua_register(m_luaState, "getAccountByName", LuaScriptInterface::luaGetAccountByName);

	//getAccountIdByAccount(accName)
	lua_register(m_luaState, "getAccountIdByAccount", LuaScriptInterface::luaGetAccountIdByAccount);

	//getAccountByAccountId(accId)
	lua_register(m_luaState, "getAccountByAccountId", LuaScriptInterface::luaGetAccountByAccountId);

	//getIpByName(name)
	lua_register(m_luaState, "getIpByName", LuaScriptInterface::luaGetIpByName);

	//getPlayersByIp(ip[, mask = 0xFFFFFFFF])
	lua_register(m_luaState, "getPlayersByIp", LuaScriptInterface::luaGetPlayersByIp);

	//doPlayerPopupFYI(cid, message)
	lua_register(m_luaState, "doPlayerPopupFYI", LuaScriptInterface::luaDoPlayerPopupFYI);

	//doPlayerSendTutorial(cid, id)
	lua_register(m_luaState, "doPlayerSendTutorial", LuaScriptInterface::luaDoPlayerSendTutorial);

	//doPlayerSendMailByName(name, item[, town[, actor]])
	lua_register(m_luaState, "doPlayerSendMailByName", LuaScriptInterface::luaDoPlayerSendMailByName);

	//doPlayerAddMapMark(cid, pos, type[, description])
	lua_register(m_luaState, "doPlayerAddMapMark", LuaScriptInterface::luaDoPlayerAddMapMark);

	//doPlayerAddPremiumDays(cid, days)
	lua_register(m_luaState, "doPlayerAddPremiumDays", LuaScriptInterface::luaDoPlayerAddPremiumDays);

	//getPlayerPremiumDays(cid)
	lua_register(m_luaState, "getPlayerPremiumDays", LuaScriptInterface::luaGetPlayerPremiumDays);

	//doCreatureSetLookDirection(cid, dir)
	lua_register(m_luaState, "doCreatureSetLookDirection", LuaScriptInterface::luaDoCreatureSetLookDir);

	//getCreatureSkullType(cid[, target])
	lua_register(m_luaState, "getCreatureSkullType", LuaScriptInterface::luaGetCreatureSkullType);

	//doCreatureSetSkullType(cid, skull)
	lua_register(m_luaState, "doCreatureSetSkullType", LuaScriptInterface::luaDoCreatureSetSkullType);

	//getPlayerSkullEnd(cid)
	lua_register(m_luaState, "getPlayerSkullEnd", LuaScriptInterface::luaGetPlayerSkullEnd);

	//doPlayerSetSkullEnd(cid, time, type)
	lua_register(m_luaState, "doPlayerSetSkullEnd", LuaScriptInterface::luaDoPlayerSetSkullEnd);

	//getPlayerBlessing(cid, blessing)
	lua_register(m_luaState, "getPlayerBlessing", LuaScriptInterface::luaGetPlayerBlessing);

	//doPlayerAddBlessing(cid, blessing)
	lua_register(m_luaState, "doPlayerAddBlessing", LuaScriptInterface::luaDoPlayerAddBlessing);

	//getPlayerStamina(cid)
	lua_register(m_luaState, "getPlayerStamina", LuaScriptInterface::luaGetPlayerStamina);

	//doPlayerSetStamina(cid, minutes)
	lua_register(m_luaState, "doPlayerSetStamina", LuaScriptInterface::luaDoPlayerSetStamina);

	//getPlayerBalance(cid)
	lua_register(m_luaState, "getPlayerBalance", LuaScriptInterface::luaGetPlayerBalance);

	//doPlayerSetBalance(cid, balance)
	lua_register(m_luaState, "doPlayerSetBalance", LuaScriptInterface::luaDoPlayerSetBalance);

	//getCreatureNoMove(cid)
	lua_register(m_luaState, "getCreatureNoMove", LuaScriptInterface::luaGetCreatureNoMove);

	//doCreatureSetNoMove(cid, block)
	lua_register(m_luaState, "doCreatureSetNoMove", LuaScriptInterface::luaDoCreatureSetNoMove);

	//getPlayerIdleTime(cid)
	lua_register(m_luaState, "getPlayerIdleTime", LuaScriptInterface::luaGetPlayerIdleTime);

	//doPlayerSetIdleTime(cid, amount)
	lua_register(m_luaState, "doPlayerSetIdleTime", LuaScriptInterface::luaDoPlayerSetIdleTime);

	//getPlayerLastLoad(cid)
	lua_register(m_luaState, "getPlayerLastLoad", LuaScriptInterface::luaGetPlayerLastLoad);

	//getPlayerLastLogin(cid)
	lua_register(m_luaState, "getPlayerLastLogin", LuaScriptInterface::luaGetPlayerLastLogin);

	//getPlayerAccountManager(cid)
	lua_register(m_luaState, "getPlayerAccountManager", LuaScriptInterface::luaGetPlayerAccountManager);

	//getPlayerTradeState(cid)
	lua_register(m_luaState, "getPlayerTradeState", LuaScriptInterface::luaGetPlayerTradeState);

	//getPlayerModes(cid)
	lua_register(m_luaState, "getPlayerModes", LuaScriptInterface::luaGetPlayerModes);

	//getPlayerRates(cid)
	lua_register(m_luaState, "getPlayerRates", LuaScriptInterface::luaGetPlayerRates);

	//doPlayerSetRate(cid, type, value)
	lua_register(m_luaState, "doPlayerSetRate", LuaScriptInterface::luaDoPlayerSetRate);

	//getPlayerPartner(cid)
	lua_register(m_luaState, "getPlayerPartner", LuaScriptInterface::luaGetPlayerPartner);

	//doPlayerSetPartner(cid, guid)
	lua_register(m_luaState, "doPlayerSetPartner", LuaScriptInterface::luaDoPlayerSetPartner);

	//doPlayerFollowCreature(cid, target)
	lua_register(m_luaState, "doPlayerFollowCreature", LuaScriptInterface::luaDoPlayerFollowCreature);

	//getCreatureMaster(cid)
	lua_register(m_luaState, "getCreatureMaster", LuaScriptInterface::luaGetCreatureMaster);

	//getCreatureSummons(cid)
	lua_register(m_luaState, "getCreatureSummons", LuaScriptInterface::luaGetCreatureSummons);

	//	Party

	//getCreaturePartyShield(cid[, target])
	lua_register(m_luaState, "getCreaturePartyShield", LuaScriptInterface::luaGetCreaturePartyShield);

	//doCreatureSetPartyShield(cid, shield)
	lua_register(m_luaState, "doCreatureSetPartyShield", LuaScriptInterface::luaDoCreatureSetPartyShield);

	//getPlayerParty(cid)
	lua_register(m_luaState, "getPlayerParty", LuaScriptInterface::luaGetPlayerParty);

	//doPlayerJoinParty(cid, lid)
	lua_register(m_luaState, "doPlayerJoinParty", LuaScriptInterface::luaDoPlayerJoinParty);

	//doPlayerLeaveParty(cid[, forced = false])
	lua_register(m_luaState, "doPlayerLeaveParty", LuaScriptInterface::luaDoPlayerLeaveParty);

	//getPartyMembers(lid)
	lua_register(m_luaState, "getPartyMembers", LuaScriptInterface::luaGetPartyMembers);

	//	Position

	registerClass("Position", "", LuaScriptInterface::luaPositionCreate);
	registerMetaMethod("Position", "__add", LuaScriptInterface::luaPositionAdd);
	registerMetaMethod("Position", "__sub", LuaScriptInterface::luaPositionSub);
	registerMetaMethod("Position", "__eq", LuaScriptInterface::luaPositionCompare);

	//doSendMagicEffect(pos, type[, player])
	lua_register(m_luaState, "doSendMagicEffect", LuaScriptInterface::luaDoSendMagicEffect);
	registerMethod("Position", "doSendMagicEffect", LuaScriptInterface::luaDoSendMagicEffect);

	//doSendDistanceShoot(fromPos, toPos, type[, player])
	lua_register(m_luaState, "doSendDistanceShoot", LuaScriptInterface::luaDoSendDistanceShoot);

	//doSendAnimatedText(pos, text, color[, player])
	lua_register(m_luaState, "doSendAnimatedText", LuaScriptInterface::luaDoSendAnimatedText);

	//isSightClear(fromPos, toPos, floorCheck)
	lua_register(m_luaState, "isSightClear", LuaScriptInterface::luaIsSightClear);

	//	Tile

	registerClass("Tile", "", LuaScriptInterface::luaTileCreate);
	registerMetaMethod("Tile", "__eq", LuaScriptInterface::luaUserdataCompare);

	//getTileInfo(pos)
	lua_register(m_luaState, "getTileInfo", LuaScriptInterface::luaGetTileInfo);
	registerMethod("Tile", "getTileInfo", LuaScriptInterface::luaGetTileInfo);

	//getThingFromPos(pos[, displayError = true])
	lua_register(m_luaState, "getThingFromPos", LuaScriptInterface::luaGetThingFromPos);

	//getThing(uid)
	lua_register(m_luaState, "getThing", LuaScriptInterface::luaGetThing);

	//doTileQueryAdd(uid, pos[, flags[, displayError = true]])
	lua_register(m_luaState, "doTileQueryAdd", LuaScriptInterface::luaDoTileQueryAdd);

	//getThingPosition(uid)
	lua_register(m_luaState, "getThingPosition", LuaScriptInterface::luaGetThingPosition);

	//getTileItemById(pos, itemId[, subType = -1])
	lua_register(m_luaState, "getTileItemById", LuaScriptInterface::luaGetTileItemById);

	//getTileItemByType(pos, type)
	lua_register(m_luaState, "getTileItemByType", LuaScriptInterface::luaGetTileItemByType);

	//getTileThingByPos(pos)
	lua_register(m_luaState, "getTileThingByPos", LuaScriptInterface::luaGetTileThingByPos);

	//getTopCreature(pos)
	lua_register(m_luaState, "getTopCreature", LuaScriptInterface::luaGetTopCreature);

	//doRemoveItem(uid[, count = -1])
	lua_register(m_luaState, "doRemoveItem", LuaScriptInterface::luaDoRemoveItem);

	//	Variant

	registerClass("Variant", "", LuaScriptInterface::luaVariantCreate);

	//variantToNumber(var)
	lua_register(m_luaState, "variantToNumber", LuaScriptInterface::luaVariantToNumber);
	registerMethod("Variant", "getNumber", LuaScriptInterface::luaVariantToNumber);

	//variantToString(var)
	lua_register(m_luaState, "variantToString", LuaScriptInterface::luaVariantToString);

	//variantToPosition(var)
	lua_register(m_luaState, "variantToPosition", LuaScriptInterface::luaVariantToPosition);

	//numberToVariant(number)
	lua_register(m_luaState, "numberToVariant", LuaScriptInterface::luaNumberToVariant);

	//stringToVariant(string)
	lua_register(m_luaState, "stringToVariant", LuaScriptInterface::luaStringToVariant);

	//positionToVariant(pos)
	lua_register(m_luaState, "positionToVariant", LuaScriptInterface::luaPositionToVariant);

	//targetPositionToVariant(pos)
	lua_register(m_luaState, "targetPositionToVariant", LuaScriptInterface::luaTargetPositionToVariant);

#ifdef __WAR_SYSTEM__

	//doGuildAddEnemy(guild, enemy, war, type)
	lua_register(m_luaState, "doGuildAddEnemy", LuaScriptInterface::luaDoGuildAddEnemy);

	//doGuildRemoveEnemy(guild, enemy)
	lua_register(m_luaState, "doGuildRemoveEnemy", LuaScriptInterface::luaDoGuildRemoveEnemy);
#endif

	//getGuildId(guildName)
	lua_register(m_luaState, "getGuildId", LuaScriptInterface::luaGetGuildId);

	//getGuildMotd(guildId)
	lua_register(m_luaState, "getGuildMotd", LuaScriptInterface::luaGetGuildMotd);

	// Spells
	registerClass("Spell", "", LuaScriptInterface::luaSpellCreate);
	registerMetaMethod("Spell", "__eq", LuaScriptInterface::luaUserdataCompare);

	registerMethod("Spell", "onCastSpell", LuaScriptInterface::luaSpellOnCastSpell);
	registerMethod("Spell", "register", LuaScriptInterface::luaSpellRegister);
	registerMethod("Spell", "name", LuaScriptInterface::luaSpellName);
	registerMethod("Spell", "id", LuaScriptInterface::luaSpellId);
	registerMethod("Spell", "group", LuaScriptInterface::luaSpellGroup);
	registerMethod("Spell", "cooldown", LuaScriptInterface::luaSpellCooldown);
	registerMethod("Spell", "groupCooldown", LuaScriptInterface::luaSpellGroupCooldown);
	registerMethod("Spell", "level", LuaScriptInterface::luaSpellLevel);
	registerMethod("Spell", "magicLevel", LuaScriptInterface::luaSpellMagicLevel);
	registerMethod("Spell", "mana", LuaScriptInterface::luaSpellMana);
	registerMethod("Spell", "manaPercent", LuaScriptInterface::luaSpellManaPercent);
	registerMethod("Spell", "soul", LuaScriptInterface::luaSpellSoul);
	registerMethod("Spell", "range", LuaScriptInterface::luaSpellRange);
	registerMethod("Spell", "isPremium", LuaScriptInterface::luaSpellPremium);
	registerMethod("Spell", "isEnabled", LuaScriptInterface::luaSpellEnabled);
	registerMethod("Spell", "needTarget", LuaScriptInterface::luaSpellNeedTarget);
	registerMethod("Spell", "needWeapon", LuaScriptInterface::luaSpellNeedWeapon);
	registerMethod("Spell", "needLearn", LuaScriptInterface::luaSpellNeedLearn);
	registerMethod("Spell", "isSelfTarget", LuaScriptInterface::luaSpellSelfTarget);
	registerMethod("Spell", "isBlocking", LuaScriptInterface::luaSpellBlocking);
	registerMethod("Spell", "isAggressive", LuaScriptInterface::luaSpellAggressive);
	registerMethod("Spell", "vocation", LuaScriptInterface::luaSpellVocation);

	// only for InstantSpell
	registerMethod("Spell", "words", LuaScriptInterface::luaSpellWords);
	registerMethod("Spell", "needDirection", LuaScriptInterface::luaSpellNeedDirection);
	registerMethod("Spell", "hasParams", LuaScriptInterface::luaSpellHasParams);
	registerMethod("Spell", "hasPlayerNameParam", LuaScriptInterface::luaSpellHasPlayerNameParam);
	registerMethod("Spell", "needCasterTargetOrDirection", LuaScriptInterface::luaSpellNeedCasterTargetOrDirection);
	registerMethod("Spell", "isBlockingWalls", LuaScriptInterface::luaSpellIsBlockingWalls);

	// only for RuneSpells
	registerMethod("Spell", "runeId", LuaScriptInterface::luaSpellRuneId);
	registerMethod("Spell", "charges", LuaScriptInterface::luaSpellCharges);
	registerMethod("Spell", "allowFarUse", LuaScriptInterface::luaSpellAllowFarUse);
	registerMethod("Spell", "blockWalls", LuaScriptInterface::luaSpellBlockWalls);
	registerMethod("Spell", "checkFloor", LuaScriptInterface::luaSpellCheckFloor);

	// Action
	registerClass("Action", "", LuaScriptInterface::luaCreateAction);
	registerMethod("Action", "onUse", LuaScriptInterface::luaActionOnUse);
	registerMethod("Action", "register", LuaScriptInterface::luaActionRegister);
	registerMethod("Action", "id", LuaScriptInterface::luaActionItemId);
	registerMethod("Action", "aid", LuaScriptInterface::luaActionActionId);
	registerMethod("Action", "uid", LuaScriptInterface::luaActionUniqueId);
	registerMethod("Action", "allowFarUse", LuaScriptInterface::luaActionAllowFarUse);
	registerMethod("Action", "blockWalls", LuaScriptInterface::luaActionBlockWalls);
	registerMethod("Action", "checkFloor", LuaScriptInterface::luaActionCheckFloor);

	// TalkAction
	registerClass("TalkAction", "", LuaScriptInterface::luaCreateTalkaction);
	registerMethod("TalkAction", "onSay", LuaScriptInterface::luaTalkactionOnSay);
	registerMethod("TalkAction", "register", LuaScriptInterface::luaTalkactionRegister);
	registerMethod("TalkAction", "separator", LuaScriptInterface::luaTalkactionSeparator);

	// CreatureEvent
	registerClass("CreatureEvent", "", LuaScriptInterface::luaCreateCreatureEvent);
	registerMethod("CreatureEvent", "type", LuaScriptInterface::luaCreatureEventType);
	registerMethod("CreatureEvent", "register", LuaScriptInterface::luaCreatureEventRegister);
	registerMethod("CreatureEvent", "onLogin", LuaScriptInterface::luaCreatureEventOnCallback);
	registerMethod("CreatureEvent", "onLogout", LuaScriptInterface::luaCreatureEventOnCallback);
	registerMethod("CreatureEvent", "onThink", LuaScriptInterface::luaCreatureEventOnCallback);
	registerMethod("CreatureEvent", "onPrepareDeath", LuaScriptInterface::luaCreatureEventOnCallback);
	registerMethod("CreatureEvent", "onDeath", LuaScriptInterface::luaCreatureEventOnCallback);
	registerMethod("CreatureEvent", "onKill", LuaScriptInterface::luaCreatureEventOnCallback);
	registerMethod("CreatureEvent", "onAdvance", LuaScriptInterface::luaCreatureEventOnCallback);
	registerMethod("CreatureEvent", "onModalWindow", LuaScriptInterface::luaCreatureEventOnCallback);
	registerMethod("CreatureEvent", "onTextEdit", LuaScriptInterface::luaCreatureEventOnCallback);
	registerMethod("CreatureEvent", "onHealthChange", LuaScriptInterface::luaCreatureEventOnCallback);
	registerMethod("CreatureEvent", "onManaChange", LuaScriptInterface::luaCreatureEventOnCallback);
	registerMethod("CreatureEvent", "onExtendedOpcode", LuaScriptInterface::luaCreatureEventOnCallback);

	// MoveEvent
	registerClass("MoveEvent", "", LuaScriptInterface::luaCreateMoveEvent);
	registerMethod("MoveEvent", "type", LuaScriptInterface::luaMoveEventType);
	registerMethod("MoveEvent", "register", LuaScriptInterface::luaMoveEventRegister);
	registerMethod("MoveEvent", "level", LuaScriptInterface::luaMoveEventLevel);
	registerMethod("MoveEvent", "magicLevel", LuaScriptInterface::luaMoveEventMagLevel);
	registerMethod("MoveEvent", "slot", LuaScriptInterface::luaMoveEventSlot);
	registerMethod("MoveEvent", "id", LuaScriptInterface::luaMoveEventItemId);
	registerMethod("MoveEvent", "aid", LuaScriptInterface::luaMoveEventActionId);
	registerMethod("MoveEvent", "uid", LuaScriptInterface::luaMoveEventUniqueId);
	registerMethod("MoveEvent", "premium", LuaScriptInterface::luaMoveEventPremium);
	registerMethod("MoveEvent", "vocation", LuaScriptInterface::luaMoveEventVocation);
	registerMethod("MoveEvent", "onEquip", LuaScriptInterface::luaMoveEventOnCallback);
	registerMethod("MoveEvent", "onDeEquip", LuaScriptInterface::luaMoveEventOnCallback);
	registerMethod("MoveEvent", "onStepIn", LuaScriptInterface::luaMoveEventOnCallback);
	registerMethod("MoveEvent", "onStepOut", LuaScriptInterface::luaMoveEventOnCallback);
	registerMethod("MoveEvent", "onAddItem", LuaScriptInterface::luaMoveEventOnCallback);
	registerMethod("MoveEvent", "onRemoveItem", LuaScriptInterface::luaMoveEventOnCallback);

	// MonsterType
	registerClass("MonsterType", "", LuaScriptInterface::luaMonsterTypeCreate);
	registerMetaMethod("MonsterType", "__eq", LuaScriptInterface::luaUserdataCompare);

	registerMethod("MonsterType", "isAttackable", LuaScriptInterface::luaMonsterTypeIsAttackable);
	registerMethod("MonsterType", "isConvinceable", LuaScriptInterface::luaMonsterTypeIsConvinceable);
	registerMethod("MonsterType", "isSummonable", LuaScriptInterface::luaMonsterTypeIsSummonable);
	registerMethod("MonsterType", "isIllusionable", LuaScriptInterface::luaMonsterTypeIsIllusionable);
	registerMethod("MonsterType", "isHostile", LuaScriptInterface::luaMonsterTypeIsHostile);
	registerMethod("MonsterType", "isPushable", LuaScriptInterface::luaMonsterTypeIsPushable);
	registerMethod("MonsterType", "isHealthHidden", LuaScriptInterface::luaMonsterTypeIsHealthHidden);

	registerMethod("MonsterType", "canPushItems", LuaScriptInterface::luaMonsterTypeCanPushItems);
	registerMethod("MonsterType", "canPushCreatures", LuaScriptInterface::luaMonsterTypeCanPushCreatures);

	registerMethod("MonsterType", "name", LuaScriptInterface::luaMonsterTypeName);

	registerMethod("MonsterType", "nameDescription", LuaScriptInterface::luaMonsterTypeNameDescription);

	registerMethod("MonsterType", "health", LuaScriptInterface::luaMonsterTypeHealth);
	registerMethod("MonsterType", "maxHealth", LuaScriptInterface::luaMonsterTypeMaxHealth);
	registerMethod("MonsterType", "runHealth", LuaScriptInterface::luaMonsterTypeRunHealth);
	registerMethod("MonsterType", "experience", LuaScriptInterface::luaMonsterTypeExperience);

	registerMethod("MonsterType", "combatImmunities", LuaScriptInterface::luaMonsterTypeCombatImmunities);
	registerMethod("MonsterType", "conditionImmunities", LuaScriptInterface::luaMonsterTypeConditionImmunities);

	registerMethod("MonsterType", "getAttackList", LuaScriptInterface::luaMonsterTypeGetAttackList);
	registerMethod("MonsterType", "addAttack", LuaScriptInterface::luaMonsterTypeAddAttack);

	registerMethod("MonsterType", "getDefenseList", LuaScriptInterface::luaMonsterTypeGetDefenseList);
	registerMethod("MonsterType", "addDefense", LuaScriptInterface::luaMonsterTypeAddDefense);

	registerMethod("MonsterType", "getElementList", LuaScriptInterface::luaMonsterTypeGetElementList);
	registerMethod("MonsterType", "addElement", LuaScriptInterface::luaMonsterTypeAddElement);

	registerMethod("MonsterType", "getVoices", LuaScriptInterface::luaMonsterTypeGetVoices);
	registerMethod("MonsterType", "addVoice", LuaScriptInterface::luaMonsterTypeAddVoice);

	registerMethod("MonsterType", "getLoot", LuaScriptInterface::luaMonsterTypeGetLoot);
	registerMethod("MonsterType", "addLoot", LuaScriptInterface::luaMonsterTypeAddLoot);

	registerMethod("MonsterType", "getCreatureEvents", LuaScriptInterface::luaMonsterTypeGetCreatureEvents);
	registerMethod("MonsterType", "registerEvent", LuaScriptInterface::luaMonsterTypeRegisterEvent);

	registerMethod("MonsterType", "setScriptFile", LuaScriptInterface::luaMonsterTypeSetScriptFile);

	registerMethod("MonsterType", "getSummonList", LuaScriptInterface::luaMonsterTypeGetSummonList);
	registerMethod("MonsterType", "addSummon", LuaScriptInterface::luaMonsterTypeAddSummon);

	registerMethod("MonsterType", "maxSummons", LuaScriptInterface::luaMonsterTypeMaxSummons);

	registerMethod("MonsterType", "armor", LuaScriptInterface::luaMonsterTypeArmor);
	registerMethod("MonsterType", "defense", LuaScriptInterface::luaMonsterTypeDefense);
	registerMethod("MonsterType", "outfit", LuaScriptInterface::luaMonsterTypeOutfit);
	registerMethod("MonsterType", "race", LuaScriptInterface::luaMonsterTypeRace);
	registerMethod("MonsterType", "corpseId", LuaScriptInterface::luaMonsterTypeCorpseId);
	registerMethod("MonsterType", "manaCost", LuaScriptInterface::luaMonsterTypeManaCost);
	registerMethod("MonsterType", "baseSpeed", LuaScriptInterface::luaMonsterTypeBaseSpeed);
	registerMethod("MonsterType", "light", LuaScriptInterface::luaMonsterTypeLight);

	registerMethod("MonsterType", "staticAttackChance", LuaScriptInterface::luaMonsterTypeStaticAttackChance);
	registerMethod("MonsterType", "targetDistance", LuaScriptInterface::luaMonsterTypeTargetDistance);
	registerMethod("MonsterType", "yellChance", LuaScriptInterface::luaMonsterTypeYellChance);
	registerMethod("MonsterType", "yellSpeedTicks", LuaScriptInterface::luaMonsterTypeYellSpeedTicks);
	registerMethod("MonsterType", "changeTargetChance", LuaScriptInterface::luaMonsterTypeChangeTargetChance);
	registerMethod("MonsterType", "changeTargetSpeed", LuaScriptInterface::luaMonsterTypeChangeTargetSpeed);

	// Loot
	registerClass("Loot", "", LuaScriptInterface::luaCreateLoot);
	registerMetaMethod("Loot", "__gc", LuaScriptInterface::luaDeleteLoot);
	registerMethod("Loot", "delete", LuaScriptInterface::luaDeleteLoot);

	registerMethod("Loot", "setId", LuaScriptInterface::luaLootSetId);
	registerMethod("Loot", "setMaxCount", LuaScriptInterface::luaLootSetMaxCount);
	registerMethod("Loot", "setSubType", LuaScriptInterface::luaLootSetSubType);
	registerMethod("Loot", "setChance", LuaScriptInterface::luaLootSetChance);
	registerMethod("Loot", "setActionId", LuaScriptInterface::luaLootSetActionId);
	registerMethod("Loot", "setDescription", LuaScriptInterface::luaLootSetDescription);
	registerMethod("Loot", "addChildLoot", LuaScriptInterface::luaLootAddChildLoot);

	// MonsterSpell
	registerClass("MonsterSpell", "", LuaScriptInterface::luaCreateMonsterSpell);
	registerMetaMethod("MonsterSpell", "__gc", LuaScriptInterface::luaDeleteMonsterSpell);
	registerMethod("MonsterSpell", "delete", LuaScriptInterface::luaDeleteMonsterSpell);

	registerMethod("MonsterSpell", "setType", LuaScriptInterface::luaMonsterSpellSetType);
	registerMethod("MonsterSpell", "setScriptName", LuaScriptInterface::luaMonsterSpellSetScriptName);
	registerMethod("MonsterSpell", "setChance", LuaScriptInterface::luaMonsterSpellSetChance);
	registerMethod("MonsterSpell", "setInterval", LuaScriptInterface::luaMonsterSpellSetInterval);
	registerMethod("MonsterSpell", "setRange", LuaScriptInterface::luaMonsterSpellSetRange);
	registerMethod("MonsterSpell", "setCombatValue", LuaScriptInterface::luaMonsterSpellSetCombatValue);
	registerMethod("MonsterSpell", "setCombatType", LuaScriptInterface::luaMonsterSpellSetCombatType);
	registerMethod("MonsterSpell", "setAttackValue", LuaScriptInterface::luaMonsterSpellSetAttackValue);
	registerMethod("MonsterSpell", "setNeedTarget", LuaScriptInterface::luaMonsterSpellSetNeedTarget);
	registerMethod("MonsterSpell", "setCombatLength", LuaScriptInterface::luaMonsterSpellSetCombatLength);
	registerMethod("MonsterSpell", "setCombatSpread", LuaScriptInterface::luaMonsterSpellSetCombatSpread);
	registerMethod("MonsterSpell", "setCombatRadius", LuaScriptInterface::luaMonsterSpellSetCombatRadius);
	registerMethod("MonsterSpell", "setConditionType", LuaScriptInterface::luaMonsterSpellSetConditionType);
	registerMethod("MonsterSpell", "setConditionDamage", LuaScriptInterface::luaMonsterSpellSetConditionDamage);
	registerMethod("MonsterSpell", "setConditionSpeedChange", LuaScriptInterface::luaMonsterSpellSetConditionSpeedChange);
	registerMethod("MonsterSpell", "setConditionDuration", LuaScriptInterface::luaMonsterSpellSetConditionDuration);
	registerMethod("MonsterSpell", "setConditionTickInterval", LuaScriptInterface::luaMonsterSpellSetConditionTickInterval);
	registerMethod("MonsterSpell", "setCombatShootEffect", LuaScriptInterface::luaMonsterSpellSetCombatShootEffect);
	registerMethod("MonsterSpell", "setCombatEffect", LuaScriptInterface::luaMonsterSpellSetCombatEffect);
	
	// GlobalEvent
	registerClass("GlobalEvent", "", LuaScriptInterface::luaCreateGlobalEvent);
	registerMethod("GlobalEvent", "type", LuaScriptInterface::luaGlobalEventType);
	registerMethod("GlobalEvent", "register", LuaScriptInterface::luaGlobalEventRegister);
	registerMethod("GlobalEvent", "time", LuaScriptInterface::luaGlobalEventTime);
	registerMethod("GlobalEvent", "interval", LuaScriptInterface::luaGlobalEventInterval);
	registerMethod("GlobalEvent", "onThink", LuaScriptInterface::luaGlobalEventOnCallback);
	registerMethod("GlobalEvent", "onTime", LuaScriptInterface::luaGlobalEventOnCallback);
	registerMethod("GlobalEvent", "onStartup", LuaScriptInterface::luaGlobalEventOnCallback);
	registerMethod("GlobalEvent", "onShutdown", LuaScriptInterface::luaGlobalEventOnCallback);
	registerMethod("GlobalEvent", "onRecord", LuaScriptInterface::luaGlobalEventOnCallback);

	// Weapon
	registerClass("Weapon", "", LuaScriptInterface::luaCreateWeapon);
	registerMethod("Weapon", "action", LuaScriptInterface::luaWeaponAction);
	registerMethod("Weapon", "register", LuaScriptInterface::luaWeaponRegister);
	registerMethod("Weapon", "id", LuaScriptInterface::luaWeaponId);
	registerMethod("Weapon", "level", LuaScriptInterface::luaWeaponLevel);
	registerMethod("Weapon", "magicLevel", LuaScriptInterface::luaWeaponMagicLevel);
	registerMethod("Weapon", "mana", LuaScriptInterface::luaWeaponMana);
	registerMethod("Weapon", "manaPercent", LuaScriptInterface::luaWeaponManaPercent);
	registerMethod("Weapon", "health", LuaScriptInterface::luaWeaponHealth);
	registerMethod("Weapon", "healthPercent", LuaScriptInterface::luaWeaponHealthPercent);
	registerMethod("Weapon", "soul", LuaScriptInterface::luaWeaponSoul);
	registerMethod("Weapon", "breakChance", LuaScriptInterface::luaWeaponBreakChance);
	registerMethod("Weapon", "premium", LuaScriptInterface::luaWeaponPremium);
	registerMethod("Weapon", "wieldUnproperly", LuaScriptInterface::luaWeaponUnproperly);
	registerMethod("Weapon", "vocation", LuaScriptInterface::luaWeaponVocation);
	registerMethod("Weapon", "onUseWeapon", LuaScriptInterface::luaWeaponOnUseWeapon);
	registerMethod("Weapon", "element", LuaScriptInterface::luaWeaponElement);
	registerMethod("Weapon", "attack", LuaScriptInterface::luaWeaponAttack);
	registerMethod("Weapon", "defense", LuaScriptInterface::luaWeaponDefense);
	registerMethod("Weapon", "range", LuaScriptInterface::luaWeaponRange);
	registerMethod("Weapon", "charges", LuaScriptInterface::luaWeaponCharges);
	registerMethod("Weapon", "duration", LuaScriptInterface::luaWeaponDuration);
	registerMethod("Weapon", "decayTo", LuaScriptInterface::luaWeaponDecayTo);
	registerMethod("Weapon", "transformEquipTo", LuaScriptInterface::luaWeaponTransformEquipTo);
	registerMethod("Weapon", "transformDeEquipTo", LuaScriptInterface::luaWeaponTransformDeEquipTo);
	registerMethod("Weapon", "slotType", LuaScriptInterface::luaWeaponSlotType);
	registerMethod("Weapon", "hitChance", LuaScriptInterface::luaWeaponHitChance);
	registerMethod("Weapon", "extraElement", LuaScriptInterface::luaWeaponExtraElement);

	// exclusively for distance weapons
	registerMethod("Weapon", "ammoType", LuaScriptInterface::luaWeaponAmmoType);
	registerMethod("Weapon", "maxHitChance", LuaScriptInterface::luaWeaponMaxHitChance);

	// exclusively for wands
	registerMethod("Weapon", "damage", LuaScriptInterface::luaWeaponWandDamage);

	// exclusively for wands & distance weapons
	registerMethod("Weapon", "shootType", LuaScriptInterface::luaWeaponShootType);

	//loadmodlib(lib)
	lua_register(m_luaState, "loadmodlib", LuaScriptInterface::luaL_loadmodlib);

	//domodlib(lib)
	lua_register(m_luaState, "domodlib", LuaScriptInterface::luaL_domodlib);

	//dodirectory(dir[, recursively = false])
	lua_register(m_luaState, "dodirectory", LuaScriptInterface::luaL_dodirectory);

	//errors(var)
	lua_register(m_luaState, "errors", LuaScriptInterface::luaL_errors);

	//db table
	luaL_register(m_luaState, "db", LuaScriptInterface::luaDatabaseTable);

	//result table
	luaL_register(m_luaState, "result", LuaScriptInterface::luaResultTable);

#ifndef LUAJIT_VERSION
	//bit table
	luaL_register(m_luaState, "bit", LuaScriptInterface::luaBitTable);
#endif

	//std table
	luaL_register(m_luaState, "std", LuaScriptInterface::luaStdTable);

	// os
	registerMethod("os", "mtime", LuaScriptInterface::luaSystemTime);

	// table
	registerMethod("table", "create", LuaScriptInterface::luaTableCreate);	
}

const luaL_Reg LuaScriptInterface::luaDatabaseTable[] =
{
	//db.query(query)
	{"query", LuaScriptInterface::luaDatabaseExecute},

	//db.storeQuery(query)
	{"storeQuery", LuaScriptInterface::luaDatabaseStoreQuery},

	//db.escapeString(str)
	{"escapeString", LuaScriptInterface::luaDatabaseEscapeString},

	//db.escapeBlob(s, length)
	{"escapeBlob", LuaScriptInterface::luaDatabaseEscapeBlob},

	//db.lastInsertId()
	{"lastInsertId", LuaScriptInterface::luaDatabaseLastInsertId},

	//db.stringComparer()
	{"stringComparer", LuaScriptInterface::luaDatabaseStringComparer},

	//db.updateLimiter()
	{"updateLimiter", LuaScriptInterface::luaDatabaseUpdateLimiter},

	{NULL, NULL}
};

const luaL_Reg LuaScriptInterface::luaResultTable[] =
{
	//result.getDataInt(resId, s)
	{"getDataInt", LuaScriptInterface::luaResultGetDataInt},

	//result.getDataLong(resId, s)
	{"getDataLong", LuaScriptInterface::luaResultGetDataLong},

	//result.getDataString(resId, s)
	{"getDataString", LuaScriptInterface::luaResultGetDataString},

	//result.getDataStream(resId, s, length)
	{"getDataStream", LuaScriptInterface::luaResultGetDataStream},

	//result.next(resId)
	{"next", LuaScriptInterface::luaResultNext},

	//result.free(resId)
	{"free", LuaScriptInterface::luaResultFree},

	{NULL, NULL}
};

const luaL_Reg LuaScriptInterface::luaStdTable[] =
{
	{"cout", LuaScriptInterface::luaStdCout},
	{"clog", LuaScriptInterface::luaStdClog},
	{"cerr", LuaScriptInterface::luaStdCerr},

	{"md5", LuaScriptInterface::luaStdMD5},
	{"sha1", LuaScriptInterface::luaStdSHA1},
	{"sha256", LuaScriptInterface::luaStdSHA256},
	{"sha512", LuaScriptInterface::luaStdSHA512},
	{"vahash", LuaScriptInterface::luaStdVAHash},

	{NULL, NULL}
};

#ifndef LUAJIT_VERSION
const luaL_Reg LuaScriptInterface::luaBitTable[] =
{
	//{"cast", LuaScriptInterface::luaBitCast},
	{"bnot", LuaScriptInterface::luaBitNot},
	{"band", LuaScriptInterface::luaBitAnd},
	{"bor", LuaScriptInterface::luaBitOr},
	{"bxor", LuaScriptInterface::luaBitXor},
	{"lshift", LuaScriptInterface::luaBitLeftShift},
	{"rshift", LuaScriptInterface::luaBitRightShift},
	//{"arshift", LuaScriptInterface::luaBitArithmeticalRightShift},

	//{"ucast", LuaScriptInterface::luaBitUCast},
	{"ubnot", LuaScriptInterface::luaBitUNot},
	{"uband", LuaScriptInterface::luaBitUAnd},
	{"ubor", LuaScriptInterface::luaBitUOr},
	{"ubxor", LuaScriptInterface::luaBitUXor},
	{"ulshift", LuaScriptInterface::luaBitULeftShift},
	{"urshift", LuaScriptInterface::luaBitURightShift},
	//{"uarshift", LuaScriptInterface::luaBitUArithmeticalRightShift},

	{NULL, NULL}
};
#endif

void LuaScriptInterface::registerClass(const std::string& className, const std::string& baseClass, lua_CFunction newFunction/* = nullptr*/)
{
	// className = {}
	lua_newtable(m_luaState);
	lua_pushvalue(m_luaState, -1);
	lua_setglobal(m_luaState, className.c_str());
	int methods = lua_gettop(m_luaState);

	// methodsTable = {}
	lua_newtable(m_luaState);
	int methodsTable = lua_gettop(m_luaState);

	if (newFunction) {
		// className.__call = newFunction
		lua_pushcfunction(m_luaState, newFunction);
		lua_setfield(m_luaState, methodsTable, "__call");
	}

	uint32_t parents = 0;
	if (!baseClass.empty()) {
		lua_getglobal(m_luaState, baseClass.c_str());
		lua_rawgeti(m_luaState, -1, 'p');
		parents = getNumber<uint32_t>(m_luaState, -1) + 1;
		lua_pop(m_luaState, 1);
		lua_setfield(m_luaState, methodsTable, "__index");
	}

	// setmetatable(className, methodsTable)
	lua_setmetatable(m_luaState, methods);

	// className.metatable = {}
	luaL_newmetatable(m_luaState, className.c_str());
	int metatable = lua_gettop(m_luaState);

	// className.metatable.__metatable = className
	lua_pushvalue(m_luaState, methods);
	lua_setfield(m_luaState, metatable, "__metatable");

	// className.metatable.__index = className
	lua_pushvalue(m_luaState, methods);
	lua_setfield(m_luaState, metatable, "__index");

	// className.metatable['h'] = hash
	lua_pushnumber(m_luaState, std::hash<std::string>()(className));
	lua_rawseti(m_luaState, metatable, 'h');

	// className.metatable['p'] = parents
	lua_pushnumber(m_luaState, parents);
	lua_rawseti(m_luaState, metatable, 'p');

	// className.metatable['t'] = type
	if (className == "Item") {
		lua_pushnumber(m_luaState, LuaData_Item);
	} else if (className == "Container") {
		lua_pushnumber(m_luaState, LuaData_Container);
	} else if (className == "Teleport") {
		lua_pushnumber(m_luaState, LuaData_Teleport);
	} else if (className == "Player") {
		lua_pushnumber(m_luaState, LuaData_Player);
	} else if (className == "Monster") {
		lua_pushnumber(m_luaState, LuaData_Monster);
	} else if (className == "Npc") {
		lua_pushnumber(m_luaState, LuaData_Npc);
	} else if (className == "Tile") {
		lua_pushnumber(m_luaState, LuaData_Tile);
	} else {
		lua_pushnumber(m_luaState, LuaData_Unknown);
	}
	lua_rawseti(m_luaState, metatable, 't');

	// pop className, className.metatable
	lua_pop(m_luaState, 2);
}

void LuaScriptInterface::registerTable(const std::string& tableName)
{
	// _G[tableName] = {}
	lua_newtable(m_luaState);
	lua_setglobal(m_luaState, tableName.c_str());
}

void LuaScriptInterface::registerMethod(const std::string& globalName, const std::string& methodName, lua_CFunction func)
{
	// globalName.methodName = func
	lua_getglobal(m_luaState, globalName.c_str());
	lua_pushcfunction(m_luaState, func);
	lua_setfield(m_luaState, -2, methodName.c_str());

	// pop globalName
	lua_pop(m_luaState, 1);
}

void LuaScriptInterface::registerMetaMethod(const std::string& className, const std::string& methodName, lua_CFunction func)
{
	// className.metatable.methodName = func
	luaL_getmetatable(m_luaState, className.c_str());
	lua_pushcfunction(m_luaState, func);
	lua_setfield(m_luaState, -2, methodName.c_str());

	// pop className.metatable
	lua_pop(m_luaState, 1);
}

void LuaScriptInterface::registerGlobalMethod(const std::string& functionName, lua_CFunction func)
{
	// _G[functionName] = func
	lua_pushcfunction(m_luaState, func);
	lua_setglobal(m_luaState, functionName.c_str());
}

void LuaScriptInterface::registerVariable(const std::string& tableName, const std::string& name, lua_Number value)
{
	// tableName.name = value
	lua_getglobal(m_luaState, tableName.c_str());
	setField(m_luaState, name.c_str(), value);

	// pop tableName
	lua_pop(m_luaState, 1);
}

void LuaScriptInterface::registerGlobalVariable(const std::string& name, lua_Number value)
{
	// _G[name] = value
	lua_pushnumber(m_luaState, value);
	lua_setglobal(m_luaState, name.c_str());
}

void LuaScriptInterface::registerGlobalBoolean(const std::string& name, bool value)
{
	// _G[name] = value
	pushBoolean(m_luaState, value);
	lua_setglobal(m_luaState, name.c_str());
}

int32_t LuaScriptInterface::internalGetPlayerInfo(lua_State* L, PlayerInfo_t info)
{
	LuaEnvironment* env = getScriptEnv();
	const Player* player = env->getPlayerByUID(popNumber(L));
	if(!player)
	{
		std::stringstream s;
		s << getError(LUA_ERROR_PLAYER_NOT_FOUND) << " when requesting player info #" << info;
		errorEx(s.str());

		pushBoolean(L, false);
		return 1;
	}

	int64_t value = 0;
	Position pos;
	switch(info)
	{
		case PlayerInfoNameDescription:
			lua_pushstring(L, player->getNameDescription().c_str());
			return 1;
		case PlayerInfoSpecialDescription:
			lua_pushstring(L, player->getSpecialDescription().c_str());
			return 1;
		case PlayerInfoAccess:
			value = player->getAccess();
			break;
		case PlayerInfoGhostAccess:
			value = player->getGhostAccess();
			break;
		case PlayerInfoLevel:
			value = player->getLevel();
			break;
		case PlayerInfoExperience:
			value = player->getExperience();
			break;
		case PlayerInfoManaSpent:
			value = player->getSpentMana();
			break;
		case PlayerInfoTown:
			value = player->getTown();
			break;
		case PlayerInfoPromotionLevel:
			value = player->getPromotionLevel();
			break;
		case PlayerInfoGUID:
			value = player->getGUID();
			break;
		case PlayerInfoAccountId:
			value = player->getAccount();
			break;
		case PlayerInfoAccount:
			lua_pushstring(L, player->getAccountName().c_str());
			return 1;
		case PlayerInfoPremiumDays:
			value = player->getPremiumDays();
			break;
		case PlayerInfoFood:
		{
			if(Condition* condition = player->getCondition(CONDITION_REGENERATION, CONDITIONID_DEFAULT))
				value = condition->getTicks() / 1000;

			break;
		}
		case PlayerInfoVocation:
			value = player->getVocationId();
			break;
		case PlayerInfoMoney:
			value = g_game.getMoney(player);
			break;
		case PlayerInfoFreeCap:
			value = (int64_t)player->getFreeCapacity();
			break;
		case PlayerInfoGuildId:
			value = player->getGuildId();
			break;
		case PlayerInfoGuildName:
			lua_pushstring(L, player->getGuildName().c_str());
			return 1;
		case PlayerInfoGuildRankId:
			value = player->getRankId();
			break;
		case PlayerInfoGuildRank:
			lua_pushstring(L, player->getRankName().c_str());
			return 1;
		case PlayerInfoGuildLevel:
			value = player->getGuildLevel();
			break;
		case PlayerInfoGuildNick:
			lua_pushstring(L, player->getGuildNick().c_str());
			return 1;
		case PlayerInfoGroupId:
			value = player->getGroupId();
			break;
		case PlayerInfoBalance:
			if(g_config.getBool(ConfigManager::BANK_SYSTEM))
				lua_pushnumber(L, player->balance);
			else
				lua_pushnumber(L, 0);

			return 1;
		case PlayerInfoStamina:
			value = player->getStaminaMinutes();
			break;
		case PlayerInfoLossSkill:
			pushBoolean(L, player->getLossSkill());
			return 1;
		case PlayerInfoMarriage:
			value = player->marriage;
			break;
		case PlayerInfoPzLock:
			pushBoolean(L, player->isPzLocked());
			return 1;
		case PlayerInfoSaving:
			pushBoolean(L, player->isSaving());
			return 1;
		case PlayerInfoIp:
			value = player->getIP();
			break;
		case PlayerInfoSkullEnd:
			value = player->getSkullEnd();
			break;
		case PlayerInfoOutfitWindow:
			player->sendOutfitWindow();
			pushBoolean(L, true);
			return 1;
		case PlayerInfoIdleTime:
			value = player->getIdleTime();
			break;
		case PlayerInfoClient:
			pushBoolean(L, player->hasClient());
			return 1;
		case PlayerInfoLastLoad:
			value = player->getLastLoad();
			break;
		case PlayerInfoLastLogin:
			value = player->getLastLogin();
			break;
		case PlayerInfoAccountManager:
			value = player->accountManager;
			break;
		case PlayerInfoTradeState:
			value = player->tradeState;
			break;
		default:
			errorEx("Unknown player info #" + std::to_string(info));
			value = 0;
			break;
	}

	lua_pushnumber(L, value);
	return 1;
}

//getPlayer[Info](uid)
int32_t LuaScriptInterface::luaGetPlayerNameDescription(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoNameDescription);
}

int32_t LuaScriptInterface::luaGetPlayerSpecialDescription(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoSpecialDescription);
}

int32_t LuaScriptInterface::luaGetPlayerFood(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoFood);
}

int32_t LuaScriptInterface::luaGetPlayerAccess(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoAccess);
}

int32_t LuaScriptInterface::luaGetPlayerGhostAccess(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoGhostAccess);
}

int32_t LuaScriptInterface::luaGetPlayerLevel(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoLevel);
}

int32_t LuaScriptInterface::luaGetPlayerExperience(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoExperience);
}

int32_t LuaScriptInterface::luaGetPlayerSpentMana(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoManaSpent);
}

int32_t LuaScriptInterface::luaGetPlayerVocation(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoVocation);
}

int32_t LuaScriptInterface::luaGetPlayerMoney(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoMoney);
}

int32_t LuaScriptInterface::luaGetPlayerFreeCap(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoFreeCap);
}

int32_t LuaScriptInterface::luaGetPlayerGuildId(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoGuildId);
}

int32_t LuaScriptInterface::luaGetPlayerGuildName(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoGuildName);
}

int32_t LuaScriptInterface::luaGetPlayerGuildRankId(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoGuildRankId);
}

int32_t LuaScriptInterface::luaGetPlayerGuildRank(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoGuildRank);
}

int32_t LuaScriptInterface::luaGetPlayerGuildLevel(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoGuildLevel);
}

int32_t LuaScriptInterface::luaGetPlayerGuildNick(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoGuildNick);
}

int32_t LuaScriptInterface::luaGetPlayerTown(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoTown);
}

int32_t LuaScriptInterface::luaGetPlayerPromotionLevel(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoPromotionLevel);
}

int32_t LuaScriptInterface::luaGetPlayerGroupId(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoGroupId);
}

int32_t LuaScriptInterface::luaGetPlayerGUID(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoGUID);
}

int32_t LuaScriptInterface::luaGetPlayerAccountId(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoAccountId);
}

int32_t LuaScriptInterface::luaGetPlayerAccount(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoAccount);
}

int32_t LuaScriptInterface::luaGetPlayerPremiumDays(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoPremiumDays);
}

int32_t LuaScriptInterface::luaGetPlayerBalance(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoBalance);
}

int32_t LuaScriptInterface::luaGetPlayerStamina(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoStamina);
}

int32_t LuaScriptInterface::luaGetPlayerLossSkill(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoLossSkill);
}

int32_t LuaScriptInterface::luaGetPlayerPartner(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoMarriage);
}

int32_t LuaScriptInterface::luaIsPlayerPzLocked(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoPzLock);
}

int32_t LuaScriptInterface::luaIsPlayerSaving(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoSaving);
}

int32_t LuaScriptInterface::luaGetPlayerIp(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoIp);
}

int32_t LuaScriptInterface::luaGetPlayerSkullEnd(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoSkullEnd);
}

int32_t LuaScriptInterface::luaDoPlayerSendOutfitWindow(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoOutfitWindow);
}

int32_t LuaScriptInterface::luaGetPlayerIdleTime(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoIdleTime);
}

int32_t LuaScriptInterface::luaHasPlayerClient(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoClient);
}

int32_t LuaScriptInterface::luaGetPlayerLastLoad(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoLastLoad);
}

int32_t LuaScriptInterface::luaGetPlayerLastLogin(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoLastLogin);
}

int32_t LuaScriptInterface::luaGetPlayerAccountManager(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoAccountManager);
}

int32_t LuaScriptInterface::luaGetPlayerTradeState(lua_State* L)
{
	return internalGetPlayerInfo(L, PlayerInfoTradeState);
}
//

int32_t LuaScriptInterface::luaGetPlayerSex(lua_State* L)
{
	//getPlayerSex(cid[, full = false])
	bool full = false;
	if(lua_gettop(L) > 1)
		full = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	Player* player = env->getPlayerByUID((uint32_t)popNumber(L));
	if(!player)
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}
	else
		lua_pushnumber(L, player->getSex(full));

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSetNameDescription(lua_State* L)
{
	//doPlayerSetNameDescription(cid, description)
	std::string description = popString(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		player->nameDescription += description;
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSetSpecialDescription(lua_State* L)
{
	//doPlayerSetSpecialDescription(cid, description)
	std::string description = popString(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		player->setSpecialDescription(description);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerMagLevel(lua_State* L)
{
	//getPlayerMagLevel(cid[, ignoreModifiers = false])
	bool ignoreModifiers = false;
	if(lua_gettop(L) > 1)
		ignoreModifiers = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(const Player* player = env->getPlayerByUID(popNumber(L)))
		lua_pushnumber(L, ignoreModifiers ? player->magLevel : player->getMagicLevel());
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerRequiredMana(lua_State* L)
{
	//getPlayerRequiredMana(cid, magicLevel)
	uint32_t magLevel = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
		lua_pushnumber(L, player->vocation->getReqMana(magLevel));
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerRequiredSkillTries(lua_State* L)
{
	//getPlayerRequiredSkillTries(cid, skill, level)
	int32_t level = popNumber(L), skill = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
		lua_pushnumber(L, player->vocation->getReqSkillTries(skill, level));
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerFlagValue(lua_State* L)
{
	//getPlayerFlagValue(cid, flag)
	uint32_t index = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		if(index < PlayerFlag_LastFlag)
			pushBoolean(L, player->hasFlag((PlayerFlags)index));
		else
		{
			std::stringstream ss;
			ss << index;
			errorEx("No valid flag index - " + ss.str());
			pushBoolean(L, false);
		}
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerCustomFlagValue(lua_State* L)
{
	//getPlayerCustomFlagValue(cid, flag)
	uint32_t index = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		if(index < PlayerCustomFlag_LastFlag)
			pushBoolean(L, player->hasCustomFlag((PlayerCustomFlags)index));
		else
		{
			std::stringstream ss;
			ss << index;
			errorEx("No valid flag index - " + ss.str());
			pushBoolean(L, false);
		}
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerLearnInstantSpell(lua_State* L)
{
	//doPlayerLearnInstantSpell(cid, name)
	std::string spellName = popString(L);

	LuaEnvironment* env = getScriptEnv();
	Player* player = env->getPlayerByUID(popNumber(L));
	if(!player)
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	InstantSpell* spell = g_spells->getInstantSpellByName(spellName);
	if(!spell)
	{
		pushBoolean(L, false);
		return 1;
	}

	player->learnInstantSpell(spell->getName());
	pushBoolean(L, true);
	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerUnlearnInstantSpell(lua_State* L)
{
	//doPlayerUnlearnInstantSpell(cid, name)
	std::string spellName = popString(L);

	LuaEnvironment* env = getScriptEnv();
	Player* player = env->getPlayerByUID(popNumber(L));
	if(!player)
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	InstantSpell* spell = g_spells->getInstantSpellByName(spellName);
	if(!spell)
	{
		pushBoolean(L, false);
		return 1;
	}

	player->unlearnInstantSpell(spell->getName());
	pushBoolean(L, true);
	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerLearnedInstantSpell(lua_State* L)
{
	//getPlayerLearnedInstantSpell(cid, name)
	std::string spellName = popString(L);

	LuaEnvironment* env = getScriptEnv();
	Player* player = env->getPlayerByUID(popNumber(L));
	if(!player)
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	InstantSpell* spell = g_spells->getInstantSpellByName(spellName);
	if(!spell)
	{
		pushBoolean(L, false);
		return 1;
	}

	pushBoolean(L, player->hasLearnedInstantSpell(spellName));
	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerInstantSpellCount(lua_State* L)
{
	//getPlayerInstantSpellCount(cid)
	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
		lua_pushnumber(L, g_spells->getInstantSpellCount(player));
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerInstantSpellInfo(lua_State* L)
{
	//getPlayerInstantSpellInfo(cid, index)
	uint32_t index = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	Player* player = env->getPlayerByUID(popNumber(L));
	if(!player)
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	InstantSpell* spell = g_spells->getInstantSpellByIndex(player, index);
	if(!spell)
	{
		errorEx(getError(LUA_ERROR_SPELL_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	lua_newtable(L);
	setField(L, "name", spell->getName());
	setField(L, "words", spell->getWords());
	setField(L, "level", spell->getLevel());
	setField(L, "mlevel", spell->getMagicLevel());
	setField(L, "mana", spell->getManaCost(player));
	setField(L, "manapercent", spell->getManaPercent());
	return 1;
}

int32_t LuaScriptInterface::luaGetInstantSpellInfo(lua_State* L)
{
	//getInstantSpellInfo(name)
	InstantSpell* spell = g_spells->getInstantSpellByName(popString(L));
	if(!spell)
	{
		errorEx(getError(LUA_ERROR_SPELL_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	lua_newtable(L);
	setField(L, "name", spell->getName());
	setField(L, "words", spell->getWords());
	setField(L, "level", spell->getLevel());
	setField(L, "mlevel", spell->getMagicLevel());
	setField(L, "mana", spell->getManaCost(NULL));
	setField(L, "manapercent", spell->getManaPercent());
	return 1;
}

int32_t LuaScriptInterface::luaDoRemoveItem(lua_State* L)
{
	//doRemoveItem(uid[, count = -1])
	int32_t count = -1;
	if(lua_gettop(L) > 1)
		count = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	Item* item = env->getItemByUID(popNumber(L));
	if(!item)
	{
		errorEx(getError(LUA_ERROR_ITEM_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	if(g_game.internalRemoveItem(NULL, item, count) != RET_NOERROR)
	{
		pushBoolean(L, false);
		return 1;
	}

	pushBoolean(L, true);
	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerRemoveItem(lua_State* L)
{
	//doPlayerRemoveItem(cid, itemid, count[, subType = -1])
	int32_t subType = -1;
	if(lua_gettop(L) > 3)
		subType = popNumber(L);

	uint32_t count = popNumber(L);
	uint16_t itemId = (uint16_t)popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
		pushBoolean(L, g_game.removeItemOfType(player, itemId, count, subType));
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerFeed(lua_State* L)
{
	//doPlayerFeed(cid, food)
	int32_t food = (int32_t)popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		player->addDefaultRegeneration((food * 1000) * 3);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerCastBans(lua_State* L)
{
	//getPlayerCastBan(cid)
	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		PlayerCast pc = player->getCast();
		lua_newtable(L);
		for(std::list<CastBan>::iterator it = pc.bans.begin(); it != pc.bans.end(); ++it)
		{
			createTable(L, it->ip);
			setField(L, "name", it->name);
			pushTable(L);
		}
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}


int32_t LuaScriptInterface::luaGetPlayerCastMutes(lua_State* L)
{
	//getPlayerCastMutes(cid)
	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		PlayerCast pc = player->getCast();
		lua_newtable(L); 
		for(std::list<CastBan>::iterator it = pc.muted.begin(); it != pc.muted.end(); ++it)
		{
			createTable(L, it->ip);
			setField(L, "name", it->name);
			pushTable(L);
		}
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerRemoveCastMute(lua_State* L)
{
	//doPlayerRemoveCastMute(cid, ip)
	std::string name = popString(L);
	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		if(player->removeCastMute(name)) 
			pushBoolean(L, true);
		else
			pushBoolean(L, false);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerAddCastMute(lua_State* L)
{
	//doPlayerAddCastMute(cid, ip)
	std::string name = popString(L);
	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		if(player->addCastMute(name))
			pushBoolean(L, true);
		else
			pushBoolean(L, false);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerCastViewers(lua_State* L)
{
	//getPlayerCastBan(cid)
	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		PlayerCast pc = player->getCast();
		lua_newtable(L);
		for(AutoList<ProtocolGame>::iterator it = Player::cSpectators.begin(); it != Player::cSpectators.end(); ++it)
		{
			if(it->second->getPlayer() != player)
				continue;

			createTable(L, it->first);
			setField(L, "name", it->second->getViewerName());
			setField(L, "ip", it->second->getIP());
			pushTable(L);
		}
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}


int32_t LuaScriptInterface::luaDoPlayerRemoveCastBan(lua_State* L)
{
	//doPlayerRemoveCastBan(cid, ip)
	std::string name = popString(L);
	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		if(player->removeCastBan(name)) 
			pushBoolean(L, true);
		else
			pushBoolean(L, false);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerAddCastKick(lua_State* L)
{
    //doPlayerAddCastKick(cid, ip)
    std::string name = popString(L);
    LuaEnvironment* env = getScriptEnv();
    if(Player* player = env->getPlayerByUID(popNumber(L)))
    {
    if(player->addCastKick(name))
    pushBoolean(L, true);
   else
			pushBoolean(L, false);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}
    
int32_t LuaScriptInterface::luaDoPlayerAddCastBan(lua_State* L)
{
	//doPlayerAddCastBan(cid, ip)
	std::string name = popString(L);
	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		if(player->addCastBan(name))
			pushBoolean(L, true);
		else
			pushBoolean(L, false);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}


int32_t LuaScriptInterface::luaDoPlayerSetCastPassword(lua_State* L)
{
	//doPlayerSetCastPassword(cid, password)
	std::string str = popString(L);
	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		player->setCastPassword(str);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSetCastDescription(lua_State* L)
{
	//doPlayerSetCastPassword(cid, password)
	std::string str = popString(L);
	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		player->setCastDescription(str);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSetCastState(lua_State* L)
{
	//doPlayerSetCastState(cid, bool)
	bool state = popNumber(L);
	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		player->setCasting(state);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerCast(lua_State* L)
{
	//getPlayerCast(cid)
	LuaEnvironment* env = getScriptEnv();
	if(const Player* player = env->getPlayerByUID(popNumber(L)))
	{
		lua_newtable(L);
		setFieldBool(L, "status", player->getCastingState());
		setField(L, "password", player->getCastingPassword());
		setField(L, "description", player->getCastDescription());
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetCastsOnline(lua_State* L)
{
	//
	LuaEnvironment* env = getScriptEnv();
	AutoList<Player>::iterator it = Player::castAutoList.begin();

	lua_newtable(L);
	for(int32_t i = 1; it != Player::castAutoList.end(); ++it, ++i)
	{
		lua_pushnumber(L, i);
		lua_pushnumber(L, env->addThing(it->second));
		pushTable(L);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSendCancel(lua_State* L)
{
	//doPlayerSendCancel(cid, text)
	std::string text = popString(L);
	LuaEnvironment* env = getScriptEnv();
	if(const Player* player = env->getPlayerByUID(popNumber(L)))
	{
		player->sendCancel(text);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoSendDefaultCancel(lua_State* L)
{
	//doPlayerSendDefaultCancel(cid, ReturnValue)
	ReturnValue ret = (ReturnValue)popNumber(L);
	LuaEnvironment* env = getScriptEnv();
	if(const Player* player = env->getPlayerByUID(popNumber(L)))
	{
		player->sendCancelMessage(ret);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetSearchString(lua_State* L)
{
	//getSearchString(fromPosition, toPosition[, fromIsCreature = false[, toIsCreature = false]])
	Position toPos, fromPos;
	bool toIsCreature = false, fromIsCreature = false;

	int32_t params = lua_gettop(L);
	if(params > 3)
		toIsCreature = popNumber(L);

	if(params > 2)
		fromIsCreature = popNumber(L);

	toPos = getPosition(L, lua_gettop(L));
	fromPos = getPosition(L, lua_gettop(L));
	if(!toPos.x || !toPos.y || !fromPos.x || !fromPos.y)
	{
		errorEx("wrong position(s) specified.");
		pushBoolean(L, false);
	}
	else
		lua_pushstring(L, g_game.getSearchString(fromPos, toPos, fromIsCreature, toIsCreature).c_str());

	return 1;
}

int32_t LuaScriptInterface::luaGetClosestFreeTile(lua_State* L)
{
	//getClosestFreeTile(cid, targetPos[, extended = false[, ignoreHouse = true]])
	uint32_t params = lua_gettop(L);
	bool ignoreHouse = true, extended = false;
	if(params > 3)
		ignoreHouse = popNumber(L);

	if(params > 2)
		extended = popNumber(L);

	Position pos = getPosition(L, lua_gettop(L));

	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
	{
		Position newPos = g_game.getClosestFreeTile(creature, pos, extended, ignoreHouse);
		if(newPos.x != 0)
			pushPosition(L, newPos, 0);
		else
			pushBoolean(L, false);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoTeleportThing(lua_State* L)
{
	//doTeleportThing(cid, newpos[, pushMove = true[, fullTeleport = true]])
	bool fullTeleport = true, pushMove = true;
	int32_t params = lua_gettop(L);
	if(params > 3)
		fullTeleport = popNumber(L);

	if(params > 2)
		pushMove = popNumber(L);

	Position pos = getPosition(L, lua_gettop(L));

	LuaEnvironment* env = getScriptEnv();
	if(Thing* tmp = env->getThingByUID(popNumber(L)))
		pushBoolean(L, g_game.internalTeleport(tmp, pos, !pushMove, FLAG_NOLIMIT, fullTeleport) == RET_NOERROR);
	else
	{
		errorEx(getError(LUA_ERROR_THING_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoTransformItem(lua_State* L)
{
	//doTransformItem(uid, newId[, count/subType])
	int32_t count = -1;
	if(lua_gettop(L) > 2)
		count = popNumber(L);

	uint16_t newId = popNumber(L);
	uint32_t uid = popNumber(L);
	LuaEnvironment* env = getScriptEnv();

	Item* item = env->getItemByUID(uid);
	if(!item)
	{
		errorEx(getError(LUA_ERROR_ITEM_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	const ItemType& it = Item::items[newId];
	if(it.stackable && count > 100)
		count = 100;

	Item* newItem = g_game.transformItem(item, newId, count);
	if(item->isRemoved())
		env->removeThing(uid);

	if(newItem && newItem != item)
		env->insertThing(uid, newItem);

	pushBoolean(L, true);
	return 1;
}

int32_t LuaScriptInterface::luaDoCreatureSay(lua_State* L)
{
	//doCreatureSay(uid, text[, type = SPEAK_SAY[, ghost = false[, cid = 0[, pos]]]])
	uint32_t params = lua_gettop(L), cid = 0, uid = 0;
	Position pos;
	if(params > 5)
		pos = getPosition(L, lua_gettop(L));

	if(params > 4)
		cid = popNumber(L);

	bool ghost = false;
	if(params > 3)
		ghost = popNumber(L);

	SpeakClasses type = SPEAK_SAY;
	if(params > 2)
		type = (SpeakClasses)popNumber(L);

	std::string text = popString(L);

	uid = popNumber(L);
	if(params > 5 && (!pos.x || !pos.y))
	{
		errorEx("Invalid position specified.");
		pushBoolean(L, false);
		return 1;
	}

	LuaEnvironment* env = getScriptEnv();
	Creature* creature = env->getCreatureByUID(uid);
	if(!creature)
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	SpectatorVec list;
	if(cid)
	{
		Creature* target = env->getCreatureByUID(cid);
		if(!target)
		{
			errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
			pushBoolean(L, false);
			return 1;
		}

		list.push_back(target);
	}

	if(params > 5)
		pushBoolean(L, g_game.internalCreatureSay(creature, type, text, ghost, &list, &pos));
	else
		pushBoolean(L, g_game.internalCreatureSay(creature, type, text, ghost, &list));

	return 1;
}

// Position
int LuaScriptInterface::luaPositionCreate(lua_State* L)
{
	// Position([x = 0[, y = 0[, z = 0[, stackpos = 0]]]])
	// Position([position])
	if (lua_gettop(L) <= 1) {
		pushPosition(L, Position());
		return 1;
	}

	int32_t stackpos;
	if (lua_istable(L, 2)) {
		const Position& position = getPosition(L, 2, stackpos);
		pushPosition(L, position, stackpos);
	} else {
		uint16_t x = getNumber<uint16_t>(L, 2, 0);
		uint16_t y = getNumber<uint16_t>(L, 3, 0);
		uint8_t z = getNumber<uint8_t>(L, 4, 0);
		stackpos = getNumber<int32_t>(L, 5, 0);

		pushPosition(L, Position(x, y, z), stackpos);
	}
	return 1;
}

int LuaScriptInterface::luaPositionAdd(lua_State* L)
{
	// positionValue = position + positionEx
	int32_t stackpos;
	const Position& position = getPosition(L, 1, stackpos);

	Position positionEx;
	if (stackpos == 0) {
		positionEx = getPosition(L, 2, stackpos);
	} else {
		positionEx = getPosition(L, 2);
	}

	pushPosition(L, position + positionEx, stackpos);
	return 1;
}

int LuaScriptInterface::luaPositionSub(lua_State* L)
{
	// positionValue = position - positionEx
	int32_t stackpos;
	const Position& position = getPosition(L, 1, stackpos);

	Position positionEx;
	if (stackpos == 0) {
		positionEx = getPosition(L, 2, stackpos);
	} else {
		positionEx = getPosition(L, 2);
	}

	pushPosition(L, position - positionEx, stackpos);
	return 1;
}

int LuaScriptInterface::luaPositionCompare(lua_State* L)
{
	// position == positionEx
	const Position& position = getPosition(L, 2);
	const Position& positionEx = getPosition(L, 1);
	pushBoolean(L, position == positionEx);
	return 1;
}

int32_t LuaScriptInterface::luaDoSendMagicEffect(lua_State* L)
{
	//doSendMagicEffect(pos, type[, player])
	LuaEnvironment* env = getScriptEnv();
	SpectatorVec list;
	if(lua_gettop(L) > 2)
	{
		if(Creature* creature = env->getCreatureByUID(popNumber(L)))
			list.push_back(creature);
	}

	uint32_t type = popNumber(L);
	Position pos = getPosition(L, lua_gettop(L));

	if(pos.x == 0xFFFF)
		pos = env->getRealPos();

	if(!list.empty())
		g_game.addMagicEffect(list, pos, type);
	else
		g_game.addMagicEffect(pos, type);

	pushBoolean(L, true);
	return 1;
}

int32_t LuaScriptInterface::luaDoSendDistanceShoot(lua_State* L)
{
	//doSendDistanceShoot(fromPos, toPos, type[, player])
	LuaEnvironment* env = getScriptEnv();
	SpectatorVec list;
	if(lua_gettop(L) > 3)
	{
		if(Creature* creature = env->getCreatureByUID(popNumber(L)))
			list.push_back(creature);
	}

	uint32_t type = popNumber(L);
	Position toPos = getPosition(L, lua_gettop(L));
	Position fromPos = getPosition(L, lua_gettop(L));

	if(fromPos.x == 0xFFFF)
		fromPos = env->getRealPos();

	if(toPos.x == 0xFFFF)
		toPos = env->getRealPos();

	if(!list.empty())
		g_game.addDistanceEffect(list, fromPos, toPos, type);
	else
		g_game.addDistanceEffect(fromPos, toPos, type);

	pushBoolean(L, true);
	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerAddSkillTry(lua_State* L)
{
	//doPlayerAddSkillTry(uid, skillid, n[, useMultiplier = true])
	bool multiplier = true;
	if(lua_gettop(L) > 3)
		multiplier = popNumber(L);

	uint64_t n = popNumber(L);
	uint16_t skillid = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		player->addSkillAdvance((skills_t)skillid, n, multiplier);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetCreatureSpeakType(lua_State* L)
{
	//getCreatureSpeakType(uid)
	LuaEnvironment* env = getScriptEnv();
	if(const Creature* creature = env->getCreatureByUID(popNumber(L)))
		lua_pushnumber(L, (SpeakClasses)creature->getSpeakType());
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoCreatureSetSpeakType(lua_State* L)
{
	//doCreatureSetSpeakType(uid, type)
	SpeakClasses type = (SpeakClasses)popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
	{
		if(type < SPEAK_CLASS_FIRST || type > SPEAK_CLASS_LAST)
		{
			errorEx("Invalid speak type!");
			pushBoolean(L, false);
			return 1;
		}

		creature->setSpeakType(type);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetCreatureHideHealth(lua_State* L)
{
	//getCreatureHideHealth(cid)
	LuaEnvironment* env = getScriptEnv();

	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
		pushBoolean(L, creature->getHideHealth());
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoCreatureSetHideHealth(lua_State* L)
{
	//doCreatureSetHideHealth(cid, hide)
	bool hide = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
	{
		creature->setHideHealth(hide);
		g_game.addCreatureHealth(creature);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoCreatureAddHealth(lua_State* L)
{
	//doCreatureAddHealth(uid, health[, hitEffect[, hitColor[, force]]])
	int32_t params = lua_gettop(L);
	bool force = false;
	if(params > 4)
		force = popNumber(L);

	Color_t hitColor = COLOR_UNKNOWN;
	if(params > 3)
		hitColor = (Color_t)popNumber(L);

	MagicEffect_t hitEffect = MAGIC_EFFECT_UNKNOWN;
	if(params > 2)
		hitEffect = (MagicEffect_t)popNumber(L);

	int32_t healthChange = popNumber(L);
	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
	{
		if(healthChange) //do not post with 0 value
			g_game.combatChangeHealth(healthChange < 1 ? COMBAT_UNDEFINEDDAMAGE : COMBAT_HEALING,
				NULL, creature, healthChange, hitEffect, hitColor, force);

		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoCreatureAddMana(lua_State* L)
{
	//doCreatureAddMana(uid, mana[, aggressive])
	bool aggressive = true;
	if(lua_gettop(L) > 2)
		aggressive = popNumber(L);

	int32_t manaChange = popNumber(L);
	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
	{
		if(aggressive)
			g_game.combatChangeMana(NULL, creature, manaChange);
		else
			creature->changeMana(manaChange);

		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerAddSpentMana(lua_State* L)
{
	//doPlayerAddSpentMana(cid, amount[, useMultiplier = true])
	bool multiplier = true;
	if(lua_gettop(L) > 2)
		multiplier = popNumber(L);

	uint32_t amount = popNumber(L);
	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		player->addManaSpent(amount, multiplier);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerAddItem(lua_State* L)
{
	//doPlayerAddItem(cid, itemid[, count/subtype = 1[, canDropOnMap = true[, slot = 0]]])
	//doPlayerAddItem(cid, itemid[, count = 1[, canDropOnMap = true[, subtype = 1[, slot = 0]]]])
	int32_t params = lua_gettop(L), subType = 1, slot = SLOT_WHEREEVER;
	if(params > 5)
		slot = popNumber(L);

	if(params > 4)
	{
		if(params > 5)
			subType = popNumber(L);
		else
			slot = popNumber(L);
	}

	bool canDropOnMap = true;
	if(params > 3)
		canDropOnMap = popNumber(L);

	uint32_t count = 1;
	if(params > 2)
		count = popNumber(L);

	uint32_t itemId = popNumber(L);
	if(slot > SLOT_AMMO)
	{
		errorEx("Invalid slot.");
		pushBoolean(L, false);
		return 1;
	}

	LuaEnvironment* env = getScriptEnv();
	Player* player = env->getPlayerByUID((uint32_t)popNumber(L));
	if(!player)
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	const ItemType& it = Item::items[itemId];
	int32_t itemCount = 1;
	if(params > 4)
		itemCount = std::max((uint32_t)1, count);
	else if(it.hasSubType())
	{
		if(it.stackable)
			itemCount = (int32_t)std::ceil((float)count / 100);

		subType = count;
	}

	while(itemCount > 0)
	{
		int32_t stackCount = std::min(100, subType);
		Item* newItem = Item::CreateItem(itemId, stackCount);
		if(!newItem)
		{
			errorEx(getError(LUA_ERROR_ITEM_NOT_FOUND));
			pushBoolean(L, false);
			return 1;
		}

		if(it.stackable)
			subType -= stackCount;

		ReturnValue ret = g_game.internalPlayerAddItem(NULL, player, newItem, canDropOnMap, (slots_t)slot);
		if(ret != RET_NOERROR)
		{
			delete newItem;
			pushBoolean(L, false);
			return 1;
		}

		--itemCount;
		if(itemCount)
			continue;

		if(newItem->getParent())
			lua_pushnumber(L, env->addThing(newItem));
		else //stackable item stacked with existing object, newItem will be released
			lua_pushnil(L);

		return 1;
	}

	lua_pushnil(L);
	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerAddItemEx(lua_State* L)
{
	//doPlayerAddItemEx(cid, uid[, canDropOnMap = false[, slot = 0]])
	int32_t params = lua_gettop(L), slot = SLOT_WHEREEVER;
	if(params > 3)
		slot = popNumber(L);

	bool canDropOnMap = false;
	if(params > 2)
		canDropOnMap = popNumber(L);

	uint32_t uid = (uint32_t)popNumber(L);
	if(slot > SLOT_AMMO)
	{
		errorEx("Invalid slot.");
		pushBoolean(L, false);
		return 1;
	}

	LuaEnvironment* env = getScriptEnv();
	Player* player = env->getPlayerByUID(popNumber(L));
	if(!player)
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	Item* item = env->getItemByUID(uid);
	if(!item)
	{
		errorEx(getError(LUA_ERROR_ITEM_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	if(item->getParent() == VirtualCylinder::virtualCylinder)
		lua_pushnumber(L, g_game.internalPlayerAddItem(NULL, player, item, canDropOnMap, (slots_t)slot));
	else
		pushBoolean(L, false);

	return 1;
}

int32_t LuaScriptInterface::luaDoTileAddItemEx(lua_State* L)
{
	//doTileAddItemEx(pos, uid)
	uint32_t uid = (uint32_t)popNumber(L);
	Position pos = getPosition(L, lua_gettop(L));

	LuaEnvironment* env = getScriptEnv();
	Tile* tile = g_game.getTile(pos);
	if(!tile)
	{
		errorEx(getError(LUA_ERROR_TILE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	Item* item = env->getItemByUID(uid);
	if(!item)
	{
		errorEx(getError(LUA_ERROR_ITEM_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	if(item->getParent() == VirtualCylinder::virtualCylinder)
		lua_pushnumber(L, g_game.internalAddItem(NULL, tile, item));
	else
		pushBoolean(L, false);

	return 1;
}

int32_t LuaScriptInterface::luaDoRelocate(lua_State* L)
{
	//doRelocate(pos, posTo[, creatures = true[, unmovable = true]])
	//Moves all movable objects from pos to posTo
	bool unmovable = true, creatures = true;
	int32_t params = lua_gettop(L);
	if(params > 3)
		unmovable = popNumber(L);

	if(params > 2)
		creatures = popNumber(L);

	Position toPos = getPosition(L, lua_gettop(L));
	Position fromPos = getPosition(L, lua_gettop(L));

	Tile* fromTile = g_game.getTile(fromPos.x, fromPos.y, fromPos.z);
	if(!fromTile)
	{
		errorEx(getError(LUA_ERROR_TILE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	Tile* toTile = g_game.getTile(toPos.x, toPos.y, toPos.z);
	if(!toTile)
	{
		errorEx(getError(LUA_ERROR_TILE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	if(fromTile != toTile)
	{
		for(int32_t i = fromTile->getThingCount() - 1; i >= 0; --i)
		{
			if(Thing* thing = fromTile->__getThing(i))
			{
				if(Item* item = thing->getItem())
				{
					const ItemType& it = Item::items[item->getID()];
					if(!it.isGroundTile() && !it.alwaysOnTop && !it.isMagicField())
						g_game.internalTeleport(item, toPos, true, unmovable ? FLAG_IGNORENOTMOVABLE : 0);
				}
				else if(creatures)
				{
					if(Creature* creature = thing->getCreature())
						g_game.internalTeleport(creature, toPos, false);
				}
			}
		}
	}

	pushBoolean(L, true);
	return 1;
}

int32_t LuaScriptInterface::luaDoCleanTile(lua_State* L)
{
	//doCleanTile(pos, forceMapLoaded = false)
	//Remove all items from tile, ignore creatures
	bool forceMapLoaded = false;
	if(lua_gettop(L) > 1)
		forceMapLoaded = popNumber(L);

	Position pos = getPosition(L, lua_gettop(L));

	Tile* tile = g_game.getTile(pos);
	if(!tile)
	{
		errorEx(getError(LUA_ERROR_TILE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	for(int32_t i = tile->getThingCount() - 1; i >= 1; --i) //ignore ground
	{
		if(Thing* thing = tile->__getThing(i))
		{
			if(Item* item = thing->getItem())
			{
				if(!item->isLoadedFromMap() || forceMapLoaded)
					g_game.internalRemoveItem(NULL, item);
			}
		}
	}

	pushBoolean(L, true);
	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSendTextMessage(lua_State* L)
{
	//doPlayerSendTextMessage(cid, MessageClasses, message)
	std::string text = popString(L);
	uint32_t messageClass = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	Player* player = env->getPlayerByUID(popNumber(L));
	if(!player)
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	player->sendTextMessage((MessageClasses)messageClass, text);
	pushBoolean(L, true);
	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSendChannelMessage(lua_State* L)
{
	//doPlayerSendChannelMessage(cid, author, message, SpeakClasses, channel)
	uint16_t channelId = popNumber(L);
	uint32_t speakClass = popNumber(L);
	std::string text = popString(L), name = popString(L);

	LuaEnvironment* env = getScriptEnv();
	Player* player = env->getPlayerByUID(popNumber(L));
	if(!player)
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	player->sendChannelMessage(name, text, (SpeakClasses)speakClass, channelId);
	pushBoolean(L, true);
	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSendToChannel(lua_State* L)
{
	//doPlayerSendToChannel(cid, targetId, SpeakClasses, message, channel[, time])
	LuaEnvironment* env = getScriptEnv();
	uint32_t time = 0;
	if(lua_gettop(L) > 5)
		time = popNumber(L);

	uint16_t channelId = popNumber(L);
	std::string text = popString(L);
	uint32_t speakClass = popNumber(L), targetId = popNumber(L);

	Player* player = env->getPlayerByUID(popNumber(L));
	if(!player)
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	Creature* creature = env->getCreatureByUID(targetId);
	if(!creature)
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	player->sendToChannel(creature, (SpeakClasses)speakClass, text, channelId, time);
	pushBoolean(L, true);
	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerOpenChannel(lua_State* L)
{
	//doPlayerOpenChannel(cid, channelId)
	uint16_t channelId = popNumber(L);
	uint32_t cid = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(env->getPlayerByUID(cid))
	{
		pushBoolean(L, g_game.playerOpenChannel(cid, channelId));
		return 1;
	}

	errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
	pushBoolean(L, false);
	return 1;
}

int32_t LuaScriptInterface::luaDoSendCreatureSquare(lua_State* L)
{
	//doSendCreatureSquare(cid, color[, player])
	LuaEnvironment* env = getScriptEnv();
	SpectatorVec list;
	if(lua_gettop(L) > 2)
	{
		if(Creature* creature = env->getCreatureByUID(popNumber(L)))
			list.push_back(creature);
	}

	uint8_t color = popNumber(L);
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
	{
		if(!list.empty())
			g_game.addCreatureSquare(list, creature, color);
		else
			g_game.addCreatureSquare(creature, color);

		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoSendAnimatedText(lua_State* L)
{
	//doSendAnimatedText(pos, text, color[, player])
	LuaEnvironment* env = getScriptEnv();
	SpectatorVec list;
	if(lua_gettop(L) > 3)
	{
		if(Creature* creature = env->getCreatureByUID(popNumber(L)))
			list.push_back(creature);
	}

	uint8_t color = popNumber(L);
	std::string text = popString(L);

	Position pos = getPosition(L, lua_gettop(L));
	if(pos.x == 0xFFFF)
		pos = env->getRealPos();

	if(!list.empty())
		g_game.addAnimatedText(list, pos, color, text);
	else
		g_game.addAnimatedText(pos, color, text);

	pushBoolean(L, true);
	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerSkillLevel(lua_State* L)
{
	//getPlayerSkillLevel(cid, skill[, ignoreModifiers = false])
	bool ignoreModifiers = false;
	if(lua_gettop(L) > 2)
		ignoreModifiers = popNumber(L);

	uint32_t skill = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(const Player* player = env->getPlayerByUID(popNumber(L)))
	{
		if(skill <= SKILL_LAST)
			lua_pushnumber(L, ignoreModifiers ? player->skills[skill][SKILL_LEVEL] :
				player->skills[skill][SKILL_LEVEL] + player->getVarSkill((skills_t)skill));
		else
			pushBoolean(L, false);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerSkillTries(lua_State* L)
{
	//getPlayerSkillTries(cid, skill)
	uint32_t skill = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(const Player* player = env->getPlayerByUID(popNumber(L)))
	{
		if(skill <= SKILL_LAST)
			lua_pushnumber(L, player->skills[skill][SKILL_TRIES]);
		else
			pushBoolean(L, false);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoCreatureSetDropLoot(lua_State* L)
{
	//doCreatureSetDropLoot(cid, doDrop)
	bool doDrop = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
	{
		creature->setDropLoot(doDrop ? LOOT_DROP_FULL : LOOT_DROP_NONE);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerLossPercent(lua_State* L)
{
	//getPlayerLossPercent(cid, lossType)
	uint8_t lossType = (uint8_t)popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(const Player* player = env->getPlayerByUID(popNumber(L)))
	{
		if(lossType <= LOSS_LAST)
		{
			uint32_t value = player->getLossPercent((lossTypes_t)lossType);
			lua_pushnumber(L, value);
		}
		else
			pushBoolean(L, false);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSetLossPercent(lua_State* L)
{
	//doPlayerSetLossPercent(cid, lossType, newPercent)
	uint32_t newPercent = popNumber(L);
	uint8_t lossType = (uint8_t)popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		if(lossType <= LOSS_LAST)
		{
			player->setLossPercent((lossTypes_t)lossType, newPercent);
			pushBoolean(L, true);
		}
		else
			pushBoolean(L, false);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSetLossSkill(lua_State* L)
{
	//doPlayerSetLossSkill(cid, doLose)
	bool doLose = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		player->setLossSkill(doLose);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoShowTextDialog(lua_State* L)
{
	//doShowTextDialog(cid, itemid, text)
	std::string text = popString(L);
	uint32_t itemId = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		player->setWriteItem(NULL, 0);
		player->sendTextWindow(itemId, text);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoDecayItem(lua_State* L)
{
	//doDecayItem(uid)
	//Note: to stop decay set decayTo = 0 in items.xml
	LuaEnvironment* env = getScriptEnv();
	if(Item* item = env->getItemByUID(popNumber(L)))
	{
		g_game.startDecay(item);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_ITEM_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetThingFromPos(lua_State* L)
{
	//getThingFromPos(pos[, displayError = true])
	//Note:
	//	stackpos = 255- top thing (movable item or creature)
	//	stackpos = 254- magic field
	//	stackpos = 253- top creature

	bool displayError = true;
	if(lua_gettop(L) > 1)
		displayError = popNumber(L);

	Position pos = getPosition(L, lua_gettop(L));

	LuaEnvironment* env = getScriptEnv();
	Thing* thing = NULL;
	if(Tile* tile = g_game.getMap()->getTile(pos))
	{
		if(pos.stackpos == 255)
		{
			if(!(thing = tile->getTopCreature()))
			{
				Item* item = tile->getTopDownItem();
				if(item && item->isMovable())
					thing = item;
			}
		}
		else if(pos.stackpos == 254)
			thing = tile->getFieldItem();
		else if(pos.stackpos == 253)
			thing = tile->getTopCreature();
		else
			thing = tile->__getThing(pos.stackpos);

		if(thing)
			pushThing(L, thing, env->addThing(thing));
		else
			pushThing(L, NULL, 0);

		return 1;
	}

	if(displayError)
		errorEx(getError(LUA_ERROR_TILE_NOT_FOUND));

	pushThing(L, NULL, 0);
	return 1;
}

int32_t LuaScriptInterface::luaGetTileItemById(lua_State* L)
{
	//getTileItemById(pos, itemId[, subType = -1])
	LuaEnvironment* env = getScriptEnv();

	int32_t subType = -1;
	if(lua_gettop(L) > 2)
		subType = (int32_t)popNumber(L);

	int32_t itemId = (int32_t)popNumber(L);
	Position pos = getPosition(L, lua_gettop(L));

	Tile* tile = g_game.getTile(pos);
	if(!tile)
	{
		pushThing(L, NULL, 0);
		return 1;
	}

	Item* item = g_game.findItemOfType(tile, itemId, false, subType);
	if(!item)
	{
		pushThing(L, NULL, 0);
		return 1;
	}

	pushThing(L, item, env->addThing(item));
	return 1;
}

int32_t LuaScriptInterface::luaGetTileItemByType(lua_State* L)
{
	//getTileItemByType(pos, type)
	uint32_t rType = (uint32_t)popNumber(L);
	if(rType >= ITEM_TYPE_LAST)
	{
		errorEx("Not a valid item type");
		pushThing(L, NULL, 0);
		return 1;
	}

	Position pos = getPosition(L, lua_gettop(L));

	Tile* tile = g_game.getTile(pos);
	if(!tile)
	{
		pushThing(L, NULL, 0);
		return 1;
	}

	bool found = true;
	switch((ItemTypes_t)rType)
	{
		case ITEM_TYPE_TELEPORT:
		{
			if(!tile->hasFlag(TILESTATE_TELEPORT))
				found = false;

			break;
		}
		case ITEM_TYPE_MAGICFIELD:
		{
			if(!tile->hasFlag(TILESTATE_MAGICFIELD))
				found = false;

			break;
		}
		case ITEM_TYPE_MAILBOX:
		{
			if(!tile->hasFlag(TILESTATE_MAILBOX))
				found = false;

			break;
		}
		case ITEM_TYPE_TRASHHOLDER:
		{
			if(!tile->hasFlag(TILESTATE_TRASHHOLDER))
				found = false;

			break;
		}
		case ITEM_TYPE_BED:
		{
			if(!tile->hasFlag(TILESTATE_BED))
				found = false;

			break;
		}
		case ITEM_TYPE_DEPOT:
		{
			if(!tile->hasFlag(TILESTATE_DEPOT))
				found = false;

			break;
		}
		default:
			break;
	}

	if(!found)
	{
		pushThing(L, NULL, 0);
		return 1;
	}

	LuaEnvironment* env = getScriptEnv();
	Item* item = NULL;
	for(uint32_t i = 0; i < tile->getThingCount(); ++i)
	{
		if(!(item = tile->__getThing(i)->getItem()))
			continue;

		if(Item::items[item->getID()].type != (ItemTypes_t)rType)
			continue;

		pushThing(L, item, env->addThing(item));
		return 1;
	}

	pushThing(L, NULL, 0);
	return 1;
}

int32_t LuaScriptInterface::luaGetTileThingByPos(lua_State* L)
{
	//getTileThingByPos(pos)
	Position pos = getPosition(L, lua_gettop(L));

	LuaEnvironment* env = getScriptEnv();

	Tile* tile = g_game.getTile(pos.x, pos.y, pos.z);
	if(!tile)
	{
		if(pos.stackpos == -1)
		{
			lua_pushnumber(L, -1);
			return 1;
		}
		else
		{
			pushThing(L, NULL, 0);
			return 1;
		}
	}

	if(pos.stackpos == -1)
	{
		lua_pushnumber(L, tile->getThingCount());
		return 1;
	}

	Thing* thing = tile->__getThing(pos.stackpos);
	if(!thing)
	{
		pushThing(L, NULL, 0);
		return 1;
	}

	pushThing(L, thing, env->addThing(thing));
	return 1;
}

int32_t LuaScriptInterface::luaGetTopCreature(lua_State* L)
{
	//getTopCreature(pos)
	Position pos = getPosition(L, lua_gettop(L));

	LuaEnvironment* env = getScriptEnv();
	Tile* tile = g_game.getTile(pos);
	if(!tile)
	{
		pushThing(L, NULL, 0);
		return 1;
	}

	Thing* thing = tile->getTopCreature();
	if(!thing || !thing->getCreature())
	{
		pushThing(L, NULL, 0);
		return 1;
	}

	pushThing(L, thing, env->addThing(thing));
	return 1;
}

int32_t LuaScriptInterface::luaDoCreateItem(lua_State* L)
{
	//doCreateItem(itemid[, type/count = 1], pos)
	//Returns uid of the created item, only works on tiles.
	Position pos = getPosition(L, lua_gettop(L));

	uint32_t count = 1;
	if(lua_gettop(L) > 1)
		count = popNumber(L);

	uint32_t itemId = popNumber(L);
	LuaEnvironment* env = getScriptEnv();
	const ItemType& it = Item::items[itemId];

	Tile* tile = g_game.getTile(pos);
	if(!tile)
	{
		if(it.group == ITEM_GROUP_GROUND)
		{
			Item* item = Item::CreateItem(itemId);
			tile = IOMap::createTile(item, NULL, pos.x, pos.y, pos.z);

			g_game.setTile(tile);
			lua_pushnumber(L, env->addThing(item));
			return 1;
		}
		else
		{
			errorEx(getError(LUA_ERROR_TILE_NOT_FOUND));
			pushBoolean(L, false);
			return 1;
		}
	}

	int32_t itemCount = 1, subType = 1;
	if(it.hasSubType())
	{
		if(it.stackable)
			itemCount = (int32_t)std::ceil((float)count / 100);

		subType = count;
	}
	else
		itemCount = std::max((uint32_t)1, count);

	while(itemCount > 0)
	{
		int32_t stackCount = std::min(100, subType);
		Item* newItem = Item::CreateItem(itemId, stackCount);
		if(!newItem)
		{
			errorEx(getError(LUA_ERROR_ITEM_NOT_FOUND));
			pushBoolean(L, false);
			return 1;
		}

		if(it.stackable)
			subType -= stackCount;

		ReturnValue ret = g_game.internalAddItem(NULL, tile, newItem, INDEX_WHEREEVER, FLAG_NOLIMIT);
		if(ret != RET_NOERROR)
		{
			delete newItem;
			pushBoolean(L, false);
			return 1;
		}

		--itemCount;
		if(itemCount)
			continue;

		if(newItem->getParent())
			lua_pushnumber(L, env->addThing(newItem));
		else //stackable item stacked with existing object, newItem will be released
			lua_pushnil(L);

		return 1;
	}

	lua_pushnil(L);
	return 1;
}

int32_t LuaScriptInterface::luaDoCreateItemEx(lua_State* L)
{
	//doCreateItemEx(itemid[, count/subType])
	uint32_t count = 0;
	if(lua_gettop(L) > 1)
		count = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	const ItemType& it = Item::items[(uint32_t)popNumber(L)];
	if(it.stackable && count > 100)
		count = 100;

	Item* newItem = Item::CreateItem(it.id, count);
	if(!newItem)
	{
		errorEx(getError(LUA_ERROR_ITEM_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	newItem->setParent(VirtualCylinder::virtualCylinder);
	env->addTempItem(env, newItem);

	lua_pushnumber(L, env->addThing(newItem));
	return 1;
}

int32_t LuaScriptInterface::luaDoCreateTeleport(lua_State* L)
{
	//doCreateTeleport(itemid, toPosition, fromPosition)
	Position createPos = getPosition(L, lua_gettop(L));
	Position toPos = getPosition(L, lua_gettop(L));

	uint32_t itemId = (uint32_t)popNumber(L);
	LuaEnvironment* env = getScriptEnv();

	Tile* tile = g_game.getMap()->getTile(createPos);
	if(!tile)
	{
		errorEx(getError(LUA_ERROR_TILE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	Item* newItem = Item::CreateItem(itemId);
	Teleport* newTeleport = newItem->getTeleport();
	if(!newTeleport)
	{
		delete newItem;
		pushBoolean(L, false);
		return 1;
	}

	newTeleport->setDestination(toPos);
	if(g_game.internalAddItem(NULL, tile, newTeleport, INDEX_WHEREEVER, FLAG_NOLIMIT) != RET_NOERROR)
	{
		delete newItem;
		pushBoolean(L, false);
		return 1;
	}

	if(newItem->getParent())
		lua_pushnumber(L, env->addThing(newItem));
	else //stackable item stacked with existing object, newItem will be released
		lua_pushnil(L);

	return 1;
}

int32_t LuaScriptInterface::luaGetCreatureStorage(lua_State* L)
{
	//getCreatureStorage(cid, key)
	std::string key = popString(L);
	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
	{
		std::string strValue;
		if(creature->getStorage(key, strValue))
		{
			int32_t intValue = atoi(strValue.c_str());
			if(intValue || strValue == "0")
				lua_pushnumber(L, intValue);
			else
				lua_pushstring(L, strValue.c_str());
		}
		else
			lua_pushnumber(L, -1);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoCreatureSetStorage(lua_State* L)
{
	//doCreatureSetStorage(cid, key[, value])
	std::string value;
	bool nil = true;
	if(lua_gettop(L) > 2)
	{
		if(!lua_isnil(L, -1))
		{
			value = popString(L);
			nil = false;
		}
		else
			lua_pop(L, 1);
	}

	std::string key = popString(L);
	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
	{
		if(!nil)
			nil = creature->setStorage(key, value);
		else
			creature->eraseStorage(key);

		pushBoolean(L, nil);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

// Tile
int LuaScriptInterface::luaTileCreate(lua_State* L)
{
	// Tile(x, y, z)
	// Tile(position)
	Tile* tile;
	if (lua_istable(L, 2)) {
		tile = g_game.getMap()->getTile(getPosition(L, 2));
	} else {
		uint8_t z = getNumber<uint8_t>(L, 4);
		uint16_t y = getNumber<uint16_t>(L, 3);
		uint16_t x = getNumber<uint16_t>(L, 2);
		tile = g_game.getMap()->getTile(x, y, z);
	}

	if (tile) {
		pushUserdata<Tile>(L, tile);
		setMetatable(L, -1, "Tile");
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int32_t LuaScriptInterface::luaGetTileInfo(lua_State* L)
{
	//getTileInfo(pos)
	Position pos = getPosition(L, lua_gettop(L));
	if(Tile* tile = g_game.getMap()->getTile(pos))
	{
		LuaEnvironment* env = getScriptEnv();
		pushThing(L, tile->ground, env->addThing(tile->ground));

		setFieldBool(L, "protection", tile->hasFlag(TILESTATE_PROTECTIONZONE));
		setFieldBool(L, "optional", tile->hasFlag(TILESTATE_OPTIONALZONE));
		setFieldBool(L, "nologout", tile->hasFlag(TILESTATE_NOLOGOUT));
		setFieldBool(L, "hardcore", tile->hasFlag(TILESTATE_HARDCOREZONE));
		setFieldBool(L, "refresh", tile->hasFlag(TILESTATE_REFRESH));
		setFieldBool(L, "trashed", tile->hasFlag(TILESTATE_TRASHED));
		setFieldBool(L, "house", tile->hasFlag(TILESTATE_HOUSE));
		setFieldBool(L, "bed", tile->hasFlag(TILESTATE_BED));
		setFieldBool(L, "depot", tile->hasFlag(TILESTATE_DEPOT));

		setField(L, "things", tile->getThingCount());
		setField(L, "creatures", tile->getCreatureCount());
		setField(L, "items", tile->getItemCount());
		setField(L, "topItems", tile->getTopItemCount());
		setField(L, "downItems", tile->getDownItemCount());
	}
	else
	{
		errorEx(getError(LUA_ERROR_TILE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetHouseFromPos(lua_State* L)
{
	//getHouseFromPos(pos)
	Position pos = getPosition(L, lua_gettop(L));

	Tile* tile = g_game.getMap()->getTile(pos);
	if(!tile)
	{
		errorEx(getError(LUA_ERROR_TILE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	HouseTile* houseTile = tile->getHouseTile();
	if(!houseTile)
	{
		pushBoolean(L, false);
		return 1;
	}

	House* house = houseTile->getHouse();
	if(!house)
	{
		pushBoolean(L, false);
		return 1;
	}

	lua_pushnumber(L, house->getId());
	return 1;
}

// Monster
int LuaScriptInterface::luaMonsterCreate(lua_State* L)
{
	// Monster(id or userdata)
	Monster* monster;
	if (isNumber(L, 2)) {
		monster = g_game.getMonsterByID(getNumber<uint32_t>(L, 2));
	} else if (isUserdata(L, 2)) {
		if (getUserdataType(L, 2) != LuaData_Monster) {
			lua_pushnil(L);
			return 1;
		}
		monster = getUserdata<Monster>(L, 2);
	} else {
		monster = nullptr;
	}

	if (monster) {
		pushUserdata<Monster>(L, monster);
		setMetatable(L, -1, "Monster");
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int32_t LuaScriptInterface::luaDoCreateMonster(lua_State* L)
{
	//doCreateMonster(name, pos[, extend = false[, force = false[, displayError = true]]])
	bool displayError = true, force = false, extend = false;
	int32_t params = lua_gettop(L);
	if(params > 4)
		displayError = popNumber(L);

	if(params > 3)
		force = popNumber(L);

	if(params > 2)
		extend = popNumber(L);

	Position pos = getPosition(L, lua_gettop(L));

	std::string name = popString(L);
	Monster* monster = Monster::createMonster(name.c_str());
	if(!monster)
	{
		if(displayError)
			errorEx("Monster with name '" + name + "' not found");

		pushBoolean(L, false);
		return 1;
	}

	if(!g_game.placeCreature(monster, pos, extend, force))
	{
		delete monster;
		if(displayError)
			errorEx("Cannot create monster: " + name);

		pushBoolean(L, true);
		return 1;
	}

	LuaEnvironment* env = getScriptEnv();
	lua_pushnumber(L, env->addThing((Thing*)monster));
	return 1;
}

int32_t LuaScriptInterface::luaDoCreateNpc(lua_State* L)
{
	//doCreateNpc(name, pos[, displayError = true])
	bool displayError = true;
	if(lua_gettop(L) > 2)
		displayError = popNumber(L);

	Position pos = getPosition(L, lua_gettop(L));

	std::string name = popString(L);
	Npc* npc = Npc::createNpc(name.c_str());
	if(!npc)
	{
		if(displayError)
			errorEx("Npc with name '" + name + "' not found");

		pushBoolean(L, false);
		return 1;
	}

	if(!g_game.placeCreature(npc, pos))
	{
		delete npc;
		if(displayError)
			errorEx("Cannot create npc: " + name);

		pushBoolean(L, true); //for scripting compatibility
		return 1;
	}

	LuaEnvironment* env = getScriptEnv();
	lua_pushnumber(L, env->addThing((Thing*)npc));
	return 1;
}

int32_t LuaScriptInterface::luaDoRemoveCreature(lua_State* L)
{
	//doRemoveCreature(cid[, forceLogout = true])
	bool forceLogout = true;
	if(lua_gettop(L) > 1)
		forceLogout = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
	{
		if(Player* player = creature->getPlayer())
			player->kick(true, forceLogout); //Players will get kicked without restrictions
		else
			g_game.removeCreature(creature); //Monsters/NPCs will get removed

		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerAddMoney(lua_State* L)
{
	//doPlayerAddMoney(cid, money)
	uint64_t money = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		g_game.addMoney(player, money);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerRemoveMoney(lua_State* L)
{
	//doPlayerRemoveMoney(cid,money)
	uint64_t money = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
		pushBoolean(L, g_game.removeMoney(player, money));
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerTransferMoneyTo(lua_State* L)
{
	//doPlayerTransferMoneyTo(cid, target, money)
	uint64_t money = popNumber(L);
	std::string target = popString(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
		pushBoolean(L, player->transferMoneyTo(target, money));
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSetPzLocked(lua_State* L)
{
	//doPlayerSetPzLocked(cid, locked)
	bool locked = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		if(player->isPzLocked() != locked)
		{
			player->setPzLocked(locked);
			player->sendIcons();
		}

		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSetTown(lua_State* L)
{
	//doPlayerSetTown(cid, townid)
	uint32_t townid = (uint32_t)popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		if(Town* town = Towns::getInstance()->getTown(townid))
		{
			player->setMasterPosition(town->getPosition());
			player->setTown(townid);
			pushBoolean(L, true);
		}
		else
			pushBoolean(L, false);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSetVocation(lua_State* L)
{
	//doPlayerSetVocation(cid, voc)
	uint32_t voc = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		player->setVocation(voc);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSetSex(lua_State* L)
{
	//doPlayerSetSex(cid, sex)
	uint32_t newSex = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		player->setSex(newSex);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerAddSoul(lua_State* L)
{
	//doPlayerAddSoul(cid, amount)
	int32_t amount = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		player->changeSoul(amount);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSetExtraAttackSpeed(lua_State *L)
{	
	uint32_t speed = popNumber(L);						
	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L))){	
		player->setPlayerExtraAttackSpeed(speed);
		lua_pushnumber(L, true);
	}	
	else
	{		  
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		lua_pushnumber(L, false);
	}	  
	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerItemCount(lua_State* L)
{
	//getPlayerItemCount(cid, itemid[, subType = -1])
	int32_t subType = -1;
	if(lua_gettop(L) > 2)
		subType = popNumber(L);

	uint32_t itemId = popNumber(L);
	LuaEnvironment* env = getScriptEnv();
	if(const Player* player = env->getPlayerByUID(popNumber(L)))
		lua_pushnumber(L, player->__getItemTypeCount(itemId, subType));
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetHouseInfo(lua_State* L)
{
	//getHouseInfo(houseId[, displayError = true])
	bool displayError = true;
	if(lua_gettop(L) > 1)
		displayError = popNumber(L);

	House* house = Houses::getInstance()->getHouse(popNumber(L));
	if(!house)
	{
		if(displayError)
			errorEx(getError(LUA_ERROR_HOUSE_NOT_FOUND));

		pushBoolean(L, false);
		return 1;
	}

	lua_newtable(L);
	setField(L, "id", house->getId());
	setField(L, "name", house->getName().c_str());
	setField(L, "owner", house->getOwner());

	lua_pushstring(L, "entry");
	pushPosition(L, house->getEntry(), 0);
	pushTable(L);

	setField(L, "rent", house->getRent());
	setField(L, "price", house->getPrice());
	setField(L, "town", house->getTownId());
	setField(L, "paidUntil", house->getPaidUntil());
	setField(L, "warnings", house->getRentWarnings());
	setField(L, "lastWarning", house->getLastWarning());

	setFieldBool(L, "guildHall", house->isGuild());
	setField(L, "size", house->getSize());
	createTable(L, "doors");

	HouseDoorList::iterator dit = house->getHouseDoorBegin();
	for(uint32_t i = 1; dit != house->getHouseDoorEnd(); ++dit, ++i)
	{
		lua_pushnumber(L, i);
		pushPosition(L, (*dit)->getPosition(), 0);
		pushTable(L);
	}

	pushTable(L);
	createTable(L, "beds");

	HouseBedList::iterator bit = house->getHouseBedsBegin();
	for(uint32_t i = 1; bit != house->getHouseBedsEnd(); ++bit, ++i)
	{
		lua_pushnumber(L, i);
		pushPosition(L, (*bit)->getPosition(), 0);
		pushTable(L);
	}

	pushTable(L);
	createTable(L, "tiles");

	HouseTileList::iterator tit = house->getHouseTileBegin();
	for(uint32_t i = 1; tit != house->getHouseTileEnd(); ++tit, ++i)
	{
		lua_pushnumber(L, i);
		pushPosition(L, (*tit)->getPosition(), 0);
		pushTable(L);
	}

	pushTable(L);
	return 1;
}

int32_t LuaScriptInterface::luaGetHouseAccessList(lua_State* L)
{
	//getHouseAccessList(houseid, listid)
	uint32_t listid = popNumber(L);
	if(House* house = Houses::getInstance()->getHouse(popNumber(L)))
	{
		std::string list;
		if(house->getAccessList(listid, list))
			lua_pushstring(L, list.c_str());
		else
			lua_pushnil(L);
	}
	else
	{
		errorEx(getError(LUA_ERROR_HOUSE_NOT_FOUND));
		lua_pushnil(L);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetHouseByPlayerGUID(lua_State* L)
{
	//getHouseByPlayerGUID(guid)
	if(House* house = Houses::getInstance()->getHouseByPlayerId(popNumber(L)))
		lua_pushnumber(L, house->getId());
	else
		lua_pushnil(L);
	return 1;
}

int32_t LuaScriptInterface::luaSetHouseAccessList(lua_State* L)
{
	//setHouseAccessList(houseid, listid, listtext)
	std::string list = popString(L);
	uint32_t listid = popNumber(L);

	if(House* house = Houses::getInstance()->getHouse(popNumber(L)))
	{
		house->setAccessList(listid, list);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_HOUSE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaSetHouseOwner(lua_State* L)
{
	//setHouseOwner(houseId, owner[, clean])
	bool clean = true;
	if(lua_gettop(L) > 2)
		clean = popNumber(L);

	uint32_t owner = popNumber(L);
	if(House* house = Houses::getInstance()->getHouse(popNumber(L)))
		pushBoolean(L, house->setOwnerEx(owner, clean));
	else
	{
		errorEx(getError(LUA_ERROR_HOUSE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetWorldType(lua_State* L)
{
	lua_pushnumber(L, (uint32_t)g_game.getWorldType());
	return 1;
}

int32_t LuaScriptInterface::luaSetWorldType(lua_State* L)
{
	//setWorldType(type)
	WorldType_t type = (WorldType_t)popNumber(L);
	if(type >= WORLDTYPE_FIRST && type <= WORLDTYPE_LAST)
	{
		g_game.setWorldType(type);
		pushBoolean(L, true);
	}
	else
		pushBoolean(L, false);

	return 1;
}

int32_t LuaScriptInterface::luaGetWorldTime(lua_State* L)
{
	//getWorldTime()
	lua_pushnumber(L, g_game.getLightHour());
	return 1;
}

int32_t LuaScriptInterface::luaGetWorldLight(lua_State* L)
{
	//getWorldLight()
	LightInfo lightInfo;
	g_game.getWorldLightInfo(lightInfo);
	lua_pushnumber(L, lightInfo.level);
	lua_pushnumber(L, lightInfo.color);
	return 2;
}

int32_t LuaScriptInterface::luaGetWorldCreatures(lua_State* L)
{
	//getWorldCreatures(type)
	//0 players, 1 monsters, 2 npcs, 3 all
	uint32_t type = popNumber(L), value;
	switch(type)
	{
		case 0:
			value = g_game.getPlayersOnline();
			break;
		case 1:
			value = g_game.getMonstersOnline();
			break;
		case 2:
			value = g_game.getNpcsOnline();
			break;
		case 3:
			value = g_game.getCreaturesOnline();
			break;
		default:
			pushBoolean(L, false);
			return 1;
	}

	lua_pushnumber(L, value);
	return 1;
}

int32_t LuaScriptInterface::luaGetWorldUpTime(lua_State* L)
{
	//getWorldUpTime()
	uint32_t uptime = 0;
	if(Status* status = Status::getInstance())
		uptime = status->getUptime();

	lua_pushnumber(L, uptime);
	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerLight(lua_State* L)
{
	//getPlayerLight(cid)
	LuaEnvironment* env = getScriptEnv();
	if(const Player* player = env->getPlayerByUID(popNumber(L)))
	{
		LightInfo lightInfo;
		player->getCreatureLight(lightInfo);
		lua_pushnumber(L, lightInfo.level);
		lua_pushnumber(L, lightInfo.color);
		return 2;
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}
}

int32_t LuaScriptInterface::luaGetPlayerSoul(lua_State* L)
{
	//getPlayerSoul(cid[, ignoreModifiers = false])
	bool ignoreModifiers = false;
	if(lua_gettop(L) > 1)
		ignoreModifiers = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(const Player* player = env->getPlayerByUID(popNumber(L)))
		lua_pushnumber(L, ignoreModifiers ? player->soul : player->getSoul());
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerAddExperience(lua_State* L)
{
	//doPlayerAddExperience(cid, amount)
	int64_t amount = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		if(amount > 0)
			player->addExperience(amount);
		else if(amount < 0)
			player->removeExperience(std::abs(amount));
		else
		{
			pushBoolean(L, false);
			return 1;
		}

		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerSlotItem(lua_State* L)
{
	//getPlayerSlotItem(cid, slot)
	uint32_t slot = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(const Player* player = env->getPlayerByUID(popNumber(L)))
	{
		if(Thing* thing = player->__getThing(slot))
			pushThing(L, thing, env->addThing(thing));
		else
			pushThing(L, NULL, 0);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushThing(L, NULL, 0);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerWeapon(lua_State* L)
{
	//getPlayerWeapon(cid[, ignoreAmmo = false])
	bool ignoreAmmo = false;
	if(lua_gettop(L) > 1)
		ignoreAmmo = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		if(Item* weapon = player->getWeapon(ignoreAmmo))
			pushThing(L, weapon, env->addThing(weapon));
		else
			pushThing(L, NULL, 0);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		lua_pushnil(L);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerItemById(lua_State* L)
{
	//getPlayerItemById(cid, deepSearch, itemId[, subType = -1])
	LuaEnvironment* env = getScriptEnv();

	int32_t subType = -1;
	if(lua_gettop(L) > 3)
		subType = (int32_t)popNumber(L);

	int32_t itemId = (int32_t)popNumber(L);
	bool deepSearch = popNumber(L);

	Player* player = env->getPlayerByUID(popNumber(L));
	if(!player)
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushThing(L, NULL, 0);
		return 1;
	}

	Item* item = g_game.findItemOfType(player, itemId, deepSearch, subType);
	if(!item)
	{
		pushThing(L, NULL, 0);
		return 1;
	}

	pushThing(L, item, env->addThing(item));
	return 1;
}

int32_t LuaScriptInterface::luaGetThing(lua_State* L)
{
	//getThing(uid)
	uint32_t uid = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Thing* thing = env->getThingByUID(uid))
		pushThing(L, thing, uid);
	else
	{
		errorEx(getError(LUA_ERROR_THING_NOT_FOUND));
		pushThing(L, NULL, 0);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoTileQueryAdd(lua_State* L)
{
	//doTileQueryAdd(uid, pos[, flags[, displayError = true]])
	uint32_t flags = 0, params = lua_gettop(L);
	bool displayError = true;
	if(params > 3)
		displayError = popNumber(L);

	if(params > 2)
		flags = popNumber(L);

	Position pos = getPosition(L, lua_gettop(L));
	uint32_t uid = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	Tile* tile = g_game.getTile(pos);
	if(!tile)
	{
		if(displayError)
			errorEx(getError(LUA_ERROR_TILE_NOT_FOUND));

		lua_pushnumber(L, (uint32_t)RET_NOTPOSSIBLE);
		return 1;
	}

	Thing* thing = env->getThingByUID(uid);
	if(!thing)
	{
		if(displayError)
			errorEx(getError(LUA_ERROR_THING_NOT_FOUND));

		lua_pushnumber(L, (uint32_t)RET_NOTPOSSIBLE);
		return 1;
	}

	lua_pushnumber(L, (uint32_t)tile->__queryAdd(0, thing, 1, flags));
	return 1;
}

int32_t LuaScriptInterface::luaDoItemRaidUnref(lua_State* L)
{
	//doItemRaidUnref(uid)
	LuaEnvironment* env = getScriptEnv();
	if(Item* item = env->getItemByUID(popNumber(L)))
	{
		if(Raid* raid = item->getRaid())
		{
			raid->unRef();
			item->setRaid(NULL);
			pushBoolean(L, true);
		}
		else
			pushBoolean(L, false);
	}
	else
	{
		errorEx(getError(LUA_ERROR_ITEM_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetThingPosition(lua_State* L)
{
	//getThingPosition(uid)
	LuaEnvironment* env = getScriptEnv();
	if(Thing* thing = env->getThingByUID(popNumber(L)))
	{
		Position pos = thing->getPosition();
		uint32_t stackpos = 0;
		if(Tile* tile = thing->getTile())
			stackpos = tile->__getIndexOfThing(thing);

		pushPosition(L, pos, stackpos);
	}
	else
	{
		errorEx(getError(LUA_ERROR_THING_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaCreateCombatObject(lua_State* L)
{
	//createCombatObject()
	LuaEnvironment* env = getScriptEnv();
	if(env->getScriptId() != EVENT_ID_LOADING)
	{
		errorEx("This function can only be used while loading the script.");
		pushBoolean(L, false);
		return 1;
	}

	Combat* combat = new Combat;
	if(!combat)
	{
		errorEx(getError(LUA_ERROR_COMBAT_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	lua_pushnumber(L, env->addCombatObject(combat));
	return 1;
}

bool LuaScriptInterface::getArea(lua_State* L, std::list<uint32_t>& list, uint32_t& rows)
{
	rows = 0;
	uint32_t i = 0;

	if(!lua_istable(L, -1))
	{
		errorEx("Object on the stack is not a table.");
		return false;
	}

	lua_pushnil(L);
	while(lua_next(L, -2))
	{
		lua_pushnil(L);
		while(lua_next(L, -2))
		{
			list.push_back((uint32_t)lua_tonumber(L, -1));
			lua_pop(L, 1); //removes value, keeps key for next iteration
			++i;
		}

		lua_pop(L, 1); //removes value, keeps key for next iteration
		++rows;
		i = 0;
	}

	lua_pop(L, 1);
	return rows;
}

int32_t LuaScriptInterface::luaCreateCombatArea(lua_State* L)
{
	//createCombatArea( {area}[, {extArea}])
	LuaEnvironment* env = getScriptEnv();
	if(env->getScriptId() != EVENT_ID_LOADING)
	{
		errorEx("This function can only be used while loading the script.");
		pushBoolean(L, false);
		return 1;
	}

	CombatArea* area = new CombatArea;
	if(lua_gettop(L) > 1)
	{
		//has extra parameter with diagonal area information
		uint32_t rowsExtArea;
		std::list<uint32_t> listExtArea;

		getArea(L, listExtArea, rowsExtArea);
		/*setup all possible rotations*/
		area->setupExtArea(listExtArea, rowsExtArea);
	}

	uint32_t rowsArea = 0;
	std::list<uint32_t> listArea;
	getArea(L, listArea, rowsArea);

	area->setupArea(listArea, rowsArea);
	lua_pushnumber(L, env->addCombatArea(area));
	return 1;
}

int32_t LuaScriptInterface::luaCreateConditionObject(lua_State* L)
{
	//createConditionObject(type[, ticks[, buff[, subId]]])
	uint32_t params = lua_gettop(L), subId = 0;
	if(params > 3)
		subId = popNumber(L);

	bool buff = false;
	if(params > 2)
		buff = popNumber(L);

	int32_t ticks = 0;
	if(params > 1)
		ticks = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Condition* condition = Condition::createCondition(CONDITIONID_COMBAT, (ConditionType_t)popNumber(L), ticks, 0, buff, subId))
	{
		if(env->getScriptId() != EVENT_ID_LOADING)
			lua_pushnumber(L, env->addTempConditionObject(condition));
		else
			lua_pushnumber(L, env->addConditionObject(condition));
	}
	else
	{
		errorEx(getError(LUA_ERROR_CONDITION_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaSetCombatArea(lua_State* L)
{
	//setCombatArea(combat, area)
	uint32_t areaId = popNumber(L);
	LuaEnvironment* env = getScriptEnv();
	if(env->getScriptId() != EVENT_ID_LOADING)
	{
		errorEx("This function can only be used while loading the script.");
		pushBoolean(L, false);
		return 1;
	}

	Combat* combat = env->getCombatObject(popNumber(L));
	if(!combat)
	{
		errorEx(getError(LUA_ERROR_COMBAT_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	const CombatArea* area = env->getCombatArea(areaId);
	if(!area)
	{
		errorEx(getError(LUA_ERROR_AREA_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	combat->setArea(new CombatArea(*area));
	pushBoolean(L, true);
	return 1;
}

int32_t LuaScriptInterface::luaSetCombatCondition(lua_State* L)
{
	//setCombatCondition(combat, condition)
	uint32_t conditionId = popNumber(L);
	LuaEnvironment* env = getScriptEnv();

	Combat* combat = env->getCombatObject(popNumber(L));
	if(!combat)
	{
		errorEx(getError(LUA_ERROR_COMBAT_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	const Condition* condition = env->getConditionObject(conditionId);
	if(!condition)
	{
		errorEx(getError(LUA_ERROR_CONDITION_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	combat->setCondition(condition->clone());
	pushBoolean(L, true);
	return 1;
}

int32_t LuaScriptInterface::luaSetCombatParam(lua_State* L)
{
	//setCombatParam(combat, key, value)
	uint32_t value = popNumber(L);
	CombatParam_t key = (CombatParam_t)popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(env->getScriptId() != EVENT_ID_LOADING)
	{
		errorEx("This function can only be used while loading the script.");
		pushBoolean(L, false);
		return 1;
	}

	Combat* combat = env->getCombatObject(popNumber(L));
	if(!combat)
	{
		errorEx(getError(LUA_ERROR_COMBAT_NOT_FOUND));
		pushBoolean(L, false);
	}
	else
	{
		combat->setParam(key, value);
		pushBoolean(L, true);
	}

	return 1;
}

int32_t LuaScriptInterface::luaSetConditionParam(lua_State* L)
{
	//setConditionParam(condition, key, value)
	int32_t value = popNumber(L);
	LuaEnvironment* env = getScriptEnv();

	ConditionParam_t key = (ConditionParam_t)popNumber(L);
	if(Condition* condition = env->getConditionObject(popNumber(L)))
	{
		condition->setParam(key, value);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CONDITION_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaAddDamageCondition(lua_State* L)
{
	//addDamageCondition(condition, rounds, time, value)
	int32_t value = popNumber(L), time = popNumber(L), rounds = popNumber(L);
	LuaEnvironment* env = getScriptEnv();
	if(ConditionDamage* condition = dynamic_cast<ConditionDamage*>(env->getConditionObject(popNumber(L))))
	{
		condition->addDamage(rounds, time, value);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CONDITION_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaAddOutfitCondition(lua_State* L)
{
	//addOutfitCondition(condition, outfit)
	Outfit_t outfit = getOutfit(L);
	LuaEnvironment* env = getScriptEnv();
	if(ConditionOutfit* condition = dynamic_cast<ConditionOutfit*>(env->getConditionObject(popNumber(L))))
	{
		condition->addOutfit(outfit);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CONDITION_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaSetCombatCallBack(lua_State* L)
{
	//setCombatCallBack(combat, key, functionName)
	std::string function = popString(L);
	CallBackParam_t key = (CallBackParam_t)popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(env->getScriptId() != EVENT_ID_LOADING)
	{
		errorEx("This function can only be used while loading the script.");
		pushBoolean(L, false);
		return 1;
	}

	Combat* combat = env->getCombatObject(popNumber(L));
	if(!combat)
	{
		errorEx(getError(LUA_ERROR_COMBAT_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	LuaScriptInterface* interface = env->getScriptInterface();
	combat->setCallback(key);

	CallBack* callback = combat->getCallback(key);
	if(!callback)
	{
		std::stringstream ss;
		ss << key;

		errorEx(ss.str() + " is not a valid callback key.");
		pushBoolean(L, false);
		return 1;
	}

	if(!callback->loadCallBack(interface, function))
	{
		errorEx("Cannot load callback");
		pushBoolean(L, false);
	}
	else
		pushBoolean(L, true);

	return 1;
}

int32_t LuaScriptInterface::luaSetCombatFormula(lua_State* L)
{
	//setCombatFormula(combat, type, mina, minb, maxa, maxb[, minl, maxl[, minm, maxm[, minc[, maxc]]]])
	LuaEnvironment* env = getScriptEnv();
	if(env->getScriptId() != EVENT_ID_LOADING)
	{
		errorEx("This function can only be used while loading the script.");
		pushBoolean(L, false);
		return 1;
	}

	int32_t params = lua_gettop(L), minc = 0, maxc = 0;
	if(params > 11)
		maxc = popNumber(L);

	if(params > 10)
		minc = popNumber(L);

	double minm = g_config.getDouble(ConfigManager::FORMULA_MAGIC), maxm = minm,
		minl = g_config.getDouble(ConfigManager::FORMULA_LEVEL), maxl = minl;
	if(params > 8)
	{
		maxm = popFloatNumber(L);
		minm = popFloatNumber(L);
	}

	if(params > 6)
	{
		maxl = popFloatNumber(L);
		minl = popFloatNumber(L);
	}

	double maxb = popFloatNumber(L), maxa = popFloatNumber(L),
		minb = popFloatNumber(L), mina = popFloatNumber(L);
	formulaType_t type = (formulaType_t)popNumber(L);
	if(Combat* combat = env->getCombatObject(popNumber(L)))
	{
		combat->setPlayerCombatValues(type, mina, minb, maxa, maxb, minl, maxl, minm, maxm, minc, maxc);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_COMBAT_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaSetConditionFormula(lua_State* L)
{
	//setConditionFormula(condition, mina, minb, maxa, maxb)
	double maxb = popFloatNumber(L), maxa = popFloatNumber(L),
		minb = popFloatNumber(L), mina = popFloatNumber(L);
	LuaEnvironment* env = getScriptEnv();
	if(ConditionSpeed* condition = dynamic_cast<ConditionSpeed*>(env->getConditionObject(popNumber(L))))
	{
		condition->setFormulaVars(mina, minb, maxa, maxb);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CONDITION_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoCombat(lua_State* L)
{
	//doCombat(cid, combat, param)
	LuaEnvironment* env = getScriptEnv();

	LuaVariant var = popVariant(L);
	uint32_t combatId = popNumber(L), cid = popNumber(L);

	Creature* creature = NULL;
	if(cid != 0)
	{
		creature = env->getCreatureByUID(cid);
		if(!creature)
		{
			errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
			pushBoolean(L, false);
			return 1;
		}
	}

	const Combat* combat = env->getCombatObject(combatId);
	if(!combat)
	{
		errorEx(getError(LUA_ERROR_COMBAT_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	if(var.type == VARIANT_NONE)
	{
		errorEx(getError(LUA_ERROR_VARIANT_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	switch(var.type)
	{
		case VARIANT_NUMBER:
		{
			Creature* target = g_game.getCreatureByID(var.number);
			if(!target || !creature || !creature->canSeeCreature(target))
			{
				pushBoolean(L, false);
				return 1;
			}

			if(combat->hasArea())
				combat->doCombat(creature, target->getPosition());
			else
				combat->doCombat(creature, target);

			break;
		}

		case VARIANT_POSITION:
		{
			combat->doCombat(creature, var.pos);
			break;
		}

		case VARIANT_TARGETPOSITION:
		{
			if(!combat->hasArea())
			{
				combat->postCombatEffects(creature, var.pos);
				g_game.addMagicEffect(var.pos, MAGIC_EFFECT_POFF);
			}
			else
				combat->doCombat(creature, var.pos);

			break;
		}

		case VARIANT_STRING:
		{
			Player* target = g_game.getPlayerByName(var.text);
			if(!target || !creature || !creature->canSeeCreature(target))
			{
				pushBoolean(L, false);
				return 1;
			}

			combat->doCombat(creature, target);
			break;
		}

		default:
		{
			errorEx(getError(LUA_ERROR_VARIANT_UNKNOWN));
			pushBoolean(L, false);
			return 1;
		}
	}

	pushBoolean(L, true);
	return 1;
}

int32_t LuaScriptInterface::luaDoCombatAreaHealth(lua_State* L)
{
	//doCombatAreaHealth(cid, type, pos, area, min, max, effect)
	MagicEffect_t effect = (MagicEffect_t)popNumber(L);
	int32_t maxChange = (int32_t)popNumber(L), minChange = (int32_t)popNumber(L);
	uint32_t areaId = popNumber(L);

	Position pos = getPosition(L, lua_gettop(L));

	CombatType_t combatType = (CombatType_t)popNumber(L);
	uint32_t cid = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	Creature* creature = NULL;
	if(cid)
	{
		if(!(creature = env->getCreatureByUID(cid)))
		{
			errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
			pushBoolean(L, false);
			return 1;
		}
	}

	const CombatArea* area = env->getCombatArea(areaId);
	if(area || !areaId)
	{
		CombatParams params;
		params.combatType = combatType;
		params.effects.impact = effect;

		Combat::doCombatHealth(creature, pos, area, minChange, maxChange, params);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_AREA_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoTargetCombatHealth(lua_State* L)
{
	//doTargetCombatHealth(cid, target, type, min, max, effect)
	MagicEffect_t effect = (MagicEffect_t)popNumber(L);
	int32_t maxChange = (int32_t)popNumber(L), minChange = (int32_t)popNumber(L);

	CombatType_t combatType = (CombatType_t)popNumber(L);
	uint32_t targetCid = popNumber(L), cid = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	Creature* creature = NULL;
	if(cid)
	{
		if(!(creature = env->getCreatureByUID(cid)))
		{
			errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
			pushBoolean(L, false);
			return 1;
		}
	}

	Creature* target = env->getCreatureByUID(targetCid);
	if(target)
	{
		CombatParams params;
		params.combatType = combatType;
		params.effects.impact = effect;

		Combat::doCombatHealth(creature, target, minChange, maxChange, params);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoCombatAreaMana(lua_State* L)
{
	//doCombatAreaMana(cid, pos, area, min, max, effect)
	MagicEffect_t effect = (MagicEffect_t)popNumber(L);
	int32_t maxChange = (int32_t)popNumber(L), minChange = (int32_t)popNumber(L);
	uint32_t areaId = popNumber(L);

	Position pos = getPosition(L, lua_gettop(L));
	uint32_t cid = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	Creature* creature = NULL;
	if(cid)
	{
		if(!(creature = env->getCreatureByUID(cid)))
		{
			errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
			pushBoolean(L, false);
			return 1;
		}
	}

	const CombatArea* area = env->getCombatArea(areaId);
	if(area || !areaId)
	{
		CombatParams params;
		params.effects.impact = effect;

		Combat::doCombatMana(creature, pos, area, minChange, maxChange, params);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_AREA_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoTargetCombatMana(lua_State* L)
{
	//doTargetCombatMana(cid, target, min, max, effect)
	MagicEffect_t effect = (MagicEffect_t)popNumber(L);
	int32_t maxChange = (int32_t)popNumber(L), minChange = (int32_t)popNumber(L);
	uint32_t targetCid = popNumber(L), cid = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	Creature* creature = NULL;
	if(cid)
	{
		if(!(creature = env->getCreatureByUID(cid)))
		{
			errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
			pushBoolean(L, false);
			return 1;
		}
	}

	if(Creature* target = env->getCreatureByUID(targetCid))
	{
		CombatParams params;
		params.effects.impact = effect;

		Combat::doCombatMana(creature, target, minChange, maxChange, params);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoCombatAreaCondition(lua_State* L)
{
	//doCombatAreaCondition(cid, pos, area, condition, effect)
	MagicEffect_t effect = (MagicEffect_t)popNumber(L);
	uint32_t conditionId = popNumber(L), areaId = popNumber(L);

	Position pos = getPosition(L, lua_gettop(L));
	uint32_t cid = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	Creature* creature = NULL;
	if(cid)
	{
		if(!(creature = env->getCreatureByUID(cid)))
		{
			errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
			pushBoolean(L, false);
			return 1;
		}
	}

	if(const Condition* condition = env->getConditionObject(conditionId))
	{
		const CombatArea* area = env->getCombatArea(areaId);
		if(area || !areaId)
		{
			CombatParams params;
			params.effects.impact = effect;
			params.conditionList.push_back(condition);

			Combat::doCombatCondition(creature, pos, area, params);
			pushBoolean(L, true);
		}
		else
		{
			errorEx(getError(LUA_ERROR_AREA_NOT_FOUND));
			pushBoolean(L, false);
		}
	}
	else
	{
		errorEx(getError(LUA_ERROR_CONDITION_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoTargetCombatCondition(lua_State* L)
{
	//doTargetCombatCondition(cid, target, condition, effect)
	MagicEffect_t effect = (MagicEffect_t)popNumber(L);
	uint32_t conditionId = popNumber(L), targetCid = popNumber(L), cid = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	Creature* creature = NULL;
	if(cid)
	{
		if(!(creature = env->getCreatureByUID(cid)))
		{
			errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
			pushBoolean(L, false);
			return 1;
		}
	}

	if(Creature* target = env->getCreatureByUID(targetCid))
	{
		if(const Condition* condition = env->getConditionObject(conditionId))
		{
			CombatParams params;
			params.effects.impact = effect;
			params.conditionList.push_back(condition);

			Combat::doCombatCondition(creature, target, params);
			pushBoolean(L, true);
		}
		else
		{
			errorEx(getError(LUA_ERROR_CONDITION_NOT_FOUND));
			pushBoolean(L, false);
		}
	}
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoCombatAreaDispel(lua_State* L)
{
	//doCombatAreaDispel(cid, pos, area, type, effect)
	MagicEffect_t effect = (MagicEffect_t)popNumber(L);
	ConditionType_t dispelType = (ConditionType_t)popNumber(L);
	uint32_t areaId = popNumber(L);

	Position pos = getPosition(L, lua_gettop(L));
	uint32_t cid = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	Creature* creature = NULL;
	if(cid)
	{
		if(!(creature = env->getCreatureByUID(cid)))
		{
			errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
			pushBoolean(L, false);
			return 1;
		}
	}

	const CombatArea* area = env->getCombatArea(areaId);
	if(area || !areaId)
	{
		CombatParams params;
		params.effects.impact = effect;
		params.dispelType = dispelType;

		Combat::doCombatDispel(creature, pos, area, params);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_AREA_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoTargetCombatDispel(lua_State* L)
{
	//doTargetCombatDispel(cid, target, type, effect)
	MagicEffect_t effect = (MagicEffect_t)popNumber(L);
	ConditionType_t dispelType = (ConditionType_t)popNumber(L);
	uint32_t targetCid = popNumber(L), cid = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	Creature* creature = NULL;
	if(cid)
	{
		if(!(creature = env->getCreatureByUID(cid)))
		{
			errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
			pushBoolean(L, false);
			return 1;
		}
	}

	if(Creature* target = env->getCreatureByUID(targetCid))
	{
		CombatParams params;
		params.effects.impact = effect;
		params.dispelType = dispelType;

		Combat::doCombatDispel(creature, target, params);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoChallengeCreature(lua_State* L)
{
	//doChallengeCreature(cid, target)
	LuaEnvironment* env = getScriptEnv();
	uint32_t targetCid = popNumber(L);

	Creature* creature = env->getCreatureByUID(popNumber(L));
	if(!creature)
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	Creature* target = env->getCreatureByUID(targetCid);
	if(!target)
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	target->challengeCreature(creature);
	pushBoolean(L, true);
	return 1;
}

int32_t LuaScriptInterface::luaDoSummonMonster(lua_State* L)
{
	//doSummonMonster(cid, name)
	std::string name = popString(L);

	LuaEnvironment* env = getScriptEnv();
	Creature* creature = env->getCreatureByUID(popNumber(L));
	if(!creature)
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	lua_pushnumber(L, g_game.placeSummon(creature, name));
	return 1;
}

int32_t LuaScriptInterface::luaDoConvinceCreature(lua_State* L)
{
	//doConvinceCreature(cid, target)
	uint32_t cid = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	Creature* creature = env->getCreatureByUID(popNumber(L));
	if(!creature)
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	Creature* target = env->getCreatureByUID(cid);
	if(!target)
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	target->convinceCreature(creature);
	pushBoolean(L, true);
	return 1;
}

int32_t LuaScriptInterface::luaGetMonsterTargetList(lua_State* L)
{
	//getMonsterTargetList(cid)
	LuaEnvironment* env = getScriptEnv();
	Creature* creature = env->getCreatureByUID(popNumber(L));
	if(!creature)
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	Monster* monster = creature->getMonster();
	if(!monster)
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	const CreatureList& targetList = monster->getTargetList();
	CreatureList::const_iterator it = targetList.begin();

	lua_newtable(L);
	for(uint32_t i = 1; it != targetList.end(); ++it, ++i)
	{
		if(monster->isTarget(*it))
		{
			lua_pushnumber(L, i);
			lua_pushnumber(L, env->addThing(*it));
			pushTable(L);
		}
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetMonsterFriendList(lua_State* L)
{
	//getMonsterFriendList(cid)
	LuaEnvironment* env = getScriptEnv();
	Creature* creature = env->getCreatureByUID(popNumber(L));
	if(!creature)
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	Monster* monster = creature->getMonster();
	if(!monster)
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	Creature* friendCreature;
	const CreatureList& friendList = monster->getFriendList();
	CreatureList::const_iterator it = friendList.begin();

	lua_newtable(L);
	for(uint32_t i = 1; it != friendList.end(); ++it, ++i)
	{
		friendCreature = (*it);
		if(!friendCreature->isRemoved() && friendCreature->getPosition().z == monster->getPosition().z)
		{
			lua_pushnumber(L, i);
			lua_pushnumber(L, env->addThing(*it));
			pushTable(L);
		}
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoMonsterSetTarget(lua_State* L)
{
	//doMonsterSetTarget(cid, target)
	uint32_t targetId = popNumber(L);
	LuaEnvironment* env = getScriptEnv();

	Creature* creature = env->getCreatureByUID(popNumber(L));
	if(!creature)
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	Monster* monster = creature->getMonster();
	if(!monster)
	{
		errorEx(getError(LUA_ERROR_MONSTER_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	Creature* target = env->getCreatureByUID(targetId);
	if(!target)
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	if(!monster->isSummon())
		pushBoolean(L, monster->selectTarget(target));
	else
		pushBoolean(L, false);

	return 1;
}

int32_t LuaScriptInterface::luaDoMonsterChangeTarget(lua_State* L)
{
	//doMonsterChangeTarget(cid)
	LuaEnvironment* env = getScriptEnv();
	Creature* creature = env->getCreatureByUID(popNumber(L));
	if(!creature)
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	Monster* monster = creature->getMonster();
	if(!monster)
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	if(!monster->isSummon())
		monster->searchTarget(TARGETSEARCH_RANDOM);

	pushBoolean(L, true);
	return 1;
}

int32_t LuaScriptInterface::luaGetMonsterInfo(lua_State* L)
{
	//getMonsterInfo(name)
	const MonsterType* mType = g_monsters.getMonsterType(popString(L));
	if(!mType)
	{
		errorEx(getError(LUA_ERROR_MONSTER_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	lua_newtable(L);
	setField(L, "name", mType->name.c_str());
	setField(L, "description", mType->nameDescription.c_str());
	setField(L, "experience", mType->experience);
	setField(L, "health", mType->health);
	setField(L, "healthMax", mType->healthMax);
	setField(L, "manaCost", mType->manaCost);
	setField(L, "defense", mType->defense);
	setField(L, "armor", mType->armor);
	setField(L, "baseSpeed", mType->baseSpeed);
	setField(L, "lookCorpse", mType->lookCorpse);
	setField(L, "race", mType->race);
	setField(L, "skull", mType->skull);
	setField(L, "partyShield", mType->partyShield);
	setField(L, "guildEmblem", mType->guildEmblem);
	setFieldBool(L, "summonable", mType->isSummonable);
	setFieldBool(L, "illusionable", mType->isIllusionable);
	setFieldBool(L, "convinceable", mType->isConvinceable);
	setFieldBool(L, "attackable", mType->isAttackable);
	setFieldBool(L, "hostile", mType->isHostile);

	lua_pushstring(L, "outfit"); // name the table created by pushOutfit
	pushOutfit(L, mType->outfit);
	pushTable(L);
	createTable(L, "defenses");

	uint32_t i = 1; 
	for(auto it = mType->spellDefenseList.begin(); it != mType->spellDefenseList.end(); ++it, ++i)
	{
		createTable(L, i);
		setField(L, "speed", it->speed);
		setField(L, "chance", it->chance);
		setField(L, "range", it->range);

		setField(L, "minCombatValue", it->minCombatValue);
		setField(L, "maxCombatValue", it->maxCombatValue);
		setFieldBool(L, "isMelee", it->isMelee);
		pushTable(L);
	}

	pushTable(L);
	createTable(L, "attacks");

	i = 1;	// O.O
	for(auto it = mType->spellAttackList.begin(); it != mType->spellAttackList.end(); ++it, ++i)
	{
		createTable(L, i);
		setField(L, "speed", it->speed);
		setField(L, "chance", it->chance);
		setField(L, "range", it->range);

		setField(L, "minCombatValue", it->minCombatValue);
		setField(L, "maxCombatValue", it->maxCombatValue);
		setFieldBool(L, "isMelee", it->isMelee);
		pushTable(L);
	}

	pushTable(L);
	createTable(L, "loot");
	i = 1;	// -.O
	for(auto it = mType->lootItems.begin(); it != mType->lootItems.end(); ++it, ++i)
	{
		createTable(L, i);
		
		setField(L, "id", it->id);
		setField(L, "count", it->countmax);
		setField(L, "chance", it->chance);
		setField(L, "subType", it->subType);
		setField(L, "actionId", it->actionId);
		setField(L, "uniqueId", it->uniqueId);
		setField(L, "text", it->text);

		if(it->childLoot.size() > 0)
		{
			createTable(L, "child");
			uint32_t j = 1;
			for(auto cit = it->childLoot.begin(); cit != it->childLoot.end(); ++cit, ++j)
			{
				createTable(L, j);				
				setField(L, "id", cit->id);
				setField(L, "count", cit->countmax);
				setField(L, "chance", cit->chance);
				setField(L, "subType", cit->subType);
				setField(L, "actionId", cit->actionId);
				setField(L, "uniqueId", cit->uniqueId);
				setField(L, "text", cit->text);

				pushTable(L);
			}

			pushTable(L);
		}

		pushTable(L);
	}

	pushTable(L);
	createTable(L, "summons");
	i = 1;	// O.-
	for(auto sit = mType->summonList.begin(); sit != mType->summonList.end(); ++sit, ++i)
	{
		createTable(L, i);
		setField(L, "name", sit->name);
		setField(L, "chance", sit->chance);

		setField(L, "interval", sit->interval);
		setField(L, "amount", sit->amount);
		pushTable(L);
	}

	pushTable(L);
	return 1;
}

int32_t LuaScriptInterface::luaGetTalkActionList(lua_State* L)
{
	//getTalkactionList()
	lua_newtable(L);

	TalkActionsMap::const_iterator it = g_talkActions->getFirstTalk();
	for(uint32_t i = 1; it != g_talkActions->getLastTalk(); ++it, ++i)
	{
		createTable(L, i);
		setField(L, "words", it->first);
		setField(L, "access", it->second->getAccess());

		setFieldBool(L, "log", it->second->isLogged());
		setFieldBool(L, "logged", it->second->isLogged());
		setFieldBool(L, "hide", it->second->isHidden());
		setFieldBool(L, "hidden", it->second->isHidden());

		setField(L, "functionName", it->second->getFunctionName());
		setField(L, "channel", it->second->getChannel());
		pushTable(L);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetExperienceStageList(lua_State* L)
{
	//getExperienceStageList()
	if(!g_config.getBool(ConfigManager::EXPERIENCE_STAGES))
	{
		pushBoolean(L, false);
		return true;
	}

	StageList::const_iterator it = g_game.getFirstStage();
	lua_newtable(L);
	for(uint32_t i = 1; it != g_game.getLastStage(); ++it, ++i)
	{
		createTable(L, i);
		setField(L, "level", it->first);
		setFieldFloat(L, "multiplier", it->second);
		pushTable(L);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoAddCondition(lua_State* L)
{
	//doAddCondition(cid, condition)
	uint32_t conditionId = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	Creature* creature = env->getCreatureByUID(popNumber(L));
	if(!creature)
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	Condition* condition = env->getConditionObject(conditionId);
	if(!condition)
	{
		errorEx(getError(LUA_ERROR_CONDITION_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	creature->addCondition(condition->clone());
	pushBoolean(L, true);
	return 1;
}

int32_t LuaScriptInterface::luaDoRemoveCondition(lua_State* L)
{
	//doRemoveCondition(cid, type[, subId])
	uint32_t subId = 0;
	if(lua_gettop(L) > 2)
		subId = popNumber(L);

	ConditionType_t conditionType = (ConditionType_t)popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	Creature* creature = env->getCreatureByUID(popNumber(L));
	if(!creature)
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	Condition* condition = NULL;
	while((condition = creature->getCondition(conditionType, CONDITIONID_COMBAT, subId)))
		creature->removeCondition(condition);

	while((condition = creature->getCondition(conditionType, CONDITIONID_DEFAULT, subId)))
		creature->removeCondition(condition);

	pushBoolean(L, true);
	return 1;
}

int32_t LuaScriptInterface::luaDoRemoveConditions(lua_State* L)
{
	//doRemoveConditions(cid[, onlyPersistent])
	bool onlyPersistent = true;
	if(lua_gettop(L) > 1)
		onlyPersistent = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	Creature* creature = env->getCreatureByUID(popNumber(L));
	if(!creature)
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	creature->removeConditions(CONDITIONEND_ABORT, onlyPersistent);
	pushBoolean(L, true);
	return 1;
}

// Variant
int LuaScriptInterface::luaVariantCreate(lua_State* L)
{
	// Variant(number or string or position or thing)
	LuaVariant variant;
	if (isUserdata(L, 2)) {
		if (Thing* thing = getThing(L, 2)) {
			variant.type = VARIANT_TARGETPOSITION;
			variant.pos = thing->getPosition();
		}
	} else if (lua_istable(L, 2)) {
		variant.type = VARIANT_POSITION;
		variant.pos = getPosition(L, 2);
	} else if (isNumber(L, 2)) {
		variant.type = VARIANT_NUMBER;
		variant.number = getNumber<uint32_t>(L, 2);
	} else if (isString(L, 2)) {
		variant.type = VARIANT_STRING;
		variant.text = getString(L, 2);
	}
	pushVariant(L, variant);
	return 1;
}

int32_t LuaScriptInterface::luaNumberToVariant(lua_State* L)
{
	//numberToVariant(number)
	LuaVariant var;
	var.type = VARIANT_NUMBER;
	var.number = popNumber(L);

	LuaScriptInterface::pushVariant(L, var);
	return 1;
}

int32_t LuaScriptInterface::luaStringToVariant(lua_State* L)
{
	//stringToVariant(string)
	LuaVariant var;
	var.type = VARIANT_STRING;
	var.text = popString(L);

	LuaScriptInterface::pushVariant(L, var);
	return 1;
}

int32_t LuaScriptInterface::luaPositionToVariant(lua_State* L)
{
	//positionToVariant(pos)
	LuaVariant var;
	var.type = VARIANT_POSITION;
	var.pos = getPosition(L, lua_gettop(L));
	
	LuaScriptInterface::pushVariant(L, var);
	return 1;
}

int32_t LuaScriptInterface::luaTargetPositionToVariant(lua_State* L)
{
	//targetPositionToVariant(pos)
	LuaVariant var;
	var.type = VARIANT_TARGETPOSITION;
	var.pos = getPosition(L, lua_gettop(L));

	LuaScriptInterface::pushVariant(L, var);
	return 1;
}

int32_t LuaScriptInterface::luaVariantToNumber(lua_State* L)
{
	//variantToNumber(var)
	LuaVariant var = popVariant(L);

	uint32_t number = 0;
	if(var.type == VARIANT_NUMBER)
		number = var.number;

	lua_pushnumber(L, number);
	return 1;
}

int32_t LuaScriptInterface::luaVariantToString(lua_State* L)
{
	//variantToString(var)
	LuaVariant var = popVariant(L);

	std::string text = "";
	if(var.type == VARIANT_STRING)
		text = var.text;

	lua_pushstring(L, text.c_str());
	return 1;
}

int32_t LuaScriptInterface::luaVariantToPosition(lua_State* L)
{
	//luaVariantToPosition(var)
	LuaVariant var = popVariant(L);

	Position pos;
	if(var.type == VARIANT_POSITION || var.type == VARIANT_TARGETPOSITION)
		pos = var.pos;

	pushPosition(L, pos, pos.stackpos);
	return 1;
}

int32_t LuaScriptInterface::luaDoChangeSpeed(lua_State* L)
{
	//doChangeSpeed(cid, delta)
	int32_t delta = (int32_t)popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
	{
		g_game.changeSpeed(creature, delta);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaSetCreatureOutfit(lua_State* L)
{
	//doSetCreatureOutfit(cid, outfit[, time = -1])
	int32_t time = -1;
	if(lua_gettop(L) > 2)
		time = (int32_t)popNumber(L);

	Outfit_t outfit = getOutfit(L);
	LuaEnvironment* env = getScriptEnv();

	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
		pushBoolean(L, Spell::CreateIllusion(creature, outfit, time) == RET_NOERROR);
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetCreatureOutfit(lua_State* L)
{
	//getCreatureOutfit(cid)
	LuaEnvironment* env = getScriptEnv();
	if(const Creature* creature = env->getCreatureByUID(popNumber(L)))
		pushOutfit(L, creature->getCurrentOutfit());
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaSetMonsterOutfit(lua_State* L)
{
	//doSetMonsterOutfit(cid, name[, time = -1])
	int32_t time = -1;
	if(lua_gettop(L) > 2)
		time = (int32_t)popNumber(L);

	std::string name = popString(L);
	LuaEnvironment* env = getScriptEnv();

	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
		pushBoolean(L, Spell::CreateIllusion(creature, name, time) == RET_NOERROR);
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaSetItemOutfit(lua_State* L)
{
	//doSetItemOutfit(cid, item[, time = -1])
	int32_t time = -1;
	if(lua_gettop(L) > 2)
		time = (int32_t)popNumber(L);

	uint32_t item = (uint32_t)popNumber(L);
	LuaEnvironment* env = getScriptEnv();

	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
		pushBoolean(L, Spell::CreateIllusion(creature, item, time) == RET_NOERROR);
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetStorage(lua_State* L)
{
	//getStorage(key)
	LuaEnvironment* env = getScriptEnv();
	std::string strValue;
	if(env->getStorage(popString(L), strValue))
	{
		int32_t intValue = atoi(strValue.c_str());
		if(intValue || strValue == "0")
			lua_pushnumber(L, intValue);
		else
			lua_pushstring(L, strValue.c_str());
	}
	else
		lua_pushnumber(L, -1);

	return 1;
}

int32_t LuaScriptInterface::luaDoSetStorage(lua_State* L)
{
	//doSetStorage(key, value)
	std::string value;
	bool nil = false;
	if(lua_isnil(L, -1))
	{
		nil = true;
		lua_pop(L, 1);
	}
	else
		value = popString(L);

	LuaEnvironment* env = getScriptEnv();
	if(!nil)
		env->setStorage(popString(L), value);
	else
		env->eraseStorage(popString(L));

	pushBoolean(L, true);
	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerDepotItems(lua_State* L)
{
	//getPlayerDepotItems(cid, depotid)
	uint32_t depotid = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		if(const Depot* depot = player->getDepot(depotid, true))
			lua_pushnumber(L, depot->getItemHoldingCount());
		else
			pushBoolean(L, false);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSetGuildId(lua_State* L)
{
	//doPlayerSetGuildId(cid, id)
	uint32_t id = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		if(player->guildId)
		{
			player->leaveGuild();
			if(!id)
				pushBoolean(L, true);
			else if(IOGuild::getInstance()->guildExists(id))
				pushBoolean(L, IOGuild::getInstance()->joinGuild(player, id));
			else
				pushBoolean(L, false);
		}
		else if(id && IOGuild::getInstance()->guildExists(id))
			pushBoolean(L, IOGuild::getInstance()->joinGuild(player, id));
		else
			pushBoolean(L, false);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSetGuildLevel(lua_State* L)
{
	//doPlayerSetGuildLevel(cid, level[, rank])
	uint32_t rank = 0;
	if(lua_gettop(L) > 2)
		rank = popNumber(L);

	GuildLevel_t level = (GuildLevel_t)popNumber(L);
	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
		pushBoolean(L, player->setGuildLevel(level, rank));
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSetGuildNick(lua_State* L)
{
	//doPlayerSetGuildNick(cid, nick)
	std::string nick = popString(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		player->setGuildNick(nick);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetGuildId(lua_State* L)
{
	//getGuildId(guildName)
	uint32_t guildId;
	if(IOGuild::getInstance()->getGuildId(guildId, popString(L)))
		lua_pushnumber(L, guildId);
	else
		pushBoolean(L, false);

	return 1;
}

int32_t LuaScriptInterface::luaGetGuildMotd(lua_State* L)
{
	//getGuildMotd(guildId)
	uint32_t guildId = popNumber(L);
	if(IOGuild::getInstance()->guildExists(guildId))
		lua_pushstring(L, IOGuild::getInstance()->getMotd(guildId).c_str());
	else
		pushBoolean(L, false);

	return 1;
}

int32_t LuaScriptInterface::luaDoMoveCreature(lua_State* L)
{
	//doMoveCreature(cid, direction[, flag = FLAG_NOLIMIT])
	uint32_t flags = FLAG_NOLIMIT;
	if(lua_gettop(L) > 2)
		flags = popNumber(L);

	int32_t direction = popNumber(L);
	if(direction < NORTH || direction > NORTHEAST)
	{
		pushBoolean(L, false);
		return 1;
	}

	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
		lua_pushnumber(L, g_game.internalMoveCreature(creature, (Direction)direction, flags));
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoSteerCreature(lua_State* L)
{
	//doSteerCreature(cid, position)
	Position pos = getPosition(L, lua_gettop(L));

	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
		pushBoolean(L, g_game.steerCreature(creature, pos));
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaIsCreature(lua_State* L)
{
	//isCreature(cid)
	LuaEnvironment* env = getScriptEnv();
	pushBoolean(L, env->getCreatureByUID(popNumber(L)) ? true : false);
	return 1;
}

int32_t LuaScriptInterface::luaIsContainer(lua_State* L)
{
	//isContainer(uid)
	LuaEnvironment* env = getScriptEnv();
	pushBoolean(L, env->getContainerByUID(popNumber(L)) ? true : false);
	return 1;
}

int32_t LuaScriptInterface::luaIsMovable(lua_State* L)
{
	//isMovable(uid)
	LuaEnvironment* env = getScriptEnv();
	Thing* thing = env->getThingByUID(popNumber(L));
	if(thing && thing->isPushable())
		pushBoolean(L, true);
	else
		pushBoolean(L, false);

	return 1;
}

int32_t LuaScriptInterface::luaGetCreatureByName(lua_State* L)
{
	//getCreatureByName(name)
	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = g_game.getCreatureByName(popString(L)))
		lua_pushnumber(L, env->addThing(creature));
	else
		lua_pushnil(L);

	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerByGUID(lua_State* L)
{
	//getPlayerByGUID(guid)
	LuaEnvironment* env = getScriptEnv();
	if(Player* player = g_game.getPlayerByGuid(popNumber(L)))
		lua_pushnumber(L, env->addThing(player));
	else
		lua_pushnil(L);

	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerByNameWildcard(lua_State* L)
{
	//getPlayerByNameWildcard(name~[, ret = false])
	Player* player = NULL;
	bool pushRet = false;
	if(lua_gettop(L) > 1)
		pushRet = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	ReturnValue ret = g_game.getPlayerByNameWildcard(popString(L), player);
	if(ret == RET_NOERROR)
		lua_pushnumber(L, env->addThing(player));
	else if(pushRet)
		lua_pushnumber(L, ret);
	else
		lua_pushnil(L);

	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerGUIDByName(lua_State* L)
{
	//getPlayerGUIDByName(name[, multiworld = false])
	bool multiworld = false;
	if(lua_gettop(L) > 1)
		multiworld = popNumber(L);

	std::string name = popString(L);
	uint32_t guid;
	if(Player* player = g_game.getPlayerByName(name.c_str()))
		lua_pushnumber(L, player->getGUID());
	else if(IOLoginData::getInstance()->getGuidByName(guid, name, multiworld))
		lua_pushnumber(L, guid);
	else
		lua_pushnil(L);

	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerNameByGUID(lua_State* L)
{
	//getPlayerNameByGUID(guid[, multiworld = false[, displayError = true]])
	int32_t parameters = lua_gettop(L);
	bool multiworld = false, displayError = true;

	if(parameters > 2)
		displayError = popNumber(L);

	if(parameters > 1)
		multiworld = popNumber(L);

	uint32_t guid = popNumber(L);
	std::string name;
	if(!IOLoginData::getInstance()->getNameByGuid(guid, name, multiworld))
	{
		if(displayError)
			errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));

		lua_pushnil(L);
		return 1;
	}

	lua_pushstring(L, name.c_str());
	return 1;
}

int32_t LuaScriptInterface::luaGetPlayersByAccountId(lua_State* L)
{
	//getPlayersByAccountId(accId)
	PlayerVector players = g_game.getPlayersByAccount(popNumber(L));

	LuaEnvironment* env = getScriptEnv();
	PlayerVector::iterator it = players.begin();

	lua_newtable(L);
	for(uint32_t i = 1; it != players.end(); ++it, ++i)
	{
		lua_pushnumber(L, i);
		lua_pushnumber(L, env->addThing(*it));
		pushTable(L);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetIpByName(lua_State* L)
{
	//getIpByName(name)
	std::string name = popString(L);

	if(Player* player = g_game.getPlayerByName(name))
		lua_pushnumber(L, player->getIP());
	else
		lua_pushnumber(L, IOLoginData::getInstance()->getLastIPByName(name));

	return 1;
}

int32_t LuaScriptInterface::luaGetPlayersByIp(lua_State* L)
{
	//getPlayersByIp(ip[, mask])
	uint32_t mask = 0xFFFFFFFF;
	if(lua_gettop(L) > 1)
		mask = (uint32_t)popNumber(L);

	PlayerVector players = g_game.getPlayersByIP(popNumber(L), mask);

	LuaEnvironment* env = getScriptEnv();
	PlayerVector::iterator it = players.begin();

	lua_newtable(L);
	for(uint32_t i = 1; it != players.end(); ++it, ++i)
	{
		lua_pushnumber(L, i);
		lua_pushnumber(L, env->addThing(*it));
		pushTable(L);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetAccountIdByName(lua_State* L)
{
	//getAccountIdByName(name)
	std::string name = popString(L);

	if(Player* player = g_game.getPlayerByName(name))
		lua_pushnumber(L, player->getAccount());
	else
		lua_pushnumber(L, IOLoginData::getInstance()->getAccountIdByName(name));

	return 1;
}

int32_t LuaScriptInterface::luaGetAccountByName(lua_State* L)
{
	//getAccountByName(name)
	std::string name = popString(L);

	if(Player* player = g_game.getPlayerByName(name))
		lua_pushstring(L, player->getAccountName().c_str());
	else
	{
		std::string tmp;
		IOLoginData::getInstance()->getAccountName(IOLoginData::getInstance()->getAccountIdByName(name), tmp);
		lua_pushstring(L, tmp.c_str());
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetAccountIdByAccount(lua_State* L)
{
	//getAccountIdByAccount(accName)
	uint32_t value = 0;
	IOLoginData::getInstance()->getAccountId(popString(L), value);
	lua_pushnumber(L, value);
	return 1;
}

int32_t LuaScriptInterface::luaGetAccountByAccountId(lua_State* L)
{
	//getAccountByAccountId(accId)
	std::string value = 0;
	IOLoginData::getInstance()->getAccountName(popNumber(L), value);
	lua_pushstring(L, value.c_str());
	return 1;
}

int32_t LuaScriptInterface::luaRegisterCreatureEvent(lua_State* L)
{
	//registerCreatureEvent(cid, name)
	std::string name = popString(L);

	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
		pushBoolean(L, creature->registerCreatureEvent(name));
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaUnregisterCreatureEvent(lua_State* L)
{
	//unregisterCreatureEvent(cid, name)
	std::string name = popString(L);

	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
		pushBoolean(L, creature->unregisterCreatureEvent(name));
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetContainerSize(lua_State* L)
{
	//getContainerSize(uid)
	LuaEnvironment* env = getScriptEnv();
	if(Container* container = env->getContainerByUID(popNumber(L)))
		lua_pushnumber(L, container->size());
	else
	{
		errorEx(getError(LUA_ERROR_CONTAINER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetContainerCap(lua_State* L)
{
	//getContainerCap(uid)
	LuaEnvironment* env = getScriptEnv();
	if(Container* container = env->getContainerByUID(popNumber(L)))
		lua_pushnumber(L, container->capacity());
	else
	{
		errorEx(getError(LUA_ERROR_CONTAINER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetContainerItems(lua_State* L)
{
	//getContainerItems(uid)
	LuaEnvironment* env = getScriptEnv();
	if(Container* container = env->getContainerByUID(popNumber(L)))
	{
		ItemList::const_iterator it = container->getItems();
		lua_newtable(L);
		for(int32_t i = 1; it != container->getEnd(); ++it, ++i)
		{
			lua_pushnumber(L, i);
			pushThing(L, *it, env->addThing(*it));
			pushTable(L);
		}
	}
	else
	{
		errorEx(getError(LUA_ERROR_CONTAINER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetContainerItem(lua_State* L)
{
	//getContainerItem(uid, slot)
	uint32_t slot = popNumber(L);
	LuaEnvironment* env = getScriptEnv();
	if(Container* container = env->getContainerByUID(popNumber(L)))
	{
		if(Item* item = container->getItem(slot))
			pushThing(L, item, env->addThing(item));
		else
			pushThing(L, NULL, 0);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CONTAINER_NOT_FOUND));
		pushThing(L, NULL, 0);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoAddContainerItemEx(lua_State* L)
{
	//doAddContainerItemEx(uid, virtuid)
	uint32_t virtuid = popNumber(L);
	LuaEnvironment* env = getScriptEnv();
	if(Container* container = env->getContainerByUID(popNumber(L)))
	{
		Item* item = env->getItemByUID(virtuid);
		if(!item)
		{
			errorEx(getError(LUA_ERROR_ITEM_NOT_FOUND));
			pushBoolean(L, false);
			return 1;
		}

		if(item->getParent() != VirtualCylinder::virtualCylinder)
		{
			pushBoolean(L, false);
			return 1;
		}

		ReturnValue ret = g_game.internalAddItem(NULL, container, item);
		if(ret == RET_NOERROR)
			env->removeTempItem(item);

		lua_pushnumber(L, ret);
		return 1;
	}
	else
	{
		errorEx(getError(LUA_ERROR_CONTAINER_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}
}

int32_t LuaScriptInterface::luaDoAddContainerItem(lua_State* L)
{
	//doAddContainerItem(uid, itemid[, count/subType = 1])
	uint32_t count = 1;
	if(lua_gettop(L) > 2)
		count = popNumber(L);

	uint16_t itemId = popNumber(L);
	LuaEnvironment* env = getScriptEnv();

	Container* container = env->getContainerByUID((uint32_t)popNumber(L));
	if(!container)
	{
		errorEx(getError(LUA_ERROR_CONTAINER_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	const ItemType& it = Item::items[itemId];
	int32_t itemCount = 1, subType = 1;
	if(it.hasSubType())
	{
		if(it.stackable)
			itemCount = (int32_t)std::ceil((float)count / 100);

		subType = count;
	}
	else
		itemCount = std::max((uint32_t)1, count);

	while(itemCount > 0)
	{
		int32_t stackCount = std::min(100, subType);
		Item* newItem = Item::CreateItem(itemId, stackCount);
		if(!newItem)
		{
			errorEx(getError(LUA_ERROR_ITEM_NOT_FOUND));
			pushBoolean(L, false);
			return 1;
		}

		if(it.stackable)
			subType -= stackCount;

		ReturnValue ret = g_game.internalAddItem(NULL, container, newItem);
		if(ret != RET_NOERROR)
		{
			delete newItem;
			pushBoolean(L, false);
			return 1;
		}

		--itemCount;
		if(itemCount)
			continue;

		if(newItem->getParent())
			lua_pushnumber(L, env->addThing(newItem));
		else //stackable item stacked with existing object, newItem will be released
			lua_pushnil(L);

		return 1;
	}

	lua_pushnil(L);
	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerAddOutfit(lua_State *L)
{
	//Consider using doPlayerAddOutfitId instead
	//doPlayerAddOutfit(cid, looktype, addon)
	uint32_t addon = popNumber(L), lookType = popNumber(L);
	LuaEnvironment* env = getScriptEnv();

	Player* player = env->getPlayerByUID((uint32_t)popNumber(L));
	if(!player)
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	Outfit outfit;
	if(Outfits::getInstance()->getOutfit(lookType, outfit))
	{
		pushBoolean(L, player->addOutfit(outfit.outfitId, addon));
		return 1;
	}

	pushBoolean(L, false);
	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerRemoveOutfit(lua_State *L)
{
	//Consider using doPlayerRemoveOutfitId instead
	//doPlayerRemoveOutfit(cid, looktype[, addon = 0])
	uint32_t addon = 0xFF;
	if(lua_gettop(L) > 2)
		addon = popNumber(L);

	uint32_t lookType = popNumber(L);
	LuaEnvironment* env = getScriptEnv();

	Player* player = env->getPlayerByUID((uint32_t)popNumber(L));
	if(!player)
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	Outfit outfit;
	if(Outfits::getInstance()->getOutfit(lookType, outfit))
	{
		pushBoolean(L, player->removeOutfit(outfit.outfitId, addon));
		return 1;
	}

	pushBoolean(L, false);
	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerAddOutfitId(lua_State *L)
{
	//doPlayerAddOutfitId(cid, outfitId, addon)
	uint32_t addon = popNumber(L), outfitId = popNumber(L);
	LuaEnvironment* env = getScriptEnv();

	Player* player = env->getPlayerByUID((uint32_t)popNumber(L));
	if(!player)
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	pushBoolean(L, player->addOutfit(outfitId, addon));
	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerRemoveOutfitId(lua_State *L)
{
	//doPlayerRemoveOutfitId(cid, outfitId[, addon = 0])
	uint32_t addon = 0xFF;
	if(lua_gettop(L) > 2)
		addon = popNumber(L);

	uint32_t outfitId = popNumber(L);
	LuaEnvironment* env = getScriptEnv();

	Player* player = env->getPlayerByUID((uint32_t)popNumber(L));
	if(!player)
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	pushBoolean(L, player->removeOutfit(outfitId, addon));
	return 1;
}

int32_t LuaScriptInterface::luaCanPlayerWearOutfit(lua_State* L)
{
	//CASTnPlayerWearOutfit(cid, looktype[, addon = 0])
	uint32_t addon = 0;
	if(lua_gettop(L) > 2)
		addon = popNumber(L);

	uint32_t lookType = popNumber(L);
	LuaEnvironment* env = getScriptEnv();

	Player* player = env->getPlayerByUID(popNumber(L));
	if(!player)
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	Outfit outfit;
	if(Outfits::getInstance()->getOutfit(lookType, outfit))
	{
		pushBoolean(L, player->canWearOutfit(outfit.outfitId, addon));
		return 1;
	}

	pushBoolean(L, false);
	return 1;
}

int32_t LuaScriptInterface::luaCanPlayerWearOutfitId(lua_State* L)
{
	//CASTnPlayerWearOutfitId(cid, outfitId[, addon = 0])
	uint32_t addon = 0;
	if(lua_gettop(L) > 2)
		addon = popNumber(L);

	uint32_t outfitId = popNumber(L);
	LuaEnvironment* env = getScriptEnv();

	Player* player = env->getPlayerByUID(popNumber(L));
	if(!player)
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	pushBoolean(L, player->canWearOutfit(outfitId, addon));
	return 1;
}

int32_t LuaScriptInterface::luaDoCreatureChangeOutfit(lua_State* L)
{
	//doCreatureChangeOutfit(cid, outfit)
	Outfit_t outfit = getOutfit(L);
	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
	{
		if(Player* player = creature->getPlayer())
			player->changeOutfit(outfit, false);
		else
			creature->defaultOutfit = outfit;

		if(!creature->hasCondition(CONDITION_OUTFIT, 1))
			g_game.internalCreatureChangeOutfit(creature, outfit);

		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerPopupFYI(lua_State* L)
{
	//doPlayerPopupFYI(cid, message)
	std::string message = popString(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		player->sendFYIBox(message);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSendTutorial(lua_State* L)
{
	//doPlayerSendTutorial(cid, id)
	uint8_t id = (uint8_t)popNumber(L);

	LuaEnvironment* env = getScriptEnv();

	Player* player = env->getPlayerByUID(popNumber(L));
	if(!player)
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	player->sendTutorial(id);
	pushBoolean(L, true);
	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSendMailByName(lua_State* L)
{
	//doPlayerSendMailByName(name, item[, town[, actor]])
	LuaEnvironment* env = getScriptEnv();
	int32_t params = lua_gettop(L);

	Creature* actor = NULL;
	if(params > 3)
		actor = env->getCreatureByUID(popNumber(L));

	uint32_t town = 0;
	if(params > 2)
		town = popNumber(L);

	Item* item = env->getItemByUID(popNumber(L));
	if(!item)
	{
		errorEx(getError(LUA_ERROR_ITEM_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	if(item->getParent() != VirtualCylinder::virtualCylinder)
	{
		pushBoolean(L, false);
		return 1;
	}

	pushBoolean(L, IOLoginData::getInstance()->playerMail(actor, popString(L), town, item));
	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerAddMapMark(lua_State* L)
{
	//doPlayerAddMapMark(cid, pos, type[, description])
	std::string description;
	if(lua_gettop(L) > 3)
		description = popString(L);

	MapMarks_t type = (MapMarks_t)popNumber(L);
	Position pos = getPosition(L, lua_gettop(L));

	LuaEnvironment* env = getScriptEnv();
	Player* player = env->getPlayerByUID(popNumber(L));
	if(!player)
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	player->sendAddMarker(pos, type, description);
	pushBoolean(L, true);
	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerAddPremiumDays(lua_State* L)
{
	//doPlayerAddPremiumDays(cid, days)
	int32_t days = popNumber(L);
	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		if(player->premiumDays < 65535)
		{
			Account account = IOLoginData::getInstance()->loadAccount(player->getAccount());
			if(days < 0)
			{
				account.premiumDays = std::max((uint32_t)0, uint32_t(account.premiumDays + (int32_t)days));
				player->premiumDays = std::max((uint32_t)0, uint32_t(player->premiumDays + (int32_t)days));
			}
			else
			{
				account.premiumDays = std::min((uint32_t)65534, uint32_t(account.premiumDays + (uint32_t)days));
				player->premiumDays = std::min((uint32_t)65534, uint32_t(player->premiumDays + (uint32_t)days));
			}
			IOLoginData::getInstance()->saveAccount(account);
		}
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetCreatureLastPosition(lua_State* L)
{
	//getCreatureLastPosition(cid)
	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
		pushPosition(L, creature->getLastPosition(), 0);
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetCreatureName(lua_State* L)
{
	//getCreatureName(cid)
	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
		lua_pushstring(L, creature->getName().c_str());
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetCreaturePathTo(lua_State* L)
{
//getCreaturePathTo(cid, pos, maxSearchDist)
    LuaEnvironment* env = getScriptEnv();
    int32_t maxSearchDist = popNumber(L);
    Position position = getPosition(L, lua_gettop(L));
    Creature* creature = env->getCreatureByUID(popNumber(L));
    if (!creature) {
        lua_pushnil(L);
        return 1;
    }
    std::list<Direction> dirList;
    lua_newtable(L);
    if (g_game.getPathTo(creature, position, dirList, maxSearchDist)) {
        std::list<Direction>::const_iterator it = dirList.begin();
        for (int32_t index = 1; it != dirList.end(); ++it, ++index) {
            lua_pushnumber(L, index);
            lua_pushnumber(L, (*it));
            pushTable(L);
        }
    } else {
        pushBoolean(L, false);
    }
    return 1;
}

int32_t LuaScriptInterface::luaGetCreatureNoMove(lua_State* L)
{
	//getCreatureNoMove(cid)
	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
		pushBoolean(L, creature->getNoMove());
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetCreatureGuildEmblem(lua_State* L)
{
	//getCreatureGuildEmblem(cid[, target])
	uint32_t tid = 0;
	if(lua_gettop(L) > 1)
		tid = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
	{
		if(!tid)
			lua_pushnumber(L, creature->getEmblem());
		else if(Creature* target = env->getCreatureByUID(tid))
			lua_pushnumber(L, creature->getGuildEmblem(target));
		else
		{
			errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
			pushBoolean(L, false);
		}
	}
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoCreatureSetGuildEmblem(lua_State* L)
{
	//doCreatureSetGuildEmblem(cid, emblem)
	GuildEmblems_t emblem = (GuildEmblems_t)popNumber(L);
	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
	{
		creature->setEmblem(emblem);
		g_game.updateCreatureEmblem(creature);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetCreaturePartyShield(lua_State* L)
{
	//getCreaturePartyShield(cid[, target])
	uint32_t tid = 0;
	if(lua_gettop(L) > 1)
		tid = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
	{
		if(!tid)
			lua_pushnumber(L, creature->getShield());
		else if(Creature* target = env->getCreatureByUID(tid))
			lua_pushnumber(L, creature->getPartyShield(target));
		else
		{
			errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
			pushBoolean(L, false);
		}
	}
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoCreatureSetPartyShield(lua_State* L)
{
	//doCreatureSetPartyShield(cid, shield)
	PartyShields_t shield = (PartyShields_t)popNumber(L);
	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
	{
		creature->setShield(shield);
		g_game.updateCreatureShield(creature);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetCreatureSkullType(lua_State* L)
{
	//getCreatureSkullType(cid[, target])
	uint32_t tid = 0;
	if(lua_gettop(L) > 1)
		tid = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
	{
		if(!tid)
			lua_pushnumber(L, creature->getSkull());
		else if(Creature* target = env->getCreatureByUID(tid))
			lua_pushnumber(L, creature->getSkullType(target));
		else
		{
			errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
			pushBoolean(L, false);
		}
	}
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoCreatureSetLookDir(lua_State* L)
{
	//doCreatureSetLookDirection(cid, dir)
	Direction dir = (Direction)popNumber(L);
	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
	{
		if(dir < NORTH || dir > WEST)
		{
			pushBoolean(L, false);
			return 1;
		}

		g_game.internalCreatureTurn(creature, dir);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoCreatureSetSkullType(lua_State* L)
{
	//doCreatureSetSkullType(cid, skull)
	Skulls_t skull = (Skulls_t)popNumber(L);
	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
	{
		creature->setSkull(skull);
		g_game.updateCreatureSkull(creature);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSetSkullEnd(lua_State* L)
{
	//doPlayerSetSkullEnd(cid, time, type)
	Skulls_t _skull = (Skulls_t)popNumber(L);
	time_t _time = (time_t)std::max((int64_t)0, popNumber(L));

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		player->setSkullEnd(_time, false, _skull);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetCreatureSpeed(lua_State* L)
{
	//getCreatureSpeed(cid)
	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
		lua_pushnumber(L, creature->getSpeed());
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetCreatureBaseSpeed(lua_State* L)
{
	//getCreatureBaseSpeed(cid)
	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
		lua_pushnumber(L, creature->getBaseSpeed());
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetCreatureTarget(lua_State* L)
{
	//getCreatureTarget(cid)
	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
	{
		Creature* target = creature->getAttackedCreature();
		lua_pushnumber(L, target ? env->addThing(target) : 0);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaIsSightClear(lua_State* L)
{
	//isSightClear(fromPos, toPos, floorCheck)
	bool floorCheck = popNumber(L);
	Position fromPos = getPosition(L, lua_gettop(L));
	Position toPos = getPosition(L, lua_gettop(L));

	pushBoolean(L, g_game.isSightClear(fromPos, toPos, floorCheck));
	return 1;
}

int32_t LuaScriptInterface::luaIsInArray(lua_State* L)
{
	//isInArray(array, value[, caseSensitive = false])
	bool caseSensitive = false;
	if(lua_gettop(L) > 2)
		caseSensitive = popNumber(L);

	boost::any value;
	if(lua_isnumber(L, -1))
		value = popFloatNumber(L);
	else if(isBoolean(L, -1))
		value = popBoolean(L);
	else if(isString(L, -1))
		value = popString(L);
	else
	{
		lua_pop(L, 1);
		pushBoolean(L, false);
		return 1;
	}

	const std::type_info& type = value.type();
	if(!caseSensitive && type == typeid(std::string))
		value = asLowerCaseString(boost::any_cast<std::string>(value));

	if(!lua_istable(L, -1))
	{
		boost::any data;
		if(lua_isnumber(L, -1))
			data = popFloatNumber(L);
		else if(isBoolean(L, -1))
			data = popBoolean(L);
		else if(isString(L, -1))
			data = popString(L);
		else
		{
			lua_pop(L, 1);
			pushBoolean(L, false);
			return 1;
		}

		if(type != data.type()) // check is it even same data type before searching deeper
			pushBoolean(L, false);
		else if(type == typeid(bool))
			pushBoolean(L, boost::any_cast<bool>(value) == boost::any_cast<bool>(data));
		else if(type == typeid(double))
			pushBoolean(L, boost::any_cast<double>(value) == boost::any_cast<double>(data));
		else if(caseSensitive)
			pushBoolean(L, boost::any_cast<std::string>(value) == boost::any_cast<std::string>(data));
		else
			pushBoolean(L, boost::any_cast<std::string>(value) == asLowerCaseString(boost::any_cast<std::string>(data)));

		return 1;
	}

	lua_pushnil(L);
	while(lua_next(L, -2))
	{
		boost::any data;
		if(lua_isnumber(L, -1))
			data = popFloatNumber(L);
		else if(isBoolean(L, -1))
			data = popBoolean(L);
		else if(isString(L, -1))
			data = popString(L);
		else
		{
			lua_pop(L, 1);
			break;
		}

		if(type != data.type()) // check is it same data type before searching deeper
			continue;

		if(type == typeid(bool))
		{
			if(boost::any_cast<bool>(value) != boost::any_cast<bool>(data))
				continue;

			pushBoolean(L, true);
			return 1;
		}
		else if(type == typeid(double))
		{
			if(boost::any_cast<double>(value) != boost::any_cast<double>(data))
				continue;

			pushBoolean(L, true);
			return 1;
		}
		else if(caseSensitive)
		{
			if(boost::any_cast<std::string>(value) != boost::any_cast<std::string>(data))
				continue;

			pushBoolean(L, true);
			return 1;
		}
		else if(boost::any_cast<std::string>(value) == asLowerCaseString(boost::any_cast<std::string>(data)))
		{
			pushBoolean(L, true);
			return 1;
		}
	}

	lua_pop(L, 2);
	pushBoolean(L, false);
	return 1;
}

int32_t LuaScriptInterface::luaAddEvent(lua_State* L)
{
	//addEvent(callback, delay, ...)
	LuaEnvironment* env = getScriptEnv();
	LuaScriptInterface* interface = env->getScriptInterface();
	if(!interface)
	{
		errorEx("No valid script interface!");
		pushBoolean(L, false);
		return 1;
	}

	int32_t parameters = lua_gettop(L);
	if(!isFunction(L, -parameters)) //-parameters means the first parameter from left to right
	{
		errorEx("Callback parameter should be a function.");
		pushBoolean(L, false);
		return 1;
	}

	std::list<int32_t> params;
	for(int32_t i = 0; i < parameters - 2; ++i) //-2 because addEvent needs at least two parameters
		params.push_back(luaL_ref(L, LUA_REGISTRYINDEX));

	LuaTimerEvent event;
	event.eventId = Scheduler::getInstance().addEvent(createSchedulerTask(std::max((int64_t)SCHEDULER_MINTICKS, popNumber(L)),
		boost::bind(&LuaScriptInterface::executeTimer, interface, ++interface->m_lastTimer)));

	event.parameters = params;
	event.function = luaL_ref(L, LUA_REGISTRYINDEX);
	event.scriptId = env->getScriptId();

	interface->m_timerEvents[interface->m_lastTimer] = event;
	lua_pushnumber(L, interface->m_lastTimer);
	return 1;
}

int32_t LuaScriptInterface::luaStopEvent(lua_State* L)
{
	//stopEvent(eventid)
	uint32_t eventId = popNumber(L);
	LuaEnvironment* env = getScriptEnv();

	LuaScriptInterface* interface = env->getScriptInterface();
	if(!interface)
	{
		errorEx("No valid script interface!");
		pushBoolean(L, false);
		return 1;
	}

	LuaTimerEvents::iterator it = interface->m_timerEvents.find(eventId);
	if(it != interface->m_timerEvents.end())
	{
		Scheduler::getInstance().stopEvent(it->second.eventId);
		for(std::list<int32_t>::iterator lt = it->second.parameters.begin(); lt != it->second.parameters.end(); ++lt)
			luaL_unref(interface->m_luaState, LUA_REGISTRYINDEX, *lt);

		it->second.parameters.clear();
		luaL_unref(interface->m_luaState, LUA_REGISTRYINDEX, it->second.function);

		interface->m_timerEvents.erase(it);
		pushBoolean(L, true);
	}
	else
		pushBoolean(L, false);

	return 1;
}

int32_t LuaScriptInterface::luaGetCreatureCondition(lua_State* L)
{
	//getCreatureCondition(cid, condition[, subId = 0])
	uint32_t subId = 0, condition = 0;
	if(lua_gettop(L) > 2)
		subId = popNumber(L);

	condition = popNumber(L);
	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
		pushBoolean(L, creature->hasCondition((ConditionType_t)condition, subId));
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerBlessing(lua_State* L)
{
	//getPlayerBlessings(cid, blessing)
	int16_t blessing = popNumber(L) - 1;

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
		pushBoolean(L, player->hasBlessing(blessing));
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerAddBlessing(lua_State* L)
{
	//doPlayerAddBlessing(cid, blessing)
	int16_t blessing = popNumber(L) - 1;
	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		if(!player->hasBlessing(blessing))
		{
			player->addBlessing(1 << blessing);
			pushBoolean(L, true);
		}
		else
			pushBoolean(L, false);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSetPromotionLevel(lua_State* L)
{
	//doPlayerSetPromotionLevel(cid, level)
	uint32_t level = popNumber(L);
	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		player->setPromotionLevel(level);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSetGroupId(lua_State* L)
{
	//doPlayerSetGroupId(cid, groupId)
	uint32_t groupId = popNumber(L);
	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		if(Group* group = Groups::getInstance()->getGroup(groupId))
		{
			player->setGroup(group);
			pushBoolean(L, true);
		}
		else
			pushBoolean(L, false);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetCreatureMana(lua_State* L)
{
	//getCreatureMana(cid)
	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
		lua_pushnumber(L, creature->getMana());
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetCreatureMaxMana(lua_State* L)
{
	//getCreatureMaxMana(cid[, ignoreModifiers = false])
	bool ignoreModifiers = false;
	if(lua_gettop(L) > 1)
		ignoreModifiers = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
		lua_pushnumber(L, creature->getPlayer() && ignoreModifiers ? creature->manaMax : creature->getMaxMana());
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

// Creature
int LuaScriptInterface::luaCreatureCreate(lua_State* L)
{
	// Creature(id or name or userdata)
	Creature* creature;
	if (isNumber(L, 2)) {
		creature = g_game.getCreatureByID(getNumber<uint32_t>(L, 2));
	} else if (isString(L, 2)) {
		creature = g_game.getCreatureByName(getString(L, 2));
	} else if (isUserdata(L, 2)) {
		LuaDataType type = getUserdataType(L, 2);
		if (type != LuaData_Player && type != LuaData_Monster && type != LuaData_Npc) {
			lua_pushnil(L);
			return 1;
		}
		creature = getUserdata<Creature>(L, 2);
	} else {
		creature = nullptr;
	}

	if (creature) {
		pushUserdata<Creature>(L, creature);
		setCreatureMetatable(L, -1, creature);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int32_t LuaScriptInterface::luaGetCreatureHealth(lua_State* L)
{
	//getCreatureHealth(cid)
	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
		lua_pushnumber(L, creature->getHealth());
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetCreatureLookDirection(lua_State* L)
{
	//getCreatureLookDirection(cid)
	LuaEnvironment* env = getScriptEnv();

	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
		lua_pushnumber(L, creature->getDirection());
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetCreatureMaxHealth(lua_State* L)
{
	//getCreatureMaxHealth(cid[, ignoreModifiers = false])
	bool ignoreModifiers = false;
	if(lua_gettop(L) > 1)
		ignoreModifiers = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
		lua_pushnumber(L, creature->getPlayer() && ignoreModifiers ? creature->healthMax : creature->getMaxHealth());
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSetStamina(lua_State* L)
{
	//doPlayerSetStamina(cid, minutes)
	uint32_t minutes = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		player->setStaminaMinutes(minutes);
		player->sendStats();
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSetBalance(lua_State* L)
{
	//doPlayerSetBalance(cid, balance)
	uint64_t balance = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		player->balance = balance;
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSetPartner(lua_State* L)
{
	//doPlayerSetPartner(cid, guid)
	uint32_t guid = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		player->marriage = guid;
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerFollowCreature(lua_State* L)
{
	//doPlayerFollowCreature(cid, target)
	LuaEnvironment* env = getScriptEnv();

	Creature* creature = env->getCreatureByUID(popNumber(L));
	if(!creature)
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	Player* player = env->getPlayerByUID(popNumber(L));
	if(!player)
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	pushBoolean(L, g_game.playerFollowCreature(player->getID(), creature->getID()));
	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerParty(lua_State* L)
{
	//getPlayerParty(cid)
	uint32_t cid = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(cid))
	{
		if(Party* party = player->getParty())
			lua_pushnumber(L, env->addThing(party->getLeader()));
		else
			lua_pushnil(L);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerJoinParty(lua_State* L)
{
	//doPlayerJoinParty(cid, lid)
	LuaEnvironment* env = getScriptEnv();

	Player* leader = env->getPlayerByUID(popNumber(L));
	if(!leader)
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	Player* player = env->getPlayerByUID(popNumber(L));
	if(!player)
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	g_game.playerJoinParty(player->getID(), leader->getID());
	pushBoolean(L, true);
	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerLeaveParty(lua_State* L)
{
	//doPlayerLeaveParty(cid[, forced = false])
	bool forced = false;
	if(lua_gettop(L) > 1)
		forced = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	Player* player = env->getPlayerByUID(popNumber(L));
	if(!player)
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	g_game.playerLeaveParty(player->getID(), forced);
	pushBoolean(L, true);
	return 1;
}

int32_t LuaScriptInterface::luaGetPartyMembers(lua_State* L)
{
	//getPartyMembers(cid)
	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		if(Party* party = player->getParty())
		{
			PlayerVector list = party->getMembers();
			list.push_back(party->getLeader());

			PlayerVector::const_iterator it = list.begin();
			lua_newtable(L);
			for(uint32_t i = 1; it != list.end(); ++it, ++i)
			{
				lua_pushnumber(L, i);
				lua_pushnumber(L, (*it)->getID());
				pushTable(L);
			}

			return 1;
		}
	}

	pushBoolean(L, false);
	return 1;
}

int32_t LuaScriptInterface::luaGetVocationInfo(lua_State* L)
{
	//getVocationInfo(id)
	uint32_t id = popNumber(L);
	Vocation* voc = Vocations::getInstance()->getVocation(id);
	if(!voc)
	{
		pushBoolean(L, false);
		return 1;
	}

	lua_newtable(L);
	setField(L, "id", voc->getId());
	setField(L, "name", voc->getName().c_str());
	setField(L, "description", voc->getDescription().c_str());
	setField(L, "healthGain", voc->getGain(GAIN_HEALTH));
	setField(L, "healthGainTicks", voc->getGainTicks(GAIN_HEALTH));
	setField(L, "healthGainAmount", voc->getGainAmount(GAIN_HEALTH));
	setField(L, "manaGain", voc->getGain(GAIN_MANA));
	setField(L, "manaGainTicks", voc->getGainTicks(GAIN_MANA));
	setField(L, "manaGainAmount", voc->getGainAmount(GAIN_MANA));
	setField(L, "attackSpeed", voc->getAttackSpeed());
	setField(L, "baseSpeed", voc->getBaseSpeed());
	setField(L, "fromVocation", voc->getFromVocation());
	setField(L, "promotedVocation", Vocations::getInstance()->getPromotedVocation(id));
	setField(L, "soul", voc->getGain(GAIN_SOUL));
	setField(L, "soulAmount", voc->getGainAmount(GAIN_SOUL));
	setField(L, "soulTicks", voc->getGainTicks(GAIN_SOUL));
	setField(L, "capacity", voc->getGainCap());
	setFieldBool(L, "attackable", voc->isAttackable());
	setFieldBool(L, "needPremium", voc->isPremiumNeeded());
	setFieldFloat(L, "experienceMultiplier", voc->getExperienceMultiplier());
	return 1;
}

int32_t LuaScriptInterface::luaGetGroupInfo(lua_State* L)
{
	//getGroupInfo(id[, premium = false])
	bool premium = false;
	if(lua_gettop(L) > 1)
		premium = popNumber(L);

	Group* group = Groups::getInstance()->getGroup(popNumber(L));
	if(!group)
	{
		pushBoolean(L, false);
		return 1;
	}

	lua_newtable(L);
	setField(L, "id", group->getId());
	setField(L, "name", group->getName().c_str());
	setField(L, "access", group->getAccess());
	setField(L, "ghostAccess", group->getGhostAccess());
	setField(L, "violationReasons", group->getViolationReasons());
	setField(L, "statementViolationFlags", group->getStatementViolationFlags());
	setField(L, "nameViolationFlags", group->getNameViolationFlags());
	setField(L, "flags", group->getFlags());
	setField(L, "customFlags", group->getCustomFlags());
	setField(L, "depotLimit", group->getDepotLimit(premium));
	setField(L, "maxVips", group->getMaxVips(premium));
	setField(L, "outfit", group->getOutfit());
	return 1;
}

int32_t LuaScriptInterface::luaGetChannelUsers(lua_State* L)
{
	//getChannelUsers(channelId)
	LuaEnvironment* env = getScriptEnv();
	uint16_t channelId = popNumber(L);

	if(ChatChannel* channel = g_chat.getChannelById(channelId))
	{
		UsersMap usersMap = channel->getUsers();
		UsersMap::iterator it = usersMap.begin();

		lua_newtable(L);
		for(int32_t i = 1; it != usersMap.end(); ++it, ++i)
		{
			lua_pushnumber(L, i);
			lua_pushnumber(L, env->addThing(it->second));
			pushTable(L);
		}
	}
	else
		pushBoolean(L, false);

	return 1;
}

int32_t LuaScriptInterface::luaGetPlayersOnline(lua_State* L)
{
	//getPlayersOnline()
	LuaEnvironment* env = getScriptEnv();
	AutoList<Player>::iterator it = Player::autoList.begin();

	lua_newtable(L);
	for(int32_t i = 1; it != Player::autoList.end(); ++it, ++i)
	{
		lua_pushnumber(L, i);
		lua_pushnumber(L, env->addThing(it->second));
		pushTable(L);
	}

	return 1;
}

int32_t LuaScriptInterface::luaSetCreatureMaxHealth(lua_State* L)
{
	//setCreatureMaxHealth(uid, health)
	uint32_t maxHealth = (uint32_t)popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
	{
		creature->changeMaxHealth(maxHealth);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaSetCreatureMaxMana(lua_State* L)
{
	//setCreatureMaxMana(uid, mana)
	uint32_t maxMana = (uint32_t)popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
	{
		creature->changeMaxMana(maxMana);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSetMaxCapacity(lua_State* L)
{
	//doPlayerSetMaxCapacity(uid, cap)
	double cap = popFloatNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		player->setCapacity(cap);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetCreatureMaster(lua_State* L)
{
	//getCreatureMaster(cid)
	uint32_t cid = popNumber(L);
	LuaEnvironment* env = getScriptEnv();

	Creature* creature = env->getCreatureByUID(cid);
	if(!creature)
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	if(Creature* master = creature->getMaster())
		lua_pushnumber(L, env->addThing(master));
	else
		lua_pushnil(L);

	return 1;
}

int32_t LuaScriptInterface::luaGetCreatureSummons(lua_State* L)
{
	//getCreatureSummons(cid)
	LuaEnvironment* env = getScriptEnv();

	Creature* creature = env->getCreatureByUID(popNumber(L));
	if(!creature)
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	const std::list<Creature*>& summons = creature->getSummons();
	CreatureList::const_iterator it = summons.begin();

	lua_newtable(L);
	for(uint32_t i = 1; it != summons.end(); ++it, ++i)
	{
		lua_pushnumber(L, i);
		lua_pushnumber(L, env->addThing(*it));
		pushTable(L);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSetIdleTime(lua_State* L)
{
	//doPlayerSetIdleTime(cid, amount)
	int64_t amount = popNumber(L);
	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		player->setIdleTime(amount);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoCreatureSetNoMove(lua_State* L)
{
	//doCreatureSetNoMove(cid, block)
	bool block = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
	{
		creature->setNoMove(block);
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerModes(lua_State* L)
{
	//getPlayerModes(cid)
	LuaEnvironment* env = getScriptEnv();

	Player* player = env->getPlayerByUID(popNumber(L));
	if(!player)
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	lua_newtable(L);
	setField(L, "chase", player->getChaseMode());
	setField(L, "fight", player->getFightMode());
	setField(L, "secure", player->getSecureMode());
	return 1;
}

int32_t LuaScriptInterface::luaGetPlayerRates(lua_State* L)
{
	//getPlayerRates(cid)
	LuaEnvironment* env = getScriptEnv();

	Player* player = env->getPlayerByUID(popNumber(L));
	if(!player)
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	lua_newtable(L);
	for(uint32_t i = SKILL_FIRST; i <= SKILL__LAST; ++i)
	{
		lua_pushnumber(L, i);
		lua_pushnumber(L, player->rates[(skills_t)i]);
		pushTable(L);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSetRate(lua_State* L)
{
	//doPlayerSetRate(cid, type, value)
	float value = popFloatNumber(L);
	uint32_t type = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		if(type <= SKILL__LAST)
		{
			player->rates[(skills_t)type] = value;
			pushBoolean(L, true);
		}
		else
			pushBoolean(L, false);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSwitchSaving(lua_State* L)
{
	//doPlayerSwitchSaving(cid)
	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		player->switchSaving();
		pushBoolean(L, true);
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoPlayerSave(lua_State* L)
{
	//doPlayerSave(cid[, shallow = false])
	bool shallow = false;
	if(lua_gettop(L) > 1)
		shallow = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L)))
	{
		player->loginPosition = player->getPosition();
		pushBoolean(L, IOLoginData::getInstance()->savePlayer(player, false, shallow));
	}
	else
	{
		errorEx(getError(LUA_ERROR_PLAYER_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetTownId(lua_State* L)
{
	//getTownId(townName)
	std::string townName = popString(L);
	if(Town* town = Towns::getInstance()->getTown(townName))
		lua_pushnumber(L, town->getID());
	else
		pushBoolean(L, false);

	return 1;
}

int32_t LuaScriptInterface::luaGetTownName(lua_State* L)
{
	//getTownName(townId)
	uint32_t townId = popNumber(L);
	if(Town* town = Towns::getInstance()->getTown(townId))
		lua_pushstring(L, town->getName().c_str());
	else
		pushBoolean(L, false);

	return 1;
}

int32_t LuaScriptInterface::luaGetTownTemplePosition(lua_State* L)
{
	//getTownTemplePosition(townId)
	bool displayError = true;
	if(lua_gettop(L) >= 2)
		displayError = popNumber(L);

	uint32_t townId = popNumber(L);
	if(Town* town = Towns::getInstance()->getTown(townId)){
		if(displayError)
			errorEx(getError(LUA_ERROR_ITEM_NOT_FOUND));

		pushPosition(L, town->getPosition(), 255);
	} else {
		pushBoolean(L, false);
	}
	
	return 1;
}

int32_t LuaScriptInterface::luaGetTownHouses(lua_State* L)
{
	//getTownHouses([townId])
	uint32_t townId = 0;
	if(lua_gettop(L) > 0)
		townId = popNumber(L);

	HouseMap::iterator it = Houses::getInstance()->getHouseBegin();
	lua_newtable(L);
	for(uint32_t i = 1; it != Houses::getInstance()->getHouseEnd(); ++i, ++it)
	{
		if(townId && it->second->getTownId() != townId)
			continue;

		lua_pushnumber(L, i);
		lua_pushnumber(L, it->second->getId());
		pushTable(L);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetSpectators(lua_State* L)
{
	//getSpectators(centerPos, rangex, rangey[, multifloor = false])
	bool multifloor = false;
	if(lua_gettop(L) > 3)
		multifloor = popNumber(L);

	uint32_t rangey = popNumber(L), rangex = popNumber(L);
	Position centerPos = getPosition(L, lua_gettop(L));

	SpectatorVec list;
	g_game.getSpectators(list, centerPos, false, multifloor, rangex, rangex, rangey, rangey);
	if(list.empty())
	{
		lua_pushnil(L);
		return 1;
	}

	LuaEnvironment* env = getScriptEnv();
	SpectatorVec::const_iterator it = list.begin();

	lua_newtable(L);
	for(uint32_t i = 1; it != list.end(); ++it, ++i)
	{
		lua_pushnumber(L, i);
		lua_pushnumber(L, env->addThing(*it));
		pushTable(L);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetHighscoreString(lua_State* L)
{
	//getHighscoreString(skillId)
	uint16_t skillId = popNumber(L);
	if(skillId <= SKILL__LAST)
		lua_pushstring(L, g_game.getHighscoreString(skillId).c_str());
	else
		pushBoolean(L, false);

	return 1;
}

int32_t LuaScriptInterface::luaGetVocationList(lua_State* L)
{
	//getVocationList()
	VocationsMap::iterator it = Vocations::getInstance()->getFirstVocation();
	lua_newtable(L);
	for(uint32_t i = 1; it != Vocations::getInstance()->getLastVocation(); ++i, ++it)
	{
		createTable(L, i);
		setField(L, "id", it->first);
		setField(L, "name", it->second->getName());
		pushTable(L);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetGroupList(lua_State* L)
{
	//getGroupList()
	GroupsMap::iterator it = Groups::getInstance()->getFirstGroup();
	lua_newtable(L);
	for(uint32_t i = 1; it != Groups::getInstance()->getLastGroup(); ++i, ++it)
	{
		createTable(L, i);
		setField(L, "id", it->first);
		setField(L, "name", it->second->getName());
		pushTable(L);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetChannelList(lua_State* L)
{
	//getChannelList()
	lua_newtable(L);
	ChannelList list = g_chat.getPublicChannels();

	ChannelList::const_iterator it = list.begin();
	for(uint32_t i = 1; it != list.end(); ++it, ++i)
	{
		createTable(L, i);
		setField(L, "id", (*it)->getId());
		setField(L, "name", (*it)->getName());

		setField(L, "flags", (*it)->getFlags());
		setField(L, "level", (*it)->getLevel());
		setField(L, "access", (*it)->getAccess());
		pushTable(L);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetTownList(lua_State* L)
{
	//getTownList()
	TownMap::const_iterator it = Towns::getInstance()->getFirstTown();
	lua_newtable(L);
	for(uint32_t i = 1; it != Towns::getInstance()->getLastTown(); ++it, ++i)
	{
		createTable(L, i);
		setField(L, "id", it->first);
		setField(L, "name", it->second->getName());
		pushTable(L);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetWaypointList(lua_State* L)
{
	//getWaypointList()
	WaypointMap waypointsMap = g_game.getMap()->waypoints.getWaypointsMap();
	WaypointMap::iterator it = waypointsMap.begin();

	lua_newtable(L);
	for(uint32_t i = 1; it != waypointsMap.end(); ++it, ++i)
	{
		createTable(L, i);
		setField(L, "name", it->first);
		setField(L, "pos", it->second->pos.x);
		pushTable(L);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetWaypointPosition(lua_State* L)
{
	//getWaypointPosition(name)
	if(WaypointPtr waypoint = g_game.getMap()->waypoints.getWaypointByName(popString(L)))
		pushPosition(L, waypoint->pos, 0);
	else
		pushBoolean(L, false);

	return 1;
}

int32_t LuaScriptInterface::luaDoWaypointAddTemporial(lua_State* L)
{
	//doWaypointAddTemporial(name, pos)
	Position pos = getPosition(L, lua_gettop(L));

	g_game.getMap()->waypoints.addWaypoint(WaypointPtr(new Waypoint(popString(L), pos)));
	pushBoolean(L, true);
	return 1;
}

int32_t LuaScriptInterface::luaGetGameState(lua_State* L)
{
	//getGameState()
	lua_pushnumber(L, g_game.getGameState());
	return 1;
}

int32_t LuaScriptInterface::luaDoSetGameState(lua_State* L)
{
	//doSetGameState(id)
	uint32_t id = popNumber(L);
	if(id >= GAMESTATE_FIRST && id <= GAMESTATE_LAST)
	{
		Dispatcher::getInstance().addTask(createTask(
			boost::bind(&Game::setGameState, &g_game, (GameState_t)id)));
		pushBoolean(L, true);
	}
	else
		pushBoolean(L, false);

	return 1;
}

int32_t LuaScriptInterface::luaDoCreatureExecuteTalkAction(lua_State* L)
{
	//doCreatureExecuteTalkAction(cid, text[, ignoreAccess = false[, channelId = CHANNEL_DEFAULT]])
	uint32_t params = lua_gettop(L), channelId = CHANNEL_DEFAULT;
	if(params > 3)
		channelId = popNumber(L);

	bool ignoreAccess = false;
	if(params > 2)
		ignoreAccess = popNumber(L);

	std::string text = popString(L);
	LuaEnvironment* env = getScriptEnv();
	if(Creature* creature = env->getCreatureByUID(popNumber(L)))
		pushBoolean(L, g_talkActions->onPlayerSay(creature, channelId, text, ignoreAccess));
	else
	{
		errorEx(getError(LUA_ERROR_CREATURE_NOT_FOUND));
		pushBoolean(L, false);
	}

	return 1;
}

int32_t LuaScriptInterface::luaDoExecuteRaid(lua_State* L)
{
	//doExecuteRaid(name)
	std::string raidName = popString(L);
	if(Raids::getInstance()->getRunning())
	{
		pushBoolean(L, false);
		return 1;
	}

	Raid* raid = Raids::getInstance()->getRaidByName(raidName);
	if(!raid || !raid->isLoaded())
	{
		errorEx("Raid with name " + raidName + " does not exists.");
		pushBoolean(L, false);
		return 1;
	}

	pushBoolean(L, raid->startRaid());
	return 1;
}

int32_t LuaScriptInterface::luaDoReloadInfo(lua_State* L)
{
	//doReloadInfo(id[, cid])
	uint32_t cid = 0;
	if(lua_gettop(L) > 1)
		cid = popNumber(L);

	uint32_t id = popNumber(L);
	if(id >= RELOAD_FIRST && id <= RELOAD_LAST)
	{
		// we're passing it to scheduler since talkactions reload will
		// re-init our lua state and crash due to unfinished call
		Scheduler::getInstance().addEvent(createSchedulerTask(SCHEDULER_MINTICKS,
			boost::bind(&Game::reloadInfo, &g_game, (ReloadInfo_t)id, cid)));
		pushBoolean(L, true);
	}
	else
		pushBoolean(L, false);

	return 1;
}

int32_t LuaScriptInterface::luaDoSaveServer(lua_State* L)
{
	//doSaveServer([shallow = false])
	bool shallow = false;
	if(lua_gettop(L) > 0)
		shallow = popNumber(L);

	Dispatcher::getInstance().addTask(createTask(boost::bind(&Game::saveGameState, &g_game, shallow)));
	lua_pushnil(L);
	return 1;
}

int32_t LuaScriptInterface::luaDoCleanHouse(lua_State* L)
{
	//doCleanHouse(houseId)
	uint32_t houseId = popNumber(L);
	if(House* house = Houses::getInstance()->getHouse(houseId))
	{
		house->clean();
		pushBoolean(L, true);
	}
	else
		pushBoolean(L, false);

	return 1;
}

int32_t LuaScriptInterface::luaDoCleanMap(lua_State* L)
{
	//doCleanMap()
	uint32_t count = 0;
	g_game.cleanMapEx(count);
	lua_pushnumber(L, count);
	return 1;
}

int32_t LuaScriptInterface::luaDoRefreshMap(lua_State* L)
{
	//doRefreshMap()
	g_game.proceduralRefresh();
	lua_pushnil(L);
	return 1;
}

int32_t LuaScriptInterface::luaDoUpdateHouseAuctions(lua_State* L)
{
	//doUpdateHouseAuctions()
	pushBoolean(L, IOMapSerialize::getInstance()->updateAuctions());
	return 1;
}

int32_t LuaScriptInterface::luaGetItemIdByName(lua_State* L)
{
	//getItemIdByName(name[, displayError = true])
	bool displayError = true;
	if(lua_gettop(L) >= 2)
		displayError = popNumber(L);

	int32_t itemId = Item::items.getItemIdByName(popString(L));
	if(itemId == -1)
	{
		if(displayError)
			errorEx(getError(LUA_ERROR_ITEM_NOT_FOUND));

		pushBoolean(L, false);
	}
	else
		lua_pushnumber(L, itemId);

	return 1;
}

int32_t LuaScriptInterface::luaGetItemInfo(lua_State* L)
{
	//getItemInfo(itemid)
	const ItemType* item;
	if(!(item = Item::items.getElement(popNumber(L))))
	{
		pushBoolean(L, false);
		return 1;
	}

	lua_newtable(L);
	setFieldBool(L, "stopTime", item->stopTime);
	setFieldBool(L, "showCount", item->showCount);
	setFieldBool(L, "stackable", item->stackable);
	setFieldBool(L, "showDuration", item->showDuration);
	setFieldBool(L, "showCharges", item->showCharges);
	setFieldBool(L, "showAttributes", item->showAttributes);
	setFieldBool(L, "distRead", item->allowDistRead);
	setFieldBool(L, "readable", item->canReadText);
	setFieldBool(L, "writable", item->canWriteText);
	setFieldBool(L, "forceSerialize", item->forceSerialize);
	setFieldBool(L, "vertical", item->isVertical);
	setFieldBool(L, "horizontal", item->isHorizontal);
	setFieldBool(L, "hangable", item->isHangable);
	setFieldBool(L, "usable", item->usable);
	setFieldBool(L, "movable", item->movable);
	setFieldBool(L, "pickupable", item->pickupable);
	setFieldBool(L, "rotable", item->rotable);
	setFieldBool(L, "replacable", item->replacable);
	setFieldBool(L, "hasHeight", item->hasHeight);
	setFieldBool(L, "blockSolid", item->blockSolid);
	setFieldBool(L, "blockPickupable", item->blockPickupable);
	setFieldBool(L, "blockProjectile", item->blockProjectile);
	setFieldBool(L, "blockPathing", item->blockPathFind);
	setFieldBool(L, "allowPickupable", item->allowPickupable);
	setFieldBool(L, "alwaysOnTop", item->alwaysOnTop);

	createTable(L, "floorChange");
	for(int32_t i = CHANGE_FIRST; i <= CHANGE_LAST; ++i)
	{
		lua_pushnumber(L, i);
		pushBoolean(L, item->floorChange[i - 1]);
		pushTable(L);
	}

	pushTable(L);
	setField(L, "magicEffect", (int32_t)item->magicEffect);
	setField(L, "fluidSource", (int32_t)item->fluidSource);
	setField(L, "weaponType", (int32_t)item->weaponType);
	setField(L, "bedPartnerDirection", (int32_t)item->bedPartnerDir);
	setField(L, "ammoAction", (int32_t)item->ammoAction);
	setField(L, "combatType", (int32_t)item->combatType);
	setField(L, "corpseType", (int32_t)item->corpseType);
	setField(L, "shootType", (int32_t)item->shootType);
	setField(L, "ammoType", (int32_t)item->ammoType);

	createTable(L, "transformUseTo");
	setField(L, "female", item->transformUseTo[PLAYERSEX_FEMALE]);
	setField(L, "male", item->transformUseTo[PLAYERSEX_MALE]);

	pushTable(L);
	setField(L, "transformToFree", item->transformToFree);
	setField(L, "transformEquipTo", item->transformEquipTo);
	setField(L, "transformDeEquipTo", item->transformDeEquipTo);
	setField(L, "clientId", item->clientId);
	setField(L, "maxItems", item->maxItems);
	setField(L, "slotPosition", item->slotPosition);
	setField(L, "wieldPosition", item->wieldPosition);
	setField(L, "speed", item->speed);
	setField(L, "maxTextLength", item->maxTextLength);
	setField(L, "writeOnceItemId", item->writeOnceItemId);
	setField(L, "date", item->date);
	setField(L, "writer", item->writer);
	setField(L, "text", item->text);
	setField(L, "attack", item->attack);
	setField(L, "extraAttack", item->extraAttack);
	setField(L, "defense", item->defense);
	setField(L, "extraDefense", item->extraDefense);
	setField(L, "armor", item->armor);
	setField(L, "breakChance", item->breakChance);
	setField(L, "hitChance", item->hitChance);
	setField(L, "maxHitChance", item->maxHitChance);
	setField(L, "runeLevel", item->runeLevel);
	setField(L, "runeMagicLevel", item->runeMagLevel);
	setField(L, "lightLevel", item->lightLevel);
	setField(L, "lightColor", item->lightColor);
	setField(L, "decayTo", item->decayTo);
	setField(L, "rotateTo", item->rotateTo);
	setField(L, "alwaysOnTopOrder", item->alwaysOnTopOrder);
	setField(L, "shootRange", item->shootRange);
	setField(L, "charges", item->charges);
	setField(L, "decayTime", item->decayTime);
	setField(L, "attackSpeed", item->attackSpeed);
	setField(L, "wieldInfo", item->wieldInfo);
	setField(L, "minRequiredLevel", item->minReqLevel);
	setField(L, "minRequiredMagicLevel", item->minReqMagicLevel);
	setField(L, "worth", item->worth);
	setField(L, "levelDoor", item->levelDoor);
	setField(L, "name", item->name.c_str());
	setField(L, "plural", item->pluralName.c_str());
	setField(L, "article", item->article.c_str());
	setField(L, "description", item->description.c_str());
	setField(L, "runeSpellName", item->runeSpellName.c_str());
	setField(L, "vocationString", item->vocationString.c_str());

	createTable(L, "abilities");
	setFieldBool(L, "manaShield", item->abilities.manaShield);
	setFieldBool(L, "invisible", item->abilities.invisible);
	setFieldBool(L, "regeneration", item->abilities.regeneration);
	setFieldBool(L, "preventLoss", item->abilities.preventLoss);
	setFieldBool(L, "preventDrop", item->abilities.preventDrop);
	setField(L, "elementType", (int32_t)item->abilities.elementType);
	setField(L, "elementDamage", item->abilities.elementDamage);
	setField(L, "speed", item->abilities.speed);
	setField(L, "healthGain", item->abilities.healthGain);
	setField(L, "healthTicks", item->abilities.healthTicks);
	setField(L, "manaGain", item->abilities.manaGain);
	setField(L, "manaTicks", item->abilities.manaTicks);
	setField(L, "conditionSuppressions", item->abilities.conditionSuppressions);

	//TODO: absorb, increment, reflect, skills, skillsPercent, stats, statsPercent

	pushTable(L);
	setField(L, "group", (int32_t)item->group);
	setField(L, "type", (int32_t)item->type);
	setFieldFloat(L, "weight", item->weight);
	return 1;
}

int32_t LuaScriptInterface::luaGetItemAttribute(lua_State* L)
{
	//getItemAttribute(uid, key)
	std::string key = popString(L);
	LuaEnvironment* env = getScriptEnv();

	Item* item = env->getItemByUID(popNumber(L));
	if(!item)
	{
		errorEx(getError(LUA_ERROR_ITEM_NOT_FOUND));
		lua_pushnil(L);
		return 1;
	}

	boost::any value = item->getAttribute(key);
	if(value.empty())
		lua_pushnil(L);
	else if(value.type() == typeid(std::string))
		lua_pushstring(L, boost::any_cast<std::string>(value).c_str());
	else if(value.type() == typeid(int32_t))
		lua_pushnumber(L, boost::any_cast<int32_t>(value));
	else if(value.type() == typeid(float))
		lua_pushnumber(L, boost::any_cast<float>(value));
	else if(value.type() == typeid(bool))
		pushBoolean(L, boost::any_cast<bool>(value));
	else
		lua_pushnil(L);

	return 1;
}

int32_t LuaScriptInterface::luaDoItemSetAttribute(lua_State* L)
{
	//doItemSetAttribute(uid, key, value)
	boost::any value;
	if(lua_isnumber(L, -1))
	{
		float tmp = popFloatNumber(L);
		if(std::floor(tmp) < tmp)
			value = tmp;
		else
			value = (int32_t)tmp;
	}
	else if(isBoolean(L, -1))
		value = popBoolean(L);
	else if(isString(L, -1))
		value = popString(L);
	else
	{
		lua_pop(L, 1);
		errorEx("Invalid data type");

		pushBoolean(L, false);
		return 1;
	}

	std::string key = popString(L);
	LuaEnvironment* env = getScriptEnv();

	Item* item = env->getItemByUID(popNumber(L));
	if(!item)
	{
		errorEx(getError(LUA_ERROR_ITEM_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	if(value.type() == typeid(int32_t))
	{
		if(key == "uid")
		{
			int32_t tmp = boost::any_cast<int32_t>(value);
			if(tmp < 1000 || tmp > 0xFFFF)
			{
				errorEx("Value for protected key \"uid\" must be in range of 1000 to 65535");
				pushBoolean(L, false);
				return 1;
			}

			item->setUniqueId(tmp);
		}
		else if(key == "aid")
			item->setActionId(boost::any_cast<int32_t>(value));
		else
			item->setAttribute(key, boost::any_cast<int32_t>(value));
	}
	else
		item->setAttribute(key, value);

	pushBoolean(L, true);
	return 1;
}

int32_t LuaScriptInterface::luaDoItemEraseAttribute(lua_State* L)
{
	//doItemEraseAttribute(uid, key)
	std::string key = popString(L);
	LuaEnvironment* env = getScriptEnv();

	Item* item = env->getItemByUID(popNumber(L));
	if(!item)
	{
		errorEx(getError(LUA_ERROR_ITEM_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	bool ret = true;
	if(key == "uid")
	{
		errorEx("Attempt to erase protected key \"uid\".");
		ret = false;
	}
	else if(key != "aid")
		item->eraseAttribute(key);
	else
		item->resetActionId();

	pushBoolean(L, ret);
	return 1;
}

// Item
int LuaScriptInterface::luaItemCreate(lua_State* L)
{
	// Item(uid)
	uint32_t id = getNumber<uint32_t>(L, 2);

	Item* item = getScriptEnv()->getItemByUID(id);
	if (item) {
		pushUserdata<Item>(L, item);
		setItemMetatable(L, -1, item);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int32_t LuaScriptInterface::luaGetItemWeight(lua_State* L)
{
	//getItemWeight(itemid[, precise = true])
	bool precise = true;
	if(lua_gettop(L) > 2)
		precise = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	Item* item = env->getItemByUID(popNumber(L));
	if(!item)
	{
		errorEx(getError(LUA_ERROR_ITEM_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	double weight = item->getWeight();
	if(precise)
	{
		std::stringstream ws;
		ws << std::fixed << std::setprecision(2) << weight;
		weight = atof(ws.str().c_str());
	}

	lua_pushnumber(L, weight);
	return 1;
}

int32_t LuaScriptInterface::luaGetItemParent(lua_State* L)
{
	//getItemParent(uid)
	LuaEnvironment* env = getScriptEnv();

	Item* item = env->getItemByUID(popNumber(L));
	if(!item)
	{
		errorEx(getError(LUA_ERROR_ITEM_NOT_FOUND));
		lua_pushnil(L);
		return 1;
	}

	Item* container = item->getParent()->getItem();
	pushThing(L, container, env->addThing(container));
	return 1;
}

int32_t LuaScriptInterface::luaHasItemProperty(lua_State* L)
{
	//hasItemProperty(uid, prop)
	uint32_t prop = popNumber(L);
	LuaEnvironment* env = getScriptEnv();

	Item* item = env->getItemByUID(popNumber(L));
	if(!item)
	{
		errorEx(getError(LUA_ERROR_ITEM_NOT_FOUND));
		pushBoolean(L, false);
		return 1;
	}

	//Check if the item is a tile, so we can get more accurate properties
	bool tmp = item->hasProperty((ITEMPROPERTY)prop);
	if(item->getTile() && item->getTile()->ground == item)
		tmp = item->getTile()->hasProperty((ITEMPROPERTY)prop);

	pushBoolean(L, tmp);
	return 1;
}

int32_t LuaScriptInterface::luaIsIpBanished(lua_State* L)
{
	//isIpBanished(ip[, mask])
	uint32_t mask = 0xFFFFFFFF;
	if(lua_gettop(L) > 1)
		mask = popNumber(L);

	pushBoolean(L, IOBan::getInstance()->isIpBanished((uint32_t)popNumber(L), mask));
	return 1;
}

int32_t LuaScriptInterface::luaIsPlayerBanished(lua_State* L)
{
	//isPlayerBanished(name/guid, type)
	PlayerBan_t type = (PlayerBan_t)popNumber(L);
	if(lua_isnumber(L, -1))
		pushBoolean(L, IOBan::getInstance()->isPlayerBanished((uint32_t)popNumber(L), type));
	else
		pushBoolean(L, IOBan::getInstance()->isPlayerBanished(popString(L), type));

	return 1;
}

int32_t LuaScriptInterface::luaIsAccountBanished(lua_State* L)
{
	//isAccountBanished(accountId[, playerId])
	uint32_t playerId = 0;
	if(lua_gettop(L) > 1)
		playerId = popNumber(L);

	pushBoolean(L, IOBan::getInstance()->isAccountBanished((uint32_t)popNumber(L), playerId));
	return 1;
}

int32_t LuaScriptInterface::luaDoAddIpBanishment(lua_State* L)
{
	//doAddIpBanishment(ip[, mask[, length[, reason[, comment[, admin[, statement]]]]]])
	uint32_t admin = 0, reason = 21, mask = 0xFFFFFFFF, params = lua_gettop(L);
	int64_t length = time(NULL) + g_config.getNumber(ConfigManager::IPBANISHMENT_LENGTH);
	std::string statement, comment;

	if(params > 6)
		statement = popString(L);

	if(params > 5)
		admin = popNumber(L);

	if(params > 4)
		comment = popString(L);

	if(params > 3)
		reason = popNumber(L);

	if(params > 2)
		length = popNumber(L);

	if(params > 1)
		mask = popNumber(L);

	pushBoolean(L, IOBan::getInstance()->addIpBanishment((uint32_t)popNumber(L),
		length, reason, comment, admin, mask, statement));
	return 1;
}

int32_t LuaScriptInterface::luaDoAddPlayerBanishment(lua_State* L)
{
	//doAddPlayerBanishment(name/guid[, type[, length[, reason[, action[, comment[, admin[, statement]]]]]]])
	uint32_t admin = 0, reason = 21, params = lua_gettop(L);
	int64_t length = -1;
	std::string statement, comment;

	ViolationAction_t action = ACTION_NAMELOCK;
	PlayerBan_t type = PLAYERBAN_LOCK;
	if(params > 7)
		statement = popString(L);

	if(params > 6)
		admin = popNumber(L);

	if(params > 5)
		comment = popString(L);

	if(params > 4)
		action = (ViolationAction_t)popNumber(L);

	if(params > 3)
		reason = popNumber(L);

	if(params > 2)
		length = popNumber(L);

	if(params > 1)
		type = (PlayerBan_t)popNumber(L);

	if(lua_isnumber(L, -1))
		pushBoolean(L, IOBan::getInstance()->addPlayerBanishment((uint32_t)popNumber(L),
			length, reason, action, comment, admin, type, statement));
	else
		pushBoolean(L, IOBan::getInstance()->addPlayerBanishment(popString(L),
			length, reason, action, comment, admin, type, statement));

	return 1;
}

int32_t LuaScriptInterface::luaDoAddAccountBanishment(lua_State* L)
{
	//doAddAccountBanishment(accountId[, playerId[, length[, reason[, action[, comment[, admin[, statement]]]]]]])
	uint32_t admin = 0, reason = 21, playerId = 0, params = lua_gettop(L);
	int64_t length = time(NULL) + g_config.getNumber(ConfigManager::BAN_LENGTH);
	std::string statement, comment;

	ViolationAction_t action = ACTION_BANISHMENT;
	if(params > 7)
		statement = popString(L);

	if(params > 6)
		admin = popNumber(L);

	if(params > 5)
		comment = popString(L);

	if(params > 4)
		action = (ViolationAction_t)popNumber(L);

	if(params > 3)

		reason = popNumber(L);

	if(params > 2)
		length = popNumber(L);

	if(params > 1)
		playerId = popNumber(L);

	pushBoolean(L, IOBan::getInstance()->addAccountBanishment((uint32_t)popNumber(L),
		length, reason, action, comment, admin, playerId, statement));
	return 1;
}

int32_t LuaScriptInterface::luaDoAddNotation(lua_State* L)
{
	//doAddNotation(accountId[, playerId[, reason[, comment[, admin[, statement]]]]]])
	uint32_t admin = 0, reason = 21, playerId = 0, params = lua_gettop(L);
	std::string statement, comment;

	if(params > 5)
		statement = popString(L);

	if(params > 4)
		admin = popNumber(L);

	if(params > 3)
		comment = popString(L);

	if(params > 2)
		reason = popNumber(L);

	if(params > 1)
		playerId = popNumber(L);

	pushBoolean(L, IOBan::getInstance()->addNotation((uint32_t)popNumber(L),
		reason, comment, admin, playerId, statement));
	return 1;
}

int32_t LuaScriptInterface::luaDoAddStatement(lua_State* L)
{
	//doAddStatement(name/guid[, channelId[, reason[, comment[, admin[, statement]]]]]])
	uint32_t admin = 0, reason = 21, params = lua_gettop(L);
	int16_t channelId = -1;
	std::string statement, comment;

	if(params > 5)
		statement = popString(L);

	if(params > 4)
		admin = popNumber(L);

	if(params > 3)
		comment = popString(L);

	if(params > 2)
		reason = popNumber(L);

	if(params > 1)
		channelId = popNumber(L);

	if(lua_isnumber(L, -1))
		pushBoolean(L, IOBan::getInstance()->addStatement((uint32_t)popNumber(L),
			reason, comment, admin, channelId, statement));
	else
		pushBoolean(L, IOBan::getInstance()->addStatement(popString(L),
			reason, comment, admin, channelId, statement));

	return 1;
}

int32_t LuaScriptInterface::luaDoRemoveIpBanishment(lua_State* L)
{
	//doRemoveIpBanishment(ip[, mask])
	uint32_t mask = 0xFFFFFFFF;
	if(lua_gettop(L) > 1)
		mask = popNumber(L);

	pushBoolean(L, IOBan::getInstance()->removeIpBanishment(
		(uint32_t)popNumber(L), mask));
	return 1;
}

int32_t LuaScriptInterface::luaDoRemovePlayerBanishment(lua_State* L)
{
	//doRemovePlayerBanishment(name/guid, type)
	PlayerBan_t type = (PlayerBan_t)popNumber(L);
	if(lua_isnumber(L, -1))
		pushBoolean(L, IOBan::getInstance()->removePlayerBanishment((uint32_t)popNumber(L), type));
	else
		pushBoolean(L, IOBan::getInstance()->removePlayerBanishment(popString(L), type));

	return 1;
}

int32_t LuaScriptInterface::luaDoRemoveAccountBanishment(lua_State* L)
{
	//doRemoveAccountBanishment(accountId[, playerId])
	uint32_t playerId = 0;
	if(lua_gettop(L) > 1)
		playerId = popNumber(L);

	pushBoolean(L, IOBan::getInstance()->removeAccountBanishment((uint32_t)popNumber(L), playerId));
	return 1;
}

int32_t LuaScriptInterface::luaDoRemoveNotations(lua_State* L)
{
	//doRemoveNotations(accountId[, playerId])
	uint32_t playerId = 0;
	if(lua_gettop(L) > 1)
		playerId = popNumber(L);

	pushBoolean(L, IOBan::getInstance()->removeNotations((uint32_t)popNumber(L), playerId));
	return 1;
}

int32_t LuaScriptInterface::luaDoRemoveStatements(lua_State* L)
{
	//doRemoveStatements(name/guid[, channelId])
	int16_t channelId = -1;
	if(lua_gettop(L) > 1)
		channelId = popNumber(L);

	if(lua_isnumber(L, -1))
		pushBoolean(L, IOBan::getInstance()->removeStatements((uint32_t)popNumber(L), channelId));
	else
		pushBoolean(L, IOBan::getInstance()->removeStatements(popString(L), channelId));

	return 1;
}

int32_t LuaScriptInterface::luaGetNotationsCount(lua_State* L)
{
	//getNotationsCount(accountId[, playerId])
	uint32_t playerId = 0;
	if(lua_gettop(L) > 1)
		playerId = popNumber(L);

	lua_pushnumber(L, IOBan::getInstance()->getNotationsCount((uint32_t)popNumber(L), playerId));
	return 1;
}

int32_t LuaScriptInterface::luaGetStatementsCount(lua_State* L)
{
	//getStatementsCount(name/guid[, channelId])
	int16_t channelId = -1;
	if(lua_gettop(L) > 1)
		channelId = popNumber(L);

	if(lua_isnumber(L, -1))
		lua_pushnumber(L, IOBan::getInstance()->getStatementsCount((uint32_t)popNumber(L), channelId));
	else
		lua_pushnumber(L, IOBan::getInstance()->getStatementsCount(popString(L), channelId));

	return 1;
}

int32_t LuaScriptInterface::luaGetBanData(lua_State* L)
{
	//getBanData(value[, type[, param]])
	Ban tmp;
	uint32_t params = lua_gettop(L);
	if(params > 2)
		tmp.param = popNumber(L);

	if(params > 1)
		tmp.type = (Ban_t)popNumber(L);

	tmp.value = popNumber(L);
	if(!IOBan::getInstance()->getData(tmp))
	{
		pushBoolean(L, false);
		return 1;
	}

	lua_newtable(L);
	setField(L, "id", tmp.id);
	setField(L, "type", tmp.type);
	setField(L, "value", tmp.value);
	setField(L, "param", tmp.param);
	setField(L, "added", tmp.added);
	setField(L, "expires", tmp.expires);
	setField(L, "adminId", tmp.adminId);
	setField(L, "reason", tmp.reason);
	setField(L, "action", tmp.action);
	setField(L, "comment", tmp.comment);
	setField(L, "statement", tmp.statement);
	return 1;
}

int32_t LuaScriptInterface::luaGetBanReason(lua_State* L)
{
	//getBanReason(id)
	lua_pushstring(L, getReason((ViolationAction_t)popNumber(L)).c_str());
	return 1;
}

int32_t LuaScriptInterface::luaGetBanAction(lua_State* L)
{
	//getBanAction(id[, ipBanishment = false])
	bool ipBanishment = false;
	if(lua_gettop(L) > 1)
		ipBanishment = popNumber(L);

	lua_pushstring(L, getAction((ViolationAction_t)popNumber(L), ipBanishment).c_str());
	return 1;
}

int32_t LuaScriptInterface::luaGetBanList(lua_State* L)
{
	//getBanList(type[, value[, param]])
	int32_t param = 0, params = lua_gettop(L);
	if(params > 2)
		param = popNumber(L);

	uint32_t value = 0;
	if(params > 1)
		value = popNumber(L);

	BansVec bans = IOBan::getInstance()->getList((Ban_t)popNumber(L), value, param);
	BansVec::const_iterator it = bans.begin();

	lua_newtable(L);
	for(uint32_t i = 1; it != bans.end(); ++it, ++i)
	{
		createTable(L, i);
		setField(L, "id", it->id);
		setField(L, "type", it->type);
		setField(L, "value", it->value);
		setField(L, "param", it->param);
		setField(L, "added", it->added);
		setField(L, "expires", it->expires);
		setField(L, "adminId", it->adminId);
		setField(L, "reason", it->reason);
		setField(L, "action", it->action);
		setField(L, "comment", it->comment);
		setField(L, "statement", it->statement);
		pushTable(L);
	}

	return 1;
}

int32_t LuaScriptInterface::luaGetExperienceStage(lua_State* L)
{
	//getExperienceStage(level[, divider])
	double divider = 1.0f;
	if(lua_gettop(L) > 1)
		divider = popFloatNumber(L);

	lua_pushnumber(L, g_game.getExperienceStage(popNumber(L), divider));
	return 1;
}

int32_t LuaScriptInterface::luaGetDataDir(lua_State* L)
{
	//getDataDir()
	lua_pushstring(L, getFilePath(FILE_TYPE_OTHER, "").c_str());
	return 1;
}

int32_t LuaScriptInterface::luaGetLogsDir(lua_State* L)
{
	//getLogsDir()
	lua_pushstring(L, getFilePath(FILE_TYPE_LOG, "").c_str());
	return 1;
}

int32_t LuaScriptInterface::luaGetConfigFile(lua_State* L)
{
	//getConfigFile()
	lua_pushstring(L, g_config.getString(ConfigManager::CONFIG_FILE).c_str());
	return 1;
}
#ifdef __WAR_SYSTEM__

int32_t LuaScriptInterface::luaDoGuildAddEnemy(lua_State* L)
{
	//doGuildAddEnemy(guild, enemy, war, type)
	War_t war;
	war.type = (WarType_t)popNumber(L);
	war.war = popNumber(L);

	uint32_t enemy = popNumber(L), guild = popNumber(L), count = 0;
	for(AutoList<Player>::iterator it = Player::autoList.begin(); it != Player::autoList.end(); ++it)
	{
		if(it->second->isRemoved() || it->second->getGuildId() != guild)
			continue;

		++count;
		it->second->addEnemy(enemy, war);
		g_game.updateCreatureEmblem(it->second);
	}

	lua_pushnumber(L, count);
	return 1;
}

int32_t LuaScriptInterface::luaDoGuildRemoveEnemy(lua_State* L)
{
	//doGuildRemoveEnemy(guild, enemy)
	uint32_t enemy = popNumber(L), guild = popNumber(L), count = 0;
	for(AutoList<Player>::iterator it = Player::autoList.begin(); it != Player::autoList.end(); ++it)
	{
		if(it->second->isRemoved() || it->second->getGuildId() != guild)
			continue;

		++count;
		it->second->removeEnemy(enemy);
		g_game.updateCreatureEmblem(it->second);
	}

	lua_pushnumber(L, count);
	return 1;
}
#endif

int32_t LuaScriptInterface::luaGetConfigValue(lua_State* L)
{
	//getConfigValue(key)
	g_config.getValue(popString(L), L);
	return 1;
}

int32_t LuaScriptInterface::luaGetModList(lua_State* L)
{
	//getModList()
	ModMap::iterator it = ScriptManager::getInstance()->getFirstMod();
	lua_newtable(L);
	for(uint32_t i = 1; it != ScriptManager::getInstance()->getLastMod(); ++it, ++i)
	{
		createTable(L, i);
		setField(L, "name", it->first);
		setField(L, "description", it->second.description);
		setField(L, "file", it->second.file);

		setField(L, "version", it->second.version);
		setField(L, "author", it->second.author);
		setField(L, "contact", it->second.contact);

		setFieldBool(L, "enabled", it->second.enabled);
		pushTable(L);
	}

	return 1;
}

int32_t LuaScriptInterface::luaL_loadmodlib(lua_State* L)
{
	//loadmodlib(lib)
	std::string name = asLowerCaseString(popString(L));
	for(LibMap::iterator it = ScriptManager::getInstance()->getFirstLib();
		it != ScriptManager::getInstance()->getLastLib(); ++it)
	{
		if(asLowerCaseString(it->first) != name)
			continue;

		luaL_loadstring(L, it->second.second.c_str());
		lua_pushvalue(L, -1);
		break;
	}

	return 1;
}

int32_t LuaScriptInterface::luaL_domodlib(lua_State* L)
{
	//domodlib(lib)
	std::string name = asLowerCaseString(popString(L));
	for(LibMap::iterator it = ScriptManager::getInstance()->getFirstLib();
		it != ScriptManager::getInstance()->getLastLib(); ++it)
	{
		if(asLowerCaseString(it->first) != name)
			continue;

		bool ret = luaL_dostring(L, it->second.second.c_str());
		if(ret)
			error(NULL, popString(L));

		pushBoolean(L, !ret);
		break;
	}

	return 1;
}

int32_t LuaScriptInterface::luaL_dodirectory(lua_State* L)
{
	//dodirectory(dir[, recursively = false])
	bool recursively = false;
	if(lua_gettop(L) > 1)
		recursively = popNumber(L);

	std::string dir = popString(L);
	if(!getScriptEnv()->getScriptInterface()->loadDirectory(dir, NULL, recursively))
	{
		errorEx("Failed to load directory " + dir + ".");
		pushBoolean(L, false);
	}
	else
		pushBoolean(L, true);

	return 1;
}

int32_t LuaScriptInterface::luaL_errors(lua_State* L)
{
	//errors(var)
	pushBoolean(L, getScriptEnv()->getScriptInterface()->m_errors);
	getScriptEnv()->getScriptInterface()->m_errors = popNumber(L);
	return 1;
}

#define EXPOSE_LOG(Name, Stream)\
	int32_t LuaScriptInterface::luaStd##Name(lua_State* L)\
	{\
		StringVec data;\
		for(int32_t i = 0, params = lua_gettop(L); i < params; ++i)\
			data.push_back(popString(L));\
\
		for(StringVec::reverse_iterator it = data.rbegin(); it != data.rend(); ++it)\
			Stream << (*it) << std::endl;\
\
		lua_pushnumber(L, data.size());\
		return 1;\
	}

EXPOSE_LOG(Cout, std::cout)
EXPOSE_LOG(Clog, std::clog)
EXPOSE_LOG(Cerr, std::cerr)

#undef EXPOSE_LOG

int32_t LuaScriptInterface::luaStdMD5(lua_State* L)
{
	//std.md5(string[, upperCase = false])
	bool upperCase = false;
	if(lua_gettop(L) > 1)
		upperCase = popNumber(L);

	lua_pushstring(L, transformToMD5(popString(L), upperCase).c_str());
	return 1;
}

int32_t LuaScriptInterface::luaStdSHA1(lua_State* L)
{
	//std.sha1(string[, upperCase = false])
	bool upperCase = false;
	if(lua_gettop(L) > 1)
		upperCase = popNumber(L);

	lua_pushstring(L, transformToSHA1(popString(L), upperCase).c_str());
	return 1;
}

int32_t LuaScriptInterface::luaStdSHA256(lua_State* L)
{
	//std.sha256(string[, upperCase = false])
	bool upperCase = false;
	if(lua_gettop(L) > 1)
		upperCase = popNumber(L);

	lua_pushstring(L, transformToSHA256(popString(L), upperCase).c_str());
	return 1;
}

int32_t LuaScriptInterface::luaStdSHA512(lua_State* L)
{
	//std.sha512(string[, upperCase = false])
	bool upperCase = false;
	if(lua_gettop(L) > 1)
		upperCase = popNumber(L);

	lua_pushstring(L, transformToSHA512(popString(L), upperCase).c_str());
	return 1;
}

int32_t LuaScriptInterface::luaStdVAHash(lua_State* L)
{
	//std.vahash(string[, upperCase = false])
	bool upperCase = false;
	if(lua_gettop(L) > 1)
		upperCase = popNumber(L);

	lua_pushstring(L, transformToVAHash(popString(L), upperCase).c_str());
	return 1;
}

// Userdata
int LuaScriptInterface::luaUserdataCompare(lua_State* L)
{
	// userdataA == userdataB
	pushBoolean(L, getUserdata<void>(L, 1) == getUserdata<void>(L, 2));
	return 1;
}

// table
int LuaScriptInterface::luaTableCreate(lua_State* L)
{
	// table.create(arrayLength, keyLength)
	lua_createtable(L, getNumber<int32_t>(L, 1), getNumber<int32_t>(L, 2));
	return 1;
}

int32_t LuaScriptInterface::luaSystemTime(lua_State* L)
{
	//os.mtime()
	lua_pushnumber(L, OTSYS_TIME());
	return 1;
}

int32_t LuaScriptInterface::luaDatabaseExecute(lua_State* L)
{
	//db.query(query)
	DBQuery query; //lock mutex
	pushBoolean(L, Database::getInstance()->query(popString(L)));
	return 1;
}

int32_t LuaScriptInterface::luaDatabaseStoreQuery(lua_State* L)
{
	//db.storeQuery(query)
	LuaEnvironment* env = getScriptEnv();

	DBQuery query; //lock mutex
	if(DBResult* res = Database::getInstance()->storeQuery(popString(L)))
		lua_pushnumber(L, env->addResult(res));
	else
		pushBoolean(L, false);

	return 1;
}

int32_t LuaScriptInterface::luaDatabaseEscapeString(lua_State* L)
{
	//db.escapeString(str)
	DBQuery query; //lock mutex
	lua_pushstring(L, Database::getInstance()->escapeString(popString(L)).c_str());
	return 1;
}

int32_t LuaScriptInterface::luaDatabaseEscapeBlob(lua_State* L)
{
	//db.escapeBlob(s, length)
	uint32_t length = popNumber(L);
	DBQuery query; //lock mutex

	lua_pushstring(L, Database::getInstance()->escapeBlob(popString(L).c_str(), length).c_str());
	return 1;
}

int32_t LuaScriptInterface::luaDatabaseLastInsertId(lua_State* L)
{
	//db.lastInsertId()
	DBQuery query; //lock mutex
	lua_pushnumber(L, Database::getInstance()->getLastInsertId());
	return 1;
}

int32_t LuaScriptInterface::luaDatabaseStringComparer(lua_State* L)
{
	//db.stringComparer()
	lua_pushstring(L, Database::getInstance()->getStringComparer().c_str());
	return 1;
}

int32_t LuaScriptInterface::luaDatabaseUpdateLimiter(lua_State* L)
{
	//db.updateLimiter()
	lua_pushstring(L, Database::getInstance()->getUpdateLimiter().c_str());
	return 1;
}

#define CHECK_RESULT()\
	if(!res)\
	{\
		pushBoolean(L, false);\
		return 1;\
	}

int32_t LuaScriptInterface::luaResultGetDataInt(lua_State* L)
{
	//result.getDataInt(res, s)
	const std::string& s = popString(L);
	LuaEnvironment* env = getScriptEnv();

	DBResult* res = env->getResultByID(popNumber(L));
	CHECK_RESULT()

	lua_pushnumber(L, res->getDataInt(s));
	return 1;
}

int32_t LuaScriptInterface::luaResultGetDataLong(lua_State* L)
{
	//result.getDataLong(res, s)
	const std::string& s = popString(L);
	LuaEnvironment* env = getScriptEnv();

	DBResult* res = env->getResultByID(popNumber(L));
	CHECK_RESULT()

	lua_pushnumber(L, res->getDataLong(s));
	return 1;
}

int32_t LuaScriptInterface::luaResultGetDataString(lua_State* L)
{
	//result.getDataString(res, s)
	const std::string& s = popString(L);
	LuaEnvironment* env = getScriptEnv();

	DBResult* res = env->getResultByID(popNumber(L));
	CHECK_RESULT()

	lua_pushstring(L, res->getDataString(s).c_str());
	return 1;
}

int32_t LuaScriptInterface::luaResultGetDataStream(lua_State* L)
{
	//result.getDataStream(res, s)
	const std::string s = popString(L);
	LuaEnvironment* env = getScriptEnv();

	DBResult* res = env->getResultByID(popNumber(L));
	CHECK_RESULT()

	uint64_t length = 0;
	lua_pushstring(L, res->getDataStream(s, length));

	lua_pushnumber(L, length);
	return 2;
}

int32_t LuaScriptInterface::luaResultNext(lua_State* L)
{
	//result.next(res)
	LuaEnvironment* env = getScriptEnv();

	DBResult* res = env->getResultByID(popNumber(L));
	CHECK_RESULT()

	pushBoolean(L, res->next());
	return 1;
}

int32_t LuaScriptInterface::luaResultFree(lua_State* L)
{
	//result.free(res)
	uint32_t rid = popNumber(L);
	LuaEnvironment* env = getScriptEnv();

	DBResult* res = env->getResultByID(rid);
	CHECK_RESULT()

	pushBoolean(L, env->removeResult(rid));
	return 1;
}

#undef CHECK_RESULT

#ifndef LUAJIT_VERSION
int32_t LuaScriptInterface::luaBitNot(lua_State* L)
{
	int32_t number = (int32_t)popNumber(L);
	lua_pushnumber(L, ~number);
	return 1;
}

int32_t LuaScriptInterface::luaBitUNot(lua_State* L)
{
	uint32_t number = (uint32_t)popNumber(L);
	lua_pushnumber(L, ~number);
	return 1;
}

#define MULTI_OPERATOR(type, name, op)\
	int32_t LuaScriptInterface::luaBit##name(lua_State* L)\
	{\
		int32_t params = lua_gettop(L);\
		type value = (type)popNumber(L);\
		for(int32_t i = 2; i <= params; ++i)\
			value op popNumber(L);\
\
		lua_pushnumber(L, value);\
		return 1;\
	}

MULTI_OPERATOR(int32_t, And, &=)
MULTI_OPERATOR(int32_t, Or, |=)
MULTI_OPERATOR(int32_t, Xor, ^=)
MULTI_OPERATOR(uint32_t, UAnd, &=)
MULTI_OPERATOR(uint32_t, UOr, |=)
MULTI_OPERATOR(uint32_t, UXor, ^=)

#undef MULTI_OPERATOR

#define SHIFT_OPERATOR(type, name, op)\
	int32_t LuaScriptInterface::luaBit##name(lua_State* L)\
	{\
		type v2 = (type)popNumber(L), v1 = (type)popNumber(L);\
		lua_pushnumber(L, (v1 op v2));\
		return 1;\
	}

SHIFT_OPERATOR(int32_t, LeftShift, <<)
SHIFT_OPERATOR(int32_t, RightShift, >>)
SHIFT_OPERATOR(uint32_t, ULeftShift, <<)
SHIFT_OPERATOR(uint32_t, URightShift, >>)

#undef SHIFT_OPERATOR
#endif

int32_t LuaScriptInterface::luaDoSendPlayerExtendedOpcode(lua_State* L)
{
	//doSendPlayerExtendedOpcode(cid, opcode, buffer)
	std::string buffer = popString(L);
	int opcode = popNumber(L);

	LuaEnvironment* env = getScriptEnv();
	if(Player* player = env->getPlayerByUID(popNumber(L))) {
	player->sendExtendedOpcode(opcode, buffer);
	pushBoolean(L, true);
	}
	pushBoolean(L, false);
	return 1;
}

// Spells
int LuaScriptInterface::luaSpellCreate(lua_State* L)
{
	// Spell(words, name or id) to get an existing spell
	// Spell(type) ex: Spell(SPELL_INSTANT) or Spell(SPELL_RUNE) to create a new spell
	if (lua_gettop(L) == 1) {
		std::cout << "[Error - Spell::luaSpellCreate] There is no parameter set!" << std::endl;
		lua_pushnil(L);
		return 1;
	}

	SpellType_t type = getNumber<SpellType_t>(L, 2);

	if (isString(L, 2)) {
		std::string tmp = asLowerCaseString(getString(L, 2));
		if (tmp == "instant") {
			type = SPELL_INSTANT;
		} else if (tmp == "rune") {
			type = SPELL_RUNE;
		}
	}

	if (type == SPELL_INSTANT) {
		InstantSpell* spell = new InstantSpell(getScriptEnv()->getScriptInterface());
		// spell->fromLua = true;
		pushUserdata<Spell>(L, spell);
		setMetatable(L, -1, "Spell");
		spell->spellType = SPELL_INSTANT;
		return 1;
	} else if (type == SPELL_RUNE) {
		RuneSpell* spell = new RuneSpell(getScriptEnv()->getScriptInterface());
		// spell->fromLua = true;
		pushUserdata<Spell>(L, spell);
		setMetatable(L, -1, "Spell");
		spell->spellType = SPELL_RUNE;
		return 1;
	}

	// isNumber(L, 2) doesn't work here for some reason, maybe a bug?
	if (getNumber<uint32_t>(L, 2)) {
		InstantSpell* instant = g_spells->getInstantSpellById(getNumber<uint32_t>(L, 2));
		if (instant) {
			pushUserdata<Spell>(L, instant);
			setMetatable(L, -1, "Spell");
			return 1;
		}
		RuneSpell* rune = g_spells->getRuneSpell(getNumber<uint32_t>(L, 2));
		if (rune) {
			pushUserdata<Spell>(L, rune);
			setMetatable(L, -1, "Spell");
			return 1;
		}
	} else if (isString(L, 2)) {
		std::string arg = getString(L, 2);
		InstantSpell* instant = g_spells->getInstantSpellByName(arg);
		if (instant) {
			pushUserdata<Spell>(L, instant);
			setMetatable(L, -1, "Spell");
			return 1;
		}
		instant = g_spells->getInstantSpell(arg);
		if (instant) {
			pushUserdata<Spell>(L, instant);
			setMetatable(L, -1, "Spell");
			return 1;
		}
		RuneSpell* rune = g_spells->getRuneSpellByName(arg);
		if (rune) {
			pushUserdata<Spell>(L, rune);
			setMetatable(L, -1, "Spell");
			return 1;
		}
	}
	lua_pushnil(L);
	return 1;
}

int LuaScriptInterface::luaSpellOnCastSpell(lua_State* L)
{
	// spell:onCastSpell(callback)
	Spell* spell = getUserdata<Spell>(L, 1);
	if (spell) {
		if (spell->spellType == SPELL_INSTANT) {
			InstantSpell* instant = dynamic_cast<InstantSpell*>(getUserdata<Spell>(L, 1));
			if (!instant->loadCallback()) {
				pushBoolean(L, false);
				return 1;
			}
			instant->m_scripted = EVENT_SCRIPT_TRUE;
			pushBoolean(L, true);
		} else if (spell->spellType == SPELL_RUNE) {
			RuneSpell* rune = dynamic_cast<RuneSpell*>(getUserdata<Spell>(L, 1));
			if (!rune->loadCallback()) {
				pushBoolean(L, false);
				return 1;
			}
			rune->m_scripted = EVENT_SCRIPT_TRUE;
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaSpellRegister(lua_State* L)
{
	// spell:register()
	Spell* spell = getUserdata<Spell>(L, 1);
	if (spell) {
		if (spell->spellType == SPELL_INSTANT) {
			InstantSpell* instant = dynamic_cast<InstantSpell*>(getUserdata<Spell>(L, 1));
			if (!instant->isScripted()) {
				pushBoolean(L, false);
				return 1;
			}
			pushBoolean(L, g_spells->registerInstantLuaEvent(instant));
		} else if (spell->spellType == SPELL_RUNE) {
			RuneSpell* rune = dynamic_cast<RuneSpell*>(getUserdata<Spell>(L, 1));
			if (rune->getMagicLevel() != 0 || rune->getLevel() != 0) {
				//Change information in the ItemType to get accurate description
				ItemType& iType = Item::items.getItemType(rune->getRuneItemId());
				iType.name = rune->getName();
				iType.runeMagLevel = rune->getMagicLevel();
				iType.runeLevel = rune->getLevel();
				iType.charges = rune->getCharges();
			}
			if (!rune->isScripted()) {
				pushBoolean(L, false);
				return 1;
			}
			pushBoolean(L, g_spells->registerRuneLuaEvent(rune));
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaSpellName(lua_State* L)
{
	// spell:name(name)
	Spell* spell = getUserdata<Spell>(L, 1);
	if (spell) {
		if (lua_gettop(L) == 1) {
			pushString(L, spell->getName());
		} else {
			spell->setName(getString(L, 2));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaSpellId(lua_State* L)
{
	// spell:id(id)
	Spell* spell = getUserdata<Spell>(L, 1);
	if (spell) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, spell->getId());
		} else {
			spell->setId(getNumber<uint8_t>(L, 2));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaSpellGroup(lua_State* L)
{
	// spell:group(primaryGroup[, secondaryGroup])
	Spell* spell = getUserdata<Spell>(L, 1);
	if (spell) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, spell->getGroup());
			lua_pushnumber(L, spell->getSecondaryGroup());
			return 2;
		} else if (lua_gettop(L) == 2) {
			SpellGroup_t group = getNumber<SpellGroup_t>(L, 2);
			if (group) {
				spell->setGroup(group);
				pushBoolean(L, true);
			} else if (isString(L, 2)) {
				group = stringToSpellGroup(getString(L, 2));
				if (group != SPELLGROUP_NONE) {
					spell->setGroup(group);
				} else {
					std::cout << "[Warning - Spell::group] Unknown group: " << getString(L, 2) << std::endl;
					pushBoolean(L, false);
					return 1;
				}
				pushBoolean(L, true);
			} else {
				std::cout << "[Warning - Spell::group] Unknown group: " << getString(L, 2) << std::endl;
				pushBoolean(L, false);
				return 1;
			}
		} else {
			SpellGroup_t primaryGroup = getNumber<SpellGroup_t>(L, 2);
			SpellGroup_t secondaryGroup = getNumber<SpellGroup_t>(L, 2);
			if (primaryGroup && secondaryGroup) {
				spell->setGroup(primaryGroup);
				spell->setSecondaryGroup(secondaryGroup);
				pushBoolean(L, true);
			} else if (isString(L, 2) && isString(L, 3)) {
				primaryGroup = stringToSpellGroup(getString(L, 2));
				if (primaryGroup != SPELLGROUP_NONE) {
					spell->setGroup(primaryGroup);
				} else {
					std::cout << "[Warning - Spell::group] Unknown primaryGroup: " << getString(L, 2) << std::endl;
					pushBoolean(L, false);
					return 1;
				}
				secondaryGroup = stringToSpellGroup(getString(L, 3));
				if (secondaryGroup != SPELLGROUP_NONE) {
					spell->setSecondaryGroup(secondaryGroup);
				} else {
					std::cout << "[Warning - Spell::group] Unknown secondaryGroup: " << getString(L, 3) << std::endl;
					pushBoolean(L, false);
					return 1;
				}
				pushBoolean(L, true);
			} else {
				std::cout << "[Warning - Spell::group] Unknown primaryGroup: " << getString(L, 2) << " or secondaryGroup: " << getString(L, 3) << std::endl;
				pushBoolean(L, false);
				return 1;
			}
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaSpellCooldown(lua_State* L)
{
	// spell:cooldown(primaryGroupCd[, secondaryGroupCd])
	Spell* spell = getUserdata<Spell>(L, 1);
	if (spell) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, spell->getCooldown());
			lua_pushnumber(L, spell->getSecondaryCooldown());
			return 2;
		} else if (lua_gettop(L) == 2) {
			spell->setCooldown(getNumber<uint32_t>(L, 2));
			pushBoolean(L, true);
		} else {
			spell->setCooldown(getNumber<uint32_t>(L, 2));
			spell->setSecondaryCooldown(getNumber<uint32_t>(L, 3));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaSpellGroupCooldown(lua_State* L)
{
	// spell:groupCooldown(cooldown)
	Spell* spell = getUserdata<Spell>(L, 1);
	if (spell) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, spell->getGroupCooldown());
		} else {
			spell->setGroupCooldown(getNumber<uint32_t>(L, 2));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaSpellLevel(lua_State* L)
{
	// spell:level(lvl)
	Spell* spell = getUserdata<Spell>(L, 1);
	if (spell) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, spell->getLevel());
		} else {
			spell->setLevel(getNumber<uint32_t>(L, 2));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaSpellMagicLevel(lua_State* L)
{
	// spell:magicLevel(lvl)
	Spell* spell = getUserdata<Spell>(L, 1);
	if (spell) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, spell->getMagicLevel());
		} else {
			spell->setMagicLevel(getNumber<uint32_t>(L, 2));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaSpellMana(lua_State* L)
{
	// spell:mana(mana)
	Spell* spell = getUserdata<Spell>(L, 1);
	if (spell) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, spell->getMana());
		} else {
			spell->setMana(getNumber<uint32_t>(L, 2));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaSpellManaPercent(lua_State* L)
{
	// spell:manaPercent(percent)
	Spell* spell = getUserdata<Spell>(L, 1);
	if (spell) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, spell->getManaPercent());
		} else {
			spell->setManaPercent(getNumber<uint32_t>(L, 2));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaSpellSoul(lua_State* L)
{
	// spell:soul(soul)
	Spell* spell = getUserdata<Spell>(L, 1);
	if (spell) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, spell->getSoulCost());
		} else {
			spell->setSoulCost(getNumber<uint32_t>(L, 2));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaSpellRange(lua_State* L)
{
	// spell:range(range)
	Spell* spell = getUserdata<Spell>(L, 1);
	if (spell) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, spell->getRange());
		} else {
			spell->setRange(getNumber<int32_t>(L, 2));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaSpellPremium(lua_State* L)
{
	// spell:isPremium(bool)
	Spell* spell = getUserdata<Spell>(L, 1);
	if (spell) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, spell->isPremium());
		} else {
			spell->setPremium(getBoolean(L, 2));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaSpellEnabled(lua_State* L)
{
	// spell:isEnabled(bool)
	Spell* spell = getUserdata<Spell>(L, 1);
	if (spell) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, spell->isEnabled());
		} else {
			spell->setEnabled(getBoolean(L, 2));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaSpellNeedTarget(lua_State* L)
{
	// spell:needTarget(bool)
	Spell* spell = getUserdata<Spell>(L, 1);
	if (spell) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, spell->getNeedTarget());
		} else {
			spell->setNeedTarget(getBoolean(L, 2));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaSpellNeedWeapon(lua_State* L)
{
	// spell:needWeapon(bool)
	Spell* spell = getUserdata<Spell>(L, 1);
	if (spell) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, spell->getNeedWeapon());
		} else {
			spell->setNeedWeapon(getBoolean(L, 2));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaSpellNeedLearn(lua_State* L)
{
	// spell:needLearn(bool)
	Spell* spell = getUserdata<Spell>(L, 1);
	if (spell) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, spell->getNeedLearn());
		} else {
			spell->setNeedLearn(getBoolean(L, 2));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaSpellSelfTarget(lua_State* L)
{
	// spell:isSelfTarget(bool)
	Spell* spell = getUserdata<Spell>(L, 1);
	if (spell) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, spell->getSelfTarget());
		} else {
			spell->setSelfTarget(getBoolean(L, 2));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaSpellBlocking(lua_State* L)
{
	// spell:isBlocking(blockingSolid, blockingCreature)
	Spell* spell = getUserdata<Spell>(L, 1);
	if (spell) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, spell->getBlockingSolid());
			pushBoolean(L, spell->getBlockingCreature());
			return 2;
		} else {
			spell->setBlockingSolid(getBoolean(L, 2));
			spell->setBlockingCreature(getBoolean(L, 3));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaSpellAggressive(lua_State* L)
{
	// spell:isAggressive(bool)
	Spell* spell = getUserdata<Spell>(L, 1);
	if (spell) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, spell->getAggressive());
		} else {
			spell->setAggressive(getBoolean(L, 2));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaSpellVocation(lua_State* L)
{
	// spell:vocation(vocation[, showInDescription = false)
	Spell* spell = getUserdata<Spell>(L, 1);
	if (spell) {
		if (lua_gettop(L) == 1) {
			lua_createtable(L, 0, 0);
			auto it = 0;
			for (auto voc : spell->getVocMap()) {
				++it;
				std::string s = std::to_string(it);
				char const *pchar = s.c_str();
				std::string name = g_vocations.getVocation(voc.first)->getName();
				setField(L, pchar, name);
			}
			setMetatable(L, -1, "Spell");
		} else {
			int32_t vocationId = g_vocations.getVocationId(getString(L, 2));
			if (vocationId != -1) {
				bool showInDescription = false;
				if (lua_gettop(L) == 3) {
					showInDescription = getBoolean(L, 3);
				}
				spell->addVocMap(vocationId, showInDescription);
				pushBoolean(L, true);
			}
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

// only for InstantSpells
int LuaScriptInterface::luaSpellWords(lua_State* L)
{
	// spell:words(words[, separator = ""])
	InstantSpell* spell = dynamic_cast<InstantSpell*>(getUserdata<Spell>(L, 1));
	if (spell) {
		// if spell != SPELL_INSTANT, it means that this actually is no InstantSpell, so we return nil
		if (spell->spellType != SPELL_INSTANT) {
			lua_pushnil(L);
			return 1;
		}

		if (lua_gettop(L) == 1) {
			pushString(L, spell->getWords());
			pushString(L, spell->getSeparator());
			return 2;
		} else {
			std::string sep = "";
			if (lua_gettop(L) == 3) {
				sep = getString(L, 3);
			}
			spell->setWords(getString(L, 2));
			spell->setSeparator(sep);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

// only for InstantSpells
int LuaScriptInterface::luaSpellNeedDirection(lua_State* L)
{
	// spell:needDirection(bool)
	InstantSpell* spell = dynamic_cast<InstantSpell*>(getUserdata<Spell>(L, 1));
	if (spell) {
		// if spell != SPELL_INSTANT, it means that this actually is no InstantSpell, so we return nil
		if (spell->spellType != SPELL_INSTANT) {
			lua_pushnil(L);
			return 1;
		}

		if (lua_gettop(L) == 1) {
			pushBoolean(L, spell->getNeedDirection());
		} else {
			spell->setNeedDirection(getBoolean(L, 2));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

// only for InstantSpells
int LuaScriptInterface::luaSpellHasParams(lua_State* L)
{
	// spell:hasParams(bool)
	InstantSpell* spell = dynamic_cast<InstantSpell*>(getUserdata<Spell>(L, 1));
	if (spell) {
		// if spell != SPELL_INSTANT, it means that this actually is no InstantSpell, so we return nil
		if (spell->spellType != SPELL_INSTANT) {
			lua_pushnil(L);
			return 1;
		}

		if (lua_gettop(L) == 1) {
			pushBoolean(L, spell->getHasParam());
		} else {
			spell->setHasParam(getBoolean(L, 2));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

// only for InstantSpells
int LuaScriptInterface::luaSpellHasPlayerNameParam(lua_State* L)
{
	// spell:hasPlayerNameParam(bool)
	InstantSpell* spell = dynamic_cast<InstantSpell*>(getUserdata<Spell>(L, 1));
	if (spell) {
		// if spell != SPELL_INSTANT, it means that this actually is no InstantSpell, so we return nil
		if (spell->spellType != SPELL_INSTANT) {
			lua_pushnil(L);
			return 1;
		}

		if (lua_gettop(L) == 1) {
			pushBoolean(L, spell->getHasPlayerNameParam());
		} else {
			spell->setHasPlayerNameParam(getBoolean(L, 2));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

// only for InstantSpells
int LuaScriptInterface::luaSpellNeedCasterTargetOrDirection(lua_State* L)
{
	// spell:needCasterTargetOrDirection(bool)
	InstantSpell* spell = dynamic_cast<InstantSpell*>(getUserdata<Spell>(L, 1));
	if (spell) {
		// if spell != SPELL_INSTANT, it means that this actually is no InstantSpell, so we return nil
		if (spell->spellType != SPELL_INSTANT) {
			lua_pushnil(L);
			return 1;
		}

		if (lua_gettop(L) == 1) {
			pushBoolean(L, spell->getNeedCasterTargetOrDirection());
		} else {
			spell->setNeedCasterTargetOrDirection(getBoolean(L, 2));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

// only for InstantSpells
int LuaScriptInterface::luaSpellIsBlockingWalls(lua_State* L)
{
	// spell:blockWalls(bool)
	InstantSpell* spell = dynamic_cast<InstantSpell*>(getUserdata<Spell>(L, 1));
	if (spell) {
		// if spell != SPELL_INSTANT, it means that this actually is no InstantSpell, so we return nil
		if (spell->spellType != SPELL_INSTANT) {
			lua_pushnil(L);
			return 1;
		}

		if (lua_gettop(L) == 1) {
			pushBoolean(L, spell->getBlockWalls());
		} else {
			spell->setBlockWalls(getBoolean(L, 2));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

// only for RuneSpells
int LuaScriptInterface::luaSpellRuneId(lua_State* L)
{
	// spell:runeId(id)
	RuneSpell* spell = dynamic_cast<RuneSpell*>(getUserdata<Spell>(L, 1));
	if (spell) {
		// if spell != SPELL_RUNE, it means that this actually is no RuneSpell, so we return nil
		if (spell->spellType != SPELL_RUNE) {
			lua_pushnil(L);
			return 1;
		}

		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, spell->getRuneItemId());
		} else {
			spell->setRuneItemId(getNumber<uint16_t>(L, 2));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

// only for RuneSpells
int LuaScriptInterface::luaSpellCharges(lua_State* L)
{
	// spell:charges(charges)
	RuneSpell* spell = dynamic_cast<RuneSpell*>(getUserdata<Spell>(L, 1));
	if (spell) {
		// if spell != SPELL_RUNE, it means that this actually is no RuneSpell, so we return nil
		if (spell->spellType != SPELL_RUNE) {
			lua_pushnil(L);
			return 1;
		}

		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, spell->getCharges());
		} else {
			spell->setCharges(getNumber<uint32_t>(L, 2));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

// only for RuneSpells
int LuaScriptInterface::luaSpellAllowFarUse(lua_State* L)
{
	// spell:allowFarUse(bool)
	RuneSpell* spell = dynamic_cast<RuneSpell*>(getUserdata<Spell>(L, 1));
	if (spell) {
		// if spell != SPELL_RUNE, it means that this actually is no RuneSpell, so we return nil
		if (spell->spellType != SPELL_RUNE) {
			lua_pushnil(L);
			return 1;
		}

		if (lua_gettop(L) == 1) {
			pushBoolean(L, spell->getAllowFarUse());
		} else {
			spell->setAllowFarUse(getBoolean(L, 2));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

// only for RuneSpells
int LuaScriptInterface::luaSpellBlockWalls(lua_State* L)
{
	// spell:blockWalls(bool)
	RuneSpell* spell = dynamic_cast<RuneSpell*>(getUserdata<Spell>(L, 1));
	if (spell) {
		// if spell != SPELL_RUNE, it means that this actually is no RuneSpell, so we return nil
		if (spell->spellType != SPELL_RUNE) {
			lua_pushnil(L);
			return 1;
		}

		if (lua_gettop(L) == 1) {
			pushBoolean(L, spell->getCheckLineOfSight());
		} else {
			spell->setCheckLineOfSight(getBoolean(L, 2));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

// only for RuneSpells
int LuaScriptInterface::luaSpellCheckFloor(lua_State* L)
{
	// spell:checkFloor(bool)
	RuneSpell* spell = dynamic_cast<RuneSpell*>(getUserdata<Spell>(L, 1));
	if (spell) {
		// if spell != SPELL_RUNE, it means that this actually is no RuneSpell, so we return nil
		if (spell->spellType != SPELL_RUNE) {
			lua_pushnil(L);
			return 1;
		}

		if (lua_gettop(L) == 1) {
			pushBoolean(L, spell->getCheckFloor());
		} else {
			spell->setCheckFloor(getBoolean(L, 2));
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaCreateAction(lua_State* L)
{
	// Action()
	Action* action = new Action(getScriptEnv()->getScriptInterface());
	if (action) {
		// action->fromLua = true;
		pushUserdata<Action>(L, action);
		setMetatable(L, -1, "Action");
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaActionOnUse(lua_State* L)
{
	// action:onUse(callback)
	Action* action = getUserdata<Action>(L, 1);
	if (action) {
		if (!action->loadCallback()) {
			pushBoolean(L, false);
			return 1;
		}
		action->m_scripted = EVENT_SCRIPT_TRUE;
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaActionRegister(lua_State* L)
{
	// action:register()
	Action* action = getUserdata<Action>(L, 1);
	if (action) {
		if (!action->isScripted()) {
			pushBoolean(L, false);
			return 1;
		}
		pushBoolean(L, g_actions->registerLuaEvent(action));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaActionItemId(lua_State* L)
{
	// action:id(ids)
	Action* action = getUserdata<Action>(L, 1);
	if (action) {
		int parameters = lua_gettop(L) - 1; // - 1 because self is a parameter aswell, which we want to skip ofc
		if (parameters > 1) {
			for (int i = 0; i < parameters; ++i) {
				action->addItemId(getNumber<uint32_t>(L, 2 + i));
			}
		} else {
			action->addItemId(getNumber<uint32_t>(L, 2));
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaActionActionId(lua_State* L)
{
	// action:aid(aids)
	Action* action = getUserdata<Action>(L, 1);
	if (action) {
		int parameters = lua_gettop(L) - 1; // - 1 because self is a parameter aswell, which we want to skip ofc
		if (parameters > 1) {
			for (int i = 0; i < parameters; ++i) {
				action->addActionId(getNumber<uint32_t>(L, 2 + i));
			}
		} else {
			action->addActionId(getNumber<uint32_t>(L, 2));
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaActionUniqueId(lua_State* L)
{
	// action:uid(uids)
	Action* action = getUserdata<Action>(L, 1);
	if (action) {
		int parameters = lua_gettop(L) - 1; // - 1 because self is a parameter aswell, which we want to skip ofc
		if (parameters > 1) {
			for (int i = 0; i < parameters; ++i) {
				action->addUniqueId(getNumber<uint32_t>(L, 2 + i));
			}
		} else {
			action->addUniqueId(getNumber<uint32_t>(L, 2));
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaActionAllowFarUse(lua_State* L)
{
	// action:allowFarUse(bool)
	Action* action = getUserdata<Action>(L, 1);
	if (action) {
		action->setAllowFarUse(getBoolean(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaActionBlockWalls(lua_State* L)
{
	// action:blockWalls(bool)
	Action* action = getUserdata<Action>(L, 1);
	if (action) {
		action->setCheckLineOfSight(getBoolean(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaActionCheckFloor(lua_State* L)
{
	// action:checkFloor(bool)
	Action* action = getUserdata<Action>(L, 1);
	if (action) {
		action->setCheckFloor(getBoolean(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaCreateTalkaction(lua_State* L)
{
	// TalkAction(words)
	TalkAction* talk = new TalkAction(getScriptEnv()->getScriptInterface());
	if (talk) {
		talk->setWords(getString(L, 2));
		// talk->fromLua = true;
		pushUserdata<TalkAction>(L, talk);
		setMetatable(L, -1, "TalkAction");
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaTalkactionOnSay(lua_State* L)
{
	// talkAction:onSay(callback)
	TalkAction* talk = getUserdata<TalkAction>(L, 1);
	if (talk) {
		if (!talk->loadCallback()) {
			pushBoolean(L, false);
			return 1;
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaTalkactionRegister(lua_State* L)
{
	// talkAction:register()
	TalkAction* talk = getUserdata<TalkAction>(L, 1);
	if (talk) {
		if (!talk->isScripted()) {
			pushBoolean(L, false);
			return 1;
		}
		pushBoolean(L, g_talkActions->registerLuaEvent(talk));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaTalkactionSeparator(lua_State* L)
{
	// talkAction:separator(sep)
	TalkAction* talk = getUserdata<TalkAction>(L, 1);
	if (talk) {
		talk->setSeparator(getString(L, 2).c_str());
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaCreateCreatureEvent(lua_State* L)
{
	// CreatureEvent(eventName)
	CreatureEvent* creature = new CreatureEvent(getScriptEnv()->getScriptInterface());
	if (creature) {
		creature->setName(getString(L, 2));
		// creature->fromLua = true;
		pushUserdata<CreatureEvent>(L, creature);
		setMetatable(L, -1, "CreatureEvent");
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaCreatureEventType(lua_State* L)
{
	// creatureevent:type(callback)
	CreatureEvent* creature = getUserdata<CreatureEvent>(L, 1);
	if (creature) {
		std::string typeName = getString(L, 2);
		std::string tmpStr = asLowerCaseString(typeName);
		if (tmpStr == "login") {
			creature->setEventType(CREATURE_EVENT_LOGIN);
		} else if (tmpStr == "logout") {
			creature->setEventType(CREATURE_EVENT_LOGOUT);
		} else if (tmpStr == "think") {
			creature->setEventType(CREATURE_EVENT_THINK);
		} else if (tmpStr == "preparedeath") {
			creature->setEventType(CREATURE_EVENT_PREPAREDEATH);
		} else if (tmpStr == "death") {
			creature->setEventType(CREATURE_EVENT_DEATH);
		} else if (tmpStr == "kill") {
			creature->setEventType(CREATURE_EVENT_KILL);
		} else if (tmpStr == "advance") {
			creature->setEventType(CREATURE_EVENT_ADVANCE);
		} else if (tmpStr == "modalwindow") {
			creature->setEventType(CREATURE_EVENT_MODALWINDOW);// only OTC
		} else if (tmpStr == "textedit") {
			creature->setEventType(CREATURE_EVENT_TEXTEDIT);
		// } else if (tmpStr == "healthchange") {
		// 	creature->setEventType(CREATURE_EVENT_HEALTHCHANGE);
		// } else if (tmpStr == "manachange") {
		// 	creature->setEventType(CREATURE_EVENT_MANACHANGE);
		} else if (tmpStr == "extendedopcode") {
			creature->setEventType(CREATURE_EVENT_EXTENDED_OPCODE);
		} else {
			std::cout << "[Error - CreatureEvent::configureLuaEvent] Invalid type for creature event: " << typeName << std::endl;
			pushBoolean(L, false);
		}
		creature->setLoaded(true);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaCreatureEventRegister(lua_State* L)
{
	// creatureevent:register()
	CreatureEvent* creature = getUserdata<CreatureEvent>(L, 1);
	if (creature) {
		if (!creature->isScripted()) {
			pushBoolean(L, false);
			return 1;
		}
		pushBoolean(L, g_creatureEvents->registerLuaEvent(creature));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaCreatureEventOnCallback(lua_State* L)
{
	// creatureevent:onLogin / logout / etc. (callback)
	CreatureEvent* creature = getUserdata<CreatureEvent>(L, 1);
	if (creature) {
		if (!creature->loadCallback()) {
			pushBoolean(L, false);
			return 1;
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaCreateMoveEvent(lua_State* L)
{
	// MoveEvent()
	MoveEvent* moveevent = new MoveEvent(getScriptEnv()->getScriptInterface());
	if (moveevent) {
		// moveevent->fromLua = true;
		pushUserdata<MoveEvent>(L, moveevent);
		setMetatable(L, -1, "MoveEvent");
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMoveEventType(lua_State* L)
{
	// moveevent:type(callback)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		std::string typeName = getString(L, 2);
		std::string tmpStr = asLowerCaseString(typeName);
		if (tmpStr == "stepin") {
			moveevent->setEventType(MOVE_EVENT_STEP_IN);
			moveevent->stepFunction = moveevent->StepInField;
		} else if (tmpStr == "stepout") {
			moveevent->setEventType(MOVE_EVENT_STEP_OUT);
			moveevent->stepFunction = moveevent->StepOutField;
		} else if (tmpStr == "equip") {
			moveevent->setEventType(MOVE_EVENT_EQUIP);
			moveevent->equipFunction = moveevent->EquipItem;
		} else if (tmpStr == "deequip") {
			moveevent->setEventType(MOVE_EVENT_DE_EQUIP);
			moveevent->equipFunction = moveevent->DeEquipItem;
		} else if (tmpStr == "additem") {
			moveevent->setEventType(MOVE_EVENT_ADD_TILEITEM);
			moveevent->moveFunction = moveevent->AddItemField;
		} else if (tmpStr == "removeitem") {
			moveevent->setEventType(MOVE_EVENT_REMOVE_TILEITEM);
			moveevent->moveFunction = moveevent->RemoveItemField;
		} else {
			std::cout << "Error: [MoveEvent::configureMoveEvent] No valid event name " << typeName << std::endl;
			pushBoolean(L, false);
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMoveEventRegister(lua_State* L)
{
	// moveevent:register()
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		if (!moveevent->isScripted()) {
			pushBoolean(L, g_moveEvents->registerLuaFunction(moveevent));
			return 1;
		}
		pushBoolean(L, g_moveEvents->registerLuaEvent(moveevent));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMoveEventOnCallback(lua_State* L)
{
	// moveevent:onEquip / deEquip / etc. (callback)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		if (!moveevent->loadCallback()) {
			pushBoolean(L, false);
			return 1;
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMoveEventSlot(lua_State* L)
{
	// moveevent:slot(slot)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		if (moveevent->getEventType() == MOVE_EVENT_EQUIP || moveevent->getEventType() == MOVE_EVENT_DE_EQUIP) {
			if (!moveevent->getSlotName().empty()) {
				std::string slotName = getString(L, 2);
				std::string tmpStr = asLowerCaseString(slotName);
				tmpStr = asLowerCaseString(moveevent->getSlotName());
				if (tmpStr == "head") {
					moveevent->setSlot(SLOTP_HEAD);
				} else if (tmpStr == "necklace") {
					moveevent->setSlot(SLOTP_NECKLACE);
				} else if (tmpStr == "backpack") {
					moveevent->setSlot(SLOTP_BACKPACK);
				} else if (tmpStr == "armor" || tmpStr == "body") {
					moveevent->setSlot(SLOTP_ARMOR);
				} else if (tmpStr == "right-hand") {
					moveevent->setSlot(SLOTP_RIGHT);
				} else if (tmpStr == "left-hand") {
					moveevent->setSlot(SLOTP_LEFT);
				} else if (tmpStr == "hand" || tmpStr == "shield") {
					moveevent->setSlot(SLOTP_RIGHT | SLOTP_LEFT);
				} else if (tmpStr == "legs") {
					moveevent->setSlot(SLOTP_LEGS);
				} else if (tmpStr == "feet") {
					moveevent->setSlot(SLOTP_FEET);
				} else if (tmpStr == "ring") {
					moveevent->setSlot(SLOTP_RING);
				} else if (tmpStr == "ammo") {
					moveevent->setSlot(SLOTP_AMMO);
				} else {
					std::cout << "[Warning - MoveEvent::configureMoveEvent] Unknown slot type: " << moveevent->getSlotName() << std::endl;
				}
			}
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMoveEventLevel(lua_State* L)
{
	// moveevent:level(lvl)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		moveevent->setRequiredLevel(getNumber<uint32_t>(L, 2));
		moveevent->setWieldInfo(WIELDINFO_LEVEL);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMoveEventMagLevel(lua_State* L)
{
	// moveevent:magicLevel(lvl)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		moveevent->setRequiredMagLevel(getNumber<uint32_t>(L, 2));
		moveevent->setWieldInfo(WIELDINFO_MAGLV);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMoveEventPremium(lua_State* L)
{
	// moveevent:premium(bool)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		moveevent->setNeedPremium(getBoolean(L, 2));
		moveevent->setWieldInfo(WIELDINFO_PREMIUM);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMoveEventVocation(lua_State* L)
{
	// moveevent:vocation(vocName[, showInDescription = false, lastVoc = false])
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		moveevent->addVocEquipMap(getString(L, 2));
		moveevent->setWieldInfo(WIELDINFO_VOCREQ);
		std::string tmp;
		bool showInDescription = false;
		bool lastVoc = false;
		if (getBoolean(L, 3)) {
			showInDescription = getBoolean(L, 3);
		}
		if (getBoolean(L, 4)) {
			lastVoc = getBoolean(L, 4);
		}
		if (showInDescription) {
			if (moveevent->getVocationString().empty()) {
				tmp = asLowerCaseString(getString(L, 2));
				tmp += "s";
				moveevent->setVocationString(tmp);
			} else {
				tmp = moveevent->getVocationString();
				if (lastVoc) {
					tmp += " and ";
				} else {
					tmp += ", ";
				}
				tmp += asLowerCaseString(getString(L, 2));
				tmp += "s";
				moveevent->setVocationString(tmp);
			}
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMoveEventItemId(lua_State* L)
{
	// moveevent:id(ids)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		int parameters = lua_gettop(L) - 1; // - 1 because self is a parameter aswell, which we want to skip ofc
		if (parameters > 1) {
			for (int i = 0; i < parameters; ++i) {
				moveevent->addItemId(getNumber<uint32_t>(L, 2 + i));
			}
		} else {
			moveevent->addItemId(getNumber<uint32_t>(L, 2));
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMoveEventActionId(lua_State* L)
{
	// moveevent:aid(ids)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		int parameters = lua_gettop(L) - 1; // - 1 because self is a parameter aswell, which we want to skip ofc
		if (parameters > 1) {
			for (int i = 0; i < parameters; ++i) {
				moveevent->addActionId(getNumber<uint32_t>(L, 2 + i));
			}
		} else {
			moveevent->addActionId(getNumber<uint32_t>(L, 2));
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMoveEventUniqueId(lua_State* L)
{
	// moveevent:uid(ids)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		int parameters = lua_gettop(L) - 1; // - 1 because self is a parameter aswell, which we want to skip ofc
		if (parameters > 1) {
			for (int i = 0; i < parameters; ++i) {
				moveevent->addUniqueId(getNumber<uint32_t>(L, 2 + i));
			}
		} else {
			moveevent->addUniqueId(getNumber<uint32_t>(L, 2));
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaCreateGlobalEvent(lua_State* L)
{
	// GlobalEvent(eventName)
	GlobalEvent* global = new GlobalEvent(getScriptEnv()->getScriptInterface());
	if (global) {
		global->setName(getString(L, 2));
		global->setEventType(GLOBALEVENT_NONE);
		// global->fromLua = true;
		pushUserdata<GlobalEvent>(L, global);
		setMetatable(L, -1, "GlobalEvent");
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaGlobalEventType(lua_State* L)
{
	// globalevent:type(callback)
	GlobalEvent* global = getUserdata<GlobalEvent>(L, 1);
	if (global) {
		std::string typeName = getString(L, 2);
		std::string tmpStr = asLowerCaseString(typeName);
		if (tmpStr == "startup") {
			global->setEventType(GLOBALEVENT_STARTUP);
		} else if (tmpStr == "shutdown") {
			global->setEventType(GLOBALEVENT_SHUTDOWN);
		} else if (tmpStr == "record") {
			global->setEventType(GLOBALEVENT_RECORD);
		} else {
			std::cout << "[Error - CreatureEvent::configureLuaEvent] Invalid type for global event: " << typeName << std::endl;
			pushBoolean(L, false);
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaGlobalEventRegister(lua_State* L)
{
	// globalevent:register()
	GlobalEvent* globalevent = getUserdata<GlobalEvent>(L, 1);
	if (globalevent) {
		if (!globalevent->isScripted()) {
			pushBoolean(L, false);
			return 1;
		}
		pushBoolean(L, g_globalEvents->registerLuaEvent(globalevent));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaGlobalEventOnCallback(lua_State* L)
{
	// globalevent:onThink / record / etc. (callback)
	GlobalEvent* globalevent = getUserdata<GlobalEvent>(L, 1);
	if (globalevent) {
		if (!globalevent->loadCallback()) {
			pushBoolean(L, false);
			return 1;
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaGlobalEventTime(lua_State* L)
{
	// globalevent:time(time)
	GlobalEvent* globalevent = getUserdata<GlobalEvent>(L, 1);
	if (globalevent) {
		std::string timer = getString(L, 2);
		std::vector<int32_t> params = vectorAtoi(explodeString(timer, ":"));

		int32_t hour = params.front();
		if (hour < 0 || hour > 23) {
			std::cout << "[Error - GlobalEvent::configureEvent] Invalid hour \"" << timer << "\" for globalevent with name: " << globalevent->getName() << std::endl;
			pushBoolean(L, false);
			return 1;
		}

		globalevent->setInterval(hour << 16);

		int32_t min = 0;
		int32_t sec = 0;
		if (params.size() > 1) {
			min = params[1];
			if (min < 0 || min > 59) {
				std::cout << "[Error - GlobalEvent::configureEvent] Invalid minute \"" << timer << "\" for globalevent with name: " << globalevent->getName() << std::endl;
				pushBoolean(L, false);
				return 1;
			}

			if (params.size() > 2) {
				sec = params[2];
				if (sec < 0 || sec > 59) {
					std::cout << "[Error - GlobalEvent::configureEvent] Invalid second \"" << timer << "\" for globalevent with name: " << globalevent->getName() << std::endl;
					pushBoolean(L, false);
					return 1;
				}
			}
		}

		time_t current_time = time(nullptr);
		tm* timeinfo = localtime(&current_time);
		timeinfo->tm_hour = hour;
		timeinfo->tm_min = min;
		timeinfo->tm_sec = sec;

		time_t difference = static_cast<time_t>(difftime(mktime(timeinfo), current_time));
		if (difference < 0) {
			difference += 86400;
		}

		globalevent->setLastExecution(current_time + difference);
		globalevent->setEventType(GLOBALEVENT_TIMER);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaGlobalEventInterval(lua_State* L)
{
	// globalevent:interval(interval)
	GlobalEvent* globalevent = getUserdata<GlobalEvent>(L, 1);
	if (globalevent) {
		globalevent->setInterval(getNumber<uint32_t>(L, 2));
		globalevent->setLastExecution(OTSYS_TIME() + getNumber<uint32_t>(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

// Weapon
int LuaScriptInterface::luaCreateWeapon(lua_State* L)
{
	// Weapon(type)
	WeaponType_t type = getNumber<WeaponType_t>(L, 2);
	switch (type) {
		case WEAPON_SWORD:
		case WEAPON_AXE:
		case WEAPON_CLUB: {
			WeaponMelee* weapon = new WeaponMelee(getScriptEnv()->getScriptInterface());
			if (weapon) {
				pushUserdata<WeaponMelee>(L, weapon);
				setMetatable(L, -1, "Weapon");
				weapon->weaponType = type;
			} else {
				lua_pushnil(L);
			}
			break;
		}
		case WEAPON_DIST:
		case WEAPON_AMMO: {
			WeaponDistance* weapon = new WeaponDistance(getScriptEnv()->getScriptInterface());
			if (weapon) {
				pushUserdata<WeaponDistance>(L, weapon);
				setMetatable(L, -1, "Weapon");
				weapon->weaponType = type;
			} else {
				lua_pushnil(L);
			}
			break;
		}
		case WEAPON_WAND: {
			WeaponWand* weapon = new WeaponWand(getScriptEnv()->getScriptInterface());
			if (weapon) {
				pushUserdata<WeaponWand>(L, weapon);
				setMetatable(L, -1, "Weapon");
				weapon->weaponType = type;
			} else {
				lua_pushnil(L);
			}
			break;
		}
		default: {
			lua_pushnil(L);
			break;
		}
	}
	return 1;
}

int LuaScriptInterface::luaWeaponAction(lua_State* L)
{
	// weapon:action(callback)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		std::string typeName = getString(L, 2);
		std::string tmpStr = asLowerCaseString(typeName);
		if (tmpStr == "removecount") {
			weapon->ammoAction = AMMOACTION_REMOVECOUNT;
		} else if (tmpStr == "removecharge") {
			weapon->ammoAction = AMMOACTION_REMOVECHARGE;
		} else if (tmpStr == "move") {
			weapon->ammoAction = AMMOACTION_MOVE;
		} else {
			std::cout << "Error: [Weapon::action] No valid action " << typeName << std::endl;
			pushBoolean(L, false);
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaWeaponRegister(lua_State* L)
{
	// weapon:register()
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		if (weapon->weaponType == WEAPON_DIST || weapon->weaponType == WEAPON_AMMO) {
			weapon = getUserdata<WeaponDistance>(L, 1);
		} else if (weapon->weaponType == WEAPON_WAND) {
			weapon = getUserdata<WeaponWand>(L, 1);
		} else {
			weapon = getUserdata<WeaponMelee>(L, 1);
		}

		uint16_t id = weapon->getID();
		ItemType& it = Item::items.getItemType(id);
		it.weaponType = weapon->weaponType;

		// if (weapon->getWieldInfo() != 0) {
		// 	it.wieldInfo = weapon->getWieldInfo();
		// 	it.vocationString = weapon->getVocationString();
		// 	it.minReqLevel = weapon->getRequiredLevel();
		// 	it.minReqMagicLevel = weapon->getReqMagLv();
		// }

		weapon->configureWeapon(it);
		pushBoolean(L, g_weapons->registerLuaEvent(weapon));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaWeaponOnUseWeapon(lua_State* L)
{
	// weapon:onUseWeapon(callback)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		if (!weapon->loadCallback()) {
			pushBoolean(L, false);
			return 1;
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaWeaponUnproperly(lua_State* L)
{
	// weapon:wieldedUnproperly(bool)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		weapon->setWieldUnproperly(getBoolean(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaWeaponLevel(lua_State* L)
{
	// weapon:level(lvl)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		weapon->setRequiredLevel(getNumber<uint32_t>(L, 2));
		weapon->setWieldInfo(WIELDINFO_LEVEL);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaWeaponMagicLevel(lua_State* L)
{
	// weapon:magicLevel(lvl)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		weapon->setRequiredMagLevel(getNumber<uint32_t>(L, 2));
		weapon->setWieldInfo(WIELDINFO_MAGLV);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaWeaponMana(lua_State* L)
{
	// weapon:mana(mana)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		weapon->setMana(getNumber<uint32_t>(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaWeaponManaPercent(lua_State* L)
{
	// weapon:manaPercent(percent)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		weapon->setManaPercent(getNumber<uint32_t>(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaWeaponHealth(lua_State* L)
{
	// weapon:health(health)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		weapon->setHealth(getNumber<int32_t>(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaWeaponHealthPercent(lua_State* L)
{
	// weapon:healthPercent(percent)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		// weapon->setHealthPercent(getNumber<uint32_t>(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaWeaponSoul(lua_State* L)
{
	// weapon:soul(soul)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		// weapon->setSoul(getNumber<uint32_t>(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaWeaponBreakChance(lua_State* L)
{
	// weapon:breakChance(percent)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		// weapon->setBreakChance(getNumber<uint32_t>(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaWeaponWandDamage(lua_State* L)
{
	// weapon:damage(damage[min, max]) only use this if the weapon is a wand!
	WeaponWand* weapon = getUserdata<WeaponWand>(L, 1);
	if (weapon) {
		weapon->setMinChange(getNumber<uint32_t>(L, 2));
		if (lua_gettop(L) > 2) {
			weapon->setMaxChange(getNumber<uint32_t>(L, 3));
		} else {
			weapon->setMaxChange(getNumber<uint32_t>(L, 2));
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaWeaponElement(lua_State* L)
{
	// weapon:element(combatType)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		if (!getNumber<CombatType_t>(L, 2)) {
			std::string element = getString(L, 2);
			std::string tmpStrValue = asLowerCaseString(element);
			if (tmpStrValue == "earth") {
				weapon->params.combatType = COMBAT_EARTHDAMAGE;
			} else if (tmpStrValue == "ice") {
				weapon->params.combatType = COMBAT_ICEDAMAGE;
			} else if (tmpStrValue == "energy") {
				weapon->params.combatType = COMBAT_ENERGYDAMAGE;
			} else if (tmpStrValue == "fire") {
				weapon->params.combatType = COMBAT_FIREDAMAGE;
			} else if (tmpStrValue == "death") {
				weapon->params.combatType = COMBAT_DEATHDAMAGE;
			} else if (tmpStrValue == "holy") {
				weapon->params.combatType = COMBAT_HOLYDAMAGE;
			} else {
				std::cout << "[Warning - weapon:element] Type \"" << element << "\" does not exist." << std::endl;
			}
		} else {
			weapon->params.combatType = getNumber<CombatType_t>(L, 2);
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaWeaponPremium(lua_State* L)
{
	// weapon:premium(bool)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		weapon->setNeedPremium(getBoolean(L, 2));
		weapon->setWieldInfo(WIELDINFO_PREMIUM);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaWeaponVocation(lua_State* L)
{
	// weapon:vocation(vocName[, showInDescription = false, lastVoc = false])
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		weapon->addVocWeaponMap(getString(L, 2));
		weapon->setWieldInfo(WIELDINFO_VOCREQ);
		std::string tmp;
		bool showInDescription = getBoolean(L, 3, false);
		bool lastVoc = getBoolean(L, 4, false);

		if (showInDescription) {
			if (weapon->getVocationString().empty()) {
				tmp = asLowerCaseString(getString(L, 2));
				tmp += "s";
				weapon->setVocationString(tmp);
			} else {
				tmp = weapon->getVocationString();
				if (lastVoc) {
					tmp += " and ";
				} else {
					tmp += ", ";
				}
				tmp += asLowerCaseString(getString(L, 2));
				tmp += "s";
				weapon->setVocationString(tmp);
			}
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaWeaponId(lua_State* L)
{
	// weapon:id(id)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		weapon->setID(getNumber<uint32_t>(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaWeaponAttack(lua_State* L)
{
	// weapon:attack(atk)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		uint16_t id = weapon->getID();
		ItemType& it = Item::items.getItemType(id);
		it.attack = getNumber<int32_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaWeaponDefense(lua_State* L)
{
	// weapon:defense(defense[, extraDefense])
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		uint16_t id = weapon->getID();
		ItemType& it = Item::items.getItemType(id);
		it.defense = getNumber<int32_t>(L, 2);
		if (lua_gettop(L) > 2) {
			it.extraDefense = getNumber<int32_t>(L, 3);
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaWeaponRange(lua_State* L)
{
	// weapon:range(range)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		uint16_t id = weapon->getID();
		ItemType& it = Item::items.getItemType(id);
		it.shootRange = getNumber<uint8_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaWeaponCharges(lua_State* L)
{
	// weapon:charges(charges[, showCharges = true])
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		bool showCharges = getBoolean(L, 3, true);
		uint16_t id = weapon->getID();
		ItemType& it = Item::items.getItemType(id);

		it.charges = getNumber<uint8_t>(L, 2);
		it.showCharges = showCharges;
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaWeaponDuration(lua_State* L)
{
	// weapon:duration(duration[, showDuration = true])
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		bool showDuration = getBoolean(L, 3, true);
		uint16_t id = weapon->getID();
		ItemType& it = Item::items.getItemType(id);

		it.decayTime = getNumber<uint32_t>(L, 2);
		it.showDuration = showDuration;
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaWeaponDecayTo(lua_State* L)
{
	// weapon:decayTo([itemid = 0]
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		uint16_t itemid = getNumber<uint16_t>(L, 2, 0);
		uint16_t id = weapon->getID();
		ItemType& it = Item::items.getItemType(id);

		it.decayTo = itemid;
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaWeaponTransformEquipTo(lua_State* L)
{
	// weapon:transformEquipTo(itemid)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		uint16_t id = weapon->getID();
		ItemType& it = Item::items.getItemType(id);
		it.transformEquipTo = getNumber<uint16_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaWeaponTransformDeEquipTo(lua_State* L)
{
	// weapon:transformDeEquipTo(itemid)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		uint16_t id = weapon->getID();
		ItemType& it = Item::items.getItemType(id);
		it.transformDeEquipTo = getNumber<uint16_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaWeaponShootType(lua_State* L)
{
	// weapon:shootType(type)
	Weapon* weapon = getUserdata<Weapon>(L, 1);
	if (weapon) {
		uint16_t id = weapon->getID();
		ItemType& it = Item::items.getItemType(id);
		it.shootType = getNumber<ShootEffect_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}


// MonsterType
int LuaScriptInterface::luaMonsterTypeCreate(lua_State* L)
{
	// MonsterType(name)
	MonsterType* monsterType = g_monsters.getMonsterType(getString(L, 2));
	if (monsterType) {
		pushUserdata<MonsterType>(L, monsterType);
		setMetatable(L, -1, "MonsterType");
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeIsAttackable(lua_State* L)
{
	// get: monsterType:isAttackable() set: monsterType:isAttackable(bool)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, monsterType->isAttackable);
		} else {
			monsterType->isAttackable = getBoolean(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeIsConvinceable(lua_State* L)
{
	// get: monsterType:isConvinceable() set: monsterType:isConvinceable(bool)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, monsterType->isConvinceable);
		} else {
			monsterType->isConvinceable = getBoolean(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeIsSummonable(lua_State* L)
{
	// get: monsterType:isSummonable() set: monsterType:isSummonable(bool)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, monsterType->isSummonable);
		} else {
			monsterType->isSummonable = getBoolean(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeIsIllusionable(lua_State* L)
{
	// get: monsterType:isIllusionable() set: monsterType:isIllusionable(bool)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, monsterType->isIllusionable);
		} else {
			monsterType->isIllusionable = getBoolean(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeIsHostile(lua_State* L)
{
	// get: monsterType:isHostile() set: monsterType:isHostile(bool)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, monsterType->isHostile);
		} else {
			monsterType->isHostile = getBoolean(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeIsPushable(lua_State* L)
{
	// get: monsterType:isPushable() set: monsterType:isPushable(bool)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, monsterType->pushable);
		} else {
			monsterType->pushable = getBoolean(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeIsHealthHidden(lua_State* L)
{
	// get: monsterType:isHealthHidden() set: monsterType:isHealthHidden(bool)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, monsterType->hiddenHealth);
		} else {
			monsterType->hiddenHealth = getBoolean(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeCanPushItems(lua_State* L)
{
	// get: monsterType:canPushItems() set: monsterType:canPushItems(bool)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, monsterType->canPushItems);
		} else {
			monsterType->canPushItems = getBoolean(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeCanPushCreatures(lua_State* L)
{
	// get: monsterType:canPushCreatures() set: monsterType:canPushCreatures(bool)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushBoolean(L, monsterType->canPushCreatures);
		} else {
			monsterType->canPushCreatures = getBoolean(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int32_t LuaScriptInterface::luaMonsterTypeName(lua_State* L)
{
	// get: monsterType:name() set: monsterType:name(name)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushString(L, monsterType->name);
		} else {
			monsterType->name = getString(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeNameDescription(lua_State* L)
{
	// get: monsterType:nameDescription() set: monsterType:nameDescription(desc)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushString(L, monsterType->nameDescription);
		} else {
			monsterType->nameDescription = getString(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeHealth(lua_State* L)
{
	// get: monsterType:health() set: monsterType:health(health)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, monsterType->health);
		} else {
			monsterType->health = getNumber<int32_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeMaxHealth(lua_State* L)
{
	// get: monsterType:maxHealth() set: monsterType:maxHealth(health)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, monsterType->healthMax);
		} else {
			monsterType->healthMax = getNumber<int32_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeRunHealth(lua_State* L)
{
	// get: monsterType:runHealth() set: monsterType:runHealth(health)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, monsterType->runAwayHealth);
		} else {
			monsterType->runAwayHealth = getNumber<int32_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeExperience(lua_State* L)
{
	// get: monsterType:experience() set: monsterType:experience(exp)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, monsterType->experience);
		} else {
			monsterType->experience = getNumber<uint64_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeCombatImmunities(lua_State* L)
{
	// get: monsterType:combatImmunities() set: monsterType:combatImmunities(immunity)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, monsterType->damageImmunities);
		} else {
			std::string immunity = getString(L, 2);
			if (immunity == "physical") {
				monsterType->damageImmunities |= COMBAT_PHYSICALDAMAGE;
				pushBoolean(L, true);
			} else if (immunity == "energy") {
				monsterType->damageImmunities |= COMBAT_ENERGYDAMAGE;
				pushBoolean(L, true);
			} else if (immunity == "fire") {
				monsterType->damageImmunities |= COMBAT_FIREDAMAGE;
				pushBoolean(L, true);
			} else if (immunity == "poison" || immunity == "earth") {
				monsterType->damageImmunities |= COMBAT_EARTHDAMAGE;
				pushBoolean(L, true);
			} else if (immunity == "drown") {
				monsterType->damageImmunities |= COMBAT_DROWNDAMAGE;
				pushBoolean(L, true);
			} else if (immunity == "ice") {
				monsterType->damageImmunities |= COMBAT_ICEDAMAGE;
				pushBoolean(L, true);
			} else if (immunity == "holy") {
				monsterType->damageImmunities |= COMBAT_HOLYDAMAGE;
				pushBoolean(L, true);
			} else if (immunity == "death") {
				monsterType->damageImmunities |= COMBAT_DEATHDAMAGE;
				pushBoolean(L, true);
			} else if (immunity == "lifedrain") {
				monsterType->damageImmunities |= COMBAT_LIFEDRAIN;
				pushBoolean(L, true);
			} else if (immunity == "manadrain") {
				monsterType->damageImmunities |= COMBAT_MANADRAIN;
				pushBoolean(L, true);
			} else {
				std::cout << "[Warning - Monsters::loadMonster] Unknown immunity name " << immunity << " for monster: " << monsterType->name << std::endl;
				lua_pushnil(L);
			}
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeConditionImmunities(lua_State* L)
{
	// get: monsterType:conditionImmunities() set: monsterType:conditionImmunities(immunity)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, monsterType->conditionImmunities);
		} else {
			std::string immunity = getString(L, 2);
			if (immunity == "physical") {
				monsterType->conditionImmunities |= CONDITION_PHYSICAL;
				pushBoolean(L, true);
			} else if (immunity == "energy") {
				monsterType->conditionImmunities |= CONDITION_ENERGY;
				pushBoolean(L, true);
			} else if (immunity == "fire") {
				monsterType->conditionImmunities |= CONDITION_FIRE;
				pushBoolean(L, true);
			} else if (immunity == "poison" || immunity == "earth") {
				monsterType->conditionImmunities |= CONDITION_POISON;
				pushBoolean(L, true);
			} else if (immunity == "drown") {
				monsterType->conditionImmunities |= CONDITION_DROWN;
				pushBoolean(L, true);
			} else if (immunity == "ice") {
				monsterType->conditionImmunities |= CONDITION_FREEZING;
				pushBoolean(L, true);
			} else if (immunity == "holy") {
				monsterType->conditionImmunities |= CONDITION_DAZZLED;
				pushBoolean(L, true);
			} else if (immunity == "death") {
				monsterType->conditionImmunities |= CONDITION_CURSED;
				pushBoolean(L, true);
			} else if (immunity == "paralyze") {
				monsterType->conditionImmunities |= CONDITION_PARALYZE;
				pushBoolean(L, true);
			} else if (immunity == "outfit") {
				monsterType->conditionImmunities |= CONDITION_OUTFIT;
				pushBoolean(L, true);
			} else if (immunity == "drunk") {
				monsterType->conditionImmunities |= CONDITION_DRUNK;
				pushBoolean(L, true);
			} else if (immunity == "invisible" || immunity == "invisibility") {
				monsterType->conditionImmunities |= CONDITION_INVISIBLE;
				pushBoolean(L, true);
			} else if (immunity == "bleed") {
				monsterType->conditionImmunities |= CONDITION_BLEEDING;
				pushBoolean(L, true);
			} else {
				std::cout << "[Warning - Monsters::loadMonster] Unknown immunity name " << immunity << " for monster: " << monsterType->name << std::endl;
				lua_pushnil(L);
			}
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeGetAttackList(lua_State* L)
{
	// monsterType:getAttackList()
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (!monsterType) {
		lua_pushnil(L);
		return 1;
	}

	lua_createtable(L, monsterType->spellAttackList.size(), 0);

	int index = 0;
	for (const auto& spellBlock : monsterType->spellAttackList) {
		lua_createtable(L, 0, 8);

		setField(L, "chance", spellBlock.chance);
		setField(L, "isCombatSpell", spellBlock.combatSpell ? 1 : 0);
		setField(L, "isMelee", spellBlock.isMelee ? 1 : 0);
		setField(L, "minCombatValue", spellBlock.minCombatValue);
		setField(L, "maxCombatValue", spellBlock.maxCombatValue);
		setField(L, "range", spellBlock.range);
		setField(L, "speed", spellBlock.speed);
		pushUserdata<CombatSpell>(L, static_cast<CombatSpell*>(spellBlock.spell));
		lua_setfield(L, -2, "spell");

		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeAddAttack(lua_State* L)
{
	// monsterType:addAttack(monsterspell)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		MonsterSpell* spell = getUserdata<MonsterSpell>(L, 2);
		if (spell) {
			spellBlock_t sb;
			if (g_monsters.deserializeSpell(spell, sb, monsterType->name)) {
				monsterType->spellAttackList.push_back(std::move(sb));
			} else {
				std::cout << monsterType->name << std::endl;
				std::cout << "[Warning - Monsters::loadMonster] Cant load spell. " << spell->name << std::endl;
			}
		} else {
			lua_pushnil(L);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeGetDefenseList(lua_State* L)
{
	// monsterType:getDefenseList()
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (!monsterType) {
		lua_pushnil(L);
		return 1;
	}

	lua_createtable(L, monsterType->spellDefenseList.size(), 0);


	int index = 0;
	for (const auto& spellBlock : monsterType->spellDefenseList) {
		lua_createtable(L, 0, 8);

		setField(L, "chance", spellBlock.chance);
		setField(L, "isCombatSpell", spellBlock.combatSpell ? 1 : 0);
		setField(L, "isMelee", spellBlock.isMelee ? 1 : 0);
		setField(L, "minCombatValue", spellBlock.minCombatValue);
		setField(L, "maxCombatValue", spellBlock.maxCombatValue);
		setField(L, "range", spellBlock.range);
		setField(L, "speed", spellBlock.speed);
		pushUserdata<CombatSpell>(L, static_cast<CombatSpell*>(spellBlock.spell));
		lua_setfield(L, -2, "spell");

		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeAddDefense(lua_State* L)
{
	// monsterType:addDefense(monsterspell)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		MonsterSpell* spell = getUserdata<MonsterSpell>(L, 2);
		if (spell) {
			spellBlock_t sb;
			if (g_monsters.deserializeSpell(spell, sb, monsterType->name)) {
				monsterType->spellDefenseList.push_back(std::move(sb));
			} else {
				std::cout << monsterType->name << std::endl;
				std::cout << "[Warning - Monsters::loadMonster] Cant load spell. " << spell->name << std::endl;
			}
		} else {
			lua_pushnil(L);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeGetElementList(lua_State* L)
{
	// monsterType:getElementList()
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (!monsterType) {
		lua_pushnil(L);
		return 1;
	}

	lua_createtable(L, monsterType->elementMap.size(), 0);
	for (const auto& elementEntry : monsterType->elementMap) {
		lua_pushnumber(L, elementEntry.second);
		lua_rawseti(L, -2, elementEntry.first);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeAddElement(lua_State* L)
{
	// monsterType:addElement(type, percent)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		CombatType_t element = getNumber<CombatType_t>(L, 2);
		monsterType->elementMap[element] = getNumber<int32_t>(L, 3);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeGetVoices(lua_State* L)
{
	// monsterType:getVoices()
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (!monsterType) {
		lua_pushnil(L);
		return 1;
	}

	int index = 0;
	lua_createtable(L, monsterType->voiceVector.size(), 0);
	for (const auto& voiceBlock : monsterType->voiceVector) {
		lua_createtable(L, 0, 2);
		setField(L, "text", voiceBlock.text);
		setField(L, "yellText", voiceBlock.yellText);
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeAddVoice(lua_State* L)
{
	// monsterType:addVoice(sentence, interval, chance, yell)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		voiceBlock_t voice;
		voice.text = getString(L, 2);
		monsterType->yellSpeedTicks = getNumber<uint32_t>(L, 3);
		monsterType->yellChance = getNumber<uint32_t>(L, 4);
		voice.yellText = getBoolean(L, 5);
		monsterType->voiceVector.push_back(voice);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeGetLoot(lua_State* L)
{
	// monsterType:getLoot()
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (!monsterType) {
		lua_pushnil(L);
		return 1;
	}

	static const std::function<void(const std::vector<LootBlock>&)> parseLoot = [&](const std::vector<LootBlock>& lootList) {
		lua_createtable(L, lootList.size(), 0);

		int index = 0;
		for (const auto& lootBlock : lootList) {
			lua_createtable(L, 0, 7);

			setField(L, "itemId", lootBlock.id);
			setField(L, "chance", lootBlock.chance);
			setField(L, "subType", lootBlock.subType);
			setField(L, "maxCount", lootBlock.countmax);
			setField(L, "actionId", lootBlock.actionId);
			setField(L, "text", lootBlock.text);

			parseLoot(lootBlock.childLoot);
			lua_setfield(L, -2, "childLoot");

			lua_rawseti(L, -2, ++index);
		}
	};
	parseLoot(monsterType->lootItems);
	return 1;
}

int LuaScriptInterface::luaMonsterTypeAddLoot(lua_State* L)
{
	// monsterType:addLoot(loot)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		Loot* loot = getUserdata<Loot>(L, 2);
		if (loot) {
			monsterType->loadLoot(monsterType, loot->lootBlock);
			pushBoolean(L, true);
		} else {
			lua_pushnil(L);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeGetCreatureEvents(lua_State* L)
{
	// monsterType:getCreatureEvents()
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (!monsterType) {
		lua_pushnil(L);
		return 1;
	}

	int index = 0;
	lua_createtable(L, monsterType->scriptList.size(), 0);
	for (const std::string& creatureEvent : monsterType->scriptList) {
		pushString(L, creatureEvent);
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeRegisterEvent(lua_State* L)
{
	// monsterType:registerEvent(name)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		monsterType->scriptList.push_back(getString(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeSetScriptFile(lua_State* L)
{
	// monsterType:setScriptFile(file)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (!g_monsters.scriptInterface) {
			g_monsters.scriptInterface.reset(new LuaScriptInterface("Monster Interface"));
			g_monsters.scriptInterface->initState();
		}

		std::string fileName = monsterType->name + ".lua";
		if (lua_gettop(L) > 1) {
			fileName = getString(L, 2) + ".lua";
		}
		if (g_monsters.scriptInterface->loadFile("data/scripts/monsters/events/" + fileName, nullptr) == 0) {
			monsterType->scriptInterface = g_monsters.scriptInterface.get();
			monsterType->creatureAppearEvent = g_monsters.scriptInterface->getEvent("onCreatureAppear");
			monsterType->creatureDisappearEvent = g_monsters.scriptInterface->getEvent("onCreatureDisappear");
			monsterType->creatureMoveEvent = g_monsters.scriptInterface->getEvent("onCreatureMove");
			monsterType->creatureSayEvent = g_monsters.scriptInterface->getEvent("onCreatureSay");
			monsterType->thinkEvent = g_monsters.scriptInterface->getEvent("onThink");
		} else {
			std::cout << "[Warning - Monsters::loadMonster] Can not load script: " << getString(L, 2) << std::endl;
			std::cout << g_monsters.scriptInterface->getLastError() << std::endl;
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeGetSummonList(lua_State* L)
{
	// monsterType:getSummonList()
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (!monsterType) {
		lua_pushnil(L);
		return 1;
	}

	int index = 0;
	lua_createtable(L, monsterType->summonList.size(), 0);
	for (const auto& summonBlock : monsterType->summonList) {
		lua_createtable(L, 0, 3);
		setField(L, "name", summonBlock.name);
		setField(L, "speed", summonBlock.speed);
		setField(L, "chance", summonBlock.chance);
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeAddSummon(lua_State* L)
{
	// monsterType:addSummon(name, interval, chance)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		summonBlock_t summon;
		summon.name = getString(L, 2);
		summon.chance = getNumber<int32_t>(L, 3);
		summon.speed = getNumber<int32_t>(L, 4);
		monsterType->summonList.push_back(summon);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeMaxSummons(lua_State* L)
{
	// get: monsterType:maxSummons() set: monsterType:maxSummons(ammount)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, monsterType->maxSummons);
		} else {
			monsterType->maxSummons = getNumber<uint32_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeArmor(lua_State* L)
{
	// get: monsterType:armor() set: monsterType:armor(armor)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, monsterType->armor);
		} else {
			monsterType->armor = getNumber<int32_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeDefense(lua_State* L)
{
	// get: monsterType:defense() set: monsterType:defense(defense)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, monsterType->defense);
		} else {
			monsterType->defense = getNumber<int32_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeOutfit(lua_State* L)
{
	// get: monsterType:outfit() set: monsterType:outfit(outfit)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			pushOutfit(L, monsterType->outfit);
		} else {
			monsterType->outfit = getOutfit(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeRace(lua_State* L)
{
	// get: monsterType:race() set: monsterType:race(race)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	std::string race = getString(L, 2);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, monsterType->race);
		} else {
			if (race == "venom") {
				monsterType->race = RACE_VENOM;
			} else if (race == "blood") {
				monsterType->race = RACE_BLOOD;
			} else if (race == "undead") {
				monsterType->race = RACE_UNDEAD;
			} else if (race == "fire") {
				monsterType->race = RACE_FIRE;
			} else if (race == "energy") {
				monsterType->race = RACE_ENERGY;
			} else {
				std::cout << "[Warning - Monsters::loadMonster] Unknown race type " << race << "." << std::endl;
				lua_pushnil(L);
				return 1;
			}
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeCorpseId(lua_State* L)
{
	// get: monsterType:corpseId() set: monsterType:corpseId(id)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, monsterType->lookCorpse);
		} else {
			monsterType->lookCorpse = getNumber<uint16_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeManaCost(lua_State* L)
{
	// get: monsterType:manaCost() set: monsterType:manaCost(mana)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, monsterType->manaCost);
		} else {
			monsterType->manaCost = getNumber<uint32_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeBaseSpeed(lua_State* L)
{
	// get: monsterType:baseSpeed() set: monsterType:baseSpeed(speed)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, monsterType->baseSpeed);
		} else {
			monsterType->baseSpeed = getNumber<uint32_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeLight(lua_State* L)
{
	// get: monsterType:light() set: monsterType:light(color, level)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (!monsterType) {
		lua_pushnil(L);
		return 1;
	}
	if (lua_gettop(L) == 1) {
		lua_pushnumber(L, monsterType->lightLevel);
		lua_pushnumber(L, monsterType->lightColor);
		return 2;
	} else {
		monsterType->lightColor = getNumber<uint8_t>(L, 2);
		monsterType->lightLevel = getNumber<uint8_t>(L, 3);
		pushBoolean(L, true);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeStaticAttackChance(lua_State* L)
{
	// get: monsterType:staticAttackChance() set: monsterType:staticAttackChance(chance)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, monsterType->staticAttackChance);
		} else {
			monsterType->staticAttackChance = getNumber<uint32_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeTargetDistance(lua_State* L)
{
	// get: monsterType:targetDistance() set: monsterType:targetDistance(distance)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, monsterType->targetDistance);
		} else {
			monsterType->targetDistance = getNumber<int32_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeYellChance(lua_State* L)
{
	// get: monsterType:yellChance() set: monsterType:yellChance(chance)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, monsterType->yellChance);
		} else {
			monsterType->yellChance = getNumber<uint32_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeYellSpeedTicks(lua_State* L)
{
	// get: monsterType:yellSpeedTicks() set: monsterType:yellSpeedTicks(rate)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, monsterType->yellSpeedTicks);
		} else {
			monsterType->yellSpeedTicks = getNumber<uint32_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeChangeTargetChance(lua_State* L)
{
	// get: monsterType:changeTargetChance() set: monsterType:changeTargetChance(chance)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, monsterType->changeTargetChance);
		} else {
			monsterType->changeTargetChance = getNumber<int32_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterTypeChangeTargetSpeed(lua_State* L)
{
	// get: monsterType:changeTargetSpeed() set: monsterType:changeTargetSpeed(speed)
	MonsterType* monsterType = getUserdata<MonsterType>(L, 1);
	if (monsterType) {
		if (lua_gettop(L) == 1) {
			lua_pushnumber(L, monsterType->changeTargetSpeed);
		} else {
			monsterType->changeTargetSpeed = getNumber<uint32_t>(L, 2);
			pushBoolean(L, true);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

// Loot
int LuaScriptInterface::luaCreateLoot(lua_State* L)
{
	// Loot() will create a new loot item
	Loot* loot = new Loot();
	if (loot) {
		pushUserdata<Loot>(L, loot);
		setMetatable(L, -1, "Loot");
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaDeleteLoot(lua_State* L)
{
	// loot:delete() loot:__gc()
	Loot** lootPtr = getRawUserdata<Loot>(L, 1);
	if (lootPtr && *lootPtr) {
		delete *lootPtr;
		*lootPtr = nullptr;
	}
	return 0;
}

int LuaScriptInterface::luaLootSetId(lua_State* L)
{
	// loot:setId(id or name)
	Loot* loot = getUserdata<Loot>(L, 1);
	uint16_t item;
	if (loot) {
		if (isNumber(L, 2)) {
			loot->lootBlock.id = getNumber<uint16_t>(L, 2);
		} else {
			item = Item::items.getItemIdByName(getString(L, 2));
			loot->lootBlock.id = item;
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaLootSetSubType(lua_State* L)
{
	// loot:setSubType(type)
	Loot* loot = getUserdata<Loot>(L, 1);
	if (loot) {
		loot->lootBlock.subType = getNumber<uint16_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaLootSetChance(lua_State* L)
{
	// loot:setChance(chance)
	Loot* loot = getUserdata<Loot>(L, 1);
	if (loot) {
		loot->lootBlock.chance = getNumber<uint32_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaLootSetMaxCount(lua_State* L)
{
	// loot:setMaxCount(max)
	Loot* loot = getUserdata<Loot>(L, 1);
	if (loot) {
		loot->lootBlock.countmax = getNumber<uint32_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaLootSetActionId(lua_State* L)
{
	// loot:setActionId(actionid)
	Loot* loot = getUserdata<Loot>(L, 1);
	if (loot) {
		loot->lootBlock.actionId = getNumber<uint32_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaLootSetDescription(lua_State* L)
{
	// loot:setDescription(desc)
	Loot* loot = getUserdata<Loot>(L, 1);
	if (loot) {
		loot->lootBlock.text = getString(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaLootAddChildLoot(lua_State* L)
{
	// loot:addChildLoot(loot)
	Loot* loot = getUserdata<Loot>(L, 1);
	if (loot) {
		loot->lootBlock.childLoot.push_back(getUserdata<Loot>(L, 2)->lootBlock);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

// MonsterSpell
int LuaScriptInterface::luaCreateMonsterSpell(lua_State* L)
{
	// MonsterSpell() will create a new Monster Spell
	MonsterSpell* spell = new MonsterSpell();
	if (spell) {
		pushUserdata<MonsterSpell>(L, spell);
		setMetatable(L, -1, "MonsterSpell");
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaDeleteMonsterSpell(lua_State* L)
{
	// monsterSpell:delete() monsterSpell:__gc()
	MonsterSpell** monsterSpellPtr = getRawUserdata<MonsterSpell>(L, 1);
	if (monsterSpellPtr && *monsterSpellPtr) {
		delete *monsterSpellPtr;
		*monsterSpellPtr = nullptr;
	}
	return 0;
}

int LuaScriptInterface::luaMonsterSpellSetType(lua_State* L)
{
	// monsterSpell:setType(type)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->name = getString(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterSpellSetScriptName(lua_State* L)
{
	// monsterSpell:setScriptName(name)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->scriptName = getString(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterSpellSetChance(lua_State* L)
{
	// monsterSpell:setChance(chance)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->chance = getNumber<uint8_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterSpellSetInterval(lua_State* L)
{
	// monsterSpell:setInterval(interval)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->interval = getNumber<uint16_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterSpellSetRange(lua_State* L)
{
	// monsterSpell:setRange(range)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->range = getNumber<uint8_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterSpellSetCombatValue(lua_State* L)
{
	// monsterSpell:setCombatValue(min, max)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->minCombatValue = getNumber<int32_t>(L, 2);
		spell->maxCombatValue = getNumber<int32_t>(L, 3);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterSpellSetCombatType(lua_State* L)
{
	// monsterSpell:setCombatType(combatType_t)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->combatType = getNumber<CombatType_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterSpellSetAttackValue(lua_State* L)
{
	// monsterSpell:setAttackValue(attack, skill)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->attack = getNumber<int32_t>(L, 2);
		spell->skill = getNumber<int32_t>(L, 3);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterSpellSetNeedTarget(lua_State* L)
{
	// monsterSpell:setNeedTarget(bool)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->needTarget = getBoolean(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterSpellSetCombatLength(lua_State* L)
{
	// monsterSpell:setCombatLength(length)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->length = getNumber<int32_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterSpellSetCombatSpread(lua_State* L)
{
	// monsterSpell:setCombatSpread(spread)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->spread = getNumber<int32_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterSpellSetCombatRadius(lua_State* L)
{
	// monsterSpell:setCombatRadius(radius)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->radius = getNumber<int32_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterSpellSetConditionType(lua_State* L)
{
	// monsterSpell:setConditionType(type)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->conditionType = getNumber<ConditionType_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterSpellSetConditionDamage(lua_State* L)
{
	// monsterSpell:setConditionDamage(min, max, start)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->conditionMinDamage = getNumber<int32_t>(L, 2);
		spell->conditionMaxDamage = getNumber<int32_t>(L, 3);
		spell->conditionStartDamage = getNumber<int32_t>(L, 4);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterSpellSetConditionSpeedChange(lua_State* L)
{
	// monsterSpell:setConditionSpeedChange(speed)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->speedChange = getNumber<int32_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterSpellSetConditionDuration(lua_State* L)
{
	// monsterSpell:setConditionDuration(duration)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->duration = getNumber<int32_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterSpellSetConditionTickInterval(lua_State* L)
{
	// monsterSpell:setConditionTickInterval(interval)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->tickInterval = getNumber<int32_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterSpellSetCombatShootEffect(lua_State* L)
{
	// monsterSpell:setCombatShootEffect(effect)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->shoot = getNumber<ShootEffect_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int LuaScriptInterface::luaMonsterSpellSetCombatEffect(lua_State* L)
{
	// monsterSpell:setCombatEffect(effect)
	MonsterSpell* spell = getUserdata<MonsterSpell>(L, 1);
	if (spell) {
		spell->effect = getNumber<MagicEffect_t>(L, 2);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}
