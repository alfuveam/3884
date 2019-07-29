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
#include "const.h"

#include "actions.h"
#include "tools.h"

#include "player.h"
#include "monster.h"
#include "npc.h"

#include "item.h"
#include "container.h"

#include "game.h"
#include "configmanager.h"

#include "combat.h"
#include "spells.h"

#include "house.h"
#include "beds.h"

extern Game g_game;
extern Spells* g_spells;
extern Actions* g_actions;
extern ConfigManager g_config;

Actions::Actions():
m_interface("Action Interface")
{
	m_interface.initState();
	defaultAction = NULL;
}

Actions::~Actions()
{
	clear();
}

inline void Actions::clearMap(ActionUseMap& map)
{
	for (auto it = map.begin(); it != map.end(); it++ ) {
		map.erase(it);
	}

	map.clear();
}

void Actions::clear()
{
	clearMap(useItemMap);
	clearMap(uniqueItemMap);
	clearMap(actionItemMap);

	m_interface.reInitState();

	delete defaultAction;
	defaultAction = NULL;
}

Event* Actions::getEvent(const std::string& nodeName)
{
	if(asLowerCaseString(nodeName) == "action")
		return new Action(&m_interface);

	return NULL;
}

bool Actions::registerLuaEvent(Event* event)
{
	Action_ptr action{ static_cast<Action*>(event) }; //event is guaranteed to be an Action
	if (action->getItemIdRange().size() > 0) {
		if (action->getItemIdRange().size() == 1) {
			auto result = useItemMap.emplace(action->getItemIdRange().at(0), std::move(*action));
			if (!result.second) {
				std::cout << "[Warning - Actions::registerLuaEvent] Duplicate registered item with id: " << action->getItemIdRange().at(0) << std::endl;
			}
			return result.second;
		} else {
			auto v = action->getItemIdRange();
			for (auto i = v.begin(); i != v.end(); i++) {
				auto result = useItemMap.emplace(*i, std::move(*action));
				if (!result.second) {
					std::cout << "[Warning - Actions::registerLuaEvent] Duplicate registered item with id: " << *i << " in range from id: " << v.at(0) << ", to id: " << v.at(v.size() - 1) << std::endl;
					continue;
				}
			}
			return true;
		}
	}
	else if (action->getUniqueIdRange().size() > 0) {
		if (action->getUniqueIdRange().size() == 1) {
			auto result = uniqueItemMap.emplace(action->getUniqueIdRange().at(0), std::move(*action));
			if (!result.second) {
				std::cout << "[Warning - Actions::registerLuaEvent] Duplicate registered item with uid: " << action->getUniqueIdRange().at(0) << std::endl;
			}
			return result.second;
		} else {
			auto v = action->getUniqueIdRange();
			for (auto i = v.begin(); i != v.end(); i++) {
				auto result = uniqueItemMap.emplace(*i, std::move(*action));
				if (!result.second) {
					std::cout << "[Warning - Actions::registerLuaEvent] Duplicate registered item with uid: " << *i << " in range from uid: " << v.at(0) << ", to uid: " << v.at(v.size() - 1) << std::endl;
					continue;
				}
			}
			return true;
		}
	}
	else if (action->getActionIdRange().size() > 0) {
		if (action->getActionIdRange().size() == 1) {
			auto result = actionItemMap.emplace(action->getActionIdRange().at(0), std::move(*action));
			if (!result.second) {
				std::cout << "[Warning - Actions::registerLuaEvent] Duplicate registered item with aid: " << action->getActionIdRange().at(0) << std::endl;
			}
			return result.second;
		} else {
			auto v = action->getActionIdRange();
			for (auto i = v.begin(); i != v.end(); i++) {
				auto result = actionItemMap.emplace(*i, std::move(*action));
				if (!result.second) {
					std::cout << "[Warning - Actions::registerLuaEvent] Duplicate registered item with aid: " << *i << " in range from aid: " << v.at(0) << ", to aid: " << v.at(v.size() - 1) << std::endl;
					continue;
				}
			}
			return true;
		}
	} else {
		std::cout << "[Warning - Actions::registerLuaEvent] There is no id / aid / uid set for this event" << std::endl;
		return false;
	}
}

