#pragma once

#include <SocketManager.h>
#include <OpCodes.h>
#include <Models/Skill.h>
#include <Models/Ability.h>

class ClientSocketManager : public SocketManager
{
private:
	int accountId{ -1 };
	std::string token{ "" };

	std::vector<std::unique_ptr<std::string>> BuildCharacterVector(const std::string& characterString);
	std::vector<std::unique_ptr<WrenCommon::Skill>> BuildSkillVector(const std::string& skillString);
	std::vector<std::unique_ptr<Ability>> BuildAbilityVector(const std::string& abilityString);
	void InitializeMessageHandlers() override;
	
public:
	ClientSocketManager();
    
	void SendPacket(const OpCode opCode);
	void SendPacket(const OpCode opcode, std::vector<std::string>& args);
	const bool Connected() const;
	void Logout();
};