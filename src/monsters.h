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

#ifndef __MONSTERS__
#define __MONSTERS__

#include "creature.h"
#define MAX_LOOTCHANCE 100000
#define MAX_STATICWALK 100

enum LootMessage_t
{
	LOOTMSG_IGNORE = -1,
	LOOTMSG_NONE = 0,
	LOOTMSG_PLAYER = 1,
	LOOTMSG_PARTY = 2,
	LOOTMSG_BOTH = 3
};

struct LootBlock {
	uint16_t id;
	uint32_t countmax;
	uint32_t chance;

	//optional
	int32_t subType;
	int32_t actionId;
	int32_t uniqueId;
	std::string text;
	std::string name;
	std::string article;
	int32_t attack;
	int32_t defense;
	int32_t extraDefense;
	int32_t armor;
	int32_t shootRange;
	int32_t hitChance;
	bool unique;

	std::vector<LootBlock> childLoot;
	LootBlock() {
		id = 0;
		countmax = 1;
		chance = 0;

		subType = -1;
		actionId = -1;
		uniqueId = -1;
		attack = -1;
		defense = -1;
		extraDefense = -1;
		armor = -1;
		shootRange = -1;
		hitChance = -1;
		unique = false;
	}
};

struct summonBlock_t
{
	std::string name;
	uint32_t chance, interval, amount, speed;
};

class BaseSpell;

struct spellBlock_t
{
	bool combatSpell, isMelee;
	int32_t minCombatValue, maxCombatValue;
	uint32_t chance, speed, range;
	BaseSpell* spell;
};

struct voiceBlock_t
{
	bool yellText;
	std::string text;
};

class Loot {
	public:
		Loot() = default;

		// non-copyable
		Loot(const Loot&) = delete;
		Loot& operator=(const Loot&) = delete;

		LootBlock lootBlock;
};

class MonsterType
{
	public:
		MonsterType() {reset();}
		virtual ~MonsterType() {reset();}

		void reset();

		LuaScriptInterface* scriptInterface;
		
		void loadLoot(MonsterType* monsterType, LootBlock lootblock);
		void dropLoot(Container* corpse);
		ItemList createLoot(const LootBlock& lootBlock);
		bool createChildLoot(Container* parent, const LootBlock& lootBlock);

		bool isSummonable, isIllusionable, isConvinceable, isAttackable, isHostile, isLureable,
			isWalkable, canPushItems, canPushCreatures, pushable, hideName, hiddenHealth;

		Outfit_t outfit;
		RaceType_t race;
		Skulls_t skull;
		PartyShields_t partyShield;
		GuildEmblems_t guildEmblem;
		LootMessage_t lootMessage;
		
		int32_t creatureAppearEvent = -1;
		int32_t creatureDisappearEvent = -1;
		int32_t creatureMoveEvent = -1;
		int32_t creatureSayEvent = -1;
		int32_t thinkEvent = -1;

		int32_t defense, armor, health, healthMax, baseSpeed, lookCorpse, corpseUnique, corpseAction,
			maxSummons, targetDistance, runAwayHealth, conditionImmunities, damageImmunities,
			lightLevel, lightColor, changeTargetSpeed, changeTargetChance;
		uint32_t yellChance, yellSpeedTicks, staticAttackChance, manaCost;
		uint64_t experience;

		std::string name, nameDescription;

		std::list<summonBlock_t> summonList;
		std::vector<LootBlock> lootItems;
		std::map<CombatType_t, int32_t> elementMap;
		std::list<spellBlock_t> spellAttackList;
		std::list<spellBlock_t> spellDefenseList;
		std::vector<voiceBlock_t> voiceVector;
		std::vector<std::string> scriptList;
};

class MonsterSpell
{
	public:
		MonsterSpell() = default;

		MonsterSpell(const MonsterSpell&) = delete;
		MonsterSpell& operator=(const MonsterSpell&) = delete;

		std::string name = "";
		std::string scriptName = "";

		uint8_t chance = 100;
		uint8_t range = 0;

		uint16_t interval = 2000;

		int32_t minCombatValue = 0;
		int32_t maxCombatValue = 0;
		int32_t attack = 0;
		int32_t skill = 0;
		int32_t length = 0;
		int32_t spread = 0;
		int32_t radius = 0;
		int32_t conditionMinDamage = 0;
		int32_t conditionMaxDamage = 0;
		int32_t conditionStartDamage = 0;
		int32_t tickInterval = 0;
		int32_t speedChange = 0;
		int32_t duration = 0;

		bool isScripted = false;
		bool needTarget = false;
		bool needDirection = false;
		bool combatSpell = false;
		bool isMelee = false;

		Outfit_t outfit = {};
		ShootEffect_t shoot = SHOOT_EFFECT_NONE;
		MagicEffect_t effect = MAGIC_EFFECT_NONE;
		ConditionType_t conditionType = CONDITION_NONE;
		CombatType_t combatType = COMBAT_UNDEFINEDDAMAGE;
};

class Monsters
{
	public:
		Monsters(): loaded(false) {}
		virtual ~Monsters();

		bool reload() {return loadFromXml(true);}
		bool loadFromXml(bool reloading = false);

		bool loadMonster(const std::string& file, const std::string& monsterName, bool reloading = false);

		void addMonsterType(const std::string& name, MonsterType* mType);
		bool deserializeSpell(MonsterSpell* spell, spellBlock_t& sb, const std::string& description = "");
		
		MonsterType* getMonsterType(const std::string& name);
		MonsterType* getMonsterType(uint32_t mid);

		uint32_t getIdByName(const std::string& name);
		bool isLoaded() const {return loaded;}
		static uint16_t getLootRandom();

		bool loadLoot(xmlNodePtr, LootBlock&);
		
		std::unique_ptr<LuaScriptInterface> scriptInterface;
	private:
		bool loaded;
		
		bool loadChildLoot(xmlNodePtr, LootBlock&);

		ConditionDamage* getDamageCondition(ConditionType_t conditionType,
			int32_t maxDamage, int32_t minDamage, int32_t startDamage, uint32_t tickInterval);
		bool deserializeSpell(xmlNodePtr node, spellBlock_t& sb, const std::string& description = "");

		std::map<std::string, MonsterType*> monsters;
};
#endif