bool Actions::registerEvent(Event* event, xmlNodePtr p, bool override)
{
	Action* action = dynamic_cast<Action*>(event);
	if(!action)
		return false;

	std::string strValue;
	if(readXMLString(p, "default", strValue) && booleanString(strValue))
	{
		if(!defaultAction)
			defaultAction = action;
		else if(override)
		{
			delete defaultAction;
			defaultAction = action;
		}
		else
			std::clog << "[Warning - Actions::registerEvent] You cannot define more than one default action." << std::endl;

		return true;
	}

	bool success = true;
	std::string endValue;
	if(readXMLString(p, "itemid", strValue))
	{
		IntegerVec intVector;
		if(!parseIntegerVec(strValue, intVector))
		{
			std::clog << "[Warning - Actions::registerEvent] Invalid itemid - '" << strValue << "'" << std::endl;
			return false;
		}

		if(useItemMap.find(intVector[0]) != useItemMap.end())
		{
			if(!override)
			{
				std::clog << "[Warning - Actions::registerEvent] Duplicate registered item id: " << intVector[0] << std::endl;
				success = false;
			}
			else
				useItemMap.erase(intVector[0]);
		}

		if(success)
			useItemMap.emplace(intVector[0], std::move(action));

		for(size_t i = 1, size = intVector.size(); i < size; ++i)
		{
			if(useItemMap.find(intVector[i]) != useItemMap.end())
			{
				if(!override)
				{
					std::clog << "[Warning - Actions::registerEvent] Duplicate registered item id: " << intVector[i] << std::endl;
					continue;
				}
				else
					useItemMap.erase(intVector[i]);
			}

			useItemMap.emplace(intVector[i], new Action(action));
		}
	}
	else if(readXMLString(p, "fromid", strValue) && readXMLString(p, "toid", endValue))
	{
		IntegerVec intVector = vectorAtoi(explodeString(strValue, ";")), endVector = vectorAtoi(explodeString(endValue, ";"));
		if(intVector[0] && endVector[0] && intVector.size() == endVector.size())
		{
			int32_t tmp = 0;
			for(size_t i = 0, size = intVector.size(); i < size; ++i)
			{
				tmp = intVector[i];
				while(intVector[i] <= endVector[i])
				{
					if(useItemMap.find(intVector[i]) != useItemMap.end())
					{
						if(!override)
						{
							std::clog << "[Warning - Actions::registerEvent] Duplicate registered item with id: " << intVector[i] <<
								", in fromid: " << tmp << " and toid: " << endVector[i] << std::endl;
							intVector[i]++;
							continue;
						}
						else
							useItemMap.erase(intVector[i]);
					}

					useItemMap.emplace(intVector[i]++, new Action(action));
				}
			}
		}
		else
			std::clog << "[Warning - Actions::registerEvent] Malformed entry (from item: \"" << strValue <<
				"\", to item: \"" << endValue << "\")" << std::endl;
	}

	if(readXMLString(p, "uniqueid", strValue))
	{
		IntegerVec intVector;
		if(!parseIntegerVec(strValue, intVector))
		{
			std::clog << "[Warning - Actions::registerEvent] Invalid uniqueid - '" << strValue << "'" << std::endl;
			return false;
		}

		if(uniqueItemMap.find(intVector[0]) != uniqueItemMap.end())
		{
			if(!override)
			{
				std::clog << "[Warning - Actions::registerEvent] Duplicate registered item uid: " << intVector[0] << std::endl;
				success = false;
			}
			else
				uniqueItemMap.erase(intVector[0]);
		}

		if(success)
			uniqueItemMap.emplace(intVector[0], std::move(action));

		for(size_t i = 1, size = intVector.size(); i < size; ++i)
		{
			if(uniqueItemMap.find(intVector[i]) != uniqueItemMap.end())
			{
				if(!override)
				{
					std::clog << "[Warning - Actions::registerEvent] Duplicate registered item uid: " << intVector[i] << std::endl;
					continue;
				}
				else
					uniqueItemMap.erase(intVector[i]);
			}

			uniqueItemMap.emplace(intVector[i], new Action(action));
		}
	}
	else if(readXMLString(p, "fromuid", strValue) && readXMLString(p, "touid", endValue))
	{
		IntegerVec intVector = vectorAtoi(explodeString(strValue, ";")), endVector = vectorAtoi(explodeString(endValue, ";"));
		if(intVector[0] && endVector[0] && intVector.size() == endVector.size())
		{
			int32_t tmp = 0;
			for(size_t i = 0, size = intVector.size(); i < size; ++i)
			{
				tmp = intVector[i];
				while(intVector[i] <= endVector[i])
				{
					if(uniqueItemMap.find(intVector[i]) != uniqueItemMap.end())
					{
						if(!override)
						{
							std::clog << "[Warning - Actions::registerEvent] Duplicate registered item with uid: " << intVector[i] <<
								", in fromuid: " << tmp << " and touid: " << endVector[i] << std::endl;
							intVector[i]++;
							continue;
						}
						else
							uniqueItemMap.erase(intVector[i]);
					}

					uniqueItemMap.emplace(intVector[i]++, Action(action));
				}
			}
		}
		else
			std::clog << "[Warning - Actions::registerEvent] Malformed entry (from unique: \"" << strValue <<
				"\", to unique: \"" << endValue << "\")" << std::endl;
	}

	if(readXMLString(p, "actionid", strValue))
	{
		IntegerVec intVector;
		if(!parseIntegerVec(strValue, intVector))
		{
			std::clog << "[Warning - Actions::registerEvent] Invalid actionid - '" << strValue << "'" << std::endl;
			return false;
		}

		if(actionItemMap.find(intVector[0]) != actionItemMap.end())
		{
			if(!override)
			{
				std::clog << "[Warning - Actions::registerEvent] Duplicate registered item aid: " << intVector[0] << std::endl;
				success = false;
			}
			else
				actionItemMap.erase(intVector[0]);
		}

		if(success)
			actionItemMap.emplace(intVector[0], std::move(action));

		for(size_t i = 1, size = intVector.size(); i < size; ++i)
		{
			if(actionItemMap.find(intVector[i]) != actionItemMap.end())
			{
				if(!override)
				{
					std::clog << "[Warning - Actions::registerEvent] Duplicate registered item aid: " << intVector[i] << std::endl;
					continue;
				}
				else
					actionItemMap.erase(intVector[i]);
			}

			actionItemMap.emplace(intVector[i], Action(action));
		}
	}
	else if(readXMLString(p, "fromaid", strValue) && readXMLString(p, "toaid", endValue))
	{
		IntegerVec intVector = vectorAtoi(explodeString(strValue, ";")), endVector = vectorAtoi(explodeString(endValue, ";"));
		if(intVector[0] && endVector[0] && intVector.size() == endVector.size())
		{
			int32_t tmp = 0;
			for(size_t i = 0, size = intVector.size(); i < size; ++i)
			{
				tmp = intVector[i];
				while(intVector[i] <= endVector[i])
				{
					if(actionItemMap.find(intVector[i]) != actionItemMap.end())
					{
						if(!override)
						{
							std::clog << "[Warning - Actions::registerEvent] Duplicate registered item with aid: " << intVector[i] <<
								", in fromaid: " << tmp << " and toaid: " << endVector[i] << std::endl;
							intVector[i]++;
							continue;
						}
						else
							actionItemMap.erase(intVector[i]);
					}

					actionItemMap.emplace(intVector[i]++, new Action(action));
				}
			}
		}
		else
			std::clog << "[Warning - Actions::registerEvent] Malformed entry (from action: \"" << strValue <<
				"\", to action: \"" << endValue << "\")" << std::endl;
	}

	return success;
}

