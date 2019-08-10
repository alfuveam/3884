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
#include "scriptmanager.h"

#include "actions.h"
#include "movement.h"
#include "spells.h"
#include "talkaction.h"
#include "creatureevent.h"
#include "globalevent.h"
#include "weapons.h"

#include "monsters.h"
#include "spawn.h"
#include "raids.h"
#include "group.h"
#include "vocation.h"
#include "outfit.h"
#include "quests.h"
#include "items.h"
#include "chat.h"

#include "configmanager.h"
#include "luascript.h"

Actions* g_actions = NULL;
CreatureEvents* g_creatureEvents = NULL;
Spells* g_spells = NULL;
TalkActions* g_talkActions = NULL;
MoveEvents* g_moveEvents = NULL;
Weapons* g_weapons = NULL;
GlobalEvents* g_globalEvents = NULL;

extern Chat g_chat;
extern ConfigManager g_config;
extern Monsters g_monsters;

ScriptManager::ScriptManager():
modsLoaded(false)
{
	g_weapons = new Weapons();
	g_spells = new Spells();
	g_actions = new Actions();
	g_talkActions = new TalkActions();
	g_moveEvents = new MoveEvents();
	g_creatureEvents = new CreatureEvents();
	g_globalEvents = new GlobalEvents();
}

bool ScriptManager::loadSystem()
{
	if(!g_weapons->loadFromXml())
	{
		std::clog << "> ERROR: Unable to load Weapons!" << std::endl;
		return false;
	}

	g_weapons->loadDefaults();
	if(!g_spells->loadFromXml())
	{
		std::clog << "> ERROR: Unable to load Spells!" << std::endl;
		return false;
	}

	if(!g_actions->loadFromXml())
	{
		std::clog << "> ERROR: Unable to load Actions!" << std::endl;
		return false;
	}

	if(!g_talkActions->loadFromXml())
	{
		std::clog << "> ERROR: Unable to load TalkActions!" << std::endl;
		return false;
	}

	if(!g_moveEvents->loadFromXml())
	{
		std::clog << "> ERROR: Unable to load MoveEvents!" << std::endl;
		return false;
	}

	if(!g_creatureEvents->loadFromXml())
	{
		std::clog << "> ERROR: Unable to load CreatureEvents!" << std::endl;
		return false;
	}

	if(!g_globalEvents->loadFromXml())
	{
		std::clog << "> ERROR: Unable to load GlobalEvents!" << std::endl;
		return false;
	}

	return true;
}

bool ScriptManager::loadMods()
{
	std::filesystem::path modsPath(getFilePath(FILE_TYPE_MOD));
	if(!std::filesystem::exists(modsPath))
	{
		std::clog << "[Error - ScriptManager::loadMods] Couldn't locate main directory" << std::endl;
		return false;
	}

	int32_t i = 0, j = 0;
	bool enabled = false;	
	for(auto& it : std::filesystem::directory_iterator(modsPath))
	{
		if(std::filesystem::is_directory(it.status()) && it.path().extension() != ".xml")
			continue;

		std::clog << "> Loading " << it.path() << "...";
		if(loadFromXml(it.path().u8string(), enabled))
		{
			std::clog << " done";
			if(!enabled)
			{
				++j;
				std::clog << ", but disabled";
			}

			std::clog << ".";
		}
		else
			std::clog << " failed!";

		std::clog << std::endl;
		++i;
	}

	std::clog << "> " << i << " mods were loaded";
	if(j)
		std::clog << " (" << j << " disabled)";

	std::clog << "." << std::endl;
	modsLoaded = true;
	return true;
}

void ScriptManager::clearMods()
{
	modMap.clear();
	libMap.clear();
}

bool ScriptManager::reloadMods()
{
	clearMods();
	return loadMods();
}

