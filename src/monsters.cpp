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

#include "monsters.h"

#include "monster.h"

#include "luascript.h"
#include "container.h"
#include "weapons.h"

#include "spells.h"
#include "combat.h"

#include "configmanager.h"
#include "game.h"

extern Game g_game;
extern Spells* g_spells;
extern Monsters g_monsters;
extern ConfigManager g_config;

void MonsterType::reset()
{
	canPushItems = canPushCreatures = isSummonable = isIllusionable = isConvinceable = isLureable = isWalkable = hideName = hideHealth = false;
	pushable = isAttackable = isHostile = true;

	outfit.lookHead = outfit.lookBody = outfit.lookLegs = outfit.lookFeet = outfit.lookType = outfit.lookTypeEx = outfit.lookAddons = 0;
	runAwayHealth = manaCost = lightLevel = lightColor = yellSpeedTicks = yellChance = changeTargetSpeed = changeTargetChance = 0;
	experience = defense = armor = lookCorpse = corpseUnique = corpseAction = conditionImmunities = damageImmunities = 0;

	maxSummons = -1;
	targetDistance = 1;
	staticAttackChance = 95;
	health = healthMax = 100;
	baseSpeed = 200;

	race = RACE_BLOOD;
	skull = SKULL_NONE;
	partyShield = SHIELD_NONE;
	guildEmblem = EMBLEM_NONE;
	lootMessage = LOOTMSG_IGNORE;

	for(SpellList::iterator it = spellAttackList.begin(); it != spellAttackList.end(); ++it)
	{
		if(!it->combatSpell)
			continue;

		delete it->spell;
		it->spell = NULL;
	}

	spellAttackList.clear();
	for(SpellList::iterator it = spellDefenseList.begin(); it != spellDefenseList.end(); ++it)
	{
		if(!it->combatSpell)
			continue;

		delete it->spell;
		it->spell = NULL;
	}

	spellDefenseList.clear();
	summonList.clear();
	scriptList.clear();

	voiceVector.clear();
	lootItems.clear();
	elementMap.clear();
}

uint16_t Monsters::getLootRandom()
{
	return (uint16_t)std::ceil((double)random_range(0, MAX_LOOTCHANCE) / g_config.getDouble(ConfigManager::RATE_LOOT));
}

void MonsterType::dropLoot(Container* corpse)
{
	ItemList items;
	for(LootItems::const_iterator it = lootItems.begin(); it != lootItems.end() && !corpse->full(); ++it)
	{
		items = createLoot(*it);
		if(items.empty())
			continue;

		for(ItemList::iterator iit = items.begin(); iit != items.end(); ++iit)
		{
			Item* tmpItem = *iit;
			if(Container* container = tmpItem->getContainer())
			{
				if(createChildLoot(container, *it))
					corpse->__internalAddThing(tmpItem);
				else
					delete container;
			}
			else
				corpse->__internalAddThing(tmpItem);
		}
	}

	corpse->__startDecaying();
	uint32_t ownerId = corpse->getCorpseOwner();
	if(!ownerId)
		return;

	Player* owner = g_game.getPlayerByGuid(ownerId);
	if(!owner)
		return;

	LootMessage_t message = lootMessage;
	if(message == LOOTMSG_IGNORE)
		message = (LootMessage_t)g_config.getNumber(ConfigManager::LOOT_MESSAGE);

	if(message < LOOTMSG_PLAYER)
		return;

	std::stringstream ss;
	ss << "Loot of " << nameDescription << ": " << corpse->getContentDescription() << ".";
	if(owner->getParty() && message > LOOTMSG_PLAYER)
		owner->getParty()->broadcastMessage((MessageClasses)g_config.getNumber(ConfigManager::LOOT_MESSAGE_TYPE), ss.str());
	else if(message == LOOTMSG_PLAYER || message == LOOTMSG_BOTH)
		owner->sendTextMessage((MessageClasses)g_config.getNumber(ConfigManager::LOOT_MESSAGE_TYPE), ss.str());
}

ItemList MonsterType::createLoot(const LootBlock& lootBlock)
{
	uint16_t item = lootBlock.ids[0], random = Monsters::getLootRandom(), count = 0;
	if(lootBlock.ids.size() > 1)
		item = lootBlock.ids[random_range((size_t)0, lootBlock.ids.size() - 1)];

	ItemList items;
	if(random < lootBlock.chance)
		count = random % lootBlock.count + 1;

	Item* tmpItem = NULL;
	while(count > 0)
	{
		uint16_t n = 1;
		if(Item::items[item].stackable)
			n = std::min(count, (uint16_t)100);

		if(!(tmpItem = Item::CreateItem(item, n)))
			break;

		count -= n;
		if(lootBlock.subType != -1)
			tmpItem->setSubType(lootBlock.subType);

		if(lootBlock.actionId != -1)
			tmpItem->setActionId(lootBlock.actionId, false);

		if(lootBlock.uniqueId != -1)
			tmpItem->setUniqueId(lootBlock.uniqueId);

		if(!lootBlock.text.empty())
			tmpItem->setText(lootBlock.text);

		items.push_back(tmpItem);
	}

	return items;
}

bool MonsterType::createChildLoot(Container* parent, const LootBlock& lootBlock)
{
	LootItems::const_iterator it = lootBlock.childLoot.begin();
	if(it == lootBlock.childLoot.end())
		return true;

	ItemList items;
	for(; it != lootBlock.childLoot.end() && !parent->full(); ++it)
	{
		items = createLoot(*it);
		if(items.empty())
			continue;

		for(ItemList::iterator iit = items.begin(); iit != items.end(); ++iit)
		{
			Item* tmpItem = *iit;
			if(Container* container = tmpItem->getContainer())
			{
				if(createChildLoot(container, *it))
					parent->__internalAddThing(tmpItem);
				else
					delete container;
			}
			else
				parent->__internalAddThing(tmpItem);
		}
	}

	return !parent->empty();
}

bool Monsters::loadFromXml(bool reloading /*= false*/)
{
	loaded = false;
	pugi::xml_attribute attr;
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(getFilePath(FILE_TYPE_OTHER, "monster/monsters.xml").c_str());
	if(!result)
	{
		std::clog << "[Warning - Monsters::loadFromXml] Cannot load monsters file." << std::endl;		
		return false;
	}

	if(strcasecmp(doc.name(), "monsters") == 0)
	{
		std::clog << "[Error - Monsters::loadFromXml] Malformed monsters file." << std::endl;		
		return false;
	}

	for(auto p : doc.children())
	{
		if(strcasecmp(p.name(), "monster") == 0)
		{
			std::clog << "[Warning - Monsters::loadFromXml] Unknown node name (" << p.name() << ")." << std::endl;			
			continue;
		}

		std::string file, name;
		if((attr = doc.attribute("file")) && (attr = doc.attribute("name")))
		{
			file = getFilePath(FILE_TYPE_OTHER, "monster/" + file);
			loadMonster(file, name, reloading);
		}
	}
	loaded = true;
	return loaded;
}