ReturnValue Actions::canUse(const Player* player, const Position& pos)
{
	const Position& playerPos = player->getPosition();
	if(pos.x == 0xFFFF)
		return RET_NOERROR;

	if(playerPos.z > pos.z)
		return RET_FIRSTGOUPSTAIRS;

	if(playerPos.z < pos.z)
		return RET_FIRSTGODOWNSTAIRS;

	if(!Position::areInRange<1,1,0>(playerPos, pos))
		return RET_TOOFARAWAY;

	return RET_NOERROR;
}

ReturnValue Actions::canUse(const Player* player, const Position& pos, const Item* item)
{
	Action* action = NULL;
	if((action = getAction(item, ACTION_UNIQUEID)))
		return action->canExecuteAction(player, pos);

	if((action = getAction(item, ACTION_ACTIONID)))
		return action->canExecuteAction(player, pos);

	if((action = getAction(item, ACTION_ITEMID)))
		return action->canExecuteAction(player, pos);

	if((action = getAction(item, ACTION_RUNEID)))
		return action->canExecuteAction(player, pos);

	if(defaultAction)
		return defaultAction->canExecuteAction(player, pos);

	return RET_NOERROR;
}

ReturnValue Actions::canUseFar(const Creature* creature, const Position& toPos, bool checkLineOfSight)
{
	if(toPos.x == 0xFFFF)
		return RET_NOERROR;

	const Position& creaturePos = creature->getPosition();
	if(creaturePos.z > toPos.z)
		return RET_FIRSTGOUPSTAIRS;

	if(creaturePos.z < toPos.z)
		return RET_FIRSTGODOWNSTAIRS;

	if(!Position::areInRange<7,5,0>(toPos, creaturePos))
		return RET_TOOFARAWAY;

	if(checkLineOfSight && !g_game.canThrowObjectTo(creaturePos, toPos))
		return RET_CANNOTTHROW;

	return RET_NOERROR;
}