bool ScriptManager::loadFromXml(const std::string& file, bool& enabled)
{
	enabled = false;
	pugi::xml_attribute attr;
	pugi::xml_document _doc;
	pugi::xml_parse_result result = _doc.load_file(file.c_str());	
	if(!result)
	{
		std::clog << "[Error - ScriptManager::loadFromXml] Cannot load mod " << file << std::endl;		
		return false;
	}

	std::string strValue;

	if(strcasecmp(_doc.name(),"mod") == 0)
	{
		std::clog << "[Error - ScriptManager::loadFromXml] Malformed mod " << file << std::endl;		
		return false;
	}
	if(strcasecmp(_doc.name(), "name") == 0)	
	{
		std::clog << "[Warning - ScriptManager::loadFromXml] Empty name in mod " << file << std::endl;		
		return false;
	}

	ModBlock mod;
	mod.enabled = false;
	pugi::xml_node doc = _doc.child("mod");
	if((attr = doc.attribute("enabled")))
	{
		if(booleanString(attr.as_string()))
		{
			strValue = attr.as_string();
			mod.enabled = true;
		}
	}
	mod.file = file;
	mod.name = strValue;
	if((attr = doc.attribute("author")))
		mod.author = attr.as_string();

	if((attr = doc.attribute("version")))
		mod.version = attr.as_string();

	if((attr = doc.attribute("contact")))
		mod.contact = attr.as_string();
	
	if(mod.enabled)
	{
		std::string scriptsPath = getFilePath(FILE_TYPE_MOD, "scripts/");		
		for(auto p : doc.children())
		{
			if(strcasecmp(p.name(), "quest") != 0)
				Quests::getInstance()->parseQuestNode(p, modsLoaded);
			else if(strcasecmp(p.name(), "outfit") != 0)
				Outfits::getInstance()->parseOutfitNode(p);
			else if(strcasecmp(p.name(), "vocation") != 0)
				Vocations::getInstance()->parseVocationNode(p); //duplicates checking is dangerous, shouldn't be performed until we find some good solution
			else if(strcasecmp(p.name(), "group") != 0)
				Groups::getInstance()->parseGroupNode(p); //duplicates checking is dangerous, shouldn't be performed until we find some good solution
			else if(strcasecmp(p.name(), "raid") != 0)
				Raids::getInstance()->parseRaidNode(p, modsLoaded, FILE_TYPE_MOD);
			else if(strcasecmp(p.name(), "spawn") != 0)
				Spawns::getInstance()->parseSpawnNode(p, modsLoaded);
			else if(strcasecmp(p.name(), "channel") != 0)
				g_chat.parseChannelNode(p); //TODO: duplicates (channel destructor needs to send closeChannel to users)
			else if(strcasecmp(p.name(), "monster") != 0)
			{
				std::string path, name;
				if(((attr = p.attribute("file")) || (attr = p.attribute("path"))) && (attr = p.attribute("name")))
					g_monsters.loadMonster(getFilePath(FILE_TYPE_MOD, "monster/" + path), name, true);
			}
			else if(strcasecmp(p.name(), "item") != 0)
			{
				if((attr = p.attribute("id")))
					Item::items.parseItemNode(p, attr.as_int()); //duplicates checking isn't necessary here
			}
			if(strcasecmp(p.name(), "description") != 0 || strcasecmp(p.name(), "info") != 0)
			{
				if((attr = p.attribute(p.name())))
				{
					std::string desc = attr.as_string();
					replaceString(desc, "\t", "");
					mod.description = desc;
				}
			}
			else if(strcasecmp(p.name(), "lib") != 0 || strcasecmp(p.name(), "config") != 0)
			{
				if(!(attr = p.attribute("name")))
				{
					std::clog << "[Warning - ScriptManager::loadFromXml] Lib without name in mod " << file << std::endl;
					continue;
				}

				strValue = attr.as_string();
				toLowerCaseString(strValue);
				std::string strLib;
				if((attr = p.attribute(p.name())))
				{
					LibMap::iterator it = libMap.find(strValue);
					if(it == libMap.end())
					{
						LibBlock lb;
						lb.first = file;
						lb.second = strLib;

						libMap[strValue] = lb;
					}
					else
					{
						std::clog << "[Warning - ScriptManager::loadFromXml] Duplicated lib in mod "
							<< strValue << ", previously declared in " << it->second.first << std::endl;
					}
				}
			}
			else if(!g_actions->parseEventNode(p, scriptsPath, modsLoaded))
			{
				if(!g_talkActions->parseEventNode(p, scriptsPath, modsLoaded))
				{
					if(!g_moveEvents->parseEventNode(p, scriptsPath, modsLoaded))
					{
						if(!g_creatureEvents->parseEventNode(p, scriptsPath, modsLoaded))
						{
							if(!g_globalEvents->parseEventNode(p, scriptsPath, modsLoaded))
							{
								if(!g_spells->parseEventNode(p, scriptsPath, modsLoaded))
									g_weapons->parseEventNode(p, scriptsPath, modsLoaded);
							}
						}
					}
				}
			}
		}
	}

	enabled = mod.enabled;
	modMap[mod.name] = mod;

	return true;
}
