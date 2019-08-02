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

#include "vocation.h"
#include "tools.h"

Vocation Vocations::defVoc = Vocation();

void Vocations::clear()
{
	for(VocationsMap::iterator it = vocationsMap.begin(); it != vocationsMap.end(); ++it)
		delete it->second;

	vocationsMap.clear();
}

bool Vocations::reload()
{
	clear();
	return loadFromXml();
}

bool Vocations::parseVocationNode(pugi::xml_node& p)
{
	int32_t intValue;
	pugi::xml_attribute attr;
	
	if((strcasecmp(p.name(), "vocation") == 0))
		return false;

	if((attr = p.attribute("id")))
	{
		intValue = attr.as_int();
	} else {
		std::clog << "[Error - Vocations::parseVocationNode] Missing vocation id." << std::endl;
		return false;
	}

	Vocation* voc = new Vocation(intValue);
	if((attr = p.attribute("name")))
		voc->setName(attr.as_string());

	if((attr = p.attribute("description")))
		voc->setDescription(attr.as_string());

	if((attr = p.attribute("needpremium")))
		voc->setNeedPremium(booleanString(attr.as_string()));
	
	if((attr = p.attribute("accountmanager")) || (attr = p.attribute("manager")))
	    voc->setAsManagerOption(booleanString(attr.as_string()));

	if((attr = p.attribute("gaincap")) || (attr = p.attribute("gaincapacity")))
		voc->setGainCap(attr.as_int());

	if((attr = p.attribute("gainhp")) || (attr = p.attribute("gainhealth")))
		voc->setGain(GAIN_HEALTH, attr.as_int());

	if((attr = p.attribute("gainmana")))
		voc->setGain(GAIN_MANA, attr.as_int());

	if((attr = p.attribute("gainhpticks")) || (attr = p.attribute("gainhealthticks")))
		voc->setGainTicks(GAIN_HEALTH, attr.as_int());

	if((attr = p.attribute("gainhpamount")) || (attr = p.attribute("gainhealthamount")))
		voc->setGainAmount(GAIN_HEALTH, attr.as_int());

	if((attr = p.attribute("gainmanaticks")))
		voc->setGainTicks(GAIN_MANA, attr.as_int());

	if((attr = p.attribute("gainmanaamount")))
		voc->setGainAmount(GAIN_MANA, attr.as_int());

	if((attr = p.attribute("manamultiplier")))
		voc->setMultiplier(MULTIPLIER_MANA, attr.as_float());

	if((attr = p.attribute("attackspeed")))
		voc->setAttackSpeed(attr.as_int());

	if((attr = p.attribute("basespeed")))
		voc->setBaseSpeed(attr.as_int());

	if((attr = p.attribute("soulmax")))
		voc->setGain(GAIN_SOUL, attr.as_int());

	if((attr = p.attribute("gainsoulamount")))
		voc->setGainAmount(GAIN_SOUL, attr.as_int());

	if((attr = p.attribute("gainsoulticks")))
		voc->setGainTicks(GAIN_SOUL, attr.as_int());

	if((attr = p.attribute("attackable")))
		voc->setAttackable(booleanString(attr.as_string()));

	if((attr = p.attribute("fromvoc")) || (attr = p.attribute("fromvocation")))
		voc->setFromVocation(attr.as_int());

	if((attr = p.attribute("lessloss")))
		voc->setLessLoss(attr.as_int());
	
	for(auto configNode : p.children())
	{
		if(!(strcasecmp(configNode.name(), "skill") == 0))
		{
			if((attr = configNode.attribute("fist")))
				voc->setSkillMultiplier(SKILL_FIST, attr.as_float());

			if((attr = configNode.attribute("fistBase")))
				voc->setSkillBase(SKILL_FIST, attr.as_int());

			if((attr = configNode.attribute("club")))
				voc->setSkillMultiplier(SKILL_CLUB, attr.as_float());

			if((attr = configNode.attribute("clubBase")))
				voc->setSkillBase(SKILL_CLUB, attr.as_int());

			if((attr = configNode.attribute("axe")))
				voc->setSkillMultiplier(SKILL_AXE, attr.as_float());

			if((attr = configNode.attribute("axeBase")))
				voc->setSkillBase(SKILL_AXE, attr.as_int());

			if((attr = configNode.attribute("sword")))
				voc->setSkillMultiplier(SKILL_SWORD, attr.as_float());

			if((attr = configNode.attribute("swordBase")))
				voc->setSkillBase(SKILL_SWORD, attr.as_int());

			if((attr = configNode.attribute("distance")) || (attr = configNode.attribute("dist")))
				voc->setSkillMultiplier(SKILL_DIST, attr.as_float());

			if((attr = configNode.attribute("distanceBase")) || (attr = configNode.attribute("distBase")))
				voc->setSkillBase(SKILL_DIST, attr.as_int());

			if((attr = configNode.attribute("shielding")) || (attr = configNode.attribute("shield")))
				voc->setSkillMultiplier(SKILL_SHIELD, attr.as_float());

			if((attr = configNode.attribute("shieldingBase")) || (attr = configNode.attribute("shieldBase")))
				voc->setSkillBase(SKILL_SHIELD, attr.as_int());

			if((attr = configNode.attribute("fishing")) || (attr = configNode.attribute("fish")))
				voc->setSkillMultiplier(SKILL_FISH, attr.as_float());

			if((attr = configNode.attribute("fishingBase")) || (attr = configNode.attribute("fishBase")))
				voc->setSkillBase(SKILL_FISH, attr.as_int());

			if((attr = configNode.attribute("experience")) || (attr = configNode.attribute("exp")))
				voc->setSkillMultiplier(SKILL__LEVEL, attr.as_float());

			if((attr = configNode.attribute("id")))
			{
				intValue = attr.as_int();
				skills_t skill = (skills_t)intValue;
				if(intValue < SKILL_FIRST || intValue >= SKILL__LAST)
				{
					std::clog << "[Error - Vocations::parseVocationNode] No valid skill id (" << intValue << ")." << std::endl;
					continue;
				}

				if((attr = configNode.attribute("base")))
					voc->setSkillBase(skill, intValue);

				if((attr = configNode.attribute("multiplier")))
					voc->setSkillMultiplier(skill, attr.as_float());
			}
		}
		else if(!(strcasecmp(configNode.name(), "formula") == 0))
		{
			if((attr = configNode.attribute("meleeDamage")))
				voc->setMultiplier(MULTIPLIER_MELEE, attr.as_float());

			if((attr = configNode.attribute("distDamage")) || (attr = configNode.attribute("distanceDamage")))
				voc->setMultiplier(MULTIPLIER_DISTANCE, attr.as_float());

			if((attr = configNode.attribute("wandDamage")) || (attr = configNode.attribute("rodDamage")))
				voc->setMultiplier(MULTIPLIER_WAND, attr.as_float());

			if((attr = configNode.attribute("magDamage")) || (attr = configNode.attribute("magicDamage")))
				voc->setMultiplier(MULTIPLIER_MAGIC, attr.as_float());

			if((attr = configNode.attribute("magHealingDamage")) || (attr = configNode.attribute("magicHealingDamage")))
				voc->setMultiplier(MULTIPLIER_HEALING, attr.as_float());

			if((attr = configNode.attribute("defense")))
				voc->setMultiplier(MULTIPLIER_DEFENSE, attr.as_float());

			if((attr = configNode.attribute("magDefense")) || (attr = configNode.attribute("magicDefense")))
				voc->setMultiplier(MULTIPLIER_MAGICDEFENSE, attr.as_float());

			if((attr = configNode.attribute("armor")))
				voc->setMultiplier(MULTIPLIER_ARMOR, attr.as_float());		
		}
		else if(!(strcasecmp(configNode.name(), "absorb") == 0))
		{
			if((attr = configNode.attribute("percentAll")))
			{
				for(int32_t i = COMBAT_FIRST; i <= COMBAT_LAST; i++)
					voc->increaseAbsorb((CombatType_t)i, attr.as_int());
			}

			if((attr = configNode.attribute("percentElements")))
			{
				voc->increaseAbsorb(COMBAT_ENERGYDAMAGE, attr.as_int());
				voc->increaseAbsorb(COMBAT_FIREDAMAGE, attr.as_int());
				voc->increaseAbsorb(COMBAT_EARTHDAMAGE, attr.as_int());
				voc->increaseAbsorb(COMBAT_ICEDAMAGE, attr.as_int());
			}

			if((attr = configNode.attribute("percentMagic")))
			{
				voc->increaseAbsorb(COMBAT_ENERGYDAMAGE, attr.as_int());
				voc->increaseAbsorb(COMBAT_FIREDAMAGE, attr.as_int());
				voc->increaseAbsorb(COMBAT_EARTHDAMAGE, attr.as_int());
				voc->increaseAbsorb(COMBAT_ICEDAMAGE, attr.as_int());
				voc->increaseAbsorb(COMBAT_HOLYDAMAGE, attr.as_int());
				voc->increaseAbsorb(COMBAT_DEATHDAMAGE, attr.as_int());
			}

			if((attr = configNode.attribute("percentEnergy")))
				voc->increaseAbsorb(COMBAT_ENERGYDAMAGE, attr.as_int());

			if((attr = configNode.attribute("percentFire")))
				voc->increaseAbsorb(COMBAT_FIREDAMAGE, attr.as_int());

			if((attr = configNode.attribute("percentPoison")) || (attr = configNode.attribute("percentEarth")))
				voc->increaseAbsorb(COMBAT_EARTHDAMAGE, attr.as_int());

			if((attr = configNode.attribute("percentIce")))
				voc->increaseAbsorb(COMBAT_ICEDAMAGE, attr.as_int());

			if((attr = configNode.attribute("percentHoly")))
				voc->increaseAbsorb(COMBAT_HOLYDAMAGE, attr.as_int());

			if((attr = configNode.attribute("percentDeath")))
				voc->increaseAbsorb(COMBAT_DEATHDAMAGE, attr.as_int());

			if((attr = configNode.attribute("percentLifeDrain")))
				voc->increaseAbsorb(COMBAT_LIFEDRAIN, attr.as_int());

			if((attr = configNode.attribute("percentManaDrain")))
				voc->increaseAbsorb(COMBAT_MANADRAIN, attr.as_int());

			if((attr = configNode.attribute("percentDrown")))
				voc->increaseAbsorb(COMBAT_DROWNDAMAGE, attr.as_int());

			if((attr = configNode.attribute("percentPhysical")))
				voc->increaseAbsorb(COMBAT_PHYSICALDAMAGE, attr.as_int());

			if((attr = configNode.attribute("percentHealing")))
				voc->increaseAbsorb(COMBAT_HEALING, attr.as_int());

			if((attr = configNode.attribute("percentUndefined")))
				voc->increaseAbsorb(COMBAT_UNDEFINEDDAMAGE, attr.as_int());
		}
		else if(!(strcasecmp(configNode.name(), "reflect") == 0))
		{
			if((attr = configNode.attribute("percentAll")))
			{
				for(int32_t i = COMBAT_FIRST; i <= COMBAT_LAST; i++)
					voc->increaseReflect(REFLECT_PERCENT, (CombatType_t)i, attr.as_int());
			}

			if((attr = configNode.attribute("percentElements")))
			{
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_ENERGYDAMAGE, attr.as_int());
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_FIREDAMAGE, attr.as_int());
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_EARTHDAMAGE, attr.as_int());
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_ICEDAMAGE, attr.as_int());
			}

			if((attr = configNode.attribute("percentMagic")))
			{
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_ENERGYDAMAGE, attr.as_int());
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_FIREDAMAGE, attr.as_int());
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_EARTHDAMAGE, attr.as_int());
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_ICEDAMAGE, attr.as_int());
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_HOLYDAMAGE, attr.as_int());
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_DEATHDAMAGE, attr.as_int());
			}

			if((attr = configNode.attribute("percentEnergy")))
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_ENERGYDAMAGE, attr.as_int());

			if((attr = configNode.attribute("percentFire")))
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_FIREDAMAGE, attr.as_int());

			if((attr = configNode.attribute("percentPoison")) || (attr = configNode.attribute("percentEarth")))
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_EARTHDAMAGE, attr.as_int());

			if((attr = configNode.attribute("percentIce")))
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_ICEDAMAGE, attr.as_int());

			if((attr = configNode.attribute("percentHoly")))
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_HOLYDAMAGE, attr.as_int());

			if((attr = configNode.attribute("percentDeath")))
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_DEATHDAMAGE, attr.as_int());

			if((attr = configNode.attribute("percentLifeDrain")))
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_LIFEDRAIN, attr.as_int());

			if((attr = configNode.attribute("percentManaDrain")))
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_MANADRAIN, attr.as_int());

			if((attr = configNode.attribute("percentDrown")))
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_DROWNDAMAGE, attr.as_int());

			if((attr = configNode.attribute("percentPhysical")))
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_PHYSICALDAMAGE, attr.as_int());

			if((attr = configNode.attribute("percentHealing")))
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_HEALING, attr.as_int());

			if((attr = configNode.attribute("percentUndefined")))
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_UNDEFINEDDAMAGE, attr.as_int());

			if((attr = configNode.attribute("chanceAll")))
			{
				for(int32_t i = COMBAT_FIRST; i <= COMBAT_LAST; i++)
					voc->increaseReflect(REFLECT_CHANCE, (CombatType_t)i, attr.as_int());
			}

			if((attr = configNode.attribute("chanceElements")))
			{
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_ENERGYDAMAGE, attr.as_int());
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_FIREDAMAGE, attr.as_int());
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_EARTHDAMAGE, attr.as_int());
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_ICEDAMAGE, attr.as_int());
			}

			if((attr = configNode.attribute("chanceMagic")))
			{
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_ENERGYDAMAGE, attr.as_int());
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_FIREDAMAGE, attr.as_int());
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_EARTHDAMAGE, attr.as_int());
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_ICEDAMAGE, attr.as_int());
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_HOLYDAMAGE, attr.as_int());
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_DEATHDAMAGE, attr.as_int());
			}

			if((attr = configNode.attribute("chanceEnergy")))
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_ENERGYDAMAGE, attr.as_int());

			if((attr = configNode.attribute("chanceFire")))
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_FIREDAMAGE, attr.as_int());

			if((attr = configNode.attribute("chancePoison")) || (attr = configNode.attribute("chanceEarth")))
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_EARTHDAMAGE, attr.as_int());

			if((attr = configNode.attribute("chanceIce")))
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_ICEDAMAGE, attr.as_int());

			if((attr = configNode.attribute("chanceHoly")))
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_HOLYDAMAGE, attr.as_int());

			if((attr = configNode.attribute("chanceDeath")))
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_DEATHDAMAGE, attr.as_int());

			if((attr = configNode.attribute("chanceLifeDrain")))
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_LIFEDRAIN, attr.as_int());

			if((attr = configNode.attribute("chanceManaDrain")))
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_MANADRAIN, attr.as_int());

			if((attr = configNode.attribute("chanceDrown")))
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_DROWNDAMAGE, attr.as_int());

			if((attr = configNode.attribute("chancePhysical")))
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_PHYSICALDAMAGE, attr.as_int());

			if((attr = configNode.attribute("chanceHealing")))
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_HEALING, attr.as_int());

			if((attr = configNode.attribute("chanceUndefined")))
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_UNDEFINEDDAMAGE, attr.as_int());
		}
	}

	vocationsMap[voc->getId()] = voc;
	return true;
}

bool Vocations::loadFromXml()
{
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(getFilePath(FILE_TYPE_XML,"vocations.xml").c_str());	
	if(!result)
	{
		std::clog << "[Warning - Vocations::loadFromXml] Cannot load vocations file." << std::endl;		
		return false;
	}
	
	if((strcasecmp(doc.name(),"vocations") == 0))
	{
		std::clog << "[Error - Vocations::loadFromXml] Malformed vocations file." << std::endl;		
		return false;
	}
	
	for(auto p : doc.child("vocations").children())
		parseVocationNode(p);
	
	return true;
}

Vocation* Vocations::getVocation(uint32_t vocId)
{
	VocationsMap::iterator it = vocationsMap.find(vocId);
	if(it != vocationsMap.end())
		return it->second;

	std::clog << "[Warning - Vocations::getVocation] Vocation " << vocId << " not found." << std::endl;
	return &Vocations::defVoc;
}

int32_t Vocations::getVocationId(const std::string& name)
{
	for(VocationsMap::iterator it = vocationsMap.begin(); it != vocationsMap.end(); ++it)
	{
		if(!(strcasecmp(it->second->getName().c_str(), name.c_str())))
			return it->first;
	}

	return -1;
}

int32_t Vocations::getPromotedVocation(uint32_t vocationId)
{
	for(VocationsMap::iterator it = vocationsMap.begin(); it != vocationsMap.end(); ++it)
	{
		if(it->second->getFromVocation() == vocationId && it->first != vocationId)
			return it->first;
	}

	return -1;
}

Vocation::~Vocation()
{
	cacheMana.clear();
	for(int32_t i = SKILL_FIRST; i < SKILL_LAST; ++i)
		cacheSkill[i].clear();
}

void Vocation::reset()
{
	memset(absorb, 0, sizeof(absorb));
	memset(reflect[REFLECT_PERCENT], 0, sizeof(reflect[REFLECT_PERCENT]));
	memset(reflect[REFLECT_CHANCE], 0, sizeof(reflect[REFLECT_CHANCE]));

	needPremium = false;
	manager = true;
	attackable = true;
	lessLoss = fromVocation = 0;
	gain[GAIN_SOUL] = 100;
	gainTicks[GAIN_SOUL] = 120;
	baseSpeed = 220;
	attackSpeed = 1500;
	name = description = "";

	gainAmount[GAIN_HEALTH] = gainAmount[GAIN_MANA] = gainAmount[GAIN_SOUL] = 1;
	gain[GAIN_HEALTH] = gain[GAIN_MANA] = capGain = 5;
	gainTicks[GAIN_HEALTH] = gainTicks[GAIN_MANA] = 6;

	skillBase[SKILL_SHIELD] = 100;
	skillBase[SKILL_DIST] = 30;
	skillBase[SKILL_FISH] = 20;
	for(int32_t i = SKILL_FIST; i < SKILL_DIST; i++)
		skillBase[i] = 50;

	skillMultipliers[SKILL_FIST] = 1.5f;
	skillMultipliers[SKILL_FISH] = 1.1f;
	skillMultipliers[SKILL__LEVEL] = 1.0f;
	for(int32_t i = SKILL_CLUB; i < SKILL_FISH; i++)
		skillMultipliers[i] = 2.0f;

	formulaMultipliers[MULTIPLIER_MANA] = 4.0f;
	for(int32_t i = MULTIPLIER_FIRST; i < MULTIPLIER_LAST; i++)
		formulaMultipliers[i] = 1.0f;
}

int16_t Vocation::getReflect(CombatType_t combat) const
{
	if(reflect[REFLECT_CHANCE][combat] < random_range(0, 100))
		return reflect[REFLECT_PERCENT][combat];

	return 0;
}

uint64_t Vocation::getReqSkillTries(int32_t skill, int32_t level)
{
	if(skill < SKILL_FIRST || skill > SKILL_LAST)
		return 0;

	cacheMap& skillMap = cacheSkill[skill];
	cacheMap::iterator it = skillMap.find(level);
	if(it != cacheSkill[skill].end())
		return it->second;

	skillMap[level] = (uint64_t)(skillBase[skill] * std::pow(skillMultipliers[skill], (level - 11)));
	return skillMap[level];
}

uint64_t Vocation::getReqMana(uint32_t magLevel)
{
	cacheMap::iterator it = cacheMana.find(magLevel);
	if(it != cacheMana.end())
		return it->second;

	cacheMana[magLevel] = (uint64_t)(1600 * std::pow(formulaMultipliers[MULTIPLIER_MANA], (float)(magLevel - 1)));
	return cacheMana[magLevel];
}