ConditionDamage* Monsters::getDamageCondition(ConditionType_t conditionType,
	int32_t maxDamage, int32_t minDamage, int32_t startDamage, uint32_t tickInterval)
{
	if(ConditionDamage* condition = dynamic_cast<ConditionDamage*>(Condition::createCondition(CONDITIONID_COMBAT, conditionType, 0)))
	{
		condition->setParam(CONDITIONPARAM_TICKINTERVAL, tickInterval);
		condition->setParam(CONDITIONPARAM_MINVALUE, minDamage);
		condition->setParam(CONDITIONPARAM_MAXVALUE, maxDamage);
		condition->setParam(CONDITIONPARAM_STARTVALUE, startDamage);
		condition->setParam(CONDITIONPARAM_DELAYED, 1);
		return condition;
	}

	return NULL;
}

bool Monsters::deserializeSpell(pugi::xml_node& node, spellBlock_t& sb, const std::string& description)
{
	sb.range = sb.minCombatValue = sb.maxCombatValue = 0;
	sb.combatSpell = sb.isMelee = false;
	sb.chance = 100;
	sb.speed = 2000;

	std::string name, scriptName;
	bool isScripted = false;
	pugi::xml_attribute attr;
	
	if((attr = node.attribute("script")))
		isScripted = true;
	else if(!(attr = node.attribute("name")))
		return false;

	int32_t intValue;
	std::string strValue;
	if((attr = node.attribute("speed")) || (attr = node.attribute("interval")))
		sb.speed = std::max(1, pugi::cast<int32_t>(attr.value()));

	if((attr = node.attribute("chance")))
	{
		intValue = pugi::cast<int32_t>(attr.value());
		if(intValue < 0 || intValue > 100)
			intValue = 100;

		sb.chance = intValue;
	}

	if((attr = node.attribute("range")))
	{
		intValue = pugi::cast<int32_t>(attr.value());
		if(intValue < 0)
			intValue = 0;

		if(intValue > Map::maxViewportX * 2)
			intValue = Map::maxViewportX * 2;

		sb.range = intValue;
	}

	if((attr = node.attribute("min")))
		sb.minCombatValue = pugi::cast<int32_t>(attr.value());

	if((attr = node.attribute("max")))
		sb.maxCombatValue = pugi::cast<int32_t>(attr.value());

	//normalize values
	if(std::abs(sb.minCombatValue) > std::abs(sb.maxCombatValue))
		std::swap(sb.minCombatValue, sb.maxCombatValue);

	if((sb.spell = g_spells->getSpellByName(name)))
		return true;

	CombatSpell* combatSpell = NULL;
	bool needTarget = false, needDirection = false;
	if(isScripted)
	{
		if((attr = node.attribute("direction")))
			needDirection = booleanString(pugi::cast<std::string>(attr.value()));

		if((attr = node.attribute("target")))
			needTarget = booleanString(pugi::cast<std::string>(attr.value()));

		combatSpell = new CombatSpell(NULL, needTarget, needDirection);
		if(!combatSpell->loadScript(getFilePath(FILE_TYPE_OTHER, g_spells->getScriptBaseName() + "/scripts/" + scriptName), true))
		{
			delete combatSpell;
			return false;
		}

		if(!combatSpell->loadScriptCombat())
		{
			delete combatSpell;
			return false;

		}

		combatSpell->getCombat()->setPlayerCombatValues(FORMULA_VALUE, sb.minCombatValue, 0, sb.maxCombatValue, 0, 0, 0, 0, 0, 0, 0);
	}
	else
	{
		Combat* combat = new Combat;
		sb.combatSpell = true;
		if((attr = node.attribute("length")))
		{
			int32_t length = attr.as_int();
			if(length > 0)
			{
				int32_t spread = 3;
				//need direction spell
				if((attr = node.attribute("spread")))
					spread = std::max(0, pugi::cast<int32_t>(attr.value()));

				CombatArea* area = new CombatArea();
				area->setupArea(length, spread);

				combat->setArea(area);
				needDirection = true;
			}
		}

		if((attr = node.attribute("radius")))
		{
			int32_t radius = pugi::cast<int32_t>(attr.value());
			//target spell
			if((attr = node.attribute("target")))
				needTarget = (pugi::cast<int32_t>(attr.value()) != 0);

			CombatArea* area = new CombatArea();
			area->setupArea(radius);
			combat->setArea(area);
		}

		std::string tmpName = asLowerCaseString(name);
		if(tmpName == "melee" || tmpName == "distance")
		{
			int32_t attack = 0, skill = 0;
			if((attr = node.attribute("attack")) && (attr = node.attribute("skill")))
			{
				sb.minCombatValue = 0;
				sb.maxCombatValue = -Weapons::getMaxMeleeDamage(skill, attack);
			}

			uint32_t tickInterval = 10000;
			ConditionType_t conditionType = CONDITION_NONE;
			if((attr = node.attribute("physical")))
				conditionType = CONDITION_PHYSICAL;
			else if((attr = node.attribute("fire")))
				conditionType = CONDITION_FIRE;
			else if((attr = node.attribute("energy")))
				conditionType = CONDITION_ENERGY;
			else if((attr = node.attribute("earth")))
				conditionType = CONDITION_POISON;
			else if((attr = node.attribute("freeze")))
				conditionType = CONDITION_FREEZING;
			else if((attr = node.attribute("dazzle")))
				conditionType = CONDITION_DAZZLED;
			else if((attr = node.attribute("curse")))
				conditionType = CONDITION_CURSED;
			else if((attr = node.attribute("drown")))
			{
				conditionType = CONDITION_DROWN;
				tickInterval = 5000;
			}
			else if((attr = node.attribute("poison")))
			{
				conditionType = CONDITION_POISON;
				tickInterval = 5000;
			}

			uint32_t damage = std::abs(pugi::cast<int32_t>(attr.value()));
			if((attr = node.attribute("tick")))
			{	
				intValue = pugi::cast<int32_t>(attr.value());
				if(intValue > 0)
					tickInterval = intValue;
			}
			if(conditionType != CONDITION_NONE)
			{
				Condition* condition = getDamageCondition(conditionType, damage, damage, 0, tickInterval);
				if(condition)
					combat->setCondition(condition);
			}

			sb.isMelee = true;
			sb.range = 1;

			combat->setParam(COMBATPARAM_COMBATTYPE, COMBAT_PHYSICALDAMAGE);
			combat->setParam(COMBATPARAM_BLOCKEDBYSHIELD, 1);
			combat->setParam(COMBATPARAM_BLOCKEDBYARMOR, 1);
		}
		else if(tmpName == "physical")
		{
			combat->setParam(COMBATPARAM_COMBATTYPE, COMBAT_PHYSICALDAMAGE);
			combat->setParam(COMBATPARAM_BLOCKEDBYARMOR, 1);
		}
		else if(tmpName == "drown")
			combat->setParam(COMBATPARAM_COMBATTYPE, COMBAT_DROWNDAMAGE);
		else if(tmpName == "fire")
			combat->setParam(COMBATPARAM_COMBATTYPE, COMBAT_FIREDAMAGE);
		else if(tmpName == "energy")
			combat->setParam(COMBATPARAM_COMBATTYPE, COMBAT_ENERGYDAMAGE);
		else if(tmpName == "poison" || tmpName == "earth")
			combat->setParam(COMBATPARAM_COMBATTYPE, COMBAT_EARTHDAMAGE);
		else if(tmpName == "ice")
			combat->setParam(COMBATPARAM_COMBATTYPE, COMBAT_ICEDAMAGE);
		else if(tmpName == "holy")
			combat->setParam(COMBATPARAM_COMBATTYPE, COMBAT_HOLYDAMAGE);
		else if(tmpName == "death")
			combat->setParam(COMBATPARAM_COMBATTYPE, COMBAT_DEATHDAMAGE);
		else if(tmpName == "lifedrain")
			combat->setParam(COMBATPARAM_COMBATTYPE, COMBAT_LIFEDRAIN);
		else if(tmpName == "manadrain")
			combat->setParam(COMBATPARAM_COMBATTYPE, COMBAT_MANADRAIN);
		else if(tmpName == "healing")
		{
			bool aggressive = false;
			if((attr = node.attribute("self")))
				aggressive = pugi::cast<int32_t>(attr.value());

			combat->setParam(COMBATPARAM_COMBATTYPE, COMBAT_HEALING);
			combat->setParam(COMBATPARAM_AGGRESSIVE, aggressive);
		}
		else if(tmpName == "undefined")
			combat->setParam(COMBATPARAM_COMBATTYPE, COMBAT_UNDEFINEDDAMAGE);
		else if(tmpName == "speed")
		{
			int32_t speedChange = 0, duration = 10000;
			if((attr = node.attribute("duration")))
				duration = pugi::cast<int32_t>(attr.value());

			enum Aggressive {
				NO,
				YES,
				AUTO
			} aggressive = AUTO;
			if((attr = node.attribute("self")))
				aggressive = (Aggressive)pugi::cast<int32_t>(attr.value());

			if((attr = node.attribute("speedchange")))
				speedChange = std::max(-1000, pugi::cast<int32_t>(attr.value())); //cant be slower than 100%

			std::vector<Outfit_t> outfits;
			
			for(auto tmpNode: node.children())
			{
				if(strcasecmp(tmpNode.name(),"outfit") == 0)
					continue;

				if((attr = tmpNode.attribute("type")))
				{
					Outfit_t outfit;
					outfit.lookType = pugi::cast<int32_t>(attr.value());
					if((attr = tmpNode.attribute("head")))
						outfit.lookHead = pugi::cast<int32_t>(attr.value());

					if((attr = tmpNode.attribute("body")))
						outfit.lookBody = pugi::cast<int32_t>(attr.value());

					if((attr = tmpNode.attribute("legs")))
						outfit.lookLegs = pugi::cast<int32_t>(attr.value());

					if((attr = tmpNode.attribute("feet")))
						outfit.lookFeet = pugi::cast<int32_t>(attr.value());

					if((attr = tmpNode.attribute("addons")))
						outfit.lookAddons = pugi::cast<int32_t>(attr.value());

					outfits.push_back(outfit);
				}

				if((attr = tmpNode.attribute("typeex")) || (attr = tmpNode.attribute("item")))
				{
					Outfit_t outfit;
					outfit.lookTypeEx = pugi::cast<int32_t>(attr.value());
					outfits.push_back(outfit);
				}

				if((attr = tmpNode.attribute("monster")))
				{
					if(MonsterType* mType = g_monsters.getMonsterType(pugi::cast<std::string>(attr.value())))
						outfits.push_back(mType->outfit);
				}
			}

			ConditionType_t conditionType = CONDITION_PARALYZE;
			if(speedChange > 0)
			{
				conditionType = CONDITION_HASTE;
				if(aggressive == AUTO)
					aggressive = NO;
			}
			else if(aggressive == AUTO)
				aggressive = YES;

			if(ConditionSpeed* condition = dynamic_cast<ConditionSpeed*>(Condition::createCondition(
				CONDITIONID_COMBAT, conditionType, duration)))
			{
				condition->setFormulaVars((speedChange / 1000.), 0, (speedChange / 1000.), 0);
				if(!outfits.empty())
					condition->setOutfits(outfits);

				combat->setCondition(condition);
				combat->setParam(COMBATPARAM_AGGRESSIVE, aggressive);
			}
		}
		else if(tmpName == "outfit")
		{
			std::vector<Outfit_t> outfits;

			for(auto tmpNode : node.children())
			{
				if(strcasecmp(tmpNode.name(),"outfit") == 0)
					continue;

				if((attr = tmpNode.attribute("type")))
				{
					Outfit_t outfit;
					outfit.lookType = pugi::cast<int32_t>(attr.value());
					if((attr = tmpNode.attribute("head")))
						outfit.lookHead = pugi::cast<int32_t>(attr.value());

					if((attr = tmpNode.attribute("body")))
						outfit.lookBody = pugi::cast<int32_t>(attr.value());

					if((attr = tmpNode.attribute("legs")))
						outfit.lookLegs = pugi::cast<int32_t>(attr.value());

					if((attr = tmpNode.attribute("feet")))
						outfit.lookFeet = pugi::cast<int32_t>(attr.value());

					if((attr = tmpNode.attribute("addons")))
						outfit.lookAddons = pugi::cast<int32_t>(attr.value());

					outfits.push_back(outfit);
				}

				if((attr = tmpNode.attribute("typeex")) || (attr = tmpNode.attribute("item")))
				{
					Outfit_t outfit;
					outfit.lookTypeEx = pugi::cast<int32_t>(attr.value());
					outfits.push_back(outfit);
				}

				if((attr = tmpNode.attribute("monster")))
				{
					if(MonsterType* mType = g_monsters.getMonsterType(pugi::cast<std::string>(attr.value())))
						outfits.push_back(mType->outfit);
				}
			}

			if(outfits.empty())
			{
				if((attr = node.attribute("type")))
				{
					Outfit_t outfit;
					outfit.lookType = pugi::cast<int32_t>(attr.value());
					if((attr = node.attribute("head")))
						outfit.lookHead = pugi::cast<int32_t>(attr.value());

					if((attr = node.attribute("body")))
						outfit.lookBody = pugi::cast<int32_t>(attr.value());

					if((attr = node.attribute("legs")))
						outfit.lookLegs = pugi::cast<int32_t>(attr.value());

					if((attr = node.attribute("feet")))
						outfit.lookFeet = pugi::cast<int32_t>(attr.value());

					if((attr = node.attribute("addons")))
						outfit.lookAddons = pugi::cast<int32_t>(attr.value());

					outfits.push_back(outfit);
				}

				if((attr = node.attribute("typeex")) || (attr = node.attribute("item")))
				{
					Outfit_t outfit;
					outfit.lookTypeEx = pugi::cast<int32_t>(attr.value());
					outfits.push_back(outfit);
				}

				if((attr = node.attribute("monster")))
				{
					if(MonsterType* mType = g_monsters.getMonsterType(pugi::cast<std::string>(attr.value())))
						outfits.push_back(mType->outfit);
				}
			}

			if(!outfits.empty())
			{
				int32_t duration = 10000;
				if((attr = node.attribute("duration")))
					duration = pugi::cast<int32_t>(attr.value());

				bool aggressive = false;
				if((attr = node.attribute("self")))
					aggressive = pugi::cast<int32_t>(attr.value());

				if(ConditionOutfit* condition = dynamic_cast<ConditionOutfit*>(Condition::createCondition(
					CONDITIONID_COMBAT, CONDITION_OUTFIT, duration)))
				{
					condition->setOutfits(outfits);
					combat->setCondition(condition);
					combat->setParam(COMBATPARAM_AGGRESSIVE, aggressive);
				}
			}
		}
		else if(tmpName == "invisible")
		{
			int32_t duration = 10000;
			if((attr = node.attribute("duration")))
				duration = pugi::cast<int32_t>(attr.value());

			bool aggressive = false;
			if((attr = node.attribute("self")))
				aggressive = pugi::cast<int32_t>(attr.value());

			if(Condition* condition = Condition::createCondition(CONDITIONID_COMBAT, CONDITION_INVISIBLE, duration))
			{
				combat->setParam(COMBATPARAM_AGGRESSIVE, aggressive);
				combat->setCondition(condition);
			}
		}
		else if(tmpName == "drunk")
		{
			int32_t duration = 10000;
			if((attr = node.attribute("duration")))
				duration = pugi::cast<int32_t>(attr.value());

			if(Condition* condition = Condition::createCondition(CONDITIONID_COMBAT, CONDITION_DRUNK, duration))
				combat->setCondition(condition);
		}
		else if(tmpName == "skills" || tmpName == "attributes")
		{
			uint32_t duration = 10000, subId = 0;
			if((attr = node.attribute("duration")))
				duration = pugi::cast<int32_t>(attr.value());

			if((attr = node.attribute("subid")))
				subId = pugi::cast<int32_t>(attr.value());

			intValue = 0;
			ConditionParam_t param = CONDITIONPARAM_BUFF; //to know was it loaded
			if((attr = node.attribute("melee")))
				param = CONDITIONPARAM_SKILL_MELEE;
			else if((attr = node.attribute("fist")))
				param = CONDITIONPARAM_SKILL_FIST;
			else if((attr = node.attribute("club")))
				param = CONDITIONPARAM_SKILL_CLUB;
			else if((attr = node.attribute("axe")))
				param = CONDITIONPARAM_SKILL_AXE;
			else if((attr = node.attribute("sword")))
				param = CONDITIONPARAM_SKILL_SWORD;
			else if((attr = node.attribute("distance")) || (attr = node.attribute("dist")))
				param = CONDITIONPARAM_SKILL_DISTANCE;
			else if((attr = node.attribute("shielding")) || (attr = node.attribute("shield")))
				param = CONDITIONPARAM_SKILL_SHIELD;
			else if((attr = node.attribute("fishing")) || (attr = node.attribute("fish")))
				param = CONDITIONPARAM_SKILL_FISHING;
			else if((attr = node.attribute("meleePercent")))
				param = CONDITIONPARAM_SKILL_MELEEPERCENT;
			else if((attr = node.attribute("fistPercent")))
				param = CONDITIONPARAM_SKILL_FISTPERCENT;
			else if((attr = node.attribute("clubPercent")))
				param = CONDITIONPARAM_SKILL_CLUBPERCENT;
			else if((attr = node.attribute("axePercent")))
				param = CONDITIONPARAM_SKILL_AXEPERCENT;
			else if((attr = node.attribute("swordPercent")))
				param = CONDITIONPARAM_SKILL_SWORDPERCENT;
			else if((attr = node.attribute("distancePercent")) || (attr = node.attribute("distPercent")))
				param = CONDITIONPARAM_SKILL_DISTANCEPERCENT;
			else if((attr = node.attribute("shieldingPercent")) || (attr = node.attribute("shieldPercent")))
				param = CONDITIONPARAM_SKILL_SHIELDPERCENT;
			else if((attr = node.attribute("fishingPercent")) || (attr = node.attribute("fishPercent")))
				param = CONDITIONPARAM_SKILL_FISHINGPERCENT;
			else if((attr = node.attribute("maxhealth")))
				param = CONDITIONPARAM_STAT_MAXHEALTHPERCENT;
			else if((attr = node.attribute("maxmana")))
				param = CONDITIONPARAM_STAT_MAXMANAPERCENT;
			else if((attr = node.attribute("soul")))
				param = CONDITIONPARAM_STAT_SOULPERCENT;
			else if((attr = node.attribute("magiclevel")) || (attr = node.attribute("maglevel")))
				param = CONDITIONPARAM_STAT_MAGICLEVELPERCENT;
			else if((attr = node.attribute("maxhealthPercent")))
				param = CONDITIONPARAM_STAT_MAXHEALTHPERCENT;
			else if((attr = node.attribute("maxmanaPercent")))
				param = CONDITIONPARAM_STAT_MAXMANAPERCENT;
			else if((attr = node.attribute("soulPercent")))
				param = CONDITIONPARAM_STAT_SOULPERCENT;
			else if((attr = node.attribute("magiclevelPercent")) || (attr = node.attribute("maglevelPercent")))
				param = CONDITIONPARAM_STAT_MAGICLEVELPERCENT;

			if(param != CONDITIONPARAM_BUFF)
			{
				if(ConditionAttributes* condition = dynamic_cast<ConditionAttributes*>(Condition::createCondition(
					CONDITIONID_COMBAT, CONDITION_ATTRIBUTES, duration, false, subId)))
				{
					condition->setParam(param, intValue);
					combat->setCondition(condition);
				}
			}
		}
		else if(tmpName == "firefield")
			combat->setParam(COMBATPARAM_CREATEITEM, 1492);
		else if(tmpName == "poisonfield")
			combat->setParam(COMBATPARAM_CREATEITEM, 1496);
		else if(tmpName == "energyfield")
			combat->setParam(COMBATPARAM_CREATEITEM, 1495);
		else if(tmpName == "firecondition" || tmpName == "energycondition" || tmpName == "drowncondition" ||
			tmpName == "poisoncondition" || tmpName == "earthcondition" || tmpName == "freezecondition" ||
			tmpName == "cursecondition" || tmpName == "dazzlecondition")
		{
			ConditionType_t conditionType = CONDITION_NONE;
			uint32_t tickInterval = 2000;
			if(tmpName == "physicalcondition")
			{
				conditionType = CONDITION_PHYSICAL;
				tickInterval = 5000;
			}
			else if(tmpName == "firecondition")
			{
				conditionType = CONDITION_FIRE;
				tickInterval = 10000;
			}
			else if(tmpName == "energycondition")
			{
				conditionType = CONDITION_ENERGY;
				tickInterval = 10000;
			}
			else if(tmpName == "earthcondition")
			{
				conditionType = CONDITION_POISON;
				tickInterval = 10000;
			}
			else if(tmpName == "freezecondition")
			{
				conditionType = CONDITION_FREEZING;
				tickInterval = 10000;
			}
			else if(tmpName == "cursecondition")
			{
				conditionType = CONDITION_CURSED;
				tickInterval = 10000;
			}
			else if(tmpName == "dazzlecondition")
			{
				conditionType = CONDITION_DAZZLED;
				tickInterval = 10000;
			}
			else if(tmpName == "drowncondition")
			{
				conditionType = CONDITION_DROWN;
				tickInterval = 5000;
			}
			else if(tmpName == "poisoncondition")
			{
				conditionType = CONDITION_POISON;
				tickInterval = 5000;
			}

			if((attr = node.attribute("tick")))
			{		
				intValue = pugi::cast<int32_t>(attr.value());
				if(intValue > 0)
					tickInterval = intValue;
			}
			int32_t startDamage = 0, minDamage = std::abs(sb.minCombatValue), maxDamage = std::abs(sb.maxCombatValue);
			if((attr = node.attribute("start")))
				startDamage = std::max(std::abs(pugi::cast<int32_t>(attr.value())), minDamage);

			if(Condition* condition = getDamageCondition(conditionType, maxDamage, minDamage, startDamage, tickInterval))
				combat->setCondition(condition);
		}
		else if(tmpName == "strength")
		{
			//TODO: monster extra strength
		}
		else if(tmpName == "effect")
			{/*show some effect and bye bye!*/}
		else
		{
			delete combat;
			std::clog << "[Error - Monsters::deserializeSpell] " << description << " - Unknown spell name: " << name << std::endl;
			return false;
		}

		combat->setPlayerCombatValues(FORMULA_VALUE, sb.minCombatValue, 0, sb.maxCombatValue, 0, 0, 0, 0, 0, 0, 0);
		combatSpell = new CombatSpell(combat, needTarget, needDirection);
		
		for(auto attributeNode : node.children())
		{
			if(strcasecmp(attributeNode.name(),"attribute") != 0)
			{
				if((attr = attributeNode.attribute("key")))
				{
					std::string tmpStrValue = asLowerCaseString(pugi::cast<std::string>(attr.value()));
					if(tmpStrValue == "shooteffect")
					{
						if((attr = attributeNode.attribute("value")))
						{
							ShootEffect_t shoot = getShootType(pugi::cast<std::string>(attr.value()));
							if(shoot != SHOOT_EFFECT_UNKNOWN)
								combat->setParam(COMBATPARAM_DISTANCEEFFECT, shoot);
							else
								std::clog << "[Warning - Monsters::deserializeSpell] " << description << " - Unknown shootEffect: " << pugi::cast<std::string>(attr.value()) << std::endl;
						}
					}
					else if(tmpStrValue == "areaeffect")
					{
						if((attr = attributeNode.attribute("value")))
						{
							MagicEffect_t effect = getMagicEffect(pugi::cast<std::string>(attr.value()));
							if(effect != MAGIC_EFFECT_UNKNOWN)
								combat->setParam(COMBATPARAM_EFFECT, effect);
							else
								std::clog << "[Warning - Monsters::deserializeSpell] " << description << " - Unknown areaEffect: " << pugi::cast<std::string>(attr.value()) << std::endl;
						}
					}
					else
						std::clog << "[Warning - Monsters::deserializeSpells] Effect type \"" << pugi::cast<std::string>(attr.value()) << "\" does not exist." << std::endl;
				}
			}
		}
	}

	sb.spell = combatSpell;
	return true;
}

