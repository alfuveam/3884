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

#include "baseevents.h"


bool BaseEvents::loadFromXml()
{
	std::string scriptsName = getScriptBaseName();
	if(m_loaded)
	{
		std::clog << "[Error - BaseEvents::loadFromXml] " << scriptsName << " interface already loaded!" << std::endl;
		return false;
	}

	if(!getInterface().loadDirectory(getFilePath(FILE_TYPE_OTHER, std::string(scriptsName + "/lib/"))))
		std::clog << "[Warning - BaseEvents::loadFromXml] Cannot load " << scriptsName << "/lib/" << std::endl;

	std::string filename = getFilePath(FILE_TYPE_OTHER, std::string(scriptsName + "/" + scriptsName + ".xml"));
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(filename.c_str());
	
	if(!result)
	{
		printXMLError("[Warning - BaseEvents::loadFromXml]", filename, result);
		return false;
	}

	m_loaded = true;

	std::string scriptsPath = getFilePath(FILE_TYPE_OTHER, std::string(scriptsName + "/scripts/"));	
	for(auto node : doc.child(scriptsName.c_str()).children()){			
		parseEventNode(node, scriptsPath, false);		
	}
	return m_loaded;
}

bool BaseEvents::parseEventNode(pugi::xml_node& node, std::string scriptsPath, bool override)
{
	Event* event = getEvent(node.name());
	if(!event)
		return false;

	if(!event->configureEvent(node))
	{
		std::clog << "[Warning - BaseEvents::loadFromXml] Cannot configure an event" << std::endl;
		delete event;
		event = NULL;
		return false;
	}

	bool success = false;
	std::string strValue;
	pugi::xml_attribute attr;
	if((attr = node.attribute("event")))
	{
		strValue = attr.as_string();
		if(strValue == "script")
		{
			bool file = false;
			pugi::xml_attribute _attr;
			if((_attr = node.attribute("value"))){
				file = true;
				strValue = scriptsPath + _attr.as_string();
			} 

			success = event->loadScript(strValue, file);
		}
		else if(strValue == "buffer")
		{
			if(!(attr = node.attribute("value")))
				strValue = attr.as_string();

			success = event->loadBuffer(strValue);
		}
		else if(strValue == "function")
		{
			if((attr = node.attribute("value")))
				success = event->loadFunction(attr.as_string());
		}
	}
	else if((attr = node.attribute("script")))
	{
		strValue = attr.as_string();
		bool file = asLowerCaseString(strValue) != "cdata";
		if(!file)
			strValue = attr.as_string();
		else
			strValue = scriptsPath + strValue;

		success = event->loadScript(strValue, file);
	}
	else if((attr = node.attribute("buffer")))
	{
		strValue = attr.as_string();
		if(asLowerCaseString(strValue) == "cdata")
			strValue = attr.as_string();

		success = event->loadBuffer(strValue);
	}
	else if((attr = node.attribute("function")))
	{
		strValue = attr.as_string();
		success = event->loadFunction(strValue);
	} else if((attr = node.value())) {
		strValue = attr.as_string();
		success = event->loadBuffer(strValue);
	}

	if(!override && (attr = node.attribute("override"))){
		if(attr.as_bool()){
			override = true;
		}
	}		

	if(success && !registerEvent(event, node, override) && event)
	{
		delete event;
		event = NULL;
	}

	return success;
}

bool BaseEvents::reload()
{
	m_loaded = false;
	clear();
	return loadFromXml();
}

Event::Event(const Event* copy)
{
	m_interface = copy->m_interface;
	m_scripted = copy->m_scripted;
	m_scriptId = copy->m_scriptId;
	m_scriptData = copy->m_scriptData;
}

bool Event::loadBuffer(const std::string& buffer)
{
	if(!m_interface || m_scriptData != "")
	{
		std::clog << "[Error - Event::loadScriptFile] m_interface == NULL, scriptData != \"\"" << std::endl;
		return false;
	}

	m_scripted = EVENT_SCRIPT_BUFFER;
	m_scriptData = buffer;
	return true;
}

bool Event::loadScript(const std::string& script, bool file)
{
	if(!m_interface || m_scriptId != 0)
	{
		std::clog << "[Error - Event::loadScript] m_interface == NULL, scriptId = " << m_scriptId << std::endl;
		return false;
	}

	bool result = false;
	if(!file)
	{
		std::string buffer = script, function = "function " + getScriptEventName();
		trimString(buffer);
		if(buffer.find(function) == std::string::npos)
		{
			std::stringstream scriptstream;
			scriptstream << function << "(" << getScriptEventParams() << ")" << std::endl << buffer << std::endl << "end";
			buffer = scriptstream.str();
		}

		result = m_interface->loadBuffer(buffer);
	}
	else
		result = m_interface->loadFile(script);

	if(!result)
	{
		std::clog << "[Warning - Event::loadScript] Cannot load script (" << script << ")" << std::endl;
		std::clog << m_interface->getLastError() << std::endl;
		return false;
	}

	int32_t id = m_interface->getEvent(getScriptEventName());
	if(id == -1)
	{
		std::clog << "[Warning - Event::loadScript] Event " << getScriptEventName() << " not found (" << script << ")" << std::endl;
		return false;
	}

	m_scripted = EVENT_SCRIPT_TRUE;
	m_scriptId = id;
	return true;
}

// bool Event::checkScript(const std::string&, bool)
// {
// 	return true; //TODO
// }

CallBack::CallBack()
{
	m_scriptId = 0;
	m_interface = NULL;
	m_loaded = false;
}

bool CallBack::loadCallBack(LuaInterface* _interface, std::string name)
{
	if(!_interface)
	{
		std::clog << "Failure: [CallBack::loadCallBack] m_interface == NULL" << std::endl;
		return false;
	}

	m_interface = _interface;
	int32_t id = m_interface->getEvent(name);
	if(id == -1)
	{
		std::clog << "Warning: [CallBack::loadCallBack] Event " << name << " not found." << std::endl;
		return false;
	}

	m_callbackName = name;
	m_scriptId = id;
	m_loaded = true;
	return true;
}
