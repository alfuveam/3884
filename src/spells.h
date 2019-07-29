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

#ifndef __SPELLS__
#define __SPELLS__

#include "enums.h"
#include "player.h"
#include "luascript.h"

#include "baseevents.h"
#include "actions.h"
#include "talkaction.h"

class InstantSpell;
class ConjureSpell;
class RuneSpell;
class Spell;

typedef std::map<uint32_t, RuneSpell*> RunesMap;
typedef std::map<std::string, InstantSpell*> InstantsMap;

class Spells : public BaseEvents
{
	public:
		Spells();
		virtual ~Spells() {clear();}

		Spell* getSpellByName(const std::string& name);

		RuneSpell* getRuneSpell(uint32_t id);
		RuneSpell* getRuneSpellByName(const std::string& name);

		InstantSpell* getInstantSpellById(uint32_t spellId);
		InstantSpell* getInstantSpell(const std::string& words);
		InstantSpell* getInstantSpellByName(const std::string& name);
		InstantSpell* getInstantSpellByIndex(const Player* player, uint32_t index);

		uint32_t getInstantSpellCount(const Player* player);
		ReturnValue onPlayerSay(Player* player, const std::string& words);
		virtual std::string getScriptBaseName() const {return "spells";}
		static Position getCasterPosition(Creature* creature, Direction dir);

		bool registerInstantLuaEvent(InstantSpell* event);
		bool registerRuneLuaEvent(RuneSpell* event);
	protected:
		virtual void clear();

		virtual Event* getEvent(const std::string& nodeName);
		virtual bool registerEvent(Event* event, xmlNodePtr p, bool override);

		virtual LuaScriptInterface& getScriptInterface() {return m_interface;}
		LuaScriptInterface m_interface;

		RunesMap runes;
		InstantsMap instants;
		friend class CombatSpell;
};

typedef bool (InstantSpellFunction)(const InstantSpell* spell, Creature* creature, const std::string& param);
typedef bool (ConjureSpellFunction)(const ConjureSpell* spell, Creature* creature, const std::string& param);
typedef bool (RuneSpellFunction)(const RuneSpell* spell, Creature* creature, Item* item, const Position& posFrom, const Position& posTo);

class BaseSpell
{
	public:
		BaseSpell() {}
		virtual ~BaseSpell() {}

		virtual bool castSpell(Creature* creature);
		virtual bool castSpell(Creature* creature, Creature* target);
};

class CombatSpell : public Event, public BaseSpell
{
	public:
		CombatSpell(Combat* _combat, bool _needTarget, bool _needDirection);
		virtual ~CombatSpell();

		virtual bool castSpell(Creature* creature);
		virtual bool castSpell(Creature* creature, Creature* target);
		virtual bool configureEvent(xmlNodePtr) {return true;}

		//scripting
		bool executeCastSpell(Creature* creature, const LuaVariant& var);

		bool loadScriptCombat();
		Combat* getCombat() {return combat;}

	protected:
		virtual std::string getScriptEventName() const {return "onCastSpell";}
		virtual std::string getScriptEventParams() const {return "cid, var";}

		bool needDirection;
		bool needTarget;
		Combat* combat;
};

class Spell : public BaseSpell
{
	public:
		Spell();
		virtual ~Spell() {}

		bool configureSpell(xmlNodePtr xmlspell);
		const std::string& getName() const {return name;}
		void setName(std::string n) {
			name = n;
		}
		uint8_t getId() const {
			return spellId;
		}
		void setId(uint8_t id) {
			spellId = id;
		}

		void postSpell(Player* player) const;
		void postSpell(Player* player, uint32_t manaCost, uint32_t soulCost) const;

		int32_t getManaCost(const Player* player) const;
		void setSoulCost(uint32_t lsoul){
			soul = lsoul;
		}
		int32_t getSoulCost() const {return soul;}
		void setLevel(uint32_t lvl){
			level = lvl;
		}
		uint32_t getLevel() const {return level;}
		void setMagicLevel(uint32_t magLvl){
			magLevel = magLevel;
		}
		int32_t getMagicLevel() const {return magLevel;}
		void setMana(uint32_t lmana){
			mana = lmana;
		}
		int32_t getMana() const {return mana;}
		void setManaPercent(uint32_t lmanapercent){
			manaPercent = lmanapercent;
		}
		int32_t getManaPercent() const {return manaPercent;}
		uint32_t getExhaustion() const {return exhaustion;}

