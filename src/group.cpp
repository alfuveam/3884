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

#include "group.h"

#include "configmanager.h"

extern ConfigManager g_config;

Group Groups::defGroup = Group();

void Groups::clear()
{
	for(GroupsMap::iterator it = groupsMap.begin(); it != groupsMap.end(); ++it)
		delete it->second;

	groupsMap.clear();
}

bool Groups::reload()
{
	clear();
	return loadFromXml();
}

bool Groups::loadFromXml()
{
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(getFilePath(FILE_TYPE_XML, "groups.xml").c_str());
	
	if(!result)
	{
		printXMLError("[Warning - Groups::loadFromXml] Cannot load groups file.", "groups.xml", result);
		return false;
	}

	if(!doc.child("groups"))
	{
		printXMLError("[Error - Groups::loadFromXml] Malformed groups file.", "groups.xml", result);
		return false;
	}

	for(auto group : doc.child("groups").children())
	{
		parseGroupNode(group);
	}

	return true;
}

bool Groups::parseGroupNode(pugi::xml_node& node)
{
	pugi::xml_attribute attr;
	std::string strValue;
	int32_t intValue;
	if((attr = node.attribute("id")))	{
		intValue = pugi::cast<int32_t>(attr.value());
	} else {
		std::clog << "[Warning - Groups::parseGroupNode] Missing group id." << std::endl;
		return false;
	}
	
	Group* group = new Group(intValue);
	if((attr = node.attribute("name")))
	{
		strValue = pugi::cast<std::string>(attr.value());
		group->setFullName(strValue);
		group->setName(asLowerCaseString(strValue));
	}

	if((attr = node.attribute("flags")))
		group->setFlags(pugi::cast<int>(attr.value()));

	if((attr = node.attribute("customFlags")))
		group->setCustomFlags(pugi::cast<int>(attr.value()));

	if((attr = node.attribute("access")))
		group->setAccess(pugi::cast<int>(attr.value()));

	if((attr = node.attribute("ghostAccess")))
		group->setGhostAccess(pugi::cast<int>(attr.value()));
	else
		group->setGhostAccess(group->getAccess());

	if((attr = node.attribute("violationReasons")))
		group->setViolationReasons(pugi::cast<int>(attr.value()));

	if((attr = node.attribute("nameViolationFlags")))
		group->setNameViolationFlags(pugi::cast<int>(attr.value()));

	if((attr = node.attribute("statementViolationFlags")))
		group->setStatementViolationFlags(pugi::cast<int>(attr.value()));

	if((attr = node.attribute("depotLimit")))
		group->setDepotLimit(pugi::cast<int>(attr.value()));

	if((attr = node.attribute("maxVips")))
		group->setMaxVips(pugi::cast<int>(attr.value()));

	if((attr = node.attribute("outfit")))
		group->setOutfit(pugi::cast<int>(attr.value()));

	groupsMap[group->getId()] = group;
	return true;
}

Group* Groups::getGroup(uint32_t groupId)
{
	GroupsMap::iterator it = groupsMap.find(groupId);
	if(it != groupsMap.end())
		return it->second;

	std::clog << "[Warning - Groups::getGroup] Group " << groupId << " not found." << std::endl;
	return &defGroup;
}

int32_t Groups::getGroupId(const std::string& name)
{
	for(GroupsMap::iterator it = groupsMap.begin(); it != groupsMap.end(); ++it)
	{
		if(!strcasecmp(it->second->getName().c_str(), name.c_str()))
			return it->first;
	}

	return -1;
}

uint32_t Group::getDepotLimit(bool premium) const
{
	if(m_depotLimit > 0)
		return m_depotLimit;

	return (premium ? 2000 : 1000);
}

uint32_t Group::getMaxVips(bool premium) const
{
	if(m_maxVips > 0)
		return m_maxVips;

	return (premium ? g_config.getNumber(ConfigManager::VIPLIST_DEFAULT_PREMIUM_LIMIT) : g_config.getNumber(ConfigManager::VIPLIST_DEFAULT_LIMIT));
}
