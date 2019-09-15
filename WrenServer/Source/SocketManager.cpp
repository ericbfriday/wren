#include "stdafx.h"
#include <OpCodes.h>
#include <ObjectManager.h>
#include "SocketManager.h"
#include <Components/StatsComponentManager.h>
#include "Components/AIComponentManager.h"
#include "Components/PlayerComponentManager.h"
#include "Components/SkillComponentManager.h"

constexpr auto PLAYER_NOT_FOUND = "Player not found.";
constexpr auto SOCKET_INIT_FAILED = "Failed to initialize sockets.";
constexpr auto INCORRECT_USERNAME = "Incorrect Username.";
constexpr auto INCORRECT_PASSWORD = "Incorrect Password.";
constexpr auto ACCOUNT_ALREADY_EXISTS = "Account already exists.";
constexpr auto LIBSODIUM_MEMORY_ERROR = "Ran out of memory while hashing password.";
constexpr auto CHARACTER_ALREADY_EXISTS = "Character already exists.";
constexpr auto INVALID_ATTACK_TARGET = "You can't attack that!";
constexpr auto NO_ATTACK_TARGET = "You need a target before attacking!";
constexpr auto MESSAGE_TYPE_ERROR = "ERROR";

constexpr auto PORT_NUMBER = 27016;

extern ObjectManager g_objectManager;
extern StatsComponentManager g_statsComponentManager;
extern AIComponentManager g_aiComponentManager;
extern PlayerComponentManager g_playerComponentManager;
extern SkillComponentManager g_skillComponentManager;
extern GameMap g_gameMap;
extern EventHandler g_eventHandler;

SocketManager::SocketManager(ServerRepository& repository, CommonRepository& commonRepository)
	: repository{ repository },
	  commonRepository{ commonRepository }
{
    sodium_init();
	InitializeMessageHandlers();

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != NO_ERROR)
        throw new std::exception(SOCKET_INIT_FAILED);

    local.sin_family = AF_INET;
    local.sin_port = htons(PORT_NUMBER);
    local.sin_addr.s_addr = INADDR_ANY;

	fromlen = sizeof(from);

    socketS = socket(AF_INET, SOCK_DGRAM, 0);
    int error;
    if (socketS == INVALID_SOCKET)
        error = WSAGetLastError();
    bind(socketS, (sockaddr*)&local, sizeof(local));

    DWORD nonBlocking = 1;
    ioctlsocket(socketS, FIONBIO, &nonBlocking);

	// initialize Abilities
	abilities = repository.ListAbilities();

	// initialize StaticObjects
	auto staticObjects = commonRepository.ListStaticObjects();

	for (auto staticObject : staticObjects)
	{
		const auto pos = staticObject->GetPosition();
		GameObject& gameObject = g_objectManager.CreateGameObject(pos, XMFLOAT3{ 14.0f, 14.0f, 14.0f }, 0.0f, GameObjectType::StaticObject, staticObject->GetName(), staticObject->GetId(), true);
		g_gameMap.SetTileOccupied(gameObject.localPosition, true);

		delete staticObject;
	}

	// initialize test dummy
	// we need to move ListNpcs to CommonRepository
	auto dummyName = new std::string("Dummy");
	GameObject& dummyGameObject = g_objectManager.CreateGameObject(XMFLOAT3{ 30.0f, 0.0f, 30.0f }, XMFLOAT3{ 14.0f, 14.0f, 14.0f }, 30.0f, GameObjectType::Npc, dummyName, 101, false, 2, 4);
	const auto dummyId = dummyGameObject.GetId();
	auto dummyAIComponent = g_aiComponentManager.CreateAIComponent(dummyId);
	dummyGameObject.aiComponentId = dummyAIComponent.id;
	StatsComponent& dummyStatsComponent = g_statsComponentManager.CreateStatsComponent(dummyId, 10, 10, 10, 10, 10, 10, 10, 100, 100, 100, 100, 100, 100);
	dummyGameObject.statsComponentId = dummyStatsComponent.GetId();
	g_gameMap.SetTileOccupied(dummyGameObject.localPosition, true);
}