#define SHOW_XML_WARNING(desc) std::clog << "[Warning - Monsters::loadMonster] " << desc << ". (" << file << ")" << std::endl;
#define SHOW_XML_ERROR(desc) std::clog << "[Error - Monsters::loadMonster] " << desc << ". (" << file << ")" << std::endl;

bool Monsters::loadMonster(const std::string& file, const std::string& monsterName, bool reloading/* = false*/)
{
	if(getIdByName(monsterName) && !reloading)
	{
		std::clog << "[Warning - Monsters::loadMonster] Duplicate registered monster with name: " << monsterName << std::endl;
		return true;
	}

	bool monsterLoad, new_mType = true;
	MonsterType* mType = NULL;
	if(reloading)
	{
		uint32_t id = getIdByName(monsterName);
		if(id != 0)
		{
			mType = getMonsterType(id);
			if(mType != NULL)
			{
				new_mType = false;
				mType->reset();
			}
		}
	}

	if(new_mType)
		mType = new MonsterType();
	
	pugi::xml_attribute attr;
	pugi::xml_document doc;	
	pugi::xml_parse_result result = doc.load_file(file.c_str());
	if(!result)
	{
		std::clog << "[Warning - Monsters::loadMonster] Cannot load monster (" << monsterName << ") file (" << file << ")." << std::endl;		
		return false;
	}

	monsterLoad = true;	
	if(strcasecmp(doc.name(),"monster") == 0)
	{
		std::clog << "[Error - Monsters::loadMonster] Malformed monster (" << monsterName << ") file (" << file << ")." << std::endl;		
		return false;
	}

	int32_t intValue;
	std::string strValue;
	if((attr = doc.attribute("name")))
		mType->name = pugi::cast<std::string>(attr.value());
	else
		monsterLoad = false;

	if((attr = doc.attribute("nameDescription")))
		mType->nameDescription = pugi::cast<std::string>(attr.value());
	else
	{
		mType->nameDescription = "a " + mType->name;
		toLowerCaseString(mType->nameDescription);
	}

	if((attr = doc.attribute("race")))
	{
		std::string tmpStrValue = asLowerCaseString(pugi::cast<std::string>(attr.value()));
		if(tmpStrValue == "venom" || atoi(pugi::cast<std::string>(attr.value()).c_str()) == 1)
			mType->race = RACE_VENOM;
		else if(tmpStrValue == "blood" || atoi(pugi::cast<std::string>(attr.value()).c_str()) == 2)
			mType->race = RACE_BLOOD;
		else if(tmpStrValue == "undead" || atoi(pugi::cast<std::string>(attr.value()).c_str()) == 3)
			mType->race = RACE_UNDEAD;
		else if(tmpStrValue == "fire" || atoi(pugi::cast<std::string>(attr.value()).c_str()) == 4)
			mType->race = RACE_FIRE;
		else if(tmpStrValue == "energy" || atoi(pugi::cast<std::string>(attr.value()).c_str()) == 5)
			mType->race = RACE_ENERGY;
		else
			SHOW_XML_WARNING("Unknown race type " << pugi::cast<std::string>(attr.value()));
	}

	if((attr = doc.attribute("experience")))
		mType->experience = pugi::cast<int32_t>(attr.value());

	if((attr = doc.attribute("speed")))
		mType->baseSpeed = pugi::cast<int32_t>(attr.value());

	if((attr = doc.attribute("manacost")))
		mType->manaCost = pugi::cast<int32_t>(attr.value());

	if((attr = doc.attribute("skull")))
		mType->skull = getSkulls(pugi::cast<std::string>(attr.value()));

	if((attr = doc.attribute("shield")))
		mType->partyShield = getShields(pugi::cast<std::string>(attr.value()));

	if((attr = doc.attribute("emblem")))
		mType->guildEmblem = getEmblems(pugi::cast<std::string>(attr.value()));

	if(monsterLoad){
		for(auto p : doc.children())
		{

			if(strcasecmp(p.name(),"health") != 0)
			{
				if((attr = p.attribute("now")))
					mType->health = pugi::cast<int32_t>(attr.value());
				else
				{
					SHOW_XML_ERROR("Missing health.now");
					monsterLoad = false;
				}

				if((attr = p.attribute("max")))
					mType->healthMax = pugi::cast<int32_t>(attr.value());
				else
				{
					SHOW_XML_ERROR("Missing health.max");
					monsterLoad = false;
				}
			}
			else if(strcasecmp(p.name(),"flags") != 0)
			{
				for(auto tmpNode : p.children())
				{
					if(strcasecmp(tmpNode.name(),"flag") == 0)
					{
						if((attr = tmpNode.attribute("summonable")))
							mType->isSummonable = booleanString(pugi::cast<std::string>(attr.value()));

						if((attr = tmpNode.attribute("attackable")))
							mType->isAttackable = booleanString(pugi::cast<std::string>(attr.value()));

						if((attr = tmpNode.attribute("hostile")))
							mType->isHostile = booleanString(pugi::cast<std::string>(attr.value()));

						if((attr = tmpNode.attribute("illusionable")))
							mType->isIllusionable = booleanString(pugi::cast<std::string>(attr.value()));

						if((attr = tmpNode.attribute("convinceable")))
							mType->isConvinceable = booleanString(pugi::cast<std::string>(attr.value()));

						if((attr = tmpNode.attribute("pushable")))
							mType->pushable = booleanString(pugi::cast<std::string>(attr.value()));

						if((attr = tmpNode.attribute("canpushitems")))
							mType->canPushItems = booleanString(pugi::cast<std::string>(attr.value()));

						if((attr = tmpNode.attribute("canpushcreatures")))
							mType->canPushCreatures = booleanString(pugi::cast<std::string>(attr.value()));

						if((attr = tmpNode.attribute("hidename")))
							mType->hideName = booleanString(pugi::cast<std::string>(attr.value()));

						if((attr = tmpNode.attribute("hidehealth")))
							mType->hideHealth = booleanString(pugi::cast<std::string>(attr.value()));

						if((attr = tmpNode.attribute("lootmessage")))
							mType->lootMessage = (LootMessage_t)pugi::cast<int32_t>(attr.value());

						if((attr = tmpNode.attribute("staticattack")))
						{
							intValue = pugi::cast<int32_t>(attr.value());
							if(intValue < 0 || intValue > 100)
							{
								SHOW_XML_WARNING("staticattack lower than 0 or greater than 100");
								intValue = 0;
							}

							mType->staticAttackChance = pugi::cast<int32_t>(attr.value());
						}

						if((attr = tmpNode.attribute("lightlevel")))
							mType->lightLevel = pugi::cast<int32_t>(attr.value());

						if((attr = tmpNode.attribute("lightcolor")))
							mType->lightColor = pugi::cast<int32_t>(attr.value());

						if((attr = tmpNode.attribute("targetdistance")))
						{
							if(pugi::cast<int32_t>(attr.value()) > Map::maxViewportX)
								SHOW_XML_WARNING("targetdistance greater than maxViewportX");

							mType->targetDistance = std::max(1, pugi::cast<int32_t>(attr.value()));
						}

						if((attr = tmpNode.attribute("runonhealth")))
							mType->runAwayHealth = pugi::cast<int32_t>(attr.value());

						if((attr = tmpNode.attribute("lureable")))
							mType->isLureable = booleanString(pugi::cast<std::string>(attr.value()));

						if((attr = tmpNode.attribute("walkable")))
							mType->isWalkable = booleanString(pugi::cast<std::string>(attr.value()));

						if((attr = tmpNode.attribute("skull")))
							mType->skull = getSkulls(pugi::cast<std::string>(attr.value()));

						if((attr = tmpNode.attribute("shield")))
							mType->partyShield = getShields(pugi::cast<std::string>(attr.value()));

						if((attr = tmpNode.attribute("emblem")))
							mType->guildEmblem = getEmblems(pugi::cast<std::string>(attr.value()));
					}
				}

				//if a monster can push creatures, it should not be pushable
				if(mType->canPushCreatures && mType->pushable)
					mType->pushable = false;
			}
			else if(strcasecmp(p.name(),"targetchange") != 0)
			{
				if((attr = p.attribute("speed")) || (attr = p.attribute("interval")))
					mType->changeTargetSpeed = std::max(1, pugi::cast<int32_t>(attr.value()));
				else
					SHOW_XML_WARNING("Missing targetchange.speed");

				if((attr = p.attribute("chance")))
					mType->changeTargetChance = pugi::cast<int32_t>(attr.value());
				else
					SHOW_XML_WARNING("Missing targetchange.chance");
			}
			else if(strcasecmp(p.name(),"strategy") != 0)
			{
				if((attr = p.attribute("attack"))) {}
					//mType->attackStrength = pugi::cast<int32_t>(attr.value());

				if((attr = p.attribute("defense"))) {}
					//mType->defenseStrength = pugi::cast<int32_t>(attr.value());
			}
			else if(strcasecmp(p.name(),"look") != 0)
			{
				if((attr = p.attribute("type")))
				{
					mType->outfit.lookType = pugi::cast<int32_t>(attr.value());
					if((attr = p.attribute("head")))
						mType->outfit.lookHead = pugi::cast<int32_t>(attr.value());

					if((attr = p.attribute("body")))
						mType->outfit.lookBody = pugi::cast<int32_t>(attr.value());

					if((attr = p.attribute("legs")))
						mType->outfit.lookLegs = pugi::cast<int32_t>(attr.value());

					if((attr = p.attribute("feet")))
						mType->outfit.lookFeet = pugi::cast<int32_t>(attr.value());

					if((attr = p.attribute("addons")))
						mType->outfit.lookAddons = pugi::cast<int32_t>(attr.value());
				}
				else if((attr = p.attribute("typeex")))
					mType->outfit.lookTypeEx = pugi::cast<int32_t>(attr.value());
				else
					SHOW_XML_WARNING("Missing look type/typeex");

				if((attr = p.attribute("corpse")))
					mType->lookCorpse = pugi::cast<int32_t>(attr.value());

				if((attr = p.attribute("corpseUniqueId")) || (attr = p.attribute("corpseUid")))
					mType->corpseUnique = pugi::cast<int32_t>(attr.value());

				if((attr = p.attribute("corpseActionId")) || (attr = p.attribute("corpseAid")))
					mType->corpseAction = pugi::cast<int32_t>(attr.value());
			}
			else if(strcasecmp(p.name(),"attacks") != 0)
			{
				for(auto tmpNode : p.children())
				{
					if(strcasecmp(tmpNode.name(),"attack") != 0)
					{
						spellBlock_t sb;
						if(deserializeSpell(tmpNode, sb, monsterName))
							mType->spellAttackList.push_back(sb);
						else
							SHOW_XML_WARNING("Cant load spell");
					}

				}
			}
			else if(strcasecmp(p.name(),"defenses") != 0)
			{
				if((attr = p.attribute("defense")))
					mType->defense = pugi::cast<int32_t>(attr.value());

				if((attr = p.attribute("armor")))
					mType->armor = pugi::cast<int32_t>(attr.value());

				
				for(auto tmpNode : p.children())
				{
					if(strcasecmp(tmpNode.name(),"defense") != 0)
					{
						spellBlock_t sb;
						if(deserializeSpell(tmpNode, sb, monsterName))
							mType->spellDefenseList.push_back(sb);
						else
							SHOW_XML_WARNING("Cant load spell");
					}
				}
			}
			else if(strcasecmp(p.name(),"immunities") != 0)
			{
				for(auto tmpNode : p.children())
				{
					if(strcasecmp(tmpNode.name(),"immunity") != 0)
					{
						if((attr = tmpNode.attribute("name")))
						{
							std::string tmpStrValue = asLowerCaseString(pugi::cast<std::string>(attr.value()));
							if(tmpStrValue == "physical")
							{
								mType->damageImmunities |= COMBAT_PHYSICALDAMAGE;
								mType->conditionImmunities |= CONDITION_PHYSICAL;
							}
							else if(tmpStrValue == "energy")
							{
								mType->damageImmunities |= COMBAT_ENERGYDAMAGE;
								mType->conditionImmunities |= CONDITION_ENERGY;
							}
							else if(tmpStrValue == "fire")
							{
								mType->damageImmunities |= COMBAT_FIREDAMAGE;
								mType->conditionImmunities |= CONDITION_FIRE;
							}
							else if(tmpStrValue == "poison" || tmpStrValue == "earth")
							{
								mType->damageImmunities |= COMBAT_EARTHDAMAGE;
								mType->conditionImmunities |= CONDITION_POISON;
							}
							else if(tmpStrValue == "ice")
							{
								mType->damageImmunities |= COMBAT_ICEDAMAGE;
								mType->conditionImmunities |= CONDITION_FREEZING;
							}
							else if(tmpStrValue == "holy")
							{
								mType->damageImmunities |= COMBAT_HOLYDAMAGE;
								mType->conditionImmunities |= CONDITION_DAZZLED;
							}
							else if(tmpStrValue == "death")
							{
								mType->damageImmunities |= COMBAT_DEATHDAMAGE;
								mType->conditionImmunities |= CONDITION_CURSED;
							}
							else if(tmpStrValue == "drown")
							{
								mType->damageImmunities |= COMBAT_DROWNDAMAGE;
								mType->conditionImmunities |= CONDITION_DROWN;
							}
							else if(tmpStrValue == "lifedrain")
								mType->damageImmunities |= COMBAT_LIFEDRAIN;
							else if(tmpStrValue == "manadrain")
								mType->damageImmunities |= COMBAT_MANADRAIN;
							else if(tmpStrValue == "paralyze")
								mType->conditionImmunities |= CONDITION_PARALYZE;
							else if(tmpStrValue == "outfit")
								mType->conditionImmunities |= CONDITION_OUTFIT;
							else if(tmpStrValue == "drunk")
								mType->conditionImmunities |= CONDITION_DRUNK;
							else if(tmpStrValue == "invisible")
								mType->conditionImmunities |= CONDITION_INVISIBLE;
							else
								std::cout << "Unknown immunity name " << tmpStrValue.c_str() << std::endl;
						}
						else if((attr = tmpNode.attribute("physical")))
						{
							if(booleanString(pugi::cast<std::string>(attr.value())))
								mType->damageImmunities |= COMBAT_PHYSICALDAMAGE;
								//mType->conditionImmunities |= CONDITION_PHYSICAL;
						}
						else if((attr = tmpNode.attribute("energy")))
						{
							if(booleanString(pugi::cast<std::string>(attr.value()))){
								mType->damageImmunities |= COMBAT_ENERGYDAMAGE;
								mType->conditionImmunities |= CONDITION_ENERGY;
							}
						}
						else if((attr = tmpNode.attribute("fire")))
						{
							if(booleanString(pugi::cast<std::string>(attr.value()))){
								mType->damageImmunities |= COMBAT_FIREDAMAGE;
								mType->conditionImmunities |= CONDITION_FIRE;
							}
						}
						else if((attr = tmpNode.attribute("poison")) || (attr = tmpNode.attribute("earth")))
						{
							if(booleanString(pugi::cast<std::string>(attr.value()))){
								mType->damageImmunities |= COMBAT_EARTHDAMAGE;
								mType->conditionImmunities |= CONDITION_POISON;
							}
						}
						else if((attr = tmpNode.attribute("drown")))
						{
							if(booleanString(pugi::cast<std::string>(attr.value())))
							{
								mType->damageImmunities |= COMBAT_DROWNDAMAGE;
								mType->conditionImmunities |= CONDITION_DROWN;
							}
						}
						else if((attr = tmpNode.attribute("ice")))
						{
							if(booleanString(pugi::cast<std::string>(attr.value())))
							{
								mType->damageImmunities |= COMBAT_ICEDAMAGE;
								mType->conditionImmunities |= CONDITION_FREEZING;
							}	
						}
						else if((attr = tmpNode.attribute("holy")))
						{
							if(booleanString(pugi::cast<std::string>(attr.value())))
							{
								mType->damageImmunities |= COMBAT_HOLYDAMAGE;
								mType->conditionImmunities |= CONDITION_DAZZLED;
							}
						}
						else if((attr = tmpNode.attribute("death")))
						{
							if(booleanString(pugi::cast<std::string>(attr.value())))
							{
								mType->damageImmunities |= COMBAT_DEATHDAMAGE;
								mType->conditionImmunities |= CONDITION_CURSED;
							}
						}
						else if((attr = tmpNode.attribute("lifedrain"))){
							if(booleanString(pugi::cast<std::string>(attr.value())))
								mType->damageImmunities |= COMBAT_LIFEDRAIN;
						} else if((attr = tmpNode.attribute("manadrain"))) {
							if(booleanString(pugi::cast<std::string>(attr.value())))
								mType->damageImmunities |= COMBAT_LIFEDRAIN;
						} else if((attr = tmpNode.attribute("paralyze"))) {
							if(booleanString(pugi::cast<std::string>(attr.value())))
								mType->conditionImmunities |= CONDITION_PARALYZE;
						} else if((attr = tmpNode.attribute("outfit"))) {
							if(booleanString(pugi::cast<std::string>(attr.value())))
								mType->conditionImmunities |= CONDITION_OUTFIT;
						} else if((attr = tmpNode.attribute("drunk"))) {
							if(booleanString(pugi::cast<std::string>(attr.value())))
								mType->conditionImmunities |= CONDITION_DRUNK;
						} else if((attr = tmpNode.attribute("invisible"))) {
							if(booleanString(pugi::cast<std::string>(attr.value())))					
								mType->conditionImmunities |= CONDITION_INVISIBLE;
						}
					}

				}
			}
			else if(strcasecmp(p.name(),"voices") != 0)
			{
				if((attr = p.attribute("speed")) || (attr = p.attribute("interval")))
					mType->yellSpeedTicks = pugi::cast<int32_t>(attr.value());
				else
					SHOW_XML_WARNING("Missing voices.speed");

				if((attr = p.attribute("chance")))
					mType->yellChance = pugi::cast<int32_t>(attr.value());
				else
					SHOW_XML_WARNING("Missing voices.chance");

				for(auto tmpNode : p.children())
				{
					if(strcasecmp(tmpNode.name(),"voice") != 0)
					{
						voiceBlock_t vb;
						vb.text = "";
						vb.yellText = false;

						if((attr = tmpNode.attribute("sentence")))
							vb.text = pugi::cast<std::string>(attr.value());
						else
							SHOW_XML_WARNING("Missing voice.sentence");

						if((attr = tmpNode.attribute("yell")))
							vb.yellText = booleanString(pugi::cast<std::string>(attr.value()));

						mType->voiceVector.push_back(vb);
					}
				}
			}
			else if(strcasecmp(p.name(),"loot") != 0)
			{
				
				for(auto tmpNode : p.children())
				{
					LootBlock rootBlock;
					if(loadLoot(tmpNode, rootBlock))
						mType->lootItems.push_back(rootBlock);
					else
						SHOW_XML_WARNING("Cant load loot");
				}
			}
			else if(strcasecmp(p.name(),"elements") != 0)
			{
				for(auto tmpNode : p.children())
				{
					if(strcasecmp(tmpNode.name(),"element") != 0)
					{
						if((attr = tmpNode.attribute("firePercent")))
							mType->elementMap[COMBAT_FIREDAMAGE] = pugi::cast<int32_t>(attr.value());
						else if((attr = tmpNode.attribute("energyPercent")))
							mType->elementMap[COMBAT_ENERGYDAMAGE] = pugi::cast<int32_t>(attr.value());
						else if((attr = tmpNode.attribute("icePercent")))
							mType->elementMap[COMBAT_ICEDAMAGE] = pugi::cast<int32_t>(attr.value());
						else if((attr = tmpNode.attribute("poisonPercent")) || (attr = tmpNode.attribute("earthPercent")))
							mType->elementMap[COMBAT_EARTHDAMAGE] = pugi::cast<int32_t>(attr.value());
						else if((attr = tmpNode.attribute("holyPercent")))
							mType->elementMap[COMBAT_HOLYDAMAGE] = pugi::cast<int32_t>(attr.value());
						else if((attr = tmpNode.attribute("deathPercent")))
							mType->elementMap[COMBAT_DEATHDAMAGE] = pugi::cast<int32_t>(attr.value());
						else if((attr = tmpNode.attribute("drownPercent")))
							mType->elementMap[COMBAT_DROWNDAMAGE] = pugi::cast<int32_t>(attr.value());
						else if((attr = tmpNode.attribute("physicalPercent")))
							mType->elementMap[COMBAT_PHYSICALDAMAGE] = pugi::cast<int32_t>(attr.value());
						else if((attr = tmpNode.attribute("lifeDrainPercent")))
							mType->elementMap[COMBAT_LIFEDRAIN] = pugi::cast<int32_t>(attr.value());
						else if((attr = tmpNode.attribute("manaDrainPercent")))
							mType->elementMap[COMBAT_MANADRAIN] = pugi::cast<int32_t>(attr.value());
						else if((attr = tmpNode.attribute("healingPercent")))
							mType->elementMap[COMBAT_HEALING] = pugi::cast<int32_t>(attr.value());
						else if((attr = tmpNode.attribute("undefinedPercent")))
							mType->elementMap[COMBAT_UNDEFINEDDAMAGE] = pugi::cast<int32_t>(attr.value());
					}
				}
			}
			else if(strcasecmp(p.name(),"summons") != 0)
			{
				if((attr = p.attribute("maxSummons")) || (attr = p.attribute("max")))
					mType->maxSummons = pugi::cast<int32_t>(attr.value());

				for(auto tmpNode : p.children())
				{
					if(strcasecmp(tmpNode.name(),"summon") != 0)
					{
						uint32_t chance = 100, interval = 1000, amount = 1;
						if((attr = tmpNode.attribute("speed")) || (attr = tmpNode.attribute("interval")))
							interval = pugi::cast<int32_t>(attr.value());

						if((attr = tmpNode.attribute("chance")))
							chance = pugi::cast<int32_t>(attr.value());

						if((attr = tmpNode.attribute("amount")) || (attr = tmpNode.attribute("max")))
							amount = pugi::cast<int32_t>(attr.value());

						if((attr = tmpNode.attribute("name")))
						{
							summonBlock_t sb;
							sb.name = pugi::cast<std::string>(attr.value());
							sb.interval = interval;
							sb.chance = chance;
							sb.amount = amount;

							mType->summonList.push_back(sb);
						}
						else
							SHOW_XML_WARNING("Missing summon.name");
					}

				}
			}
			else if(strcasecmp(p.name(),"script") != 0)
			{
				for(auto tmpNode : p.children())
				{
					if(strcasecmp(tmpNode.name(),"event") != 0)
					{
						if((attr = tmpNode.attribute("name")))
							mType->scriptList.push_back(pugi::cast<std::string>(attr.value()));
						else
							SHOW_XML_WARNING("Missing name for script event");
					}
				}
			}
			else
				SHOW_XML_WARNING("Unknown attribute type - " << p.name());

		}
	}
	if(monsterLoad)
	{
		static uint32_t id = 0;
		if(new_mType)
		{
			id++;
			monsterNames[asLowerCaseString(monsterName)] = id;
			monsters[id] = mType;
		}

		return true;
	}

	if(new_mType)
		delete mType;

	return false;
}