		int32_t getRange() const {
			return range;
		}
		void setRange(int32_t r) {
			range = r;
		}
		void setEnabled(bool p) {
			enabled = p;
		}	
		bool isEnabled() const {return enabled;}

		void setPremium(bool p) {
			premium = p;
		}		
		bool isPremium() const {return premium;}

		bool getNeedTarget() const {
			return needTarget;
		}
		void setNeedTarget(bool n) {
			needTarget = n;
		}
		bool getNeedWeapon() const {
			return needWeapon;
		}
		void setNeedWeapon(bool n) {
			needWeapon = n;
		}
		bool getNeedLearn() const {
			return learnable;
		}
		void setNeedLearn(bool n) {
			learnable = n;
		}
		bool getSelfTarget() const {
			return selfTarget;
		}
		void setSelfTarget(bool s) {
			selfTarget = s;
		}
		bool getBlockingSolid() const {
			return blockingSolid;
		}
		void setBlockingSolid(bool b) {
			blockingSolid = b;
		}
		bool getBlockingCreature() const {
			return blockingCreature;
		}
		void setBlockingCreature(bool b) {
			blockingCreature = b;
		}
		bool getAggressive() const {
			return aggressive;
		}
		void setAggressive(bool a) {
			aggressive = a;
		}		
		virtual bool isInstant() const = 0;		

		const VocationMap& getVocMap() const {
			return vocSpellMap;
		}
		void addVocMap(uint16_t n, bool b) {
			vocSpellMap[n] = b;
		}

		const SpellGroup_t getGroup() const {
			return group;
		}
		void setGroup(SpellGroup_t g) {
			group = g;
		}
		const SpellGroup_t getSecondaryGroup() const {
			return secondaryGroup;
		}
		void setSecondaryGroup(SpellGroup_t g) {
			secondaryGroup = g;
		}
		uint32_t getCooldown() const {
			return cooldown;
		}
		void setCooldown(uint32_t cd) {
			cooldown = cd;
		}
		uint32_t getGroupCooldown() const {
			return groupCooldown;
		}
		void setGroupCooldown(uint32_t cd) {
			groupCooldown = cd;
		}		
		uint32_t getSecondaryCooldown() const {
			return secondaryGroupCooldown;
		}
		void setSecondaryCooldown(uint32_t cd) {
			secondaryGroupCooldown = cd;
		}
		
		static ReturnValue CreateIllusion(Creature* creature, const Outfit_t& outfit, int32_t time);
		static ReturnValue CreateIllusion(Creature* creature, const std::string& name, int32_t time);
		static ReturnValue CreateIllusion(Creature* creature, uint32_t itemId, int32_t time);

		SpellType_t spellType = SPELL_UNDEFINED;
	protected:
		bool checkSpell(Player* player) const;
		bool checkInstantSpell(Player* player, Creature* creature);
		bool checkInstantSpell(Player* player, const Position& toPos);
		bool checkRuneSpell(Player* player, const Position& toPos);

		int32_t level;
		int32_t magLevel;
		bool premium;
		bool learnable;
		bool enabled;

		int32_t mana;
		int32_t manaPercent;
		int32_t soul;
		int32_t range;
		uint32_t exhaustion;
		
		uint8_t spellId = 0;

		bool needTarget;
		bool needWeapon;
		bool blockingSolid;
		bool blockingCreature;
		bool selfTarget;
		bool aggressive;

		uint32_t cooldown = 1000;
		uint32_t groupCooldown = 1000;
		uint32_t secondaryGroupCooldown = 0;
		VocationMap vocSpellMap;

		//	Incomplete
		SpellGroup_t group = SPELLGROUP_NONE;
		SpellGroup_t secondaryGroup = SPELLGROUP_NONE;		
		using vocStringVec = std::vector<std::string>;

	private:
		std::string name;
};

class InstantSpell : public TalkAction, public Spell
{
	public:
		InstantSpell(LuaScriptInterface* _interface);
		virtual ~InstantSpell() {}

		virtual bool configureEvent(xmlNodePtr p);
		virtual bool loadFunction(const std::string& functionName, bool isScripted = true);

		virtual bool castInstant(Player* player, const std::string& param);

		virtual bool castSpell(Creature* creature);
		virtual bool castSpell(Creature* creature, Creature* target);

		//scripting
		bool executeCastSpell(Creature* creature, const LuaVariant& var);

