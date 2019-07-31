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
		intValue = pugi::cast<int32_t>(attr.value());
	} else {
		std::clog << "[Error - Vocations::parseVocationNode] Missing vocation id." << std::endl;
		return false;
	}

	Vocation* voc = new Vocation(intValue);
	if((attr = p.attribute("name")))
		voc->setName(pugi::cast<std::string>(attr.value()));

	if((attr = p.attribute("description")))
		voc->setDescription(pugi::cast<std::string>(attr.value()));

	if((attr = p.attribute("needpremium")))
		voc->setNeedPremium(booleanString(pugi::cast<std::string>(attr.value())));
	
	if((attr = p.attribute("accountmanager")) || (attr = p.attribute("manager")))
	    voc->setAsManagerOption(booleanString(pugi::cast<std::string>(attr.value())));

	if((attr = p.attribute("gaincap")) || (attr = p.attribute("gaincapacity")))
		voc->setGainCap(pugi::cast<int32_t>(attr.value()));

	if((attr = p.attribute("gainhp")) || (attr = p.attribute("gainhealth")))
		voc->setGain(GAIN_HEALTH, pugi::cast<int32_t>(attr.value()));

	if((attr = p.attribute("gainmana")))
		voc->setGain(GAIN_MANA, pugi::cast<int32_t>(attr.value()));

	if((attr = p.attribute("gainhpticks")) || (attr = p.attribute("gainhealthticks")))
		voc->setGainTicks(GAIN_HEALTH, pugi::cast<int32_t>(attr.value()));

	if((attr = p.attribute("gainhpamount")) || (attr = p.attribute("gainhealthamount")))
		voc->setGainAmount(GAIN_HEALTH, pugi::cast<int32_t>(attr.value()));

	if((attr = p.attribute("gainmanaticks")))
		voc->setGainTicks(GAIN_MANA, pugi::cast<int32_t>(attr.value()));

	if((attr = p.attribute("gainmanaamount")))
		voc->setGainAmount(GAIN_MANA, pugi::cast<int32_t>(attr.value()));

	if((attr = p.attribute("manamultiplier")))
		voc->setMultiplier(MULTIPLIER_MANA, pugi::cast<float>(attr.value()));

	if((attr = p.attribute("attackspeed")))
		voc->setAttackSpeed(pugi::cast<int32_t>(attr.value()));

	if((attr = p.attribute("basespeed")))
		voc->setBaseSpeed(pugi::cast<int32_t>(attr.value()));

	if((attr = p.attribute("soulmax")))
		voc->setGain(GAIN_SOUL, pugi::cast<int32_t>(attr.value()));

	if((attr = p.attribute("gainsoulamount")))
		voc->setGainAmount(GAIN_SOUL, pugi::cast<int32_t>(attr.value()));

	if((attr = p.attribute("gainsoulticks")))
		voc->setGainTicks(GAIN_SOUL, pugi::cast<int32_t>(attr.value()));

	if((attr = p.attribute("attackable")))
		voc->setAttackable(booleanString(pugi::cast<std::string>(attr.value())));

	if((attr = p.attribute("fromvoc")) || (attr = p.attribute("fromvocation")))
		voc->setFromVocation(pugi::cast<int32_t>(attr.value()));

	if((attr = p.attribute("lessloss")))
		voc->setLessLoss(pugi::cast<int32_t>(attr.value()));

	for(xmlNodePtr configNode = p->children; configNode; configNode = configNode->next)
	
		if(!(strcasecmp(configNode.name(), "skill") == 0))
		{
			if((attr = configNode.attribute("fist")))
				voc->setSkillMultiplier(SKILL_FIST, pugi::cast<float>(attr.value()));

			if((attr = configNode.attribute("fistBase")))
				voc->setSkillBase(SKILL_FIST, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("club")))
				voc->setSkillMultiplier(SKILL_CLUB, pugi::cast<float>(attr.value()));

			if((attr = configNode.attribute("clubBase")))
				voc->setSkillBase(SKILL_CLUB, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("axe")))
				voc->setSkillMultiplier(SKILL_AXE, pugi::cast<float>(attr.value()));

			if((attr = configNode.attribute("axeBase")))
				voc->setSkillBase(SKILL_AXE, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("sword")))
				voc->setSkillMultiplier(SKILL_SWORD, pugi::cast<float>(attr.value()));

			if((attr = configNode.attribute("swordBase")))
				voc->setSkillBase(SKILL_SWORD, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("distance")) || (attr = configNode.attribute("dist")))
				voc->setSkillMultiplier(SKILL_DIST, pugi::cast<float>(attr.value()));

			if((attr = configNode.attribute("distanceBase")) || (attr = configNode.attribute("distBase")))
				voc->setSkillBase(SKILL_DIST, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("shielding")) || (attr = configNode.attribute("shield")))
				voc->setSkillMultiplier(SKILL_SHIELD, pugi::cast<float>(attr.value()));

			if((attr = configNode.attribute("shieldingBase")) || (attr = configNode.attribute("shieldBase")))
				voc->setSkillBase(SKILL_SHIELD, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("fishing")) || (attr = configNode.attribute("fish")))
				voc->setSkillMultiplier(SKILL_FISH, pugi::cast<float>(attr.value()));

			if((attr = configNode.attribute("fishingBase")) || (attr = configNode.attribute("fishBase")))
				voc->setSkillBase(SKILL_FISH, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("experience")) || (attr = configNode.attribute("exp")))
				voc->setSkillMultiplier(SKILL__LEVEL, pugi::cast<float>(attr.value()));

			if((attr = configNode.attribute("id")))
			{
				intValue = pugi::cast<int32_t>(attr.value());
				skills_t skill = (skills_t)intValue;
				if(intValue < SKILL_FIRST || intValue >= SKILL__LAST)
				{
					std::clog << "[Error - Vocations::parseVocationNode] No valid skill id (" << intValue << ")." << std::endl;
					continue;
				}

				if((attr = configNode.attribute("base")))
					voc->setSkillBase(skill, intValue);

				if((attr = configNode.attribute("multiplier")))
					voc->setSkillMultiplier(skill, pugi::cast<float>(attr.value()));
			}
		
		else if(!(strcasecmp(configNode.name(), "formula") == 0))
		{
			if((attr = configNode.attribute("meleeDamage")))
				voc->setMultiplier(MULTIPLIER_MELEE, pugi::cast<float>(attr.value()));

			if((attr = configNode.attribute("distDamage")) || (attr = configNode.attribute("distanceDamage")))
				voc->setMultiplier(MULTIPLIER_DISTANCE, pugi::cast<float>(attr.value()));

			if((attr = configNode.attribute("wandDamage")) || (attr = configNode.attribute("rodDamage")))
				voc->setMultiplier(MULTIPLIER_WAND, pugi::cast<float>(attr.value()));

			if((attr = configNode.attribute("magDamage")) || (attr = configNode.attribute("magicDamage")))
				voc->setMultiplier(MULTIPLIER_MAGIC, pugi::cast<float>(attr.value()));

			if((attr = configNode.attribute("magHealingDamage")) || (attr = configNode.attribute("magicHealingDamage")))
				voc->setMultiplier(MULTIPLIER_HEALING, pugi::cast<float>(attr.value()));

			if((attr = configNode.attribute("defense")))
				voc->setMultiplier(MULTIPLIER_DEFENSE, pugi::cast<float>(attr.value()));

			if((attr = configNode.attribute("magDefense")) || (attr = configNode.attribute("magicDefense")))
				voc->setMultiplier(MULTIPLIER_MAGICDEFENSE, pugi::cast<float>(attr.value()));

			if((attr = configNode.attribute("armor")))
				voc->setMultiplier(MULTIPLIER_ARMOR, pugi::cast<float>(attr.value()));
		
		else if(!(strcasecmp(configNode.name(), "absorb") == 0))
		{
			if((attr = configNode.attribute("percentAll")))
			{
				for(int32_t i = COMBAT_FIRST; i <= COMBAT_LAST; i++)
					voc->increaseAbsorb((CombatType_t)i, pugi::cast<int32_t>(attr.value()));
			}

			if((attr = configNode.attribute("percentElements")))
			{
				voc->increaseAbsorb(COMBAT_ENERGYDAMAGE, pugi::cast<int32_t>(attr.value()));
				voc->increaseAbsorb(COMBAT_FIREDAMAGE, pugi::cast<int32_t>(attr.value()));
				voc->increaseAbsorb(COMBAT_EARTHDAMAGE, pugi::cast<int32_t>(attr.value()));
				voc->increaseAbsorb(COMBAT_ICEDAMAGE, pugi::cast<int32_t>(attr.value()));
			}

			if((attr = configNode.attribute("percentMagic")))
			{
				voc->increaseAbsorb(COMBAT_ENERGYDAMAGE, pugi::cast<int32_t>(attr.value()));
				voc->increaseAbsorb(COMBAT_FIREDAMAGE, pugi::cast<int32_t>(attr.value()));
				voc->increaseAbsorb(COMBAT_EARTHDAMAGE, pugi::cast<int32_t>(attr.value()));
				voc->increaseAbsorb(COMBAT_ICEDAMAGE, pugi::cast<int32_t>(attr.value()));
				voc->increaseAbsorb(COMBAT_HOLYDAMAGE, pugi::cast<int32_t>(attr.value()));
				voc->increaseAbsorb(COMBAT_DEATHDAMAGE, pugi::cast<int32_t>(attr.value()));
			}

			if((attr = configNode.attribute("percentEnergy")))
				voc->increaseAbsorb(COMBAT_ENERGYDAMAGE, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("percentFire")))
				voc->increaseAbsorb(COMBAT_FIREDAMAGE, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("percentPoison")) || (attr = configNode.attribute("percentEarth")))
				voc->increaseAbsorb(COMBAT_EARTHDAMAGE, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("percentIce")))
				voc->increaseAbsorb(COMBAT_ICEDAMAGE, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("percentHoly")))
				voc->increaseAbsorb(COMBAT_HOLYDAMAGE, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("percentDeath")))
				voc->increaseAbsorb(COMBAT_DEATHDAMAGE, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("percentLifeDrain")))
				voc->increaseAbsorb(COMBAT_LIFEDRAIN, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("percentManaDrain")))
				voc->increaseAbsorb(COMBAT_MANADRAIN, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("percentDrown")))
				voc->increaseAbsorb(COMBAT_DROWNDAMAGE, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("percentPhysical")))
				voc->increaseAbsorb(COMBAT_PHYSICALDAMAGE, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("percentHealing")))
				voc->increaseAbsorb(COMBAT_HEALING, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("percentUndefined")))
				voc->increaseAbsorb(COMBAT_UNDEFINEDDAMAGE, pugi::cast<int32_t>(attr.value()));
		
		else if(!(strcasecmp(configNode.name(), "reflect") == 0))
		{
			if((attr = configNode.attribute("percentAll")))
			{
				for(int32_t i = COMBAT_FIRST; i <= COMBAT_LAST; i++)
					voc->increaseReflect(REFLECT_PERCENT, (CombatType_t)i, pugi::cast<int32_t>(attr.value()));
			}

			if((attr = configNode.attribute("percentElements")))
			{
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_ENERGYDAMAGE, pugi::cast<int32_t>(attr.value()));
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_FIREDAMAGE, pugi::cast<int32_t>(attr.value()));
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_EARTHDAMAGE, pugi::cast<int32_t>(attr.value()));
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_ICEDAMAGE, pugi::cast<int32_t>(attr.value()));
			}

			if((attr = configNode.attribute("percentMagic")))
			{
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_ENERGYDAMAGE, pugi::cast<int32_t>(attr.value()));
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_FIREDAMAGE, pugi::cast<int32_t>(attr.value()));
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_EARTHDAMAGE, pugi::cast<int32_t>(attr.value()));
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_ICEDAMAGE, pugi::cast<int32_t>(attr.value()));
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_HOLYDAMAGE, pugi::cast<int32_t>(attr.value()));
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_DEATHDAMAGE, pugi::cast<int32_t>(attr.value()));
			}

			if((attr = configNode.attribute("percentEnergy")))
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_ENERGYDAMAGE, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("percentFire")))
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_FIREDAMAGE, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("percentPoison")) || (attr = configNode.attribute("percentEarth")))
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_EARTHDAMAGE, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("percentIce")))
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_ICEDAMAGE, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("percentHoly")))
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_HOLYDAMAGE, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("percentDeath")))
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_DEATHDAMAGE, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("percentLifeDrain")))
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_LIFEDRAIN, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("percentManaDrain")))
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_MANADRAIN, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("percentDrown")))
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_DROWNDAMAGE, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("percentPhysical")))
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_PHYSICALDAMAGE, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("percentHealing")))
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_HEALING, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("percentUndefined")))
				voc->increaseReflect(REFLECT_PERCENT, COMBAT_UNDEFINEDDAMAGE, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("chanceAll")))
			{
				for(int32_t i = COMBAT_FIRST; i <= COMBAT_LAST; i++)
					voc->increaseReflect(REFLECT_CHANCE, (CombatType_t)i, pugi::cast<int32_t>(attr.value()));
			}

			if((attr = configNode.attribute("chanceElements")))
			{
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_ENERGYDAMAGE, pugi::cast<int32_t>(attr.value()));
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_FIREDAMAGE, pugi::cast<int32_t>(attr.value()));
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_EARTHDAMAGE, pugi::cast<int32_t>(attr.value()));
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_ICEDAMAGE, pugi::cast<int32_t>(attr.value()));
			}

			if((attr = configNode.attribute("chanceMagic")))
			{
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_ENERGYDAMAGE, pugi::cast<int32_t>(attr.value()));
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_FIREDAMAGE, pugi::cast<int32_t>(attr.value()));
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_EARTHDAMAGE, pugi::cast<int32_t>(attr.value()));
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_ICEDAMAGE, pugi::cast<int32_t>(attr.value()));
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_HOLYDAMAGE, pugi::cast<int32_t>(attr.value()));
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_DEATHDAMAGE, pugi::cast<int32_t>(attr.value()));
			}

			if((attr = configNode.attribute("chanceEnergy")))
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_ENERGYDAMAGE, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("chanceFire")))
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_FIREDAMAGE, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("chancePoison")) || (attr = configNode.attribute("chanceEarth")))
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_EARTHDAMAGE, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("chanceIce")))
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_ICEDAMAGE, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("chanceHoly")))
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_HOLYDAMAGE, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("chanceDeath")))
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_DEATHDAMAGE, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("chanceLifeDrain")))
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_LIFEDRAIN, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("chanceManaDrain")))
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_MANADRAIN, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("chanceDrown")))
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_DROWNDAMAGE, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("chancePhysical")))
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_PHYSICALDAMAGE, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("chanceHealing")))
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_HEALING, pugi::cast<int32_t>(attr.value()));

			if((attr = configNode.attribute("chanceUndefined")))
				voc->increaseReflect(REFLECT_CHANCE, COMBAT_UNDEFINEDDAMAGE, pugi::cast<int32_t>(attr.value()));
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
	
	for(auto p : doc.children())
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
		if(!(strcasecmp(it->second->getName().c_str)(), name.c_str()))
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
