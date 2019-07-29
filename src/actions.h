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

#ifndef __ACTIONS__
#define __ACTIONS__
#include "baseevents.h"

#include "luascript.h"
#include "thing.h"

class Action;
using Action_ptr = std::unique_ptr<Action>;
using ActionFunction = std::function<bool(Player* player, Item* item, const Position& posFrom, const Position& posTo, bool extendedUse, uint32_t creatureId)>;

enum ActionType_t
{
	ACTION_ANY,
	ACTION_UNIQUEID,
	ACTION_ACTIONID,
	ACTION_ITEMID,
	ACTION_RUNEID,
};

class Actions : public BaseEvents
{
	public:
		Actions();
		virtual ~Actions();

		// non-copyable
		Actions(const Actions&) = delete;
		Actions& operator=(const Actions&) = delete;		

		bool useItem(Player* player, const Position& pos, uint8_t index, Item* item);
		bool useItemEx(Player* player, const Position& fromPos, const Position& toPos,
			uint8_t toStackPos, Item* item, bool isHotkey, uint32_t creatureId = 0);

		ReturnValue canUse(const Player* player, const Position& pos);
		ReturnValue canUse(const Player* player, const Position& pos, const Item* item);
		ReturnValue canUseFar(const Creature* creature, const Position& toPos, bool checkLineOfSight);		
		Action* getAction(const Item* item, ActionType_t type = ACTION_ANY);
		bool registerLuaEvent(Event* event);

	protected:
		Action* defaultAction;

		virtual std::string getScriptBaseName() const {return "actions";}
		virtual void clear();

		virtual Event* getEvent(const std::string& nodeName);
		virtual bool registerEvent(Event* event, xmlNodePtr p, bool override);

		virtual LuaScriptInterface& getScriptInterface() {return m_interface;}
		LuaScriptInterface m_interface;

		void registerItemID(int32_t itemId, Event* event);
		void registerActionID(int32_t actionId, Event* event);
		void registerUniqueID(int32_t uniqueId, Event* event);

		using ActionUseMap = std::map<uint16_t, Action>;
		ActionUseMap useItemMap;
		ActionUseMap uniqueItemMap;
		ActionUseMap actionItemMap;

		bool executeUse(Action* action, Player* player, Item* item, const Position& posEx, uint32_t creatureId);
		ReturnValue internalUseItem(Player* player, const Position& pos,
			uint8_t index, Item* item, uint32_t creatureId);
		bool executeUseEx(Action* action, Player* player, Item* item, const Position& fromPosEx,
			const Position& toPosEx, bool isHotkey, uint32_t creatureId);
		ReturnValue internalUseItemEx(Player* player, const Position& fromPosEx, const Position& toPosEx,
			Item* item, bool isHotkey, uint32_t creatureId);
		
		void clearMap(ActionUseMap& map);
};

class Action : public Event
{
	public:
		Action(const Action* copy);
		explicit Action(LuaScriptInterface* _interface);
		virtual ~Action() {}

		virtual bool configureEvent(xmlNodePtr p);
		virtual bool loadFunction(const std::string& functionName, bool isScripted);

		//scripting
		virtual bool executeUse(Player* player, Item* item, const Position& posFrom,
			const Position& posTo, bool extendedUse, uint32_t creatureId);

		bool getAllowFarUse() const {
			return allowFarUse;
		}
		void setAllowFarUse(bool v) {
			allowFarUse = v;
		}

		bool getCheckLineOfSight() const {
			return checkLineOfSight;
		}
		void setCheckLineOfSight(bool v) {
			checkLineOfSight = v;
		}

		bool getCheckFloor() const {
			return checkFloor;
		}
		void setCheckFloor(bool v) {
			checkFloor = v;
		}

		std::vector<uint16_t> getItemIdRange() {
			return ids;
		}
		void addItemId(uint16_t id) {
			ids.emplace_back(id);
		}

		std::vector<uint16_t> getUniqueIdRange() {
			return uids;
		}
		void addUniqueId(uint16_t id) {
			uids.emplace_back(id);
		}

		std::vector<uint16_t> getActionIdRange() {
			return aids;
		}
		void addActionId(uint16_t id) {
			aids.emplace_back(id);
		}

		virtual ReturnValue canExecuteAction(const Player* player, const Position& toPos);
		virtual bool hasOwnErrorHandler() {return false;}

		ActionFunction function;

	protected:
		virtual std::string getScriptEventName() const {return "onUse";}
		virtual std::string getScriptEventParams() const {return "cid, item, fromPosition, itemEx, toPosition";}

		bool allowFarUse = false;
		bool checkFloor = true;
		bool checkLineOfSight = true;
		std::vector<uint16_t> ids;
		std::vector<uint16_t> uids;
		std::vector<uint16_t> aids;
};
#endif