		bool getHasParam() const {
			return hasParam;
		}
		void setHasParam(bool p) {
			hasParam = p;
		}
		bool getHasPlayerNameParam() const {
			return hasPlayerNameParam; // Incomplete
		}
		void setHasPlayerNameParam(bool p) {
			hasPlayerNameParam = p;
		}
		bool getNeedDirection() const {
			return needDirection;
		}
		void setNeedDirection(bool n) {
			needDirection = n;
		}
		bool getNeedCasterTargetOrDirection() const {
			return casterTargetOrDirection;
		}
		void setNeedCasterTargetOrDirection(bool d) {
			casterTargetOrDirection = d;
		}
		bool getBlockWalls() const {
			return checkLineOfSight;
		}
		void setBlockWalls(bool w) {
			checkLineOfSight = w;
		}		
		virtual bool isInstant() const {return true;}
		bool canCast(const Player* player) const;
		bool canThrowSpell(const Creature* creature, const Creature* target) const;

	protected:
		virtual std::string getScriptEventName() const {return "onCastSpell";}
		virtual std::string getScriptEventParams() const {return "cid, var";}

		static InstantSpellFunction SearchPlayer;
		static InstantSpellFunction SummonMonster;
		static InstantSpellFunction Levitate;
		static InstantSpellFunction Illusion;

		bool internalCastSpell(Creature* creature, const LuaVariant& var);

		bool needDirection;
		bool hasParam;
		bool hasPlayerNameParam;
		bool checkLineOfSight;
		bool casterTargetOrDirection;
		uint8_t limitRange;

		InstantSpellFunction* function;
};

class ConjureSpell : public InstantSpell
{
	public:
		ConjureSpell(LuaScriptInterface* _interface);
		virtual ~ConjureSpell() {}

		virtual bool configureEvent(xmlNodePtr p);
		virtual bool loadFunction(const std::string& functionName, bool isScripted = true);

		virtual bool castInstant(Player* player, const std::string& param);

		virtual bool castSpell(Creature*) {return false;}
		virtual bool castSpell(Creature*, Creature*) {return false;}

		uint32_t getConjureId() const {return conjureId;}
		uint32_t getConjureCount() const {return conjureCount;}
		uint32_t getReagentId() const {return conjureReagentId;}

	protected:
		virtual std::string getScriptEventName() const {return "onCastSpell";}
		virtual std::string getScriptEventParams() const {return "cid, var";}

		static ReturnValue internalConjureItem(Player* player, uint32_t conjureId, uint32_t conjureCount,
			bool transform = false, uint32_t reagentId = 0);

		static ConjureSpellFunction ConjureItem;

		bool internalCastSpell(Creature* creature, const LuaVariant& var);
		Position getCasterPosition(Creature* creature);

		ConjureSpellFunction* function;

		uint32_t conjureId;
		uint32_t conjureCount;
		uint32_t conjureReagentId;
};

class RuneSpell : public Action, public Spell
{
	public:
		RuneSpell(LuaScriptInterface* _interface);
		virtual ~RuneSpell() {}

		virtual bool configureEvent(xmlNodePtr p);
		virtual bool loadFunction(const std::string& functionName, bool isScripted = true);

		virtual ReturnValue canExecuteAction(const Player* player, const Position& toPos);
		virtual bool hasOwnErrorHandler() {return true;}

		virtual bool executeUse(Player* player, Item* item, const Position& posFrom,
			const Position& posTo, bool extendedUse, uint32_t creatureId);

		virtual bool castSpell(Creature* creature);
		virtual bool castSpell(Creature* creature, Creature* target);

		//scripting
		bool executeCastSpell(Creature* creature, const LuaVariant& var);

		virtual bool isInstant() const {return false;}
		void setRuneItemId(uint16_t i) {
			runeId = i;
		}		
		uint32_t getRuneItemId(){return runeId;}

		uint32_t getCharges() const {
			return charges;
		}
		void setCharges(uint32_t c) {
			if (c > 0) {
				hasCharges = true;
			}
			charges = c;
		}

	protected:
		virtual std::string getScriptEventName() const {return "onCastSpell";}
		virtual std::string getScriptEventParams() const {return "cid, var";}

		static RuneSpellFunction Illusion;
		static RuneSpellFunction Convince;

		bool internalCastSpell(Creature* creature, const LuaVariant& var);

		bool hasCharges;
		uint32_t charges = 0;
		uint32_t runeId;

		RuneSpellFunction* function;
};
#endif