void SocketManager::SendPacket(const OpCode opcode, std::string args[], const int argCount)
{
	auto playerComponents = g_playerComponentManager.GetPlayerComponents();
	auto playerComponentIndex = g_playerComponentManager.GetPlayerComponentIndex();

	for (auto i = 0; i < playerComponentIndex; i++)
	{
		PlayerComponent& player = playerComponents[i];
		SendPacket(player.fromSockAddr, opcode, args, argCount);
	}
}

void SocketManager::SendPacket(sockaddr_in from, const OpCode opCode)
{
	std::string args[]{ "" }; // this is janky
	SendPacket(from, opCode, args, 0);
}

void SocketManager::SendPacket(sockaddr_in from, const OpCode opCode, std::string args[], const int argCount)
{
	char buffer[PACKET_SIZE];
	memset(buffer, 0, sizeof(buffer));
	int offset{ 0 };

	memcpy(buffer, &CHECKSUM, sizeof(OpCode));
	offset += (int)sizeof(OpCode);

	memcpy(buffer + offset, &opCode, sizeof(OpCode));
	offset += (int)sizeof(OpCode);

	std::string packet{ "" };
	for (auto i = 0; i < argCount; i++)
		packet += args[i] + "|";

	strcpy_s(buffer + offset, packet.length() + 1, packet.c_str());
	auto sentBytes = sendto(socketS, buffer, sizeof(buffer), 0, (sockaddr*)& from, sizeof(from));
	if (sentBytes != sizeof(buffer))
		throw std::exception("Failed to send packet.");
}

bool SocketManager::TryRecieveMessage()
{
	char buffer[1024];
	ZeroMemory(buffer, sizeof(buffer));
	auto result = recvfrom(socketS, buffer, sizeof(buffer), 0, (sockaddr*)& from, &fromlen);
	if (result == SOCKET_ERROR)
	{
		auto errorCode = WSAGetLastError();

		if (errorCode == WSAEWOULDBLOCK)
			return false;

		throw new std::exception("WrenServer SocketManager error receiving packet. Error code: " + errorCode);
	}
	else
	{
		int offset{ 0 };

		// if the checksum is wrong, ignore the packet
		int checksum{ 0 };
		memcpy(&checksum, buffer, sizeof(OpCode));
		offset += sizeof(OpCode);
		if (checksum != (int)OpCode::Checksum)
			return true;

		OpCode opCode{ -1 };
		memcpy(&opCode, buffer + offset, sizeof(OpCode));
		offset += sizeof(OpCode);

		std::vector<std::string> args;
		auto bufferLength = strlen(buffer + offset);
		if (bufferLength > offset)
		{
			std::string arg = "";
			for (unsigned int i = offset; i < offset + bufferLength; i++)
			{
				if (buffer[i] == '|')
				{
					args.push_back(arg);
					arg = "";
				}
				else
					arg += buffer[i];
			}
		}

		const auto opCodeIndex = opCodeIndexMap[opCode];
		if (opCodeIndex >= 0 && opCodeIndex < messageHandlers.size())
		{
			messageHandlers[opCodeIndex](args);
			return true;
		}

		return false;
	}
}

const bool SocketManager::ValidateToken(const int accountId, const std::string token)
{
	const auto gameObject = g_objectManager.GetGameObjectById(accountId);
	const PlayerComponent& playerComponent = g_playerComponentManager.GetPlayerComponentById(gameObject.playerComponentId);
	
	return token == playerComponent.token;
}

PlayerComponent& SocketManager::GetPlayerComponent(const int accountId)
{
	const auto gameObject = g_objectManager.GetGameObjectById(accountId);

	return g_playerComponentManager.GetPlayerComponentById(gameObject.playerComponentId);
}

void SocketManager::HandleTimeout()
{
	auto playerComponents = g_playerComponentManager.GetPlayerComponents();
	auto playerComponentIndex = g_playerComponentManager.GetPlayerComponentIndex();

	for (auto i = 0; i < playerComponentIndex; i++)
	{
		PlayerComponent& comp = playerComponents[i];
		if (GetTickCount64() > comp.lastHeartbeat + TIMEOUT_DURATION)
		{
			//std::cout << "AccountId " << comp.gameObjectId << " timed out." << "\n\n";
			g_objectManager.DeleteGameObject(g_eventHandler, comp.gameObjectId);
		}
	}
}