bool Monsters::loadLoot(pugi::xml_node& node, LootBlock& lootBlock)
{
	std::string strValue;
	pugi::xml_attribute attr;
	if((attr = node.attribute("id")) || (attr = node.attribute("ids")))
	{
		IntegerVec idsVec;
		parseIntegerVec(pugi::cast<std::string>(attr.value()), idsVec);
		for(IntegerVec::iterator it = idsVec.begin(); it != idsVec.end(); ++it)
		{
			lootBlock.ids.push_back(*it);
			if(Item::items[(*it)].isContainer())
				loadChildLoot(node, lootBlock);
		}
	}
	else if((attr = node.attribute("name")) || (attr = node.attribute("names")))
	{
		StringVec names = explodeString(pugi::cast<std::string>(attr.value()), ";");
		for(StringVec::iterator it = names.begin(); it != names.end(); ++it)
		{
			uint16_t tmp = Item::items.getItemIdByName(pugi::cast<std::string>(attr.value()));
			if(!tmp)
				continue;

			lootBlock.ids.push_back(tmp);
			if(Item::items[tmp].isContainer())
				loadChildLoot(node, lootBlock);
		}
	}

	if(lootBlock.ids.empty())
		return false;

	if((attr = node.attribute("count")) || (attr = node.attribute("countmax")))
		lootBlock.count = pugi::cast<int32_t>(attr.value());
	else
		lootBlock.count = 1;

	if((attr = node.attribute("chance")) || (attr = node.attribute("chance1")))
		lootBlock.chance = std::min(MAX_LOOTCHANCE, pugi::cast<int32_t>(attr.value()));
	else
		lootBlock.chance = MAX_LOOTCHANCE;

	if((attr = node.attribute("subtype")) || (attr = node.attribute("subType")))
		lootBlock.subType = pugi::cast<int32_t>(attr.value());

	if((attr = node.attribute("actionId")) || (attr = node.attribute("actionid"))
		|| (attr = node.attribute("aid")))
		lootBlock.actionId = pugi::cast<int32_t>(attr.value());

	if((attr = node.attribute("uniqueId")) || (attr = node.attribute("uniqueid"))
		|| (attr = node.attribute("uid")))
		lootBlock.uniqueId = pugi::cast<int32_t>(attr.value());

	if((attr = node.attribute("text")))
		lootBlock.text = pugi::cast<std::string>(attr.value());

	return true;
}

bool Monsters::loadChildLoot(pugi::xml_node& node, LootBlock& parentBlock)
{
	if(!node)
		return false;

	for(auto p : node.children())
	{
		if(strcasecmp(node.name(),"inside") != 0)
		{			
			for(auto insideNode : node.children())
			{
				LootBlock childBlock;
				if(loadLoot(insideNode, childBlock))
					parentBlock.childLoot.push_back(childBlock);
			}

			continue;
		}

		LootBlock childBlock;
		if(loadLoot(p, childBlock))
			parentBlock.childLoot.push_back(childBlock);
	}

	return true;
}

MonsterType* Monsters::getMonsterType(const std::string& name)
{
	uint32_t mId = getIdByName(name);
	if(mId != 0)
		return getMonsterType(mId);

	return NULL;
}

MonsterType* Monsters::getMonsterType(uint32_t mid)
{
	MonsterMap::iterator it = monsters.find(mid);
	if(it != monsters.end())
		return it->second;

	return NULL;
}

uint32_t Monsters::getIdByName(const std::string& name)
{
	std::string tmp = name;
	MonsterNameMap::iterator it = monsterNames.find(asLowerCaseString(tmp));
	if(it != monsterNames.end())
		return it->second;

	return 0;
}

Monsters::~Monsters()
{
	loaded = false;
	for(MonsterMap::iterator it = monsters.begin(); it != monsters.end(); it++)
		delete it->second;
}
