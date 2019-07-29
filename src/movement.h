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

#ifndef __MOVEMENT__
#define __MOVEMENT__

#include "baseevents.h"
#include "creature.h"
#include "vocation.h"

extern Vocations g_vocations;

enum MoveEvent_t
{
	MOVE_EVENT_FIRST = 0,
	MOVE_EVENT_STEP_IN = MOVE_EVENT_FIRST,
	MOVE_EVENT_STEP_OUT = 1,
	MOVE_EVENT_EQUIP = 2,
	MOVE_EVENT_DE_EQUIP = 3,
	MOVE_EVENT_ADD_ITEM = 4,
	MOVE_EVENT_REMOVE_ITEM = 5,
	MOVE_EVENT_ADD_TILEITEM = 6,
	MOVE_EVENT_REMOVE_TILEITEM = 7,
	MOVE_EVENT_NONE = 8,
	MOVE_EVENT_LAST = MOVE_EVENT_REMOVE_TILEITEM
};

class MoveEvent;
typedef std::list<MoveEvent*> EventList;

class MoveEventScript : public LuaScriptInterface
{
	public:
		MoveEventScript() : LuaScriptInterface("MoveEvents Interface") {}
		virtual ~MoveEventScript() {}

		static MoveEvent* event;

	protected:
		virtual void registerFunctions();
		static int32_t luaCallFunction(lua_State* L);
};

class MoveEvents : public BaseEvents
{
	public:
		MoveEvents();
		virtual ~MoveEvents() {clear();}

		uint32_t onCreatureMove(Creature* actor, Creature* creature, const Tile* fromTile, const Tile* toTile, bool isStepping);
		bool onPlayerEquip(Player* player, Item* item, slots_t slot, bool isCheck);
		bool onPlayerDeEquip(Player* player, Item* item, slots_t slot, bool isRemoval);
		uint32_t onItemMove(Creature* actor, Item* item, Tile* tile, bool isAdd);

		MoveEvent* getEvent(Item* item, MoveEvent_t eventType);

		bool registerLuaEvent(Event* event);
		bool registerLuaFunction(Event* event);

		bool hasEquipEvent(Item* item);
		bool hasTileEvent(Item* item);

		void onRemoveTileItem(const Tile* tile, Item* item);
		void onAddTileItem(const Tile* tile, Item* item);

	protected:
		static uint32_t StepInField(Creature* creature, Item* item, const Position& pos);
		static uint32_t StepOutField(Creature* creature, Item* item, const Position& pos);

		static uint32_t AddItemField(Item* item, Item* tileItem, const Position& pos);		
		static uint32_t RemoveItemField(Item* item, Item* tileItem, const Position& pos);
		
		static uint32_t EquipItem(MoveEvent* moveEvent, Player* player, Item* item, slots_t slot, bool boolean);
		static uint32_t DeEquipItem(MoveEvent* moveEvent, Player* player, Item* item, slots_t slot, bool boolean);

		struct MoveEventList
		{
			EventList moveEvent[MOVE_EVENT_NONE];
		};

		virtual std::string getScriptBaseName() const {return "movements";}
		virtual void clear();

		virtual Event* getEvent(const std::string& nodeName);
		virtual bool registerEvent(Event* event, xmlNodePtr p, bool override);

		virtual LuaScriptInterface& getScriptInterface() {return m_interface;}
		MoveEventScript m_interface;

		void registerItemID(int32_t itemId, MoveEvent_t eventType);
		void registerActionID(int32_t actionId, MoveEvent_t eventType);
		void registerUniqueID(int32_t uniqueId, MoveEvent_t eventType);

		typedef std::map<int32_t, MoveEventList> MoveListMap;
		MoveListMap m_itemIdMap;
		MoveListMap m_uniqueIdMap;
		MoveListMap m_actionIdMap;

		typedef std::map<Position, MoveEventList> MovePosListMap;
		MovePosListMap m_positionMap;
		void clearMap(MoveListMap& map);

		void addEvent(MoveEvent* moveEvent, int32_t id, MoveListMap& map, bool override);
		MoveEvent* getEvent(Item* item, MoveEvent_t eventType, slots_t slot);

		void addEvent(MoveEvent* moveEvent, Position pos, MovePosListMap& map, bool override);
		MoveEvent* getEvent(const Tile* tile, MoveEvent_t eventType);

		const Tile* m_lastCacheTile;
		std::vector<Item*> m_lastCacheItemVector;
};