void SocketManager::Login(const char* accountName, const char* password, const std::string& ipAndPort, sockaddr_in from)
{
	std::string error;
	auto account = repository.GetAccount(accountName);
	if (account)
	{
		if (crypto_pwhash_str_verify(account->GetPassword().c_str(), password, strlen(password)) != 0)
			error = INCORRECT_PASSWORD;
		else
		{
			GUID guid;
			if (FAILED(CoCreateGuid(&guid)))
				throw new std::exception("Failed to create GUID.");
			char guid_cstr[39];
			snprintf(guid_cstr, sizeof(guid_cstr),
				"{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
				guid.Data1, guid.Data2, guid.Data3,
				guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
				guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
			const std::string token = std::string(guid_cstr);

			const auto accountId = account->GetId();
			auto name = new std::string("");
			GameObject& playerGameObject = g_objectManager.CreateGameObject(VEC_ZERO, XMFLOAT3{ 14.0f, 14.0f, 14.0f }, PLAYER_SPEED, GameObjectType::Player, name, accountId);
			const auto playerId = playerGameObject.GetId();
			const PlayerComponent& playerComponent = g_playerComponentManager.CreatePlayerComponent(playerId, token, ipAndPort, from, GetTickCount64());
			playerGameObject.playerComponentId = playerComponent.id;
            
			std::string args[]{ std::to_string(accountId), token, ListCharacters(accountId) };
			SendPacket(OpCode::LoginSuccess, args , 3);
		}

	}
	else
		error = INCORRECT_USERNAME;

	if (error != "")
	{
		std::string args[]{ error };
		SendPacket(from, OpCode::LoginFailure, args, 1);
	}
}

void SocketManager::Logout(const int accountId)
{
	g_objectManager.DeleteGameObject(g_eventHandler, accountId);
}

void SocketManager::CreateAccount(const std::string& accountName, const std::string& password, sockaddr_in from)
{
	if (repository.AccountExists(accountName))
	{
		const std::string error = ACCOUNT_ALREADY_EXISTS;
		std::string args[]{ error };
		SendPacket(from, OpCode::CreateAccountFailure, args, 1);
	}
	else
	{
		char hashedPassword[crypto_pwhash_STRBYTES];
		auto passwordArr = password.c_str();
		const auto result = crypto_pwhash_str(
			hashedPassword,
			passwordArr,
			strlen(passwordArr),
			crypto_pwhash_OPSLIMIT_INTERACTIVE,
			crypto_pwhash_MEMLIMIT_INTERACTIVE);
		if (result != 0)
			throw std::exception(LIBSODIUM_MEMORY_ERROR);

		repository.CreateAccount(accountName, hashedPassword);
		SendPacket(from, OpCode::CreateAccountSuccess);
	}
}

void SocketManager::CreateCharacter(const int accountId, const std::string& characterName)
{
	const PlayerComponent& playerComponent = GetPlayerComponent(accountId);

	if (repository.CharacterExists(characterName))
	{
		const std::string error = CHARACTER_ALREADY_EXISTS;
		std::string args[]{ error };
		SendPacket(playerComponent.fromSockAddr, OpCode::CreateCharacterFailure, args, 1);
	}
	else
	{
		repository.CreateCharacter(characterName, accountId);
		std::string args[]{ ListCharacters(accountId) };
		SendPacket(playerComponent.fromSockAddr, OpCode::CreateCharacterSuccess, args, 1);
	}
}

void SocketManager::UpdateLastHeartbeat(const int accountId)
{
	PlayerComponent& playerComponent = GetPlayerComponent(accountId);
	playerComponent.lastHeartbeat = GetTickCount64();
}

void SocketManager::CloseSockets()
{
    closesocket(socketS);
    WSACleanup();
}

std::string SocketManager::ListCharacters(const int accountId)
{
    auto characters = repository.ListCharacters(accountId);
    std::string characterString = "";
    for (auto i = 0; i < characters.size(); i++)
        characterString += (characters.at(i) + ";");
    return characterString;
}

std::string SocketManager::ListSkills(const int characterId)
{
	auto skills = repository.ListCharacterSkills(characterId);
	std::string skillString = "";
	for (auto i = 0; i < skills.size(); i++)
	{
		auto skill = skills.at(i);
		skillString += (std::to_string(skill.skillId) + "%" + skill.name + "%" + std::to_string(skill.value) + ";");
	}
	return skillString;
}

std::string SocketManager::ListAbilities(const int characterId)
{
	auto abilities = repository.ListCharacterAbilities(characterId);
	std::string abilityString = "";
	for (auto i = 0; i < abilities.size(); i++)
	{
		auto ability = abilities.at(i);
		abilityString += (std::to_string(ability.abilityId) + "%" + ability.name + "%" + std::to_string(ability.spriteId) +  + "%" + std::to_string(ability.toggled) + "%" + std::to_string(ability.targeted) + ";");
	}
	return abilityString;
}

void SocketManager::EnterWorld(const int accountId, const std::string& characterName)
{
	GameObject& gameObject = g_objectManager.GetGameObjectById(accountId);
	gameObject.name = new std::string(characterName);

	PlayerComponent& playerComponent = GetPlayerComponent(accountId);
	auto character = repository.GetCharacter(characterName);
	gameObject.localPosition = character->GetPosition();
	playerComponent.lastHeartbeat = GetTickCount64();
	playerComponent.characterId = character->GetId();
	playerComponent.modelId = character->GetModelId();
	playerComponent.textureId = character->GetTextureId();

	const auto agility = character->GetAgility();
	const auto strength = character->GetStrength();
	const auto wisdom = character->GetWisdom();
	const auto intelligence = character->GetIntelligence();
	const auto charisma = character->GetCharisma();
	const auto luck = character->GetLuck();
	const auto endurance = character->GetEndurance();
	const auto health = character->GetHealth();
	const auto maxHealth = character->GetMaxHealth();
	const auto mana = character->GetMana();
	const auto maxMana = character->GetMaxMana();
	const auto stamina = character->GetStamina();
	const auto maxStamina = character->GetMaxStamina();

	const int gameObjectId = gameObject.GetId();
	auto statsComponent = g_statsComponentManager.CreateStatsComponent(
		gameObjectId,
		agility, strength, wisdom, intelligence, charisma, luck, endurance,
		health, maxHealth, mana, maxMana, stamina, maxStamina
	);
	gameObject.statsComponentId = statsComponent.GetId();

	auto skills = repository.ListCharacterSkills(character->GetId());
	const auto skillComponent = g_skillComponentManager.CreateSkillComponent(gameObjectId, skills);
	gameObject.skillComponentId = skillComponent.GetId();

	const auto pos = character->GetPosition();
	const auto charId = character->GetId();
	std::string args[]
	{
		std::to_string(accountId),
		std::to_string(pos.x), std::to_string(pos.y), std::to_string(pos.z),
		std::to_string(character->GetModelId()), std::to_string(character->GetTextureId()),
		ListSkills(charId), ListAbilities(charId),
		character->GetName(),
		std::to_string(agility), std::to_string(strength), std::to_string(wisdom), std::to_string(intelligence), std::to_string(charisma), std::to_string(luck), std::to_string(endurance),
		std::to_string(health), std::to_string(maxHealth), std::to_string(mana), std::to_string(maxMana), std::to_string(stamina), std::to_string(maxStamina),
	};
	SendPacket(playerComponent.fromSockAddr, OpCode::EnterWorldSuccess, args, 22);
	g_gameMap.SetTileOccupied(pos, true);
}

void SocketManager::DeleteCharacter(const int accountId, const std::string& characterName)
{
	const PlayerComponent& playerComponent = GetPlayerComponent(accountId);
	repository.DeleteCharacter(characterName);
	std::string args[]{ ListCharacters(accountId) };
	SendPacket(playerComponent.fromSockAddr , OpCode::DeleteCharacterSuccess, args, 1);
}

void SocketManager::UpdateClients()
{
	auto gameObjectLength = g_objectManager.GetGameObjectIndex();
	auto gameObjects = g_objectManager.GetGameObjects();

	auto playerComponents = g_playerComponentManager.GetPlayerComponents();
	auto playerComponentIndex = g_playerComponentManager.GetPlayerComponentIndex();

	for (auto i = 0; i < playerComponentIndex; i++)
	{
		PlayerComponent& playerToUpdate = playerComponents[i];

		// skip players that have logged in, but haven't selected a character and entered the game yet
		if (playerToUpdate.characterId == 0)
			continue;

		// for each player, send them an update for every non-static game object
		for (auto j = 0; j < gameObjectLength; j++)
		{
			auto gameObject = gameObjects[j];

			auto pos = gameObject.GetWorldPosition();
			auto mov = gameObject.movementVector;

			const auto id = std::to_string(gameObject.GetId());
			const auto posX = std::to_string(pos.x);
			const auto posY = std::to_string(pos.y);
			const auto posZ = std::to_string(pos.z);
			const auto movX = std::to_string(mov.x);
			const auto movY = std::to_string(mov.y);
			const auto movZ = std::to_string(mov.z);

			const auto type = gameObject.GetType();
			if (type == GameObjectType::Npc)
			{
				const auto stats = g_statsComponentManager.GetStatsComponentById(gameObject.statsComponentId);

				const auto agility = std::to_string(stats.agility);
				const auto strength = std::to_string(stats.strength);
				const auto wisdom = std::to_string(stats.wisdom);
				const auto intelligence = std::to_string(stats.intelligence);
				const auto charisma = std::to_string(stats.charisma);
				const auto luck = std::to_string(stats.luck);
				const auto endurance = std::to_string(stats.endurance);
				const auto health = std::to_string(stats.health);
				const auto maxHealth = std::to_string(stats.maxHealth);
				const auto mana = std::to_string(stats.mana);
				const auto maxMana = std::to_string(stats.maxMana);
				const auto stamina = std::to_string(stats.stamina);
				const auto maxStamina = std::to_string(stats.maxStamina);

				std::string args[]{ id, posX, posY, posZ, movX, movY, movZ, agility, strength, wisdom, intelligence, charisma, luck, endurance, health, maxHealth, mana, maxMana, stamina, maxStamina };
				SendPacket(playerToUpdate.fromSockAddr, OpCode::NpcUpdate, args, 20);
			}
			else if (type == GameObjectType::Player)
			{
				const auto stats = g_statsComponentManager.GetStatsComponentById(gameObject.statsComponentId);

				const auto agility = std::to_string(stats.agility);
				const auto strength = std::to_string(stats.strength);
				const auto wisdom = std::to_string(stats.wisdom);
				const auto intelligence = std::to_string(stats.intelligence);
				const auto charisma = std::to_string(stats.charisma);
				const auto luck = std::to_string(stats.luck);
				const auto endurance = std::to_string(stats.endurance);
				const auto health = std::to_string(stats.health);
				const auto maxHealth = std::to_string(stats.maxHealth);
				const auto mana = std::to_string(stats.mana);
				const auto maxMana = std::to_string(stats.maxMana);
				const auto stamina = std::to_string(stats.stamina);
				const auto maxStamina = std::to_string(stats.maxStamina);

				const PlayerComponent& otherPlayer = g_playerComponentManager.GetPlayerComponentById(gameObject.playerComponentId);
				std::string args[]{ id, posX, posY, posZ, movX, movY, movZ, std::to_string(otherPlayer.modelId), std::to_string(otherPlayer.textureId), *gameObject.name, agility, strength, wisdom, intelligence, charisma, luck, endurance, health, maxHealth, mana, maxMana, stamina, maxStamina };
				SendPacket(playerToUpdate.fromSockAddr, OpCode::PlayerUpdate, args, 23);
			}
		}
	}
}

void SocketManager::PropagateChatMessage(const std::string& senderName, const std::string& message)
{
	auto playerComponents = g_playerComponentManager.GetPlayerComponents();
	auto playerComponentIndex = g_playerComponentManager.GetPlayerComponentIndex();

	for (auto i = 0; i < playerComponentIndex; i++)
	{
		PlayerComponent& playerToUpdate = playerComponents[i];
		std::string args[]{ senderName, message };
		SendPacket(playerToUpdate.fromSockAddr, OpCode::PropagateChatMessage, args, 2);
	}
}

void SocketManager::ActivateAbility(PlayerComponent& playerComponent, Ability& ability)
{
	if (ability.name == "Auto Attack")
	{
		if (!playerComponent.autoAttackOn && playerComponent.targetId == -1)
		{
			std::string args[]{ std::string(NO_ATTACK_TARGET), std::string(MESSAGE_TYPE_ERROR) };
			SendPacket(playerComponent.fromSockAddr, OpCode::ServerMessage, args, 2);
			return;
		}
		else if (!playerComponent.autoAttackOn && g_objectManager.GetGameObjectById(playerComponent.targetId).isStatic)
		{
			std::string args[]{ std::string(INVALID_ATTACK_TARGET), std::string(MESSAGE_TYPE_ERROR) };
			SendPacket(playerComponent.fromSockAddr, OpCode::ServerMessage, args, 2);
			return;
		}
		else
		{
			playerComponent.autoAttackOn = !playerComponent.autoAttackOn;
		}
	}
	else if (ability.name == "Fireball")
	{

	}
	else if (ability.name == "Healing")
	{

	}

	std::string args[]{ std::to_string(ability.abilityId) };
	SendPacket(playerComponent.fromSockAddr, OpCode::ActivateAbilitySuccess, args, 1);
}

void SocketManager::InitializeMessageHandlers()
{
	auto i = 0;

	InitializeMessageHandler(OpCode::Connect, i, [this](std::vector<std::string> args)
	{
		const auto accountName = args[0];
		const auto password = args[1];

		char str[INET_ADDRSTRLEN];
		ZeroMemory(str, sizeof(str));
		inet_ntop(AF_INET, &(from.sin_addr), str, INET_ADDRSTRLEN);
		const auto ipAndPort = std::string(str) + ":" + std::to_string(from.sin_port);

		Login(accountName.c_str(), password.c_str(), ipAndPort, from);
	});

	InitializeMessageHandler(OpCode::Disconnect, i, [this](std::vector<std::string> args)
	{
		const auto accountId = std::stoi(args[0]);
		const auto token = args[1];

		ValidateToken(accountId, token);
		Logout(accountId);
	});
	
	InitializeMessageHandler(OpCode::CreateAccount, i, [this](std::vector<std::string> args)
	{
		const auto accountName = args[0];
		const auto password = args[1];

		CreateAccount(accountName, password, from);
	});

	InitializeMessageHandler(OpCode::CreateCharacter, i, [this](std::vector<std::string> args)
	{
		const auto accountId = std::stoi(args[0]);
		const auto token = args[1];
		const auto characterName = args[2];

		ValidateToken(accountId, token);
		CreateCharacter(accountId, characterName);
	});

	InitializeMessageHandler(OpCode::Heartbeat, i, [this](std::vector<std::string> args)
	{
		const auto accountId = std::stoi(args[0]);
		const auto token = args[1];

		ValidateToken(accountId, token);
		UpdateLastHeartbeat(accountId);
	});

	InitializeMessageHandler(OpCode::EnterWorld, i, [this](std::vector<std::string> args)
	{
		const auto accountId = std::stoi(args[0]);
		const auto token = args[1];
		const auto characterName = args[2];

		ValidateToken(accountId, token);
		EnterWorld(accountId, characterName);
	});

	InitializeMessageHandler(OpCode::DeleteCharacter, i, [this](std::vector<std::string> args)
	{
		const auto accountId = std::stoi(args[0]);
		const auto token = args[1];
		const auto characterName = args[2];

		ValidateToken(accountId, token);
		DeleteCharacter(accountId, characterName);
	});

	InitializeMessageHandler(OpCode::ActivateAbility, i, [this](std::vector<std::string> args)
	{
		const auto accountId = std::stoi(args[0]);
		const auto token = args[1];
		const auto abilityId = args[2];

		ValidateToken(accountId, token);
		PlayerComponent& playerComponent = GetPlayerComponent(accountId);

		const auto abilityIt = find_if(abilities.begin(), abilities.end(), [&abilityId](Ability ability) { return ability.abilityId == std::stoi(abilityId); });
		if (abilityIt == abilities.end())
			return;

		ActivateAbility(playerComponent, *abilityIt);
	});

	InitializeMessageHandler(OpCode::SendChatMessage, i, [this](std::vector<std::string> args)
	{
		const auto accountId = std::stoi(args[0]);
		const auto token = args[1];
		const auto message = args[2];
		const auto senderName = args[3];

		ValidateToken(accountId, token);
		PropagateChatMessage(senderName, message);
	});

	InitializeMessageHandler(OpCode::SetTarget, i, [this](std::vector<std::string> args)
	{
		const auto accountId = std::stoi(args[0]);
		const auto token = args[1];
		const auto targetId = args[2];

		ValidateToken(accountId, token);
		PlayerComponent& playerComponent = GetPlayerComponent(accountId);
		playerComponent.targetId = std::stol(targetId);

		const auto gameObject = g_objectManager.GetGameObjectById(playerComponent.targetId);

		// Toggle off Auto-Attack on the server and the client if the player switches to an invalid target.
		if (gameObject.isStatic && playerComponent.autoAttackOn)
		{
			playerComponent.autoAttackOn = false;
			std::string args1[]{ std::string(INVALID_ATTACK_TARGET), std::string(MESSAGE_TYPE_ERROR) };
			SendPacket(playerComponent.fromSockAddr, OpCode::ServerMessage, args1, 2);
			const std::string autoAttackAbilityId = "1";
			std::string args2[]{ autoAttackAbilityId };
			SendPacket(playerComponent.fromSockAddr, OpCode::ActivateAbilitySuccess, args2, 1);
		}
	});

	InitializeMessageHandler(OpCode::UnsetTarget, i, [this](std::vector<std::string> args)
	{
		const auto accountId = std::stoi(args[0]);
		const auto token = args[1];

		ValidateToken(accountId, token);
		PlayerComponent& playerComponent = GetPlayerComponent(accountId);
		playerComponent.targetId = -1;
	});

	InitializeMessageHandler(OpCode::Ping, i, [this](std::vector<std::string> args)
	{
		const auto accountId = std::stoi(args[0]);
		const auto token = args[1];
		const auto pingId = args[2];

		ValidateToken(accountId, token);

		PlayerComponent& player = GetPlayerComponent(accountId);
		std::string outgoingArgs[]{ pingId };
		SendPacket(player.fromSockAddr, OpCode::Pong, outgoingArgs, 1);
	});

	InitializeMessageHandler(OpCode::PlayerRightMouseDown, i, [this](std::vector<std::string> args)
	{
		const auto accountId = std::stoi(args[0]);
		const auto token = args[1];
		const auto dir = XMFLOAT3{ std::stof(args[2]), std::stof(args[3]), std::stof(args[4]) };

		ValidateToken(accountId, token);

		PlayerComponent& comp = GetPlayerComponent(accountId);
		comp.rightMouseDownDir = dir;
	});

	InitializeMessageHandler(OpCode::PlayerRightMouseUp, i, [this](std::vector<std::string> args)
	{
		const auto accountId = std::stoi(args[0]);
		const auto token = args[1];

		ValidateToken(accountId, token);

		PlayerComponent& comp = GetPlayerComponent(accountId);
		comp.rightMouseDownDir = VEC_ZERO;
	});

	InitializeMessageHandler(OpCode::PlayerRightMouseDirChange, i, [this](std::vector<std::string> args)
	{
		const auto accountId = std::stoi(args[0]);
		const auto token = args[1];
		const auto dir = XMFLOAT3{ std::stof(args[2]), std::stof(args[3]), std::stof(args[4]) };

		ValidateToken(accountId, token);

		PlayerComponent& comp = GetPlayerComponent(accountId);
		comp.rightMouseDownDir = dir;
	});
}

void SocketManager::InitializeMessageHandler(const OpCode opCode, int& index, std::function<void(std::vector<std::string> args)> function)
{
	opCodeIndexMap[opCode] = index++;
	messageHandlers.push_back(function);
}