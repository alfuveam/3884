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

#include "items.h"
#include "condition.h"
#include "weapons.h"

#include "configmanager.h"
#include "spells.h"

extern Spells* g_spells;
extern ConfigManager g_config;

uint32_t Items::dwMajorVersion = 0;
uint32_t Items::dwMinorVersion = 0;
uint32_t Items::dwBuildNumber = 0;

ItemType::ItemType()
{
	group = ITEM_GROUP_NONE;
	type = ITEM_TYPE_NONE;
	stackable = usable = alwaysOnTop = lookThrough = pickupable = rotable = hasHeight = forceSerialize = false;
	blockSolid = blockProjectile = blockPathFind = allowPickupable = false;
	movable = walkStack = true;
	alwaysOnTopOrder = 0;
	rotateTo = 0;

	wieldInfo = 0;
	minReqLevel = 0;
	minReqMagicLevel = 0;

	runeMagLevel = runeLevel = 0;

	speed = id = 0;
	clientId = 100;
	maxItems = 8; //maximum size if this is a container
	weight = 0; //weight of the item, e.g. throwing distance depends on it
	showCount = true;
	weaponType = WEAPON_NONE;
	slotPosition = SLOTP_HAND | SLOTP_AMMO;
	wieldPosition = SLOT_HAND;
	ammoType = AMMO_NONE;
	ammoAction = AMMOACTION_NONE;
	shootType = (ShootEffect_t)0;
	magicEffect = MAGIC_EFFECT_NONE;
	attack = extraAttack = 0;
	defense = extraDefense = 0;
	attackSpeed = 0;
	armor = 0;
	decayTo = -1;
	decayTime = 0;
	stopTime = false;
	corpseType = RACE_NONE;
	fluidSource = FLUID_NONE;
	allowDistRead = false;

	isVertical = isHorizontal = isHangable = false;
	lightLevel = lightColor = 0;

	maxTextLength = 0;
	canReadText = canWriteText = false;
	date = 0;
	writeOnceItemId = 0;

	transformEquipTo = transformDeEquipTo = 0;
	showDuration = showCharges = showAttributes = dualWield = false;
	charges	= 0;
	hitChance = maxHitChance = breakChance = -1;
	shootRange = 1;

	condition = NULL;
	combatType = COMBAT_NONE;

	replacable = true;
	worth = 0;

	bedPartnerDir = NORTH;
	transformUseTo[PLAYERSEX_FEMALE] = 0;
	transformUseTo[PLAYERSEX_MALE] = 0;
	transformToFree = 0;
	levelDoor = 0;

	memset(floorChange, 0, sizeof(floorChange));
}

ItemType::~ItemType()
{
	delete condition;
}

void Items::clear()
{
	//TODO: clear items?
	moneyMap.clear();
	randomizationMap.clear();
	reverseItemMap.clear();
}

bool Items::reload()
{
	//TODO: reload items?
	/*for(ItemMap::iterator it = items.begin(); it != items.end(); ++it)
		delete it->second->condition;

	clear();
	return loadFromXml();*/
	return false;
}

int32_t Items::loadFromOtb(std::string file)
{
	FileLoader f;
	if(!f.openFile(file.c_str(), false, true))
		return f.getError();

	uint32_t type;
	NODE node = f.getChildNode(NO_NODE, type);

	PropStream props;
	if(f.getProps(node, props))
	{
		//4 byte flags
		//attributes
		//0x01 = version data
		uint32_t flags;
		if(!props.getLong(flags))
			return ERROR_INVALID_FORMAT;

		attribute_t attr;
		if(!props.getType(attr))
			return ERROR_INVALID_FORMAT;

		if(attr == ROOT_ATTR_VERSION)
		{
			datasize_t length = 0;
			if(!props.getType(length))
				return ERROR_INVALID_FORMAT;

			if(length != sizeof(VERSIONINFO))
				return ERROR_INVALID_FORMAT;

			VERSIONINFO *vi;
			if(!props.getStruct(vi))
				return ERROR_INVALID_FORMAT;

			Items::dwMajorVersion = vi->dwMajorVersion; //items otb format file version
			Items::dwMinorVersion = vi->dwMinorVersion; //client version
			Items::dwBuildNumber = vi->dwBuildNumber; //revision
		}
	}

	/*if(Items::dwMajorVersion == 0xFFFFFFFF)
		std::clog << "[Warning - Items::loadFromOtb] items.otb using generic client version." << std::endl;
	else if(Items::dwMajorVersion < 3)
	{
		std::clog << "[Error - Items::loadFromOtb] Old version detected, a newer version of items.otb is required." << std::endl;
		return ERROR_INVALID_FORMAT;
	}
	else if(Items::dwMajorVersion > 3)
	{
		std::clog << "[Error - Items::loadFromOtb] New version detected, an older version of items.otb is required." << std::endl;
		return ERROR_INVALID_FORMAT;
	}
	else if(Items::dwMinorVersion != CLIENT_VERSION_861)
	{
		std::clog << "[Error - Items::loadFromOtb] Another (client) version of items.otb is required." << std::endl;
		return ERROR_INVALID_FORMAT;
	}*/

	uint16_t lastId = 99;
	for(node = f.getChildNode(node, type); node != NO_NODE; node = f.getNextNode(node, type))
	{
		PropStream props;
		if(!f.getProps(node, props))
			return f.getError();

		ItemType* iType = new ItemType();
		iType->group = (itemgroup_t)type;

		flags_t flags;
		switch(type)
		{
			case ITEM_GROUP_CONTAINER:
				iType->type = ITEM_TYPE_CONTAINER;
				break;
			case ITEM_GROUP_DOOR:
				//not used
				iType->type = ITEM_TYPE_DOOR;
				break;
			case ITEM_GROUP_MAGICFIELD:
				//not used
				iType->type = ITEM_TYPE_MAGICFIELD;
				break;
			case ITEM_GROUP_TELEPORT:
				//not used
				iType->type = ITEM_TYPE_TELEPORT;
				break;
			case ITEM_GROUP_NONE:
			case ITEM_GROUP_GROUND:
			case ITEM_GROUP_SPLASH:
			case ITEM_GROUP_FLUID:
			case ITEM_GROUP_CHARGES:
			case ITEM_GROUP_DEPRECATED:
				break;
			default:
				return ERROR_INVALID_FORMAT;
		}

		//read 4 byte flags
		if(!props.getType(flags))
			return ERROR_INVALID_FORMAT;

		iType->blockSolid = hasBitSet(FLAG_BLOCK_SOLID, flags);
		iType->blockProjectile = hasBitSet(FLAG_BLOCK_PROJECTILE, flags);
		iType->blockPathFind = hasBitSet(FLAG_BLOCK_PATHFIND, flags);
		iType->hasHeight = hasBitSet(FLAG_HAS_HEIGHT, flags);
		iType->usable = hasBitSet(FLAG_USABLE, flags);
		iType->pickupable = hasBitSet(FLAG_PICKUPABLE, flags);
		iType->movable = hasBitSet(FLAG_MOVABLE, flags);
		iType->stackable = hasBitSet(FLAG_STACKABLE, flags);

		iType->alwaysOnTop = hasBitSet(FLAG_ALWAYSONTOP, flags);
		iType->isVertical = hasBitSet(FLAG_VERTICAL, flags);
		iType->isHorizontal = hasBitSet(FLAG_HORIZONTAL, flags);
		iType->isHangable = hasBitSet(FLAG_HANGABLE, flags);
		iType->allowDistRead = hasBitSet(FLAG_ALLOWDISTREAD, flags);
		iType->rotable = hasBitSet(FLAG_ROTABLE, flags);
		iType->canReadText = hasBitSet(FLAG_READABLE, flags);
		iType->lookThrough = hasBitSet(FLAG_LOOKTHROUGH, flags);

		attribute_t attr;
		while(props.getType(attr))
		{
			//size of data
			datasize_t length = 0;
			if(!props.getType(length))
			{
				delete iType;
				return ERROR_INVALID_FORMAT;
			}

			switch(attr)
			{
				case ITEM_ATTR_SERVERID:
				{
					if(length != sizeof(uint16_t))
						return ERROR_INVALID_FORMAT;

					uint16_t serverId;
					if(!props.getShort(serverId))
						return ERROR_INVALID_FORMAT;

					if(serverId > 20000 && serverId < 20100)
						serverId = serverId - 20000;
					else if(lastId > 99 && lastId != serverId - 1)
					{
						static ItemType dummyItemType;
						while(lastId != serverId - 1)
						{
							dummyItemType.id = ++lastId;
							items.addElement(&dummyItemType, lastId);
						}
					}

					iType->id = serverId;
					lastId = serverId;
					break;
				}
				case ITEM_ATTR_CLIENTID:
				{
					if(length != sizeof(uint16_t))
						return ERROR_INVALID_FORMAT;

					uint16_t clientId;
					if(!props.getShort(clientId))
						return ERROR_INVALID_FORMAT;

					iType->clientId = clientId;
					break;
				}
				case ITEM_ATTR_SPEED:
				{
					if(length != sizeof(uint16_t))
						return ERROR_INVALID_FORMAT;

					uint16_t speed;
					if(!props.getShort(speed))
						return ERROR_INVALID_FORMAT;

					iType->speed = speed;
					break;
				}
				case ITEM_ATTR_LIGHT2:
				{
					if(length != sizeof(lightBlock2))
						return ERROR_INVALID_FORMAT;

					lightBlock2* block;
					if(!props.getStruct(block))
						return ERROR_INVALID_FORMAT;

					iType->lightLevel = block->lightLevel;
					iType->lightColor = block->lightColor;
					break;
				}
				case ITEM_ATTR_TOPORDER:
				{
					if(length != sizeof(uint8_t))
						return ERROR_INVALID_FORMAT;

					uint8_t topOrder;
					if(!props.getByte(topOrder))
						return ERROR_INVALID_FORMAT;

					iType->alwaysOnTopOrder = topOrder;
					break;
				}
				default:
				{
					//skip unknown attributes
					if(!props.skip(length))
						return ERROR_INVALID_FORMAT;

					break;
				}
			}
		}

		// store the found item
		items.addElement(iType, iType->id);
		if(iType->clientId)
			reverseItemMap[iType->clientId] = iType->id;
	}

	return ERROR_NONE;
}

bool Items::loadFromXml()
{
	pugi::xml_attribute attr;
	pugi::xml_attribute _attr;
	pugi::xml_document itemDoc;
	pugi::xml_document paletteDoc;
	pugi::xml_parse_result resultItem = itemDoc.load_file(getFilePath(FILE_TYPE_OTHER, "items/items.xml").c_str());
	pugi::xml_parse_result resultPalette = paletteDoc.load_file(getFilePath(FILE_TYPE_OTHER, "items/randomization.xml").c_str());

	if(!resultItem)
	{
		std::clog << "[Warning - Items::loadFromXml] Cannot load items file." << std::endl;
		return false;
	}

	if(!resultPalette)
	{
		std::clog << "[Warning - Items::loadFromXml] Cannot load randomization file." << std::endl;
		return false;
	}

	if(strcasecmp(itemDoc.name(),"items") == 0)
	{
		std::clog << "[Warning - Items::loadFromXml] Malformed items file." << std::endl;
		return false;
	}

	if(strcasecmp(paletteDoc.name(),"randomization") == 0)
	{
		std::clog << "[Warning - Items::loadFromXml] Malformed randomization file." << std::endl;
		return false;
	}

	IntegerVec intVector, endVector;
	std::string strValue, endValue, lastId;
	StringVec strVector;	
	int32_t intValue, id = 0, endId = 0, fromId = 0, toId = 0;
	for(auto itemNode : itemDoc.child("items").children())
	{
		if(strcasecmp(itemNode.name(),"item") == 0)
			continue;

		if((attr = itemNode.attribute("id")))
		{
			lastId = attr.as_string();
			parseItemNode(itemNode, pugi::cast<int32_t>(attr.value()));
		}
		else if((attr = itemNode.attribute("fromid")) && (_attr = itemNode.attribute("toid")))
		{
			lastId = pugi::cast<std::string>(attr.value());
			strValue = pugi::cast<std::string>(attr.value());
			endValue = pugi::cast<std::string>(_attr.value());
			intVector = vectorAtoi(explodeString(strValue, ";"));
			endVector = vectorAtoi(explodeString(endValue, ";"));
			if(intVector[0] && endVector[0] && intVector.size() == endVector.size())
			{
				size_t size = intVector.size();
				for(size_t i = 0; i < size; ++i)
				{
					parseItemNode(itemNode, intVector[i]);
					while(intVector[i] < endVector[i])
						parseItemNode(itemNode, ++intVector[i]);
				}
			}
			else
			{
				std::clog << "[Warning - Items::loadFromXml] Malformed entry (from: \"" << strValue << "\", to: \"" << endValue << "\")" << std::endl;
			}
		}
		else
		{
			std::clog << "[Warning - Items::loadFromXml] No itemid found. Last itemid: " << lastId << std::endl;
			
		}
	}

	
	for(uint32_t i = 0; i < Item::items.size(); ++i) //lets do some checks...
	{
		const ItemType* it = Item::items.getElement(i);
		if(!it)
			continue;

		//check bed items
		if((it->transformToFree || it->transformUseTo[PLAYERSEX_FEMALE] || it->transformUseTo[PLAYERSEX_MALE]) && it->type != ITEM_TYPE_BED)
			std::clog << "[Warning - Items::loadFromXml] Item " << it->id << " is not set as a bed-type." << std::endl;
	}

	
	for(auto paletteNode : paletteDoc.children())
	{
		if(strcasecmp(paletteNode.name(), "config") == 0)
		{
			if((attr = paletteNode.attribute("chance")) || (attr = paletteNode.attribute("defaultChance")))
			{
				intValue = pugi::cast<int32_t>(attr.value());
				if(intValue > 100)
				{
					intValue = 100;
					std::clog << "[Warning - Items::loadFromXml] Randomize chance cannot be higher than 100." << std::endl;
				}

				m_randomizationChance = intValue;
			}
		}
		else if(strcasecmp(paletteNode.name(), "palette") == 0)
		{
			if(!(attr = paletteNode.attribute("randomize")))
				continue;

			std::vector<int32_t> itemList = vectorAtoi(explodeString(pugi::cast<std::string>(attr.value()), ";"));
			if(itemList.size() > 1)
			{
				if(itemList[0] < itemList[1])
				{
					fromId = itemList[0];
					toId = itemList[1];
				}
				else
					std::clog << "[Warning - Items::loadFromXml] Randomize min cannot be higher than max." << std::endl;
			}

			int32_t chance = getRandomizationChance();
			if((attr = paletteNode.attribute("chance")))
			{
				intValue = pugi::cast<int32_t>(attr.value());
				if(intValue > 100)
				{
					intValue = 100;
					std::clog << "[Warning: Items::loadRandomization] Randomize chance cannot be higher than 100." << std::endl;
				}

				chance = intValue;
			}

			if((attr = paletteNode.attribute("itemid"))){
				id = pugi::cast<int32_t>(attr.value());
				parseRandomizationBlock(id, fromId, toId, chance);
			}
			else if((attr = paletteNode.attribute("fromid")) && (_attr = paletteNode.attribute("toid")))
			{
				fromId = pugi::cast<int32_t>(attr.value());
				toId = pugi::cast<int32_t>(_attr.value());
				parseRandomizationBlock(id, fromId, toId, chance);
				while(id < endId)
					parseRandomizationBlock(++id, fromId, toId, chance);
			}
		}
	}

	return true;
}

void Items::parseItemNode(pugi::xml_node& itemNode, uint32_t id)
{
	std::string strValue;
	pugi::xml_attribute attr;

	if(id > 20000 && id < 20100)
	{
		id = id - 20000;

		ItemType* iType = new ItemType();
		iType->id = id;
		items.addElement(iType, iType->id);
	}

	ItemType& it = Item::items.getItemType(id);
	if(!it.name.empty() && !(attr = itemNode.attribute("override")))
		std::clog << "[Warning - Items::loadFromXml] Duplicate registered item with id " << id << std::endl;

	if((attr = itemNode.attribute("name")))
		it.name = pugi::cast<std::string>(attr.value());

	if((attr = itemNode.attribute("article")))
		it.article = pugi::cast<std::string>(attr.value());

	if((attr = itemNode.attribute("plural")))
		it.pluralName = pugi::cast<std::string>(attr.value());
	
	for(auto itemAttributesNode : itemNode.children())
	{
		if((attr = itemAttributesNode.attribute("key")))
		{
#ifdef _MSC_VER
			bool notLoaded = false;
#endif			
			std::string tmpStrValue = asLowerCaseString(pugi::cast<std::string>(attr.value()));
			if(tmpStrValue == "type")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					tmpStrValue = asLowerCaseString(pugi::cast<std::string>(attr.value()));
					if(tmpStrValue == "container")
					{
						it.type = ITEM_TYPE_CONTAINER;
						it.group = ITEM_GROUP_CONTAINER;
					}
					else if(tmpStrValue == "key")
						it.type = ITEM_TYPE_KEY;
					else if(tmpStrValue == "magicfield")
						it.type = ITEM_TYPE_MAGICFIELD;
					else if(tmpStrValue == "depot")
						it.type = ITEM_TYPE_DEPOT;
					else if(tmpStrValue == "mailbox")
						it.type = ITEM_TYPE_MAILBOX;
					else if(tmpStrValue == "trashholder")
						it.type = ITEM_TYPE_TRASHHOLDER;
					else if(tmpStrValue == "teleport")
						it.type = ITEM_TYPE_TELEPORT;
					else if(tmpStrValue == "door")
						it.type = ITEM_TYPE_DOOR;
					else if(tmpStrValue == "bed")
						it.type = ITEM_TYPE_BED;
					else if(tmpStrValue == "rune")
						it.type = ITEM_TYPE_RUNE;
					else
						std::clog << "[Warning - Items::loadFromXml] Unknown type " << pugi::cast<std::string>(attr.value()) << std::endl;
				}
			}
			else if(tmpStrValue == "name")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.name = pugi::cast<std::string>(attr.value());
			}
			else if(tmpStrValue == "article")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.article = pugi::cast<std::string>(attr.value());
			}
			else if(tmpStrValue == "plural")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.pluralName = pugi::cast<std::string>(attr.value());
			}
			else if(tmpStrValue == "clientid")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					it.clientId = pugi::cast<int32_t>(itemAttributesNode.value());
					if(it.group == ITEM_GROUP_DEPRECATED)
						it.group = ITEM_GROUP_NONE;
				}
			}
			else if(tmpStrValue == "blocksolid" || tmpStrValue == "blocking")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.blockSolid = (pugi::cast<int32_t>(itemAttributesNode.value()) != 0);
			}
			else if(tmpStrValue == "blockprojectile")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.blockProjectile = (pugi::cast<int32_t>(itemAttributesNode.value()) != 0);
			}
			else if(tmpStrValue == "blockpathfind" || tmpStrValue == "blockpathing" || tmpStrValue == "blockpath")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.blockPathFind = (pugi::cast<int32_t>(itemAttributesNode.value()) != 0);
			}
			else if(tmpStrValue == "lightlevel")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.lightLevel = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "lightcolor")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.lightColor = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "description")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.description = pugi::cast<std::string>(attr.value());
			}
			else if(tmpStrValue == "runespellname")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.runeSpellName = pugi::cast<std::string>(attr.value());
			}
			else if(tmpStrValue == "weight")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.weight = pugi::cast<int32_t>(itemAttributesNode.value()) / 100.f;
			}
			else if(tmpStrValue == "showcount")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.showCount = (pugi::cast<int32_t>(itemAttributesNode.value()) != 0);
			}
			else if(tmpStrValue == "armor")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.armor = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "defense")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.defense = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "extradefense" || tmpStrValue == "extradef")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.extraDefense = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "attack")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.attack = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "extraattack" || tmpStrValue == "extraatk")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.extraAttack = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "attackspeed")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.attackSpeed = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "rotateto")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.rotateTo = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "movable" || tmpStrValue == "moveable")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.movable = (pugi::cast<int32_t>(itemAttributesNode.value()) != 0);
			}
			else if(tmpStrValue == "pickupable")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.pickupable = (pugi::cast<int32_t>(itemAttributesNode.value()) != 0);
			}
			else if(tmpStrValue == "allowpickupable")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.allowPickupable = (pugi::cast<int32_t>(itemAttributesNode.value()) != 0);
			}
			else if(tmpStrValue == "floorchange")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					tmpStrValue = asLowerCaseString(pugi::cast<std::string>(attr.value()));
					if(tmpStrValue == "down")
						it.floorChange[CHANGE_DOWN] = true;
					else if(tmpStrValue == "north")
						it.floorChange[CHANGE_NORTH] = true;
					else if(tmpStrValue == "south")
						it.floorChange[CHANGE_SOUTH] = true;
					else if(tmpStrValue == "west")
						it.floorChange[CHANGE_WEST] = true;
					else if(tmpStrValue == "east")
						it.floorChange[CHANGE_EAST] = true;
					else if(tmpStrValue == "northex")
						it.floorChange[CHANGE_NORTH_EX] = true;
					else if(tmpStrValue == "southex")
						it.floorChange[CHANGE_SOUTH_EX] = true;
					else if(tmpStrValue == "westex")
						it.floorChange[CHANGE_WEST_EX] = true;
					else if(tmpStrValue == "eastex")
						it.floorChange[CHANGE_EAST_EX] = true;
				}
			}
			else if(tmpStrValue == "corpsetype")
			{
				tmpStrValue = asLowerCaseString(pugi::cast<std::string>(attr.value()));
				if((attr = itemAttributesNode.attribute("value")))
				{
					tmpStrValue = asLowerCaseString(pugi::cast<std::string>(attr.value()));
					if(tmpStrValue == "venom")
						it.corpseType = RACE_VENOM;
					else if(tmpStrValue == "blood")
						it.corpseType = RACE_BLOOD;
					else if(tmpStrValue == "undead")
						it.corpseType = RACE_UNDEAD;
					else if(tmpStrValue == "fire")
						it.corpseType = RACE_FIRE;
					else if(tmpStrValue == "energy")
						it.corpseType = RACE_ENERGY;
					else
						std::clog << "[Warning - Items::loadFromXml] Unknown corpseType " << pugi::cast<std::string>(attr.value()) << std::endl;
				}
			}
			else if(tmpStrValue == "containersize")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.maxItems = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "fluidsource")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					tmpStrValue = asLowerCaseString(pugi::cast<std::string>(attr.value()));
					FluidTypes_t fluid = getFluidType(tmpStrValue);
					if(fluid != FLUID_NONE)
						it.fluidSource = fluid;
					else
						std::clog << "[Warning - Items::loadFromXml] Unknown fluidSource " << pugi::cast<std::string>(attr.value()) << std::endl;
				}
			}
			else if(tmpStrValue == "writeable" || tmpStrValue == "writable")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					it.canWriteText = (pugi::cast<int32_t>(itemAttributesNode.value()) != 0);
					it.canReadText = (pugi::cast<int32_t>(itemAttributesNode.value()) != 0);
				}
			}
			else if(tmpStrValue == "readable")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.canReadText = (pugi::cast<int32_t>(itemAttributesNode.value()) != 0);
			}
			else if(tmpStrValue == "maxtextlen" || tmpStrValue == "maxtextlength")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.maxTextLength = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "text")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.text = pugi::cast<std::string>(attr.value());
			}
			else if(tmpStrValue == "author" || tmpStrValue == "writer")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.writer = pugi::cast<std::string>(attr.value());
			}
			else if(tmpStrValue == "date")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.date = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "writeonceitemid")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.writeOnceItemId = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "worth")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					if(moneyMap.find(pugi::cast<int32_t>(itemAttributesNode.value())) != moneyMap.end() && !(attr = itemNode.attribute("override")))
						std::clog << "[Warning - Items::loadFromXml] Duplicated money item " << id << " with worth " << pugi::cast<int32_t>(itemAttributesNode.value()) << "!" << std::endl;
					else
					{
						moneyMap[pugi::cast<int32_t>(itemAttributesNode.value())] = id;
						it.worth = pugi::cast<int32_t>(itemAttributesNode.value());
					}
				}
			}
			else if(tmpStrValue == "forceserialize" || tmpStrValue == "forceserialization" || tmpStrValue == "forcesave")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.forceSerialize = (pugi::cast<int32_t>(itemAttributesNode.value()) != 0);
			}
			else if(tmpStrValue == "leveldoor")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.levelDoor = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "weapontype")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					tmpStrValue = asLowerCaseString(pugi::cast<std::string>(attr.value()));
					if(tmpStrValue == "sword")
						it.weaponType = WEAPON_SWORD;
					else if(tmpStrValue == "club")
						it.weaponType = WEAPON_CLUB;
					else if(tmpStrValue == "axe")
						it.weaponType = WEAPON_AXE;
					else if(tmpStrValue == "shield")
						it.weaponType = WEAPON_SHIELD;
					else if(tmpStrValue == "distance" || tmpStrValue == "dist")
						it.weaponType = WEAPON_DIST;
					else if(tmpStrValue == "wand" || tmpStrValue == "rod")
						it.weaponType = WEAPON_WAND;
					else if(tmpStrValue == "ammunition" || tmpStrValue == "ammo")
						it.weaponType = WEAPON_AMMO;
					else if(tmpStrValue == "fist")
						it.weaponType = WEAPON_FIST;
					else
						std::clog << "[Warning - Items::loadFromXml] Unknown weaponType " << pugi::cast<std::string>(attr.value()) << std::endl;
				}
			}
			else if(tmpStrValue == "slottype")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					tmpStrValue = asLowerCaseString(pugi::cast<std::string>(attr.value()));
					if(tmpStrValue == "head")
					{
						it.slotPosition |= SLOTP_HEAD;
						it.wieldPosition = SLOT_HEAD;
					}
					else if(tmpStrValue == "body")
					{
						it.slotPosition |= SLOTP_ARMOR;
						it.wieldPosition = SLOT_ARMOR;
					}
					else if(tmpStrValue == "legs")
					{
						it.slotPosition |= SLOTP_LEGS;
						it.wieldPosition = SLOT_LEGS;
					}
					else if(tmpStrValue == "feet")
					{
						it.slotPosition |= SLOTP_FEET;
						it.wieldPosition = SLOT_FEET;
					}
					else if(tmpStrValue == "backpack")
					{
						it.slotPosition |= SLOTP_BACKPACK;
						it.wieldPosition = SLOT_BACKPACK;
					}
					else if(tmpStrValue == "two-handed")
					{
						it.slotPosition |= SLOTP_TWO_HAND;
						it.wieldPosition = SLOT_TWO_HAND;
					}
					else if(tmpStrValue == "necklace")
					{
						it.slotPosition |= SLOTP_NECKLACE;
						it.wieldPosition = SLOT_NECKLACE;
					}
					else if(tmpStrValue == "ring")
					{
						it.slotPosition |= SLOTP_RING;
						it.wieldPosition = SLOT_RING;
					}
					else if(tmpStrValue == "ammo")
						it.wieldPosition = SLOT_AMMO;
					else if(tmpStrValue == "hand")
						it.wieldPosition = SLOT_HAND;
					else
						std::clog << "[Warning - Items::loadFromXml] Unknown slotType " << pugi::cast<std::string>(attr.value()) << std::endl;
				}
			}
			else if(tmpStrValue == "ammotype")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					it.ammoType = getAmmoType(pugi::cast<std::string>(attr.value()));
					if(it.ammoType == AMMO_NONE)
						std::clog << "[Warning - Items::loadFromXml] Unknown ammoType " << pugi::cast<std::string>(attr.value()) << std::endl;
				}
			}
			else if(tmpStrValue == "shoottype")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					ShootEffect_t shoot = getShootType(pugi::cast<std::string>(attr.value()));
					if(shoot != SHOOT_EFFECT_UNKNOWN)
						it.shootType = shoot;
					else
						std::clog << "[Warning - Items::loadFromXml] Unknown shootType " << pugi::cast<std::string>(attr.value()) << std::endl;
				}
			}
			else if(tmpStrValue == "effect")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					MagicEffect_t effect = getMagicEffect(pugi::cast<std::string>(attr.value()));
					if(effect != MAGIC_EFFECT_UNKNOWN)
						it.magicEffect = effect;
					else
						std::clog << "[Warning - Items::loadFromXml] Unknown effect " << pugi::cast<std::string>(attr.value()) << std::endl;
				}
			}
			else if(tmpStrValue == "range")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.shootRange = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "stopduration")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.stopTime = (pugi::cast<int32_t>(itemAttributesNode.value()) != 0);
			}
			else if(tmpStrValue == "decayto")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.decayTo = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "transformequipto" || tmpStrValue == "onequipto")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.transformEquipTo = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "transformdeequipto" || tmpStrValue == "ondeequipto")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.transformDeEquipTo = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "duration")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.decayTime = std::max((int32_t)0, pugi::cast<int32_t>(itemAttributesNode.value()));
			}
			else if(tmpStrValue == "showduration")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.showDuration = (pugi::cast<int32_t>(itemAttributesNode.value()) != 0);
			}
			else if(tmpStrValue == "charges")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.charges = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "showcharges")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.showCharges = (pugi::cast<int32_t>(itemAttributesNode.value()) != 0);
			}
			else if(tmpStrValue == "showattributes")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.showAttributes = (pugi::cast<int32_t>(itemAttributesNode.value()) != 0);
			}
			else if(tmpStrValue == "breakchance")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.breakChance = std::max(0, std::min(100, pugi::cast<int32_t>(itemAttributesNode.value())));
			}
			else if(tmpStrValue == "ammoaction")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					AmmoAction_t ammo = getAmmoAction(pugi::cast<std::string>(attr.value()));
					if(ammo != AMMOACTION_NONE)
						it.ammoAction = ammo;
					else
						std::clog << "[Warning - Items::loadFromXml] Unknown ammoAction " << pugi::cast<std::string>(attr.value()) << std::endl;
				}
			}
			else if(tmpStrValue == "hitchance")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.hitChance = std::max(-100, std::min(100, pugi::cast<int32_t>(itemAttributesNode.value())));
			}
			else if(tmpStrValue == "maxhitchance")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.maxHitChance = std::max(0, std::min(100, pugi::cast<int32_t>(itemAttributesNode.value())));
			}
			else if(tmpStrValue == "dualwield")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.dualWield = (pugi::cast<int32_t>(itemAttributesNode.value()) != 0);
			}
			else if(tmpStrValue == "preventloss")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.preventLoss = (pugi::cast<int32_t>(itemAttributesNode.value()) != 0);
			}
			else if(tmpStrValue == "preventdrop")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.preventDrop = (pugi::cast<int32_t>(itemAttributesNode.value()) != 0);
			}
			else if(tmpStrValue == "invisible")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.invisible = (pugi::cast<int32_t>(itemAttributesNode.value()) != 0);
			}
			else if(tmpStrValue == "speed")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.speed = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "healthgain")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					it.abilities.regeneration = true;
					it.abilities.healthGain = pugi::cast<int32_t>(itemAttributesNode.value());
				}
			}
			else if(tmpStrValue == "healthticks")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					it.abilities.regeneration = true;
					it.abilities.healthTicks = pugi::cast<int32_t>(itemAttributesNode.value());
				}
			}
			else if(tmpStrValue == "managain")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					it.abilities.regeneration = true;
					it.abilities.manaGain = pugi::cast<int32_t>(itemAttributesNode.value());
				}
			}
			else if(tmpStrValue == "manaticks")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					it.abilities.regeneration = true;
					it.abilities.manaTicks = pugi::cast<int32_t>(itemAttributesNode.value());
				}
			}
			else if(tmpStrValue == "manashield")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.manaShield = (pugi::cast<int32_t>(itemAttributesNode.value()) != 0);
			}
			else if(tmpStrValue == "skillsword")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.skills[SKILL_SWORD] = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "skillaxe")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.skills[SKILL_AXE] = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "skillclub")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.skills[SKILL_CLUB] = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "skilldist")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.skills[SKILL_DIST] = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "skillfish")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.skills[SKILL_FISH] = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "skillshield")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.skills[SKILL_SHIELD] = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "skillfist")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.skills[SKILL_FIST] = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "maxhealthpoints" || tmpStrValue == "maxhitpoints")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.stats[STAT_MAXHEALTH] = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "maxhealthpercent" || tmpStrValue == "maxhitpointspercent")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.statsPercent[STAT_MAXHEALTH] = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "maxmanapoints")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.stats[STAT_MAXMANA] = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "maxmanapercent" || tmpStrValue == "maxmanapointspercent")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.statsPercent[STAT_MAXMANA] = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "soulpoints")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.stats[STAT_SOUL] = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "soulpercent" || tmpStrValue == "soulpointspercent")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.statsPercent[STAT_SOUL] = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "magiclevelpoints" || tmpStrValue == "magicpoints")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.stats[STAT_MAGICLEVEL] = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "magiclevelpercent" || tmpStrValue == "magicpointspercent")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.statsPercent[STAT_MAGICLEVEL] = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "increasemagicvalue")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.increment[MAGIC_VALUE] = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "increasemagicpercent")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.increment[MAGIC_PERCENT] = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "increasehealingvalue")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.increment[HEALING_VALUE] = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "increasehealingpercent")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.increment[HEALING_PERCENT] = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "absorbpercentall")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					for(int32_t i = COMBAT_FIRST; i <= COMBAT_LAST; i++)
						it.abilities.absorb[i] += pugi::cast<int32_t>(itemAttributesNode.value());
				}
			}
			else if(tmpStrValue == "absorbpercentelements")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					it.abilities.absorb[COMBAT_ENERGYDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.absorb[COMBAT_FIREDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.absorb[COMBAT_EARTHDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.absorb[COMBAT_ICEDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
				}
			}
			else if(tmpStrValue == "absorbpercentmagic")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					it.abilities.absorb[COMBAT_ENERGYDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.absorb[COMBAT_FIREDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.absorb[COMBAT_EARTHDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.absorb[COMBAT_ICEDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.absorb[COMBAT_HOLYDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.absorb[COMBAT_DEATHDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
				}
			}
			else if(tmpStrValue == "absorbpercentenergy")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.absorb[COMBAT_ENERGYDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "absorbpercentfire")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.absorb[COMBAT_FIREDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "absorbpercentpoison" ||	tmpStrValue == "absorbpercentearth")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.absorb[COMBAT_EARTHDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "absorbpercentice")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.absorb[COMBAT_ICEDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "absorbpercentholy")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.absorb[COMBAT_HOLYDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "absorbpercentdeath")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.absorb[COMBAT_DEATHDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "absorbpercentlifedrain")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.absorb[COMBAT_LIFEDRAIN] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "absorbpercentmanadrain")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.absorb[COMBAT_MANADRAIN] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "absorbpercentdrown")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.absorb[COMBAT_DROWNDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "absorbpercentphysical")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.absorb[COMBAT_PHYSICALDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "absorbpercenthealing")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.absorb[COMBAT_HEALING] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "absorbpercentundefined")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.absorb[COMBAT_UNDEFINEDDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "reflectpercentall")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					for(int32_t i = COMBAT_FIRST; i <= COMBAT_LAST; i++)
						it.abilities.reflect[REFLECT_PERCENT][i] += pugi::cast<int32_t>(itemAttributesNode.value());
				}
			}
			else if(tmpStrValue == "reflectpercentelements")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					it.abilities.reflect[REFLECT_PERCENT][COMBAT_ENERGYDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.reflect[REFLECT_PERCENT][COMBAT_FIREDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.reflect[REFLECT_PERCENT][COMBAT_EARTHDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.reflect[REFLECT_PERCENT][COMBAT_ICEDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
				}
			}
			else if(tmpStrValue == "reflectpercentmagic")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					it.abilities.reflect[REFLECT_PERCENT][COMBAT_ENERGYDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.reflect[REFLECT_PERCENT][COMBAT_FIREDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.reflect[REFLECT_PERCENT][COMBAT_EARTHDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.reflect[REFLECT_PERCENT][COMBAT_ICEDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.reflect[REFLECT_PERCENT][COMBAT_HOLYDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.reflect[REFLECT_PERCENT][COMBAT_DEATHDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
				}
			}
			else if(tmpStrValue == "reflectpercentenergy")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.reflect[REFLECT_PERCENT][COMBAT_ENERGYDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "reflectpercentfire")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.reflect[REFLECT_PERCENT][COMBAT_FIREDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "reflectpercentpoison" ||	tmpStrValue == "reflectpercentearth")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.reflect[REFLECT_PERCENT][COMBAT_EARTHDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "reflectpercentice")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.reflect[REFLECT_PERCENT][COMBAT_ICEDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "reflectpercentholy")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.reflect[REFLECT_PERCENT][COMBAT_HOLYDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "reflectpercentdeath")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.reflect[REFLECT_PERCENT][COMBAT_DEATHDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "reflectpercentlifedrain")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.reflect[REFLECT_PERCENT][COMBAT_LIFEDRAIN] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "reflectpercentmanadrain")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.reflect[REFLECT_PERCENT][COMBAT_MANADRAIN] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "reflectpercentdrown")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.reflect[REFLECT_PERCENT][COMBAT_DROWNDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "reflectpercentphysical")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.reflect[REFLECT_PERCENT][COMBAT_PHYSICALDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "reflectpercenthealing")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.reflect[REFLECT_PERCENT][COMBAT_HEALING] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "reflectpercentundefined")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.reflect[REFLECT_PERCENT][COMBAT_UNDEFINEDDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
#ifndef _MSC_VER
			else if(tmpStrValue == "reflectpercentall")
#else
			else
				notLoaded = true;

			if(!notLoaded)
				continue;

			if(tmpStrValue == "reflectpercentall")
#endif
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					for(int32_t i = COMBAT_FIRST; i <= COMBAT_LAST; i++)
						it.abilities.reflect[REFLECT_CHANCE][i] += pugi::cast<int32_t>(itemAttributesNode.value());
				}
			}			
			else if(tmpStrValue == "reflectchanceelements")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					it.abilities.reflect[REFLECT_CHANCE][COMBAT_ENERGYDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.reflect[REFLECT_CHANCE][COMBAT_FIREDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.reflect[REFLECT_CHANCE][COMBAT_EARTHDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.reflect[REFLECT_CHANCE][COMBAT_ICEDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
				}
			}
			else if(tmpStrValue == "reflectchancemagic")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					it.abilities.reflect[REFLECT_CHANCE][COMBAT_ENERGYDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.reflect[REFLECT_CHANCE][COMBAT_FIREDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.reflect[REFLECT_CHANCE][COMBAT_EARTHDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.reflect[REFLECT_CHANCE][COMBAT_ICEDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.reflect[REFLECT_CHANCE][COMBAT_HOLYDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.reflect[REFLECT_CHANCE][COMBAT_DEATHDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
				}
			}
			else if(tmpStrValue == "reflectchanceenergy")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.reflect[REFLECT_CHANCE][COMBAT_ENERGYDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "reflectchancefire")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.reflect[REFLECT_CHANCE][COMBAT_FIREDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "reflectchancepoison" ||	tmpStrValue == "reflectchanceearth")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.reflect[REFLECT_CHANCE][COMBAT_EARTHDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "reflectchanceice")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.reflect[REFLECT_CHANCE][COMBAT_ICEDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "reflectchanceholy")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.reflect[REFLECT_CHANCE][COMBAT_HOLYDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "reflectchancedeath")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.reflect[REFLECT_CHANCE][COMBAT_DEATHDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "reflectchancelifedrain")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.reflect[REFLECT_CHANCE][COMBAT_LIFEDRAIN] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "reflectchancemanadrain")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.reflect[REFLECT_CHANCE][COMBAT_MANADRAIN] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "reflectchancedrown")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.reflect[REFLECT_CHANCE][COMBAT_DROWNDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "reflectchancephysical")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.reflect[REFLECT_CHANCE][COMBAT_PHYSICALDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "reflectchancehealing")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.reflect[REFLECT_CHANCE][COMBAT_HEALING] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "reflectchanceundefined")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.abilities.reflect[REFLECT_CHANCE][COMBAT_UNDEFINEDDAMAGE] += pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "suppressshock" || tmpStrValue == "suppressenergy")
			{
				if((attr = itemAttributesNode.attribute("value")))
					if(pugi::cast<int32_t>(itemAttributesNode.value()) != 0)
						it.abilities.conditionSuppressions |= CONDITION_ENERGY;
			}
			else if(tmpStrValue == "suppressburn" || tmpStrValue == "suppressfire")
			{
				if((attr = itemAttributesNode.attribute("value")))
					if(pugi::cast<int32_t>(itemAttributesNode.value()) != 0)
						it.abilities.conditionSuppressions |= CONDITION_FIRE;
			}
			else if(tmpStrValue == "suppresspoison" || tmpStrValue == "suppressearth")
			{
				if((attr = itemAttributesNode.attribute("value")))
					if(pugi::cast<int32_t>(itemAttributesNode.value()) != 0)
						it.abilities.conditionSuppressions |= CONDITION_POISON;
			}
			else if(tmpStrValue == "suppressfreeze" || tmpStrValue == "suppressice")
			{
				if((attr = itemAttributesNode.attribute("value")))
					if(pugi::cast<int32_t>(itemAttributesNode.value()) != 0)
						it.abilities.conditionSuppressions |= CONDITION_FREEZING;
			}
			else if(tmpStrValue == "suppressdazzle" || tmpStrValue == "suppressholy")
			{
				if((attr = itemAttributesNode.attribute("value")))
					if(pugi::cast<int32_t>(itemAttributesNode.value()) != 0)
						it.abilities.conditionSuppressions |= CONDITION_DAZZLED;
			}
			else if(tmpStrValue == "suppresscurse" || tmpStrValue == "suppressdeath")
			{
				if((attr = itemAttributesNode.attribute("value")))
					if(pugi::cast<int32_t>(itemAttributesNode.value()) != 0)
						it.abilities.conditionSuppressions |= CONDITION_CURSED;
			}
			else if(tmpStrValue == "suppressdrown")
			{
				if((attr = itemAttributesNode.attribute("value")))
					if(pugi::cast<int32_t>(itemAttributesNode.value()) != 0)
						it.abilities.conditionSuppressions |= CONDITION_DROWN;
			}
			else if(tmpStrValue == "suppressphysical")
			{
				if((attr = itemAttributesNode.attribute("value")))
					if(pugi::cast<int32_t>(itemAttributesNode.value()) != 0)
						it.abilities.conditionSuppressions |= CONDITION_PHYSICAL;
			}
			else if(tmpStrValue == "suppresshaste")
			{
				if((attr = itemAttributesNode.attribute("value")))
					if(pugi::cast<int32_t>(itemAttributesNode.value()) != 0)
						it.abilities.conditionSuppressions |= CONDITION_HASTE;
			}
			else if(tmpStrValue == "suppressparalyze")
			{
				if((attr = itemAttributesNode.attribute("value")))
					if(pugi::cast<int32_t>(itemAttributesNode.value()) != 0)
						it.abilities.conditionSuppressions |= CONDITION_PARALYZE;
			}
			else if(tmpStrValue == "suppressdrunk")
			{
				if((attr = itemAttributesNode.attribute("value")))
					if(pugi::cast<int32_t>(itemAttributesNode.value()) != 0)
						it.abilities.conditionSuppressions |= CONDITION_DRUNK;
			}
			else if(tmpStrValue == "suppressregeneration")
			{
				if((attr = itemAttributesNode.attribute("value")))
					if(pugi::cast<int32_t>(itemAttributesNode.value()) != 0)
						it.abilities.conditionSuppressions |= CONDITION_REGENERATION;
			}
			else if(tmpStrValue == "suppresssoul")
			{
				if((attr = itemAttributesNode.attribute("value")))
					if(pugi::cast<int32_t>(itemAttributesNode.value()) != 0)
						it.abilities.conditionSuppressions |= CONDITION_SOUL;
			}
			else if(tmpStrValue == "suppressoutfit")
			{
				if((attr = itemAttributesNode.attribute("value")))
					if(pugi::cast<int32_t>(itemAttributesNode.value()) != 0)
						it.abilities.conditionSuppressions |= CONDITION_OUTFIT;
			}
			else if(tmpStrValue == "suppressinvisible")
			{
				if((attr = itemAttributesNode.attribute("value")))
					if(pugi::cast<int32_t>(itemAttributesNode.value()) != 0)
						it.abilities.conditionSuppressions |= CONDITION_INVISIBLE;
			}
			else if(tmpStrValue == "suppressinfight")
			{
				if((attr = itemAttributesNode.attribute("value")))
					if(pugi::cast<int32_t>(itemAttributesNode.value()) != 0)
						it.abilities.conditionSuppressions |= CONDITION_INFIGHT;
			}
			else if(tmpStrValue == "suppressexhaust")
			{
				if((attr = itemAttributesNode.attribute("value")))
					if(pugi::cast<int32_t>(itemAttributesNode.value()) != 0)
						it.abilities.conditionSuppressions |= CONDITION_EXHAUST;
			}
			else if(tmpStrValue == "suppressmuted")
			{
				if((attr = itemAttributesNode.attribute("value")))
					if(pugi::cast<int32_t>(itemAttributesNode.value()) != 0)
						it.abilities.conditionSuppressions |= CONDITION_MUTED;
			}
			else if(tmpStrValue == "suppresspacified")
			{
				if((attr = itemAttributesNode.attribute("value")))
					if(pugi::cast<int32_t>(itemAttributesNode.value()) != 0)
						it.abilities.conditionSuppressions |= CONDITION_PACIFIED;
			}
			else if(tmpStrValue == "suppresslight")
			{
				if((attr = itemAttributesNode.attribute("value")))
					if(pugi::cast<int32_t>(itemAttributesNode.value()) != 0)
						it.abilities.conditionSuppressions |= CONDITION_LIGHT;
			}
			else if(tmpStrValue == "suppressattributes")
			{
				if((attr = itemAttributesNode.attribute("value")))
					if(pugi::cast<int32_t>(itemAttributesNode.value()) != 0)
						it.abilities.conditionSuppressions |= CONDITION_ATTRIBUTES;
			}
			else if(tmpStrValue == "suppressmanashield")
			{
				if((attr = itemAttributesNode.attribute("value")))
					if(pugi::cast<int32_t>(itemAttributesNode.value()) != 0)
						it.abilities.conditionSuppressions |= CONDITION_MANASHIELD;
			}
			else if(tmpStrValue == "field")
			{
				it.group = ITEM_GROUP_MAGICFIELD;
				it.type = ITEM_TYPE_MAGICFIELD;
				CombatType_t combatType = COMBAT_NONE;

				ConditionDamage* conditionDamage = NULL;
				if((attr = itemAttributesNode.attribute("value")))
				{
					tmpStrValue = asLowerCaseString(pugi::cast<std::string>(attr.value()));
					if(tmpStrValue == "fire")
					{
						conditionDamage = new ConditionDamage(CONDITIONID_COMBAT, CONDITION_FIRE, false, 0);
						combatType = COMBAT_FIREDAMAGE;
					}
					else if(tmpStrValue == "energy")
					{
						conditionDamage = new ConditionDamage(CONDITIONID_COMBAT, CONDITION_ENERGY, false, 0);
						combatType = COMBAT_ENERGYDAMAGE;
					}
					else if(tmpStrValue == "earth" || tmpStrValue == "poison")
					{
						conditionDamage = new ConditionDamage(CONDITIONID_COMBAT, CONDITION_POISON, false, 0);
						combatType = COMBAT_EARTHDAMAGE;
					}
					else if(tmpStrValue == "ice" || tmpStrValue == "freezing")
					{
						conditionDamage = new ConditionDamage(CONDITIONID_COMBAT, CONDITION_FREEZING, false, 0);
						combatType = COMBAT_ICEDAMAGE;
					}
					else if(tmpStrValue == "holy" || tmpStrValue == "dazzled")
					{
						conditionDamage = new ConditionDamage(CONDITIONID_COMBAT, CONDITION_DAZZLED, false, 0);
						combatType = COMBAT_HOLYDAMAGE;
					}
					else if(tmpStrValue == "death" || tmpStrValue == "cursed")
					{
						conditionDamage = new ConditionDamage(CONDITIONID_COMBAT, CONDITION_CURSED, false, 0);
						combatType = COMBAT_DEATHDAMAGE;
					}
					else if(tmpStrValue == "drown")
					{
						conditionDamage = new ConditionDamage(CONDITIONID_COMBAT, CONDITION_DROWN, false, 0);
						combatType = COMBAT_DROWNDAMAGE;
					}
					else if(tmpStrValue == "physical")
					{
						conditionDamage = new ConditionDamage(CONDITIONID_COMBAT, CONDITION_PHYSICAL, false, 0);
						combatType = COMBAT_PHYSICALDAMAGE;
					}
					else
						std::clog << "[Warning - Items::loadFromXml] Unknown field value " << pugi::cast<std::string>(attr.value()) << std::endl;

					if(combatType != COMBAT_NONE)
					{
						it.combatType = combatType;
						it.condition = conditionDamage;
						uint32_t ticks = 0;
						int32_t damage = 0, start = 0, count = 1;

						for(auto fieldAttributesNode : itemAttributesNode.children())
						{
							if((attr = fieldAttributesNode.attribute("key")))
							{
								tmpStrValue = asLowerCaseString(pugi::cast<std::string>(attr.value()));
								if(tmpStrValue == "ticks")
								{
									if((attr = fieldAttributesNode.attribute("value")))
										ticks = std::max(0, pugi::cast<int32_t>(fieldAttributesNode.value()));
								}

								if(tmpStrValue == "count")
								{
									if((attr = fieldAttributesNode.attribute("value")))
										count = std::max(1, pugi::cast<int32_t>(fieldAttributesNode.value()));
								}

								if(tmpStrValue == "start")
								{
									if((attr = fieldAttributesNode.attribute("value")))
										start = std::max(0, pugi::cast<int32_t>(fieldAttributesNode.value()));
								}

								if(tmpStrValue == "damage")
								{
									if((attr = fieldAttributesNode.attribute("value")))
									{
										damage = -pugi::cast<int32_t>(fieldAttributesNode.value());
										if(start > 0)
										{
											std::list<int32_t> damageList;
											ConditionDamage::generateDamageList(damage, start, damageList);

											for(std::list<int32_t>::iterator it = damageList.begin(); it != damageList.end(); ++it)
												conditionDamage->addDamage(1, ticks, -*it);

											start = 0;
										}
										else
											conditionDamage->addDamage(count, ticks, damage);
									}
								}
							}
						}
						if(conditionDamage->getTotalDamage() > 0)
							it.condition->setParam(CONDITIONPARAM_FORCEUPDATE, true);
					}
				}
			}
			else if(tmpStrValue == "elementphysical")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					it.abilities.elementDamage = pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.elementType = COMBAT_PHYSICALDAMAGE;
				}
			}
			else if(tmpStrValue == "elementfire")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					it.abilities.elementDamage = pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.elementType = COMBAT_FIREDAMAGE;
				}
			}
			else if(tmpStrValue == "elementenergy")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					it.abilities.elementDamage = pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.elementType = COMBAT_ENERGYDAMAGE;
				}
			}
			else if(tmpStrValue == "elementearth")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					it.abilities.elementDamage = pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.elementType = COMBAT_EARTHDAMAGE;
				}
			}
			else if(tmpStrValue == "elementice")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					it.abilities.elementDamage = pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.elementType = COMBAT_ICEDAMAGE;
				}
			}
			else if(tmpStrValue == "elementholy")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					it.abilities.elementDamage = pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.elementType = COMBAT_HOLYDAMAGE;
				}
			}
			else if(tmpStrValue == "elementdeath")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					it.abilities.elementDamage = pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.elementType = COMBAT_DEATHDAMAGE;
				}
			}
			else if(tmpStrValue == "elementlifedrain")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					it.abilities.elementDamage = pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.elementType = COMBAT_LIFEDRAIN;
				}
			}
			else if(tmpStrValue == "elementmanadrain")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					it.abilities.elementDamage = pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.elementType = COMBAT_MANADRAIN;
				}
			}
			else if(tmpStrValue == "elementhealing")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					it.abilities.elementDamage = pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.elementType = COMBAT_HEALING;
				}
			}
			else if(tmpStrValue == "elementundefined")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					it.abilities.elementDamage = pugi::cast<int32_t>(itemAttributesNode.value());
					it.abilities.elementType = COMBAT_UNDEFINEDDAMAGE;
				}
			}
			else if(tmpStrValue == "replacable" || tmpStrValue == "replaceable")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.replacable = (pugi::cast<int32_t>(itemAttributesNode.value()) != 0);
			}
			else if(tmpStrValue == "partnerdirection")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.bedPartnerDir = getDirection(pugi::cast<std::string>(attr.value()));
			}
			else if(tmpStrValue == "maletransformto")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					it.transformUseTo[PLAYERSEX_MALE] = pugi::cast<int32_t>(itemAttributesNode.value());
					ItemType& ot = getItemType(pugi::cast<int32_t>(itemAttributesNode.value()));
					if(!ot.transformToFree)
						ot.transformToFree = it.id;

					if(!it.transformUseTo[PLAYERSEX_FEMALE])
						it.transformUseTo[PLAYERSEX_FEMALE] = pugi::cast<int32_t>(itemAttributesNode.value());
				}
			}
			else if(tmpStrValue == "femaletransformto")
			{
				if((attr = itemAttributesNode.attribute("value")))
				{
					it.transformUseTo[PLAYERSEX_FEMALE] = pugi::cast<int32_t>(itemAttributesNode.value());
					ItemType& ot = getItemType(pugi::cast<int32_t>(itemAttributesNode.value()));
					if(!ot.transformToFree)
						ot.transformToFree = it.id;

					if(!it.transformUseTo[PLAYERSEX_MALE])
						it.transformUseTo[PLAYERSEX_MALE] = pugi::cast<int32_t>(itemAttributesNode.value());
				}
			}
			else if(tmpStrValue == "transformto")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.transformToFree = pugi::cast<int32_t>(itemAttributesNode.value());
			}
			else if(tmpStrValue == "walkstack")
			{
				if((attr = itemAttributesNode.attribute("value")))
					it.walkStack = (pugi::cast<int32_t>(itemAttributesNode.value()) != 0);
			}
			else
				std::clog << "[Warning - Items::loadFromXml] Unknown key value " << pugi::cast<std::string>(attr.value()) << std::endl;
		}
	}

	if(it.pluralName.empty() && !it.name.empty())
	{
		it.pluralName = it.name;
		if(it.showCount)
			it.pluralName += "s";
	}
}

void Items::parseRandomizationBlock(int32_t id, int32_t fromId, int32_t toId, int32_t chance)
{
	RandomizationMap::iterator it = randomizationMap.find(id);
	if(it != randomizationMap.end())
	{
		std::clog << "[Warning - Items::parseRandomizationBlock] Duplicated item with id: " << id << std::endl;
		return;
	}

	RandomizationBlock rand;
	rand.fromRange = fromId;
	rand.toRange = toId;

	rand.chance = chance;
	randomizationMap[id] = rand;
}

uint16_t Items::getRandomizedItem(uint16_t id)
{
	if(!g_config.getBool(ConfigManager::RANDOMIZE_TILES))
		return id;

	RandomizationBlock randomize = getRandomization(id);
	if(randomize.chance > 0 && random_range(0, 100) <= randomize.chance)
		id = random_range(randomize.fromRange, randomize.toRange);

	return id;
}

ItemType& Items::getItemType(int32_t id)
{
	ItemType* iType = items.getElement(id);
	if(iType)
		return *iType;

	#ifdef __DEBUG__
	std::clog << "[Warning - Items::getItemType] Unknown itemtype with id " << id << ", using defaults." << std::endl;
	#endif
	static ItemType dummyItemType; // use this for invalid ids
	return dummyItemType;
}

const ItemType& Items::getItemType(int32_t id) const
{
	if(ItemType* iType = items.getElement(id))
		return *iType;

	static ItemType dummyItemType; // use this for invalid ids
	return dummyItemType;
}

const ItemType& Items::getItemIdByClientId(int32_t spriteId) const
{
	uint32_t i = 100;
	ItemType* iType;
	do
	{
		if((iType = items.getElement(i)) && iType->clientId == spriteId)
			return *iType;

		i++;
	}
	while(iType);
	static ItemType dummyItemType; // use this for invalid ids
	return dummyItemType;
}

int32_t Items::getItemIdByName(const std::string& name)
{
	if(!name.empty())
	{
		uint32_t i = 100;
		ItemType* iType = NULL;
		do
		{
			if((iType = items.getElement(i)) && !strcasecmp(name.c_str(), iType->name.c_str()))
				return i;

			i++;
		}
		while(iType);
	}

	return -1;
}