typedef uint32_t (MoveFunction)(Item* item);
typedef uint32_t (StepFunction)(Creature* creature, Item* item);
typedef bool (EquipFunction)(MoveEvent* moveEvent, Player* player, Item* item, slots_t slot, bool boolean);

class MoveEvent : public Event
{
	public:
		MoveEvent(LuaScriptInterface* _interface);
		MoveEvent(const MoveEvent* copy);
		virtual ~MoveEvent();

		MoveEvent_t getEventType() const;
		void setEventType(MoveEvent_t type);

		virtual bool configureEvent(xmlNodePtr p);
		virtual bool loadFunction(const std::string& functionName, bool isScripted = true) override;

		uint32_t fireStepEvent(Creature* actor, Creature* creature, Item* item, const Position& pos, const Position& fromPos, const Position& toPos);
		uint32_t fireAddRemItem(Creature* actor, Item* item, Item* tileItem, const Position& pos);
		bool fireEquip(Player* player, Item* item, slots_t slot, bool boolean);

		uint32_t executeStep(Creature* actor, Creature* creature, Item* item, const Position& pos, const Position& fromPos, const Position& toPos);
		bool executeEquip(Player* player, Item* item, slots_t slot, bool boolean);
		uint32_t executeAddRemItem(Creature* actor, Item* item, Item* tileItem, const Position& pos);

		static StepFunction StepInField;
		static StepFunction StepOutField; //check
		static MoveFunction AddItemField;
		static MoveFunction RemoveItemField;
		static EquipFunction EquipItem;
		static EquipFunction DeEquipItem;

		uint32_t getWieldInfo() const {return wieldInfo;}
		uint32_t getSlot() const {return slot;}
		int32_t getRequiredLevel() const {return reqLevel;}
		int32_t getReqMagLv() const {return reqMagLevel;}
		bool isPremium() const {return premium;}

		void setVocationString(const std::string& str) {
			vocationString = str;
		}
		const std::string& getVocationString() const {return vocationString;}

		const VocationMap& getVocEquipMap() const {return vocEquipMap;}

		void addVocEquipMap(std::string vocName) {
			int32_t vocationId = g_vocations.getVocationId(vocName);
			if (vocationId != -1) {
				vocEquipMap[vocationId] = true;
			}
		}
		bool getTileItem() const {
			return tileItem;
		}
		void setTileItem(bool b) {
			tileItem = b;
		}
		std::vector<uint32_t> getItemIdRange() {
			return itemIdRange;
		}
		void addItemId(uint32_t id) {
			itemIdRange.emplace_back(id);
		}
		std::vector<uint32_t> getActionIdRange() {
			return actionIdRange;
		}
		void addActionId(uint32_t id) {
			actionIdRange.emplace_back(id);
		}
		std::vector<uint32_t> getUniqueIdRange() {
			return uniqueIdRange;
		}
		void addUniqueId(uint32_t id) {
			uniqueIdRange.emplace_back(id);
		}
		std::vector<std::string> getPosList() {
			return posList;
		}
		void addPosList(std::string pos) {
			posList.emplace_back(pos);
		}
		std::string getSlotName() {
			return slotName;
		}
		void setSlotName(std::string name) {
			slotName = name;
		}
		void setSlot(uint32_t s) {
			slot = s;
		}
		uint32_t getRequiredLevel() {
			return reqLevel;
		}
		void setRequiredLevel(uint32_t level) {
			reqLevel = level;
		}
		uint32_t getRequiredMagLevel() {
			return reqMagLevel;
		}
		void setRequiredMagLevel(uint32_t level) {
			reqMagLevel = level;
		}
		bool needPremium() {
			return premium;
		}
		void setNeedPremium(bool b) {
			premium = b;
		}
		uint32_t getWieldInfo() {
			return wieldInfo;
		}
		void setWieldInfo(WieldInfo_t info) {
			wieldInfo |= info;
		}

		MoveFunction* moveFunction;
		StepFunction* stepFunction;
		EquipFunction* equipFunction;
	protected:
		MoveEvent_t m_eventType;

		virtual std::string getScriptEventName() const;
		virtual std::string getScriptEventParams() const;

		uint32_t wieldInfo, slot;
		int32_t reqLevel, reqMagLevel;
		bool premium;

		VocationMap vocEquipMap;
		std::string vocationString;
		std::string slotName;

		bool tileItem = false;

		std::vector<uint32_t> itemIdRange;
		std::vector<uint32_t> actionIdRange;
		std::vector<uint32_t> uniqueIdRange;
		std::vector<std::string> posList;		
};
#endif