Action* Actions::getAction(const Item* item, ActionType_t type/* = ACTION_ANY*/)
{
	if(item->getUniqueId() && (type == ACTION_ANY || type == ACTION_UNIQUEID))
	{
		auto it = uniqueItemMap.find(item->getUniqueId());
		if (it != uniqueItemMap.end()) {
			return &it->second;
		}
	}

	if(item->getActionId() && (type == ACTION_ANY || type == ACTION_ACTIONID))
	{
		auto it = actionItemMap.find(item->getActionId());
		if (it != actionItemMap.end()) {
			return &it->second;
		}		
	}

	if(type == ACTION_ANY || type == ACTION_ITEMID)
	{
		auto it = useItemMap.find(item->getID());
		if (it != useItemMap.end()) {
			return &it->second;
		}
	}

	if(type == ACTION_ANY || type == ACTION_RUNEID)
	{
		if(Action* runeSpell = g_spells->getRuneSpell(item->getID()))
			return runeSpell;
	}

	return nullptr;
}

bool Actions::executeUse(Action* action, Player* player, Item* item,
	const Position& pos, uint32_t creatureId)
{
	return action->executeUse(player, item, pos, pos, false, creatureId);
}

ReturnValue Actions::internalUseItem(Player* player, const Position& pos, uint8_t index, Item* item, uint32_t creatureId)
{
	if(Door* door = item->getDoor())
	{
		if(!door->canUse(player))
			return RET_CANNOTUSETHISOBJECT;
	}

	Action* action = NULL;
	if((action = getAction(item, ACTION_UNIQUEID)))
	{
		if(action->isScripted())
		{
			if(executeUse(action, player, item, pos, creatureId))
				return RET_NOERROR;
		}
		else if(action->function)
		{
			if(action->function(player, item, pos, pos, false, creatureId))
				return RET_NOERROR;
		}
	}

	if((action = getAction(item, ACTION_ACTIONID)))
	{
		if(action->isScripted())
		{
			if(executeUse(action, player, item, pos, creatureId))
				return RET_NOERROR;
		}
		else if(action->function)
		{
			if(action->function(player, item, pos, pos, false, creatureId))
				return RET_NOERROR;
		}
	}

	if((action = getAction(item, ACTION_ITEMID)))
	{
		if(action->isScripted())
		{
			if(executeUse(action, player, item, pos, creatureId))
				return RET_NOERROR;
		}
		else if(action->function)
		{
			if(action->function(player, item, pos, pos, false, creatureId))
				return RET_NOERROR;
		}
	}

	if((action = getAction(item, ACTION_RUNEID)))
	{
		if(action->isScripted())
		{
			if(executeUse(action, player, item, pos, creatureId))
				return RET_NOERROR;
		}
		else if(action->function)
		{
			if(action->function(player, item, pos, pos, false, creatureId))
				return RET_NOERROR;
		}
	}

	if(defaultAction)
	{
		if(defaultAction->isScripted())
		{
			if(executeUse(defaultAction, player, item, pos, creatureId))
				return RET_NOERROR;
		}
		else if(defaultAction->function)
		{
			if(defaultAction->function(player, item, pos, pos, false, creatureId))
				return RET_NOERROR;
		}
	}

	if(BedItem* bed = item->getBed())
	{
		if(!bed->canUse(player))
			return RET_CANNOTUSETHISOBJECT;

		bed->sleep(player);
		return RET_NOERROR;
	}

	if(Container* container = item->getContainer())
	{
		if(container->getCorpseOwner() && !player->canOpenCorpse(container->getCorpseOwner())
			&& g_config.getBool(ConfigManager::CHECK_CORPSE_OWNER))
			return RET_YOUARENOTTHEOWNER;

		Container* tmpContainer = NULL;
		if(Depot* depot = container->getDepot())
		{
			if(player->hasFlag(PlayerFlag_CannotPickupItem))
				return RET_CANNOTUSETHISOBJECT;

			if(Depot* playerDepot = player->getDepot(depot->getDepotId(), true))
			{
				player->useDepot(depot->getDepotId(), true);
				playerDepot->setParent(depot->getParent());
				tmpContainer = playerDepot;
			}
		}

		if(!tmpContainer)
			tmpContainer = container;

		int32_t oldId = player->getContainerID(tmpContainer);
		if(oldId != -1)
		{
			player->onCloseContainer(tmpContainer);
			player->closeContainer(oldId);
		}
		else
		{
			player->addContainer(index, tmpContainer);
			player->onSendContainer(tmpContainer);
		}

		return RET_NOERROR;
	}

	if(item->isReadable())
	{
		if(item->canWriteText())
		{
			player->setWriteItem(item, item->getMaxWriteLength());
			player->sendTextWindow(item, item->getMaxWriteLength(), true);
		}
		else
		{
			player->setWriteItem(NULL);
			player->sendTextWindow(item, 0, false);
		}

		return RET_NOERROR;
	}

	return RET_CANNOTUSETHISOBJECT;
}

