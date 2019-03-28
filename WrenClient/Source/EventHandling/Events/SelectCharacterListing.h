#pragma once

#include "Event.h"
#include <string>

class SelectCharacterListing : public Event
{
public:
	SelectCharacterListing(const std::string* characterName)
		: Event(EventType::SelectCharacterListing),
		characterName{ characterName }
	{
	}
	const std::string* characterName;
};