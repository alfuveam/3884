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

#ifndef __WEAPONS__
#define __WEAPONS__

#include "const.h"
#include "combat.h"

#include "baseevents.h"
#include "luascript.h"

#include "player.h"
#include "item.h"

#include "vocation.h"

extern Vocations g_vocations;

class Weapon;
class WeaponMelee;
class WeaponDistance;
class WeaponWand;

using Weapon_ptr = std::unique_ptr<Weapon>;

class Weapons : public BaseEvents
{
	public:
		Weapons();
		virtual ~Weapons() {clear();}

		bool loadDefaults();
		const Weapon* getWeapon(const Item* item) const;

		static int32_t getMaxMeleeDamage(int32_t attackSkill, int32_t attackValue);
		static int32_t getMaxWeaponDamage(int32_t level, int32_t attackSkill, int32_t attackValue, float attackFactor);
		bool registerLuaEvent(Weapon* event);

	protected:
		virtual std::string getScriptBaseName() const {return "weapons";}
		virtual void clear();

		virtual Event* getEvent(const std::string& nodeName);
		virtual bool registerEvent(Event* event, xmlNodePtr p, bool override);

		virtual LuaScriptInterface& getScriptInterface() {return m_interface;}
		LuaScriptInterface m_interface;

		using weapons = std::map<uint32_t, Weapon*>;
};

class Weapon : public Event
{
	public:
		Weapon(LuaScriptInterface* _interface);
		virtual ~Weapon() {}

		static bool useFist(Player* player, Creature* target);

		virtual bool loadFunction(const std::string& functionName, bool isScripted = true) final;
		virtual bool configureEvent(xmlNodePtr p);
		virtual bool configureWeapon(const ItemType& it);

		virtual int32_t playerWeaponCheck(Player* player, Creature* target) const;

		void setID(uint16_t newId) {
			id = newId;
		}
		uint16_t getID() const {return id;}
		CombatParams getCombatParam() const {return params;}
		virtual bool interruptSwing() const {return !swing;}

		virtual bool useWeapon(Player* player, Item* item, Creature* target) const;
		virtual int32_t getWeaponDamage(const Player* player, const Creature* target, const Item* item, bool maxDamage = false) const = 0;
		virtual int32_t getElementDamage(const Player*, const Item*) const = 0;

		void setRequiredLevel(uint32_t reqlvl) {
			level = reqlvl;
		}
		uint32_t getRequiredLevel() const {return level;}

		void setRequiredMagLevel(uint32_t reqlvl) {
			magLevel = reqlvl;
		}		
		uint32_t getReqMagLv() const {return magLevel;}
		bool hasExhaustion() const {return exhaustion;}

		void setNeedPremium(bool prem) {
			premium = prem;
		}

		bool isPremium() const {return premium;}
		void setWieldUnproperly(bool unproperly) {
			wieldUnproperly = unproperly;
		}		
		bool isWieldedUnproperly() const {return wieldUnproperly;}

		uint32_t getWieldInfo() const {
			ItemType& it = Item::items.getItemType(this->id);
			return it.wieldInfo;
		}
		void setWieldInfo(uint32_t info) {
			ItemType& it = Item::items.getItemType(this->id);
			it.wieldInfo |= info;
		}
		
		void addVocWeaponMap(std::string vocName) {
			int32_t vocationId = g_vocations.getVocationId(vocName);
			if (vocationId != -1) {
				vocWeaponMap[vocationId] = true;
			}
		}

		const std::string& getVocationString() const {
			ItemType& it = Item::items.getItemType(this->id);			
			return it.vocationString;
		}
		void setVocationString(const std::string& str) {
			ItemType& it = Item::items.getItemType(this->id);
			it.vocationString = str;
		}

		int32_t getHealth() const {
			return health;
		}
		void setHealth(int32_t h) {
			health = h;
		}

		uint32_t getMana() const {
			return mana;
		}
		void setMana(uint32_t m) {
			mana = m;
		}
		uint32_t getManaPercent() const {
			return manaPercent;
		}
		void setManaPercent(uint32_t m) {
			manaPercent = m;
		}
		int32_t getManaCost(const Player* player) const;

		WeaponType_t weaponType;
		AmmoAction_t ammoAction;
		CombatParams params;
	protected:
		virtual std::string getScriptEventName() const {return "onUseWeapon";}
		virtual std::string getScriptEventParams() const {return "cid, var";}

		bool executeUseWeapon(Player* player, const LuaVariant& var) const;

		bool internalUseWeapon(Player* player, Item* item, Creature* target, int32_t damageModifier) const;
		bool internalUseWeapon(Player* player, Item* item, Tile* tile) const;

		virtual void onUsedWeapon(Player* player, Item* item, Tile* destTile) const;
		virtual void onUsedAmmo(Player* player, Item* item, Tile* destTile) const;
		virtual bool getSkillType(const Player*, const Item*, skills_t&, uint64_t&) const {return false;}

		uint16_t id;
		uint32_t exhaustion;
		uint32_t health = 0;
		bool enabled, premium, wieldUnproperly, swing;
		int32_t level, magLevel, mana, manaPercent, soul;
		
	private:
		VocationMap vocWeaponMap;
};

class WeaponMelee : public Weapon
{
	public:
		WeaponMelee(LuaScriptInterface* _interface);
		virtual ~WeaponMelee() {}

		virtual bool configureWeapon(const ItemType& it);

		virtual bool useWeapon(Player* player, Item* item, Creature* target) const;
		virtual int32_t getWeaponDamage(const Player* player, const Creature* target, const Item* item, bool maxDamage = false) const;
		int32_t getElementDamage(const Player* player, const Item* item) const;

	protected:
		virtual bool getSkillType(const Player* player, const Item* item, skills_t& skill, uint64_t& skillPoint) const;

		CombatType_t elementType;
		int16_t elementDamage;
};

class WeaponDistance : public Weapon
{
	public:
		WeaponDistance(LuaScriptInterface* _interface);
		virtual ~WeaponDistance() {}

		virtual bool configureWeapon(const ItemType& it);

		virtual int32_t playerWeaponCheck(Player* player, Creature* target) const;

		virtual bool useWeapon(Player* player, Item* item, Creature* target) const;
		virtual int32_t getWeaponDamage(const Player* player, const Creature* target, const Item* item, bool maxDamage = false) const;
		int32_t getElementDamage(const Player* player, const Item* item) const { return 0; }

	protected:
		virtual void onUsedAmmo(Player* player, Item* item, Tile* destTile) const;
		virtual bool getSkillType(const Player* player, const Item* item, skills_t& skill, uint64_t& skillPoint) const;

		int32_t hitChance, maxHitChance, breakChance, ammoAttackValue;
};

class WeaponWand : public Weapon
{
	public:
		WeaponWand(LuaScriptInterface* _interface);
		virtual ~WeaponWand() {}

		virtual bool configureEvent(xmlNodePtr p);
		virtual bool configureWeapon(const ItemType& it);

		virtual int32_t getWeaponDamage(const Player* player, const Creature* target, const Item* item, bool maxDamage = false) const;
		int32_t getElementDamage(const Player* player, const Item* item) const { return 0; }
		void setMinChange(int32_t change) {
			minChange = change;
		}

		void setMaxChange(int32_t change) {
			maxChange = change;
		}
	protected:
		virtual bool getSkillType(const Player*, const Item*, skills_t&, uint64_t&) const {return false;}

		int32_t minChange, maxChange;
};
#endif