bool Actions::useItem(Player* player, const Position& pos, uint8_t index, Item* item)
{
	if(!player->canDoAction())
		return false;

	player->setNextActionTask(NULL);
	player->stopWalk();
	player->setNextAction(OTSYS_TIME() + g_config.getNumber(ConfigManager::ACTIONS_DELAY_INTERVAL) - SCHEDULER_MINTICKS);

	ReturnValue ret = internalUseItem(player, pos, index, item, 0);
	if(ret == RET_NOERROR)
		return true;

	player->sendCancelMessage(ret);
	return false;
}

bool Actions::executeUseEx(Action* action, Player* player, Item* item, const Position& fromPosEx,
	const Position& toPosEx, bool isHotkey, uint32_t creatureId)
{
	return (action->executeUse(player, item, fromPosEx, toPosEx, isHotkey,
		creatureId) || action->hasOwnErrorHandler());
}

ReturnValue Actions::internalUseItemEx(Player* player, const Position& fromPosEx, const Position& toPosEx,
	Item* item, bool isHotkey, uint32_t creatureId)
{
	Action* action = NULL;
	if((action = getAction(item, ACTION_UNIQUEID)))
	{
		ReturnValue ret = action->canExecuteAction(player, toPosEx);
		if(ret != RET_NOERROR)
			return ret;

		//only continue with next action in the list if the previous returns false
		if(executeUseEx(action, player, item, fromPosEx, toPosEx, isHotkey, creatureId))
			return RET_NOERROR;
	}

	if((action = getAction(item, ACTION_ACTIONID)))
	{
		ReturnValue ret = action->canExecuteAction(player, toPosEx);
		if(ret != RET_NOERROR)
			return ret;

		//only continue with next action in the list if the previous returns false
		if(executeUseEx(action, player, item, fromPosEx, toPosEx, isHotkey, creatureId))
			return RET_NOERROR;

	}

	if((action = getAction(item, ACTION_ITEMID)))
	{
		ReturnValue ret = action->canExecuteAction(player, toPosEx);
		if(ret != RET_NOERROR)
			return ret;

		//only continue with next action in the list if the previous returns false
		if(executeUseEx(action, player, item, fromPosEx, toPosEx, isHotkey, creatureId))
			return RET_NOERROR;
	}

	if((action = getAction(item, ACTION_RUNEID)))
	{
		ReturnValue ret = action->canExecuteAction(player, toPosEx);
		if(ret != RET_NOERROR)
			return ret;

		//only continue with next action in the list if the previous returns false
		if(executeUseEx(action, player, item, fromPosEx, toPosEx, isHotkey, creatureId))
			return RET_NOERROR;
	}

	if(defaultAction)
	{
		ReturnValue ret = defaultAction->canExecuteAction(player, toPosEx);
		if(ret != RET_NOERROR)
			return ret;

		//only continue with next action in the list if the previous returns false
		if(executeUseEx(defaultAction, player, item, fromPosEx, toPosEx, isHotkey, creatureId))
			return RET_NOERROR;
	}

	return RET_CANNOTUSETHISOBJECT;
}

bool Actions::useItemEx(Player* player, const Position& fromPos, const Position& toPos,
	uint8_t toStackPos, Item* item, bool isHotkey, uint32_t creatureId/* = 0*/)
{
	if(!player->canDoAction())
		return false;

	player->setNextActionTask(NULL);
	player->stopWalk();
	player->setNextAction(OTSYS_TIME() + g_config.getNumber(ConfigManager::EX_ACTIONS_DELAY_INTERVAL) - SCHEDULER_MINTICKS);

	if(!getAction(item))
	{
		player->sendCancelMessage(RET_CANNOTUSETHISOBJECT);
		return false;
	}

	ReturnValue ret = internalUseItemEx(player, fromPos, toPos, item, isHotkey, creatureId);
	if(ret == RET_NOERROR)
		return true;

	player->sendCancelMessage(ret);
	return false;
}

Action::Action(const Action* copy):
Event(copy)
{
	allowFarUse = copy->allowFarUse;
	checkLineOfSight = copy->checkLineOfSight;
}

Action::Action(LuaScriptInterface* _interface):
Event(_interface)
{
	allowFarUse = false;
	checkLineOfSight = true;
}

bool Action::configureEvent(xmlNodePtr p)
{
	std::string strValue;
	if(readXMLString(p, "allowfaruse", strValue) || readXMLString(p, "allowFarUse", strValue))
		setAllowFarUse(booleanString(strValue));

	if(readXMLString(p, "blockwalls", strValue) || readXMLString(p, "blockWalls", strValue))
		setCheckLineOfSight(booleanString(strValue));

	return true;
}

namespace {
	bool increaseItemId(Player* player, Item* item, const Position&, const Position&, bool, uint32_t)
	{
		if(!player || !item)
			return false;

		g_game.transformItem(item, item->getID() + 1);
		g_game.startDecay(item);
		return true;
	}

	bool decreaseItemId(Player* player, Item* item, const Position&, const Position&, bool, uint32_t)
	{
		if(!player || !item)
			return false;

		g_game.transformItem(item, item->getID() - 1);
		g_game.startDecay(item);
		return true;
	}
}

bool Action::loadFunction(const std::string& functionName, bool isScripted)
{
	std::string tmpFunctionName = asLowerCaseString(functionName);
	if(tmpFunctionName == "increaseitemid")
		function = increaseItemId;
	else if(tmpFunctionName == "decreaseitemid")
		function = decreaseItemId;
	else
	{
		if (!isScripted) {
			std::cout << "[Warning - Action::loadFunction] Function \"" << functionName << "\" does not exist." << std::endl;
			return false;
		}
	}
	if (!isScripted) {
		m_scripted = EVENT_SCRIPT_FALSE;
	}

	m_scripted = EVENT_SCRIPT_FALSE;
	return true;
}


ReturnValue Action::canExecuteAction(const Player* player, const Position& toPos)
{
	if(!getAllowFarUse())
		return g_actions->canUse(player, toPos);

	return g_actions->canUseFar(player, toPos, getCheckLineOfSight());
}

bool Action::executeUse(Player* player, Item* item, const Position& fromPos, const Position& toPos, bool extendedUse, uint32_t)
{
	//onUse(cid, item, fromPosition, itemEx, toPosition)
	if(m_interface->reserveEnv())
	{
		LuaEnvironment* env = m_interface->getScriptEnv();
		if(m_scripted == EVENT_SCRIPT_BUFFER)
		{
			env->setRealPos(player->getPosition());
			std::stringstream scriptstream;

			scriptstream << "local cid = " << env->addThing(player) << std::endl;
			env->streamThing(scriptstream, "item", item, env->addThing(item));
			env->streamPosition(scriptstream, "fromPosition", fromPos, fromPos.stackpos);

			Thing* thing = g_game.internalGetThing(player, toPos, toPos.stackpos);
			if(thing && (thing != item || !extendedUse))
			{
				env->streamThing(scriptstream, "itemEx", thing, env->addThing(thing));
				env->streamPosition(scriptstream, "toPosition", toPos, toPos.stackpos);
			}
			else
			{
				env->streamThing(scriptstream, "itemEx", NULL, 0);
				env->streamPosition(scriptstream, "toPosition", Position());
			}

			scriptstream << m_scriptData;
			bool result = true;
			if(m_interface->loadBuffer(scriptstream.str()))
			{
				lua_State* L = m_interface->getState();
				result = m_interface->getGlobalBool(L, "_result", true);
			}

			m_interface->releaseEnv();
			return result;
		}
		else
		{
			#ifdef __DEBUG_LUASCRIPTS__
			std::stringstream desc;
			desc << player->getName() << " - " << item->getID() << " " << fromPos << "|" << toPos;
			env->setEvent(desc.str());
			#endif

			env->setScriptId(m_scriptId, m_interface);
			env->setRealPos(player->getPosition());

			lua_State* L = m_interface->getState();
			m_interface->pushFunction(m_scriptId);

			lua_pushnumber(L, env->addThing(player));
			LuaScriptInterface::pushThing(L, item, env->addThing(item));
			LuaScriptInterface::pushPosition(L, fromPos, fromPos.stackpos);

			Thing* thing = g_game.internalGetThing(player, toPos, toPos.stackpos);
			if(thing && (thing != item || !extendedUse))
			{
				LuaScriptInterface::pushThing(L, thing, env->addThing(thing));
				LuaScriptInterface::pushPosition(L, toPos, toPos.stackpos);
			}
			else
			{
				LuaScriptInterface::pushThing(L, NULL, 0);
				LuaScriptInterface::pushPosition(L, Position());
			}

			bool result = m_interface->callFunction(5);
			m_interface->releaseEnv();
			return result;
		}
	}
	else
	{
		std::clog << "[Error - Action::executeUse]: Call stack overflow." << std::endl;
		return false;
	}
}
