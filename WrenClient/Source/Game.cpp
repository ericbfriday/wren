#include "stdafx.h"
#include "Game.h"
#include "ConstantBufferPerFrame.h"
#include "Events/SkillIncreaseEvent.h"
#include "Events/DoubleLeftMouseDownEvent.h"
#include "Events/MoveItemSuccessEvent.h"
#include "EventHandling/Events/ChangeActiveLayerEvent.h"
#include "EventHandling/Events/CreateAccountFailedEvent.h"
#include "EventHandling/Events/LoginSuccessEvent.h"
#include "EventHandling/Events/LoginFailedEvent.h"
#include "EventHandling/Events/CreateCharacterFailedEvent.h"
#include "EventHandling/Events/CreateCharacterSuccessEvent.h"
#include "EventHandling/Events/DeleteCharacterSuccessEvent.h"
#include "EventHandling/Events/EnterWorldSuccessEvent.h"
#include "EventHandling/Events/NpcUpdateEvent.h"
#include "EventHandling/Events/PlayerUpdateEvent.h"
#include "EventHandling/Events/ActivateAbilityEvent.h"
#include "EventHandling/Events/SetTargetEvent.h"
#include "EventHandling/Events/SendChatMessage.h"
#include "EventHandling/Events/PropagateChatMessageEvent.h"
#include "EventHandling/Events/ServerMessageEvent.h"
#include "EventHandling/Events/SystemKeyDownEvent.h"
#include "EventHandling/Events/SystemKeyUpEvent.h"
#include "UI\UIInputType.h"

bool g_mouseIsDragging{ false };
bool g_leftCtrlHeld{ false };
bool g_leftAltHeld{ false };
bool g_leftShiftHeld{ false };
unsigned int g_zIndex{ 0 };
float g_clientWidth{ CLIENT_WIDTH };
float g_clientHeight{ CLIENT_HEIGHT };
float g_mousePosX{ 0.0f };
float g_mousePosY{ 0.0f };
XMMATRIX g_projectionTransform{ XMMatrixIdentity() };

bool CompareUIComponents(UIComponent* a, UIComponent* b) { return (a->zIndex < b->zIndex); }

Game::Game(
	EventHandler& eventHandler,
	ObjectManager& objectManager,
	RenderComponentManager& renderComponentManager,
	StatsComponentManager& statsComponentManager,
	InventoryComponentManager& inventoryComponentManager,
	ClientRepository& clientRepository,
	CommonRepository& commonRepository,
	ClientSocketManager& socketManager)
	: eventHandler{ eventHandler },
	  objectManager{ objectManager },
	  renderComponentManager{ renderComponentManager },
	  statsComponentManager{ statsComponentManager },
	  inventoryComponentManager{ inventoryComponentManager },
	  clientRepository{ clientRepository },
	  commonRepository{ commonRepository },
	  socketManager{ socketManager }
{
	eventHandler.Subscribe(*this);

	deviceResources = std::make_unique<DX::DeviceResources>();
	deviceResources->RegisterDeviceNotify(this);

	npcs = clientRepository.ListNpcs();
	items = clientRepository.ListItems();
	CreateEventHandlers();
}

void Game::Initialize(HWND window, int width, int height)
{
	CreateInputs();
	CreateButtons();
	CreateLabels();
	CreatePanels();

	targetHUD = std::make_unique<UITargetHUD>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 260.0f, 12.0f }; }, InGame, 0 });
	hotbar = std::make_unique<UIHotbar>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float height) { return XMFLOAT2{ 5.0f, height - 45.0f }; }, InGame, 0 }, eventHandler);
	textWindow = std::make_unique<UITextWindow>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float height) { return XMFLOAT2{ 5.0f, height - 300.0f }; }, InGame, 0 }, eventHandler, objectManager, items, textWindowMessages, textWindowMessageIndex.get());

	deviceResources->SetWindow(window, width, height);
	deviceResources->CreateDeviceResources();
	CreateDeviceDependentResources();

	deviceResources->CreateWindowSizeDependentResources();
	CreateWindowSizeDependentResources();

	timer.Reset();
	SetActiveLayer(Login);
}

// Allocate all memory resources that depend on the D2
void Game::CreateDeviceDependentResources()
{
	InitializeBrushes();
	InitializeTextFormats();
	InitializeShaders();
	InitializeBuffers();
	InitializeRasterStates();
	InitializeTextures();
	InitializeMeshes();

	InitializeInputs();
	InitializeButtons();
	InitializeLabels();
	InitializePanels();
	InitializeCharacterListings();
	InitializeStaticObjects();

	skillsContainer->Initialize(blackBrush.Get(), textFormatFPS.Get());
	abilitiesContainer->Initialize(blackBrush.Get(), abilityHighlightBrush.Get(), blackBrush.Get(), abilityPressedBrush.Get(), errorMessageBrush.Get(), textFormatHeaders.Get(), spriteVertexShader.Get(), spritePixelShader.Get(), spriteVertexShaderBuffer.buffer, spriteVertexShaderBuffer.size, &textures, lightGrayBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatTooltipTitle.Get(), textFormatTooltipDescription.Get());
	lootContainer->Initialize(blackBrush.Get(), abilityHighlightBrush.Get(), spriteVertexShader.Get(), spritePixelShader.Get(), spriteVertexShaderBuffer.buffer, spriteVertexShaderBuffer.size, lightGrayBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatTooltipTitle.Get(), textFormatTooltipDescription.Get(), &textures);
	inventory->Initialize(blackBrush.Get(), abilityHighlightBrush.Get(), spriteVertexShader.Get(), spritePixelShader.Get(), spriteVertexShaderBuffer.buffer, spriteVertexShaderBuffer.size, lightGrayBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatTooltipTitle.Get(), textFormatTooltipDescription.Get());

	if (characterHUD)
		characterHUD->Initialize(textFormatSuccessMessage.Get(), healthBrush.Get(), manaBrush.Get(), staminaBrush.Get(), statBackgroundBrush.Get(), blackBrush.Get(), blackBrush.Get(), whiteBrush.Get());

	targetHUD->Initialize(textFormatSuccessMessage.Get(), healthBrush.Get(), manaBrush.Get(), staminaBrush.Get(), statBackgroundBrush.Get(), blackBrush.Get(), blackBrush.Get(), whiteBrush.Get());
	hotbar->Initialize(blackBrush.Get());
	textWindow->Initialize(statBackgroundBrush.Get(), blackBrush.Get(), darkGrayBrush.Get(), whiteBrush.Get(), mediumGrayBrush.Get(), blackBrush.Get(), scrollBarBackgroundBrush.Get(), scrollBarBrush.Get(), textFormatTextWindow.Get(), textFormatTextWindowInactive.Get());

	// TODO: fix me - split apart into ctor and Initialize
	gameMapRenderComponent = std::make_unique<GameMapRenderComponent>(deviceResources->GetD3DDevice(), vertexShaderBuffer.buffer, vertexShaderBuffer.size, vertexShader.Get(), pixelShader.Get(), textures.at(2).Get());

	// initialize gameEditor elements
	gameEditorPanelDirectionalLightColorRInput->SetInputValue(L"1", 1);
	gameEditorPanelDirectionalLightColorGInput->SetInputValue(L"1", 1);
	gameEditorPanelDirectionalLightColorBInput->SetInputValue(L"1", 1);
	gameEditorPanelDirectionalLightXPosInput->SetInputValue(L"0", 1);
	gameEditorPanelDirectionalLightYPosInput->SetInputValue(L"-1.0", 4);
	gameEditorPanelDirectionalLightZPosInput->SetInputValue(L"0.5", 3);
	gameEditorPanelDirectionalLightIntensityInput->SetInputValue(L"0.7", 3);
	gameEditorPanelAmbientLightIntensityInput->SetInputValue(L"0.6", 3);
}

// Allocate all memory resources that change on a window SizeChanged event.
void Game::CreateWindowSizeDependentResources()
{
	g_projectionTransform = XMMatrixOrthographicLH(g_clientWidth, g_clientHeight, 0.0f, 5000.0f);

	std::unique_ptr<Event> e = std::make_unique<Event>(EventType::WindowResize);
	eventHandler.QueueEvent(e);

	std::sort(uiComponents.begin(), uiComponents.end(), CompareUIComponents);
}

// D2D/D3D Resources follow a Create/Initialize pattern.

// Resources are created using the D2DContext and D3DContext objects that provide
//   an interface with the GPU. Certain things out of our control can trigger a
//   "Device Lost" event where the interface with the GPU is lost, and all
//   resources that were created through the D2DContext/D3DContext are cleaned up.
//   All classes that depend on GPU resources should be able to recover from such
//   an event. To do so, non-device resources can be provided via the constructor.
//   But device dependent resource should be passed in post-constructor via the
//   Initialize function. That way, when we handle a Device Lost event, we can
//   call the Initialiez function on all objects that rely on device-dependent
//   resources.

// Additionally, all classes that render graphics to the window (whether text,
//   2d, or 3d) should be able to recreate window size-dependent resources
//   to support window resizing (both alternating between fullscreen and windowed 
//   modes, and manual window resizing). This functionality is handled on a 
//   case-by-case basis, but in most cases, classes will implement a 
//   "CreateWindowSizeDependentResources" or "CreatePositionDependentResources"
//   function.

void Game::CreateInputs()
{
	// LoginScreen
	loginScreen_accountNameInput = std::make_unique<UIInput>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 15.0f, 20.0f }; }, Login, 0 }, false, 120.0f, 260.0f, 24.0f, "Account Name:", UIInputType::Text);
	loginScreen_passwordInput = std::make_unique<UIInput>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 15.0f, 50.0f }; }, Login, 0 }, true, 120.0f, 260.0f, 24.0f, "Password:", UIInputType::Text);
	loginScreen_inputGroup = std::make_unique<UIInputGroup>(Login, eventHandler);
	loginScreen_inputGroup->AddInput(loginScreen_accountNameInput.get());
	loginScreen_inputGroup->AddInput(loginScreen_passwordInput.get());

	// CreateAccount
	createAccount_accountNameInput = std::make_unique<UIInput>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 15.0f, 20.0f }; }, CreateAccount, 0 }, false, 120.0f, 260.0f, 24.0f, "Account Name:", UIInputType::Text);
	createAccount_passwordInput = std::make_unique<UIInput>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 15.0f, 50.0f }; }, CreateAccount, 0 }, true, 120.0f, 260.0f, 24.0f, "Password:", UIInputType::Text);
	createAccount_inputGroup = std::make_unique<UIInputGroup>(CreateAccount, eventHandler);
	createAccount_inputGroup->AddInput(createAccount_accountNameInput.get());
	createAccount_inputGroup->AddInput(createAccount_passwordInput.get());

	// CreateCharacter
	createCharacter_characterNameInput = std::make_unique<UIInput>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 15.0f, 20.0f }; }, CreateCharacter, 0 }, false, 140.0f, 260.0f, 24.0f, "Character Name:", UIInputType::Text);
	createCharacter_inputGroup = std::make_unique<UIInputGroup>(CreateCharacter, eventHandler);
	createCharacter_inputGroup->AddInput(createCharacter_characterNameInput.get());
}

void Game::CreateButtons()
{
	const auto onClickLoginButton = [this]()
	{
		loginScreen_successMessageLabel->SetText("");
		loginScreen_errorMessageLabel->SetText("");

		const auto accountName = Utility::ws2s(std::wstring(loginScreen_accountNameInput->GetInputValue()));
		const auto password = Utility::ws2s(std::wstring(loginScreen_passwordInput->GetInputValue()));

		if (accountName.length() == 0)
		{
			loginScreen_errorMessageLabel->SetText("Username field can't be empty.");
			return;
		}
		if (password.length() == 0)
		{
			loginScreen_errorMessageLabel->SetText("Password field can't be empty.");
			return;
		}

		std::vector<std::string> args{ accountName, password };
		socketManager.SendPacket(OpCode::Connect, args);
		SetActiveLayer(Connecting);
	};

	const auto onClickLoginScreenCreateAccountButton = [this]()
	{
		loginScreen_successMessageLabel->SetText("");
		loginScreen_errorMessageLabel->SetText("");
		SetActiveLayer(CreateAccount);
	};

	const auto onClickLoginScreeQuitGameButton = [this]()
	{
		QuitGame();
	};

	// LoginScreen
	loginScreen_loginButton = std::make_unique<UIButton>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 145.0f, 96.0f }; }, Login, 0 }, 80.0f, 24.0f, "LOGIN", onClickLoginButton);
	loginScreen_createAccountButton = std::make_unique<UIButton>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float width, const float height) { return XMFLOAT2{ 15.0f, height - 40.0f }; }, Login, 0 }, 160.0f, 24.0f, "CREATE ACCOUNT", onClickLoginScreenCreateAccountButton);
	loginScreen_quitGameButton = std::make_unique<UIButton>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float width, const float height) { return XMFLOAT2{ width - 95.0f, height - 40.0f }; }, Login, 0 }, 80.0f, 24.0f, "QUIT", onClickLoginScreeQuitGameButton);

	const auto onClickCreateAccountCreateAccountButton = [this]()
	{
		createAccount_errorMessageLabel->SetText("");

		const auto accountName = Utility::ws2s(std::wstring(createAccount_accountNameInput->GetInputValue()));
		const auto password = Utility::ws2s(std::wstring(createAccount_passwordInput->GetInputValue()));

		if (accountName.length() == 0)
		{
			createAccount_errorMessageLabel->SetText("Username field can't be empty.");
			return;
		}
		if (password.length() == 0)
		{
			createAccount_errorMessageLabel->SetText("Password field can't be empty.");
			return;
		}

		std::vector<std::string> args{ accountName, password };
		socketManager.SendPacket(OpCode::CreateAccount, args);
	};

	const auto onClickCreateAccountCancelButton = [this]()
	{
		createAccount_errorMessageLabel->SetText("");
		SetActiveLayer(Login);
	};

	// CreateAccount
	createAccount_createAccountButton = std::make_unique<UIButton>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 145.0f, 96.0f }; }, CreateAccount, 0 }, 80.0f, 24.0f, "CREATE", onClickCreateAccountCreateAccountButton);
	createAccount_cancelButton = std::make_unique<UIButton>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float height) { return XMFLOAT2{ 15.0f, height - 40.0f }; }, CreateAccount, 0 }, 80.0f, 24.0f, "CANCEL", onClickCreateAccountCancelButton);

	const auto onClickCharacterSelectNewCharacterButton = [this]()
	{
		characterSelect_successMessageLabel->SetText("");

		if (characterList.size() == 5)
			characterSelect_errorMessageLabel->SetText("You can not have more than 5 characters.");
		else
			SetActiveLayer(CreateCharacter);
	};

	const auto onClickCharacterSelectEnterWorldButton = [this]()
	{
		characterSelect_successMessageLabel->SetText("");
		characterSelect_errorMessageLabel->SetText("");

		const auto* const characterInput = GetCurrentlySelectedCharacterListing();
		if (characterInput == nullptr)
			characterSelect_errorMessageLabel->SetText("You must select a character before entering the game.");
		else
		{
			std::vector<std::string> args{ characterInput->GetName() };
			socketManager.SendPacket(OpCode::EnterWorld, args);
			SetActiveLayer(EnteringWorld);
		}
	};

	const auto onClickCharacterSelectDeleteCharacterButton = [this]()
	{
		characterSelect_successMessageLabel->SetText("");
		characterSelect_errorMessageLabel->SetText("");

		const auto* const characterInput = GetCurrentlySelectedCharacterListing();
		if (characterInput == nullptr)
			characterSelect_errorMessageLabel->SetText("You must select a character to delete.");
		else
		{
			characterNamePendingDeletion = characterInput->GetName();
			SetActiveLayer(DeleteCharacter);
		}
	};

	const auto onClickCharacterSelectLogoutButton = [this]()
	{
		socketManager.Logout();
		SetActiveLayer(Login);
	};

	// CharacterSelect
	characterSelect_newCharacterButton = std::make_unique<UIButton>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 15.0f, 20.0f }; }, CharacterSelect, 0 }, 140.0f, 24.0f, "NEW CHARACTER", onClickCharacterSelectNewCharacterButton);
	characterSelect_enterWorldButton = std::make_unique<UIButton>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 170.0f, 20.0f }; }, CharacterSelect, 0 }, 120.0f, 24.0f, "ENTER WORLD", onClickCharacterSelectEnterWorldButton);
	characterSelect_deleteCharacterButton = std::make_unique<UIButton>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 305.0f, 20.0f }; }, CharacterSelect, 0 }, 160.0f, 24.0f, "DELETE CHARACTER", onClickCharacterSelectDeleteCharacterButton);
	characterSelect_logoutButton = std::make_unique<UIButton>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float width, const float height) { return XMFLOAT2{ 15.0f, height - 40.0f }; }, CharacterSelect, 0 }, 80.0f, 24.0f, "LOGOUT", onClickCharacterSelectLogoutButton);

	// CreateCharacter
	const auto onClickCreateCharacterCreateCharacterButton = [this]()
	{
		createCharacter_errorMessageLabel->SetText("");

		const auto characterName = Utility::ws2s(std::wstring(createCharacter_characterNameInput->GetInputValue()));

		if (characterName.length() == 0)
		{
			createCharacter_errorMessageLabel->SetText("Character name can't be empty.");
			return;
		}

		std::vector<std::string> args{ characterName };
		socketManager.SendPacket(OpCode::CreateCharacter, args);
	};

	const auto onClickCreateCharacterBackButton = [this]()
	{
		createCharacter_errorMessageLabel->SetText("");
		SetActiveLayer(CharacterSelect);
	};

	createCharacter_createCharacterButton = std::make_unique<UIButton>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 165.0f, 64.0f }; }, CreateCharacter, 0 }, 160.0f, 24.0f, "CREATE CHARACTER", onClickCreateCharacterCreateCharacterButton);
	createCharacter_backButton = std::make_unique<UIButton>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float width, const float height) { return XMFLOAT2{ 15.0f, height - 40.0f }; }, CreateCharacter, 0 }, 80.0f, 24.0f, "BACK", onClickCreateCharacterBackButton);

	// DeleteCharacter
	const auto onClickDeleteCharacterConfirm = [this]()
	{
		std::vector<std::string> args{ characterNamePendingDeletion };
		socketManager.SendPacket(OpCode::DeleteCharacter, args);
	};

	const auto onClickDeleteCharacterCancel = [this]()
	{
		characterNamePendingDeletion = "";
		SetActiveLayer(CharacterSelect);
	};

	deleteCharacter_confirmButton = std::make_unique<UIButton>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 10.0f, 30.0f }; }, DeleteCharacter, 0 }, 100.0f, 24.0f, "CONFIRM", onClickDeleteCharacterConfirm);
	deleteCharacter_cancelButton = std::make_unique<UIButton>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 120.0f, 30.0f }; }, DeleteCharacter, 0 }, 100.0f, 24.0f, "CANCEL", onClickDeleteCharacterCancel);
}

void Game::CreateLabels()
{
	loginScreen_successMessageLabel = std::make_unique<UILabel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 30.0f, 170.0f }; }, Login, 0 }, 400.0f);
	loginScreen_errorMessageLabel = std::make_unique<UILabel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 30.0f, 170.0f }; }, Login, 0 }, 400.0f);
	createAccount_errorMessageLabel = std::make_unique<UILabel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 30.0f, 170.0f }; }, CreateAccount, 0 }, 400.0f);
	connecting_statusLabel = std::make_unique<UILabel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 15.0f, 20.0f }; }, Connecting, 0 }, 400.0f);
	characterSelect_successMessageLabel = std::make_unique<UILabel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 30.0f, 400.0f }; }, CharacterSelect, 0 }, 400.0f);
	characterSelect_errorMessageLabel = std::make_unique<UILabel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 30.0f, 400.0f }; }, CharacterSelect, 0 }, 400.0f);
	characterSelect_headerLabel = std::make_unique<UILabel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 15.0f, 60.0f }; }, CharacterSelect, 0 }, 200.0f);
	createCharacter_errorMessageLabel = std::make_unique<UILabel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 30.0f, 170.0f }; }, CreateCharacter, 0 }, 400.0f);
	deleteCharacter_headerLabel = std::make_unique<UILabel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 10.0f, 10.0f }; }, DeleteCharacter, 0 }, 400.0f);
	enteringWorld_statusLabel = std::make_unique<UILabel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 5.0f, 20.0f }; }, EnteringWorld, 0 }, 400.0f);
}

void Game::CreatePanels()
{
	// Game Settings
	const auto onClickGameSettingsLogoutButton = [this]()
	{
		clientSettingsManager->SaveClientSettings();
		clientSettingsManager.reset();
		socketManager.SendPacket(OpCode::Disconnect);
		socketManager.Logout();
		SetActiveLayer(Login);
	};
	gameSettingsPanel = std::make_unique<UIPanel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float width, const float height) { return XMFLOAT2{ (width - 400.0f) / 2.0f, (height - 200.0f) / 2.0f }; }, InGame, 1 }, eventHandler, false, 400.0f, 200.0f, VK_ESCAPE);
	gameSettings_logoutButton = std::make_unique<UIButton>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 10.0f, 26.0f }; }, InGame, 2 }, 80.0f, 24.0f, "LOGOUT", onClickGameSettingsLogoutButton);
	gameSettingsPanelHeader = std::make_unique<UILabel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 2.0f, 2.0f }; }, InGame, 2 }, 200.0f);
	gameSettingsPanel->AddChildComponent(*gameSettingsPanelHeader);
	gameSettingsPanel->AddChildComponent(*gameSettings_logoutButton);

	// Game Editor
	gameEditorPanel = std::make_unique<UIPanel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 580.0f, 5.0f }; }, InGame, 1 }, eventHandler, true, 300.0f, 400.0f, VK_F1);
	gameEditorPanelHeader = std::make_unique<UILabel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 2.0f, 2.0f }; }, InGame, 2 }, 200.0f);
	gameEditorPanel->AddChildComponent(*gameEditorPanelHeader);

	// header
	gameEditorPanelLightingHeader = std::make_unique<UILabel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 4.0f, 24.0f }; }, InGame, 2 }, 200.0f);
	gameEditorPanel->AddChildComponent(*gameEditorPanelLightingHeader);
	// directional color
	gameEditorPanelDirectionalLightColorRInput = std::make_unique<UIInput>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 10.0f, 44.0f }; }, InGame, 2 }, false, 120.0f, 100.0f, 20.0f, "(Sun) Red:", UIInputType::Number);
	gameEditorPanel->AddChildComponent(*gameEditorPanelDirectionalLightColorRInput);
	gameEditorPanelDirectionalLightColorGInput = std::make_unique<UIInput>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 10.0f, 68.0f }; }, InGame, 2 }, false, 120.0f, 100.0f, 20.0f, "(Sun) Green:", UIInputType::Number);
	gameEditorPanel->AddChildComponent(*gameEditorPanelDirectionalLightColorGInput);
	gameEditorPanelDirectionalLightColorBInput = std::make_unique<UIInput>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 10.0f, 92.0f }; }, InGame, 2 }, false, 120.0f, 100.0f, 20.0f, "(Sun) Blue:", UIInputType::Number);
	gameEditorPanel->AddChildComponent(*gameEditorPanelDirectionalLightColorBInput);
	// directional position
	gameEditorPanelDirectionalLightXPosInput = std::make_unique<UIInput>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 10.0f, 116.0f }; }, InGame, 2 }, false, 120.0f, 100.0f, 20.0f, "(Sun) X:", UIInputType::Number);
	gameEditorPanel->AddChildComponent(*gameEditorPanelDirectionalLightXPosInput);
	gameEditorPanelDirectionalLightYPosInput = std::make_unique<UIInput>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 10.0f, 140.0f }; }, InGame, 2 }, false, 120.0f, 100.0f, 20.0f, "(Sun) Y:", UIInputType::Number);
	gameEditorPanel->AddChildComponent(*gameEditorPanelDirectionalLightYPosInput);
	gameEditorPanelDirectionalLightZPosInput = std::make_unique<UIInput>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 10.0f, 164.0f }; }, InGame, 2 }, false, 120.0f, 100.0f, 20.0f, "(Sun) Z:", UIInputType::Number);
	gameEditorPanel->AddChildComponent(*gameEditorPanelDirectionalLightZPosInput);
	// intensity
	gameEditorPanelDirectionalLightIntensityInput = std::make_unique<UIInput>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 10.0f, 188.0f }; }, InGame, 2 }, false, 120.0f, 100.0f, 20.0f, "Diffuse Intensity:", UIInputType::Number);
	gameEditorPanel->AddChildComponent(*gameEditorPanelDirectionalLightIntensityInput);
	gameEditorPanelAmbientLightIntensityInput = std::make_unique<UIInput>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 10.0f, 212.0f }; }, InGame, 2 }, false, 120.0f, 100.0f, 20.0f, "Ambient Intensity:", UIInputType::Number);
	gameEditorPanel->AddChildComponent(*gameEditorPanelAmbientLightIntensityInput);

	gameEditorPanelInputGroup = std::make_unique<UIInputGroup>(InGame, eventHandler);
	gameEditorPanelInputGroup->AddInput(gameEditorPanelDirectionalLightColorRInput.get());
	gameEditorPanelInputGroup->AddInput(gameEditorPanelDirectionalLightColorGInput.get());
	gameEditorPanelInputGroup->AddInput(gameEditorPanelDirectionalLightColorBInput.get());
	gameEditorPanelInputGroup->AddInput(gameEditorPanelDirectionalLightXPosInput.get());
	gameEditorPanelInputGroup->AddInput(gameEditorPanelDirectionalLightYPosInput.get());
	gameEditorPanelInputGroup->AddInput(gameEditorPanelDirectionalLightZPosInput.get());
	gameEditorPanelInputGroup->AddInput(gameEditorPanelDirectionalLightIntensityInput.get());
	gameEditorPanelInputGroup->AddInput(gameEditorPanelAmbientLightIntensityInput.get());

	// Diagnostics
	diagnosticsPanel = std::make_unique<UIPanel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 580.0f, 336.0f }; }, InGame, 1 }, eventHandler, true, 200.0f, 200.0f, VK_F2);
	diagnosticsPanelHeader = std::make_unique<UILabel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 2.0f, 2.0f }; }, InGame, 2 }, 280.0f);
	diagnosticsPanel->AddChildComponent(*diagnosticsPanelHeader);

	mousePosLabel = std::make_unique<UILabel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 2.0f, 22.0f }; }, InGame, 2 }, 280.0f);
	diagnosticsPanel->AddChildComponent(*mousePosLabel);

	fpsTextLabel = std::make_unique<UILabel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 2.0f, 36.0f }; }, InGame, 2 }, 280.0f);
	diagnosticsPanel->AddChildComponent(*fpsTextLabel);

	pingTextLabel = std::make_unique<UILabel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 2.0f, 50.0f }; }, InGame, 2 }, 280.0f);
	diagnosticsPanel->AddChildComponent(*pingTextLabel);

	// Skills
	skillsPanel = std::make_unique<UIPanel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 200.0f, 200.0f }; }, InGame, 1 }, eventHandler, true, 200.0f, 200.0f, VK_F3);
	skillsPanelHeader = std::make_unique<UILabel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 2.0f, 2.0f }; }, InGame, 3 }, 280.0f);
	skillsPanel->AddChildComponent(*skillsPanelHeader);

	skillsContainer = std::make_unique<UISkillsContainer>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 0.0f, 0.0f }; }, InGame, 2 });
	skillsPanel->AddChildComponent(*skillsContainer);

	// Abilities
	abilitiesPanel = std::make_unique<UIPanel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 10.0f, 10.0f }; }, InGame, 1 }, eventHandler, true, 240.0f, 400.0f, VK_F4);
	abilitiesPanelHeader = std::make_unique<UILabel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 2.0f, 2.0f }; }, InGame, 3 }, 240.0f);
	abilitiesPanel->AddChildComponent(*abilitiesPanelHeader);

	abilitiesContainer = std::make_unique<UIAbilitiesContainer>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 0.0f, 0.0f }; }, InGame, 2 }, eventHandler);
	abilitiesPanel->AddChildComponent(*abilitiesContainer);

	// Loot
	lootPanel = std::make_unique<UIPanel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 400.0f, 300.0f }; }, InGame, 1 }, eventHandler, true, 140.0f, 185.0f, 0);

	lootPanelHeader = std::make_unique<UILabel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 2.0f, 2.0f }; }, InGame, 3 }, 140.0f);
	lootPanel->AddChildComponent(*lootPanelHeader);

	lootContainer = std::make_unique<UILootContainer>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 0.0f, 0.0f }; }, InGame, 2 }, eventHandler, socketManager, statsComponentManager, inventoryComponentManager, items);
	lootPanel->AddChildComponent(*lootContainer);

	// Inventory
	inventoryPanel = std::make_unique<UIPanel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 500.0f, 350.0f }; }, InGame, 1 }, eventHandler, true, 185.0f, 185.0f, VK_F5);

	inventoryPanelHeader = std::make_unique<UILabel>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 2.0f, 2.0f }; }, InGame, 3 }, 185.0f);
	inventoryPanel->AddChildComponent(*inventoryPanelHeader);

	inventory = std::make_unique<UIInventory>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 0.0f, 0.0f }; }, InGame, 2 }, eventHandler, socketManager, items, textures);
	inventoryPanel->AddChildComponent(*inventory);

	if (player)
		inventory->playerId = player->GetId();
}

void Game::CreateCharacterListings(const std::vector<std::unique_ptr<std::string>>& characterNames)
{
	characterList.clear();

	for (auto i = 0; i < characterNames.size(); i++)
	{
		characterList.push_back(std::make_unique<UICharacterListing>(UIComponentArgs{ deviceResources.get(), uiComponents, [i](const float, const float) { return XMFLOAT2{ 25.0f, 100.0f + (i * 40.0f) }; }, CharacterSelect, 1 }, eventHandler, 260.0f, 30.0f, characterNames.at(i)->c_str()));
		characterList.at(i)->Initialize(whiteBrush.Get(), selectedCharacterBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatAccountCredsInputValue.Get());
	}
}

void Game::InitializeBrushes()
{
	auto d2dContext = deviceResources->GetD2DDeviceContext();

	d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.35f, 0.35f, 1.0f), grayBrush.ReleaseAndGetAddressOf());
	d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.1f, 0.1f, 0.1f, 1.0f), blackBrush.ReleaseAndGetAddressOf());
	d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), whiteBrush.ReleaseAndGetAddressOf());
	d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.619f, 0.854f, 1.0f, 1.0f), blueBrush.ReleaseAndGetAddressOf());
	d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.301f, 0.729f, 1.0f, 1.0f), darkBlueBrush.ReleaseAndGetAddressOf());
	d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.137f, 0.98f, 0.117f, 1.0f), successMessageBrush.ReleaseAndGetAddressOf());
	d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.98f, 0.117f, 0.156f, 1.0f), errorMessageBrush.ReleaseAndGetAddressOf());
	d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.921f, 1.0f, 0.921f, 1.0f), selectedCharacterBrush.ReleaseAndGetAddressOf());
	d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.9f, 0.9f, 0.9f, 1.0f), lightGrayBrush.ReleaseAndGetAddressOf());
	d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.619f, 0.854f, 1.0f, 0.75f), abilityHighlightBrush.ReleaseAndGetAddressOf());
	d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.619f, 0.854f, 1.0f, 0.95f), abilityPressedBrush.ReleaseAndGetAddressOf());
	d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.0f, 0.0f, 1.0f), healthBrush.ReleaseAndGetAddressOf());
	d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 1.0f, 1.0f), manaBrush.ReleaseAndGetAddressOf());
	d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.0f, 1.0f, 0.0f, 1.0f), staminaBrush.ReleaseAndGetAddressOf());
	d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.95f, 0.95f, 1.0f), statBackgroundBrush.ReleaseAndGetAddressOf());
	d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.3f, 0.3f, 0.3f, 1.0f), darkGrayBrush.ReleaseAndGetAddressOf());
	d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.6f, 0.6f, 0.6f, 1.0f), mediumGrayBrush.ReleaseAndGetAddressOf());
	d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.85f, 0.85f, 0.85f, 1.0f), scrollBarBackgroundBrush.ReleaseAndGetAddressOf());
	d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.65f, 0.65f, 0.65f, 1.0f), scrollBarBrush.ReleaseAndGetAddressOf());
}

void Game::InitializeTextFormats()
{
	auto writeFactory = deviceResources->GetWriteFactory();

	// FPS / MousePos
	writeFactory->CreateTextFormat(ARIAL_FONT_FAMILY, nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.0f, LOCALE, textFormatFPS.ReleaseAndGetAddressOf());
	textFormatFPS->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
	textFormatFPS->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

	// Account Creds Input Values
	writeFactory->CreateTextFormat(ARIAL_FONT_FAMILY, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, LOCALE, textFormatAccountCredsInputValue.ReleaseAndGetAddressOf());
	textFormatAccountCredsInputValue->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
	textFormatAccountCredsInputValue->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

	// Account Creds Labels
	writeFactory->CreateTextFormat(ARIAL_FONT_FAMILY, nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, LOCALE, textFormatAccountCreds.ReleaseAndGetAddressOf());
	textFormatAccountCreds->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
	textFormatAccountCreds->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

	// Headers
	writeFactory->CreateTextFormat(ARIAL_FONT_FAMILY, nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, LOCALE, textFormatHeaders.ReleaseAndGetAddressOf());
	textFormatHeaders->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
	textFormatHeaders->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

	// Button Text
	writeFactory->CreateTextFormat(ARIAL_FONT_FAMILY, nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, LOCALE, textFormatButtonText.ReleaseAndGetAddressOf());
	textFormatButtonText->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
	textFormatButtonText->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

	// SuccessMessage Text
	writeFactory->CreateTextFormat(ARIAL_FONT_FAMILY, nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, LOCALE, textFormatSuccessMessage.ReleaseAndGetAddressOf());
	textFormatSuccessMessage->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
	textFormatSuccessMessage->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

	// ErrorMessage Message
	writeFactory->CreateTextFormat(ARIAL_FONT_FAMILY, nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, LOCALE, textFormatErrorMessage.ReleaseAndGetAddressOf());
	textFormatErrorMessage->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
	textFormatErrorMessage->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

	// UITextWindow
	writeFactory->CreateTextFormat(ARIAL_FONT_FAMILY, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, LOCALE, textFormatTextWindow.ReleaseAndGetAddressOf());
	textFormatTextWindow->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
	textFormatTextWindow->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

	// UITextWindow inactive
	writeFactory->CreateTextFormat(ARIAL_FONT_FAMILY, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_ITALIC, DWRITE_FONT_STRETCH_NORMAL, 14.0f, LOCALE, textFormatTextWindowInactive.ReleaseAndGetAddressOf());
	textFormatTextWindowInactive->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
	textFormatTextWindowInactive->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

	// Tooltip Title
	writeFactory->CreateTextFormat(ARIAL_FONT_FAMILY, nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16.0f, LOCALE, textFormatTooltipTitle.ReleaseAndGetAddressOf());
	textFormatTooltipTitle->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
	textFormatTooltipTitle->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

	// Tooltip Description
	writeFactory->CreateTextFormat(ARIAL_FONT_FAMILY, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, LOCALE, textFormatTooltipDescription.ReleaseAndGetAddressOf());
	textFormatTooltipDescription->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
	textFormatTooltipDescription->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

	writeFactory->CreateTextFormat(ARIAL_FONT_FAMILY, nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, LOCALE, textFormat_size12_leading_centered_bold.ReleaseAndGetAddressOf());
	textFormat_size12_leading_centered_bold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
	textFormat_size12_leading_centered_bold->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

	writeFactory->CreateTextFormat(ARIAL_FONT_FAMILY, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, LOCALE, textFormat_size12_leading_centered.ReleaseAndGetAddressOf());
	textFormat_size12_leading_centered->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
	textFormat_size12_leading_centered->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

void Game::InitializeShaders()
{
	auto d3dDevice = deviceResources->GetD3DDevice();

	vertexShaderBuffer = LoadShader(L"VertexShader.cso");
	d3dDevice->CreateVertexShader(vertexShaderBuffer.buffer, vertexShaderBuffer.size, nullptr, vertexShader.ReleaseAndGetAddressOf());

	pixelShaderBuffer = LoadShader(L"PixelShader.cso");
	d3dDevice->CreatePixelShader(pixelShaderBuffer.buffer, pixelShaderBuffer.size, nullptr, pixelShader.ReleaseAndGetAddressOf());

	spriteVertexShaderBuffer = LoadShader(L"SpriteVertexShader.cso");
	d3dDevice->CreateVertexShader(spriteVertexShaderBuffer.buffer, spriteVertexShaderBuffer.size, nullptr, spriteVertexShader.ReleaseAndGetAddressOf());

	spritePixelShaderBuffer = LoadShader(L"SpritePixelShader.cso");
	d3dDevice->CreatePixelShader(spritePixelShaderBuffer.buffer, spritePixelShaderBuffer.size, nullptr, spritePixelShader.ReleaseAndGetAddressOf());
}

void Game::InitializeBuffers()
{
	auto d3dDevice = deviceResources->GetD3DDevice();
	auto d3dContext = deviceResources->GetD3DDeviceContext();

	// create constant buffer
	D3D11_BUFFER_DESC bufferDesc;
	ZeroMemory(&bufferDesc, sizeof(bufferDesc));
	bufferDesc.MiscFlags = 0;
	bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bufferDesc.ByteWidth = sizeof(ConstantBufferPerFrame);

	DX::ThrowIfFailed(d3dDevice->CreateBuffer(&bufferDesc, nullptr, constantBufferPerFrame.ReleaseAndGetAddressOf()));
}

void Game::InitializeRasterStates()
{
	auto d3dDevice{ deviceResources->GetD3DDevice() };

	CD3D11_RASTERIZER_DESC wireframeRasterStateDesc{ D3D11_FILL_WIREFRAME, D3D11_CULL_BACK, FALSE,
		D3D11_DEFAULT_DEPTH_BIAS, D3D11_DEFAULT_DEPTH_BIAS_CLAMP,
		D3D11_DEFAULT_SLOPE_SCALED_DEPTH_BIAS, TRUE, FALSE, TRUE, FALSE };
	d3dDevice->CreateRasterizerState(&wireframeRasterStateDesc, wireframeRasterState.ReleaseAndGetAddressOf());

	CD3D11_RASTERIZER_DESC solidRasterStateDesc{ D3D11_FILL_SOLID, D3D11_CULL_BACK, FALSE,
		D3D11_DEFAULT_DEPTH_BIAS, D3D11_DEFAULT_DEPTH_BIAS_CLAMP,
		D3D11_DEFAULT_SLOPE_SCALED_DEPTH_BIAS, TRUE, FALSE, TRUE, FALSE };
	d3dDevice->CreateRasterizerState(&solidRasterStateDesc, solidRasterState.ReleaseAndGetAddressOf());
}

void Game::InitializeTextures()
{
	auto d3dDevice = deviceResources->GetD3DDevice();

	const std::vector<const wchar_t*> paths
	{
		L"../../WrenClient/Textures/texture01.dds",     // 0
		L"../../WrenClient/Textures/texture02.dds",     // 1
		L"../../WrenClient/Textures/grass01.dds",       // 2
		L"../../WrenClient/Textures/abilityicon01.dds", // 3
		L"../../WrenClient/Textures/texture03.dds",     // 4
		L"../../WrenClient/Textures/abilityicon02.dds", // 5
		L"../../WrenClient/Textures/abilityicon03.dds", // 6
		L"../../WrenClient/Textures/jade.dds",          // 7
		L"../../WrenClient/Textures/jade_gray.dds",     // 8
		L"../../WrenClient/Textures/ruby.dds",          // 9
		L"../../WrenClient/Textures/ruby_gray.dds",     // 10
		L"../../WrenClient/Textures/sapphire.dds",      // 11
		L"../../WrenClient/Textures/sapphire_gray.dds", // 12
		L"../../WrenClient/Textures/white.dds",         // 13
	};

	// clear calls the destructor of its elements, and ComPtr's destructor handles calling Release()
	textures.clear();

	for (auto i = 0; i < paths.size(); i++)
	{
		ComPtr<ID3D11ShaderResourceView> ptr;
		CreateDDSTextureFromFile(d3dDevice, paths.at(i), nullptr, ptr.ReleaseAndGetAddressOf());
		textures.push_back(ptr);
	}
}

void Game::InitializeMeshes()
{
	auto d3dDevice = deviceResources->GetD3DDevice();

	const std::vector<std::string> paths
	{
		"../../WrenClient/Models/sphere.blend",  // 0
		"../../WrenClient/Models/tree.blend",    // 1
		"../../WrenClient/Models/dummy.blend"    // 2
	};

	// clear calls the destructor of its elements, and unique_ptr's destructor handles cleaning itself up
	meshes.clear();

	for (auto i = 0; i < paths.size(); i++)
		meshes.push_back(std::make_unique<Mesh>(paths.at(i), d3dDevice, vertexShaderBuffer.buffer, vertexShaderBuffer.size));
}

void Game::InitializeInputs()
{
	loginScreen_accountNameInput->Initialize(blackBrush.Get(), whiteBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatAccountCredsInputValue.Get(), textFormatAccountCreds.Get());
	loginScreen_passwordInput->Initialize(blackBrush.Get(), whiteBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatAccountCredsInputValue.Get(), textFormatAccountCreds.Get());
	createAccount_accountNameInput->Initialize(blackBrush.Get(), whiteBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatAccountCredsInputValue.Get(), textFormatAccountCreds.Get());
	createAccount_passwordInput->Initialize(blackBrush.Get(), whiteBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatAccountCredsInputValue.Get(), textFormatAccountCreds.Get());
	createCharacter_characterNameInput->Initialize(blackBrush.Get(), whiteBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatAccountCredsInputValue.Get(), textFormatAccountCreds.Get());

	// initialize panel inputs
	gameEditorPanelDirectionalLightColorRInput->Initialize(blackBrush.Get(), whiteBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormat_size12_leading_centered.Get(), textFormat_size12_leading_centered_bold.Get());
	gameEditorPanelDirectionalLightColorGInput->Initialize(blackBrush.Get(), whiteBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormat_size12_leading_centered.Get(), textFormat_size12_leading_centered_bold.Get());
	gameEditorPanelDirectionalLightColorBInput->Initialize(blackBrush.Get(), whiteBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormat_size12_leading_centered.Get(), textFormat_size12_leading_centered_bold.Get());
	gameEditorPanelDirectionalLightXPosInput->Initialize(blackBrush.Get(), whiteBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormat_size12_leading_centered.Get(), textFormat_size12_leading_centered_bold.Get());
	gameEditorPanelDirectionalLightYPosInput->Initialize(blackBrush.Get(), whiteBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormat_size12_leading_centered.Get(), textFormat_size12_leading_centered_bold.Get());
	gameEditorPanelDirectionalLightZPosInput->Initialize(blackBrush.Get(), whiteBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormat_size12_leading_centered.Get(), textFormat_size12_leading_centered_bold.Get());
	gameEditorPanelDirectionalLightIntensityInput->Initialize(blackBrush.Get(), whiteBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormat_size12_leading_centered.Get(), textFormat_size12_leading_centered_bold.Get());
	gameEditorPanelAmbientLightIntensityInput->Initialize(blackBrush.Get(), whiteBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormat_size12_leading_centered.Get(), textFormat_size12_leading_centered_bold.Get());
}

void Game::InitializeButtons()
{
	loginScreen_loginButton->Initialize(blueBrush.Get(), darkBlueBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatButtonText.Get());
	loginScreen_createAccountButton->Initialize(blueBrush.Get(), darkBlueBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatButtonText.Get());
	loginScreen_quitGameButton->Initialize(blueBrush.Get(), darkBlueBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatButtonText.Get());
	createAccount_createAccountButton->Initialize(blueBrush.Get(), darkBlueBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatButtonText.Get());
	createAccount_cancelButton->Initialize(blueBrush.Get(), darkBlueBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatButtonText.Get());
	characterSelect_newCharacterButton->Initialize(blueBrush.Get(), darkBlueBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatButtonText.Get());
	characterSelect_enterWorldButton->Initialize(blueBrush.Get(), darkBlueBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatButtonText.Get());
	characterSelect_deleteCharacterButton->Initialize(blueBrush.Get(), darkBlueBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatButtonText.Get());
	characterSelect_logoutButton->Initialize(blueBrush.Get(), darkBlueBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatButtonText.Get());
	createCharacter_createCharacterButton->Initialize(blueBrush.Get(), darkBlueBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatButtonText.Get());
	createCharacter_backButton->Initialize(blueBrush.Get(), darkBlueBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatButtonText.Get());
	deleteCharacter_confirmButton->Initialize(blueBrush.Get(), darkBlueBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatButtonText.Get());
	deleteCharacter_cancelButton->Initialize(blueBrush.Get(), darkBlueBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatButtonText.Get());

	// panels
	gameSettings_logoutButton->Initialize(blueBrush.Get(), darkBlueBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatButtonText.Get());
}

void Game::InitializeLabels()
{
	loginScreen_successMessageLabel->Initialize(successMessageBrush.Get(), textFormatSuccessMessage.Get());
	loginScreen_errorMessageLabel->Initialize(errorMessageBrush.Get(), textFormatErrorMessage.Get());
	createAccount_errorMessageLabel->Initialize(errorMessageBrush.Get(), textFormatErrorMessage.Get());
	connecting_statusLabel->Initialize(blackBrush.Get(), textFormatAccountCreds.Get());
	connecting_statusLabel->SetText("Connecting...");
	characterSelect_successMessageLabel->Initialize(successMessageBrush.Get(), textFormatSuccessMessage.Get());
	characterSelect_errorMessageLabel->Initialize(errorMessageBrush.Get(), textFormatErrorMessage.Get());
	characterSelect_headerLabel->Initialize(blackBrush.Get(), textFormatHeaders.Get());
	characterSelect_headerLabel->SetText("Character List:");
	createCharacter_errorMessageLabel->Initialize(errorMessageBrush.Get(), textFormatErrorMessage.Get());
	deleteCharacter_headerLabel->Initialize(errorMessageBrush.Get(), textFormatErrorMessage.Get());
	deleteCharacter_headerLabel->SetText("Are you sure you want to delete this character?");
	enteringWorld_statusLabel->Initialize(blackBrush.Get(), textFormatAccountCreds.Get());
	enteringWorld_statusLabel->SetText("Entering World...");

	// labels in panels
	gameSettingsPanelHeader->Initialize(blackBrush.Get(), textFormatHeaders.Get());
	gameSettingsPanelHeader->SetText("Game Settings");
	gameEditorPanelHeader->Initialize(blackBrush.Get(), textFormatHeaders.Get());
	gameEditorPanelHeader->SetText("Game Editor");
	gameEditorPanelLightingHeader->Initialize(blackBrush.Get(), textFormatSuccessMessage.Get());
	gameEditorPanelLightingHeader->SetText("Lighting");
	diagnosticsPanelHeader->Initialize(blackBrush.Get(), textFormatHeaders.Get());
	diagnosticsPanelHeader->SetText("Diagnostics");
	mousePosLabel->Initialize(blackBrush.Get(), textFormatFPS.Get());
	fpsTextLabel->Initialize(blackBrush.Get(), textFormatFPS.Get());
	pingTextLabel->Initialize(blackBrush.Get(), textFormatFPS.Get());
	skillsPanelHeader->Initialize(blackBrush.Get(), textFormatHeaders.Get());
	skillsPanelHeader->SetText("Skills");
	abilitiesPanelHeader->Initialize(blackBrush.Get(), textFormatHeaders.Get());
	abilitiesPanelHeader->SetText("Abilities");
	lootPanelHeader->Initialize(blackBrush.Get(), textFormatHeaders.Get());
	lootPanelHeader->SetText("Loot");
	inventoryPanelHeader->Initialize(blackBrush.Get(), textFormatHeaders.Get());
	inventoryPanelHeader->SetText("Inventory");
}

void Game::InitializePanels()
{
	gameSettingsPanel->Initialize(darkBlueBrush.Get(), lightGrayBrush.Get(), grayBrush.Get());
	gameEditorPanel->Initialize(darkBlueBrush.Get(), lightGrayBrush.Get(), grayBrush.Get());
	diagnosticsPanel->Initialize(darkBlueBrush.Get(), lightGrayBrush.Get(), grayBrush.Get());
	skillsPanel->Initialize(darkBlueBrush.Get(), lightGrayBrush.Get(), grayBrush.Get());
	abilitiesPanel->Initialize(darkBlueBrush.Get(), lightGrayBrush.Get(), grayBrush.Get());
	lootPanel->Initialize(darkBlueBrush.Get(), lightGrayBrush.Get(), grayBrush.Get());
	inventoryPanel->Initialize(darkBlueBrush.Get(), lightGrayBrush.Get(), grayBrush.Get());
}

void Game::InitializeCharacterListings()
{
	for (auto i = 0; i < characterList.size(); i++)
		characterList.at(i)->Initialize(whiteBrush.Get(), selectedCharacterBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatAccountCredsInputValue.Get());
}

void Game::InitializeStaticObjects()
{
	auto staticObjects = commonRepository.ListStaticObjects();

	for (auto i = 0; i < staticObjects.size(); i++)
	{
		const StaticObject* staticObject = staticObjects.at(i).get();
		const auto pos = staticObject->GetPosition();
		GameObject& gameObject = objectManager.CreateGameObject(pos, XMFLOAT3{ 14.0f, 14.0f, 14.0f }, 0.0f, GameObjectType::StaticObject, staticObject->GetName(), staticObject->GetId(), true);
		const auto gameObjectId = gameObject.GetId();

		const RenderComponent& renderComponent = renderComponentManager.CreateRenderComponent(gameObjectId, meshes.at(staticObject->GetModelId()).get(), vertexShader.Get(), pixelShader.Get(), textures.at(staticObject->GetTextureId()).Get());
		gameObject.renderComponentId = renderComponent.GetId();

		const StatsComponent& statsComponent = statsComponentManager.CreateStatsComponent(gameObjectId, 100, 100, 100, 100, 100, 100, 10, 10, 10, 10, 10, 10, 10);
		gameObject.statsComponentId = statsComponent.GetId();
		gameMap.SetTileOccupied(gameObject.localPosition, true);
	}
}

void Game::PublishEvents()
{
	std::sort(uiComponents.begin(), uiComponents.end(), CompareUIComponents);
	std::queue<std::unique_ptr<const Event>>& eventQueue = eventHandler.GetEventQueue();
	while (!eventQueue.empty())
	{
		auto event = std::move(eventQueue.front());
		eventQueue.pop();

		// We pass events to the UIComponents first, because those are usually overlaid on top
		//   of 3D GameObjects, and therefore we want certain events like clicks, etc to hit
		//   the UIComponents first.
		// These UIComponents are sorted in ascending order of their z-index.
		auto stopPropagation = false;

		for (auto i = (int)uiComponents.size() - 1; i >= 0; i--)
		{
			stopPropagation = uiComponents.at(i)->HandleEvent(event.get());
			if (stopPropagation)
				break;
		}

		// There are times where we want to avoid having events propagate to GameObjects if they're
		//   handled by a UIComponent, so if any UIComponent returns true, we skip passing that event
		//   to the GameObjects and move to the next iteration of the while loop.
		if (stopPropagation)
			continue;

		std::list<Observer*>& observers = eventHandler.GetObservers();
		for (auto observer : observers)
		{
			stopPropagation = observer->HandleEvent(event.get());
			if (stopPropagation)
				break;
		}
	}
}

void Game::Tick()
{
	timer.Tick();

	socketManager.ProcessPackets();

	// to get an accurate ping, we should handle this outside of the main update loop which is locked at 60 updates / second
	if (activeLayer == InGame)
	{
		if (pingStart == 0.0f)
		{
			pingStart = timer.TotalTime();

			std::vector<std::string> args{ std::to_string(pingId) };
			socketManager.SendPacket(OpCode::Ping, args);
		}
	}

	// reset double click timer if necessary
	if (timer.TotalTime() - doubleClickStart > 0.5f)
		doubleClickStart = 0.0f;
	
	updateTimer += timer.DeltaTime();
	if (updateTimer >= UPDATE_FREQUENCY)
	{
		if (activeLayer == InGame)
		{
			camera.Update(player->GetWorldPosition(), UPDATE_FREQUENCY);
			
			textWindow->Update(); // this should be handled by objectManager.Update()...
			objectManager.Update();
		}
		
		PublishEvents();

		updateTimer -= UPDATE_FREQUENCY;
	}
	
	Render(updateTimer);
}

void Game::Render(const float updateTimer)
{
	// Don't try to render anything before the first Update.
	if (timer.TotalTime() == 0)
		return;

	float r, g, b, x, y, z, directionalIntensity, ambientIntensity;
	try
	{
		r = std::stof(gameEditorPanelDirectionalLightColorRInput->GetInputValue());
		g = std::stof(gameEditorPanelDirectionalLightColorGInput->GetInputValue());
		b = std::stof(gameEditorPanelDirectionalLightColorBInput->GetInputValue());
		x = std::stof(gameEditorPanelDirectionalLightXPosInput->GetInputValue());
		y = std::stof(gameEditorPanelDirectionalLightYPosInput->GetInputValue());
		z = std::stof(gameEditorPanelDirectionalLightZPosInput->GetInputValue());
		directionalIntensity = std::stof(gameEditorPanelDirectionalLightIntensityInput->GetInputValue());
		ambientIntensity = std::stof(gameEditorPanelAmbientLightIntensityInput->GetInputValue());
	}
	catch (std::exception e)
	{
		r = 1.0f;
		g = 1.0f;
		b = 1.0f;
		x = 0.0f;
		y = -1.0f;
		z = 0.5f;
		directionalIntensity = 1.0f;
		ambientIntensity = 1.0f;
	}

	// set constant buffers
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	auto d3dContext = deviceResources->GetD3DDeviceContext();
	d3dContext->Map(constantBufferPerFrame.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	auto pCB{ reinterpret_cast<ConstantBufferPerFrame*>(mappedResource.pData) };
	XMStoreFloat4(&pCB->directionalLightPos, XMVECTOR{ x, y, z, 0.0f });
	XMStoreFloat3(&pCB->directionalLightColor, XMVECTOR{ r, g, b });
	pCB->directionalLightIntensity = directionalIntensity;
	pCB->ambientIntensity = ambientIntensity;
	d3dContext->Unmap(constantBufferPerFrame.Get(), 0);
	d3dContext->PSSetConstantBuffers(0, 1, constantBufferPerFrame.GetAddressOf());

	Clear();

	auto d2dContext = deviceResources->GetD2DDeviceContext();

	d2dContext->BeginDraw();

	// used for FPS text
	static int frameCnt = 0;
	static float timeElapsed = 0.0f;
	frameCnt++;
	
	// update FPS text
	if (timer.TotalTime() - timeElapsed >= 1)
	{
		const float mspf = 1000.0f / frameCnt;
		const auto fpsText = "FPS: " + std::to_string(frameCnt) + ", MSPF: " + std::to_string(mspf);
		fpsTextLabel->SetText(fpsText.c_str());

		frameCnt = 0;
		timeElapsed += 1.0f;

		if (socketManager.Connected())
			socketManager.SendPacket(OpCode::Heartbeat);
	}

	// update ping
	const auto pingText = "Ping: " + std::to_string(ping) + " ms";
	pingTextLabel->SetText(pingText.c_str());

	// update MousePos text
	const auto mousePosText = "MousePosX: " + std::to_string((int)g_mousePosX) + ", MousePosY: " + std::to_string((int)g_mousePosY);
	mousePosLabel->SetText(mousePosText.c_str());

	if (activeLayer == InGame)
	{
		const auto camPos{ camera.GetPosition() };
		const XMVECTORF32 s_Eye{ camPos.x, camPos.y, camPos.z, 0.0f };
		const XMVECTORF32 s_At{ camPos.x - 500.0f, 0.0f, camPos.z + 500.0f, 0.0f };
		const XMVECTORF32 s_Up{ 0.0f, 1.0f, 0.0f, 0.0f };
		viewTransform = XMMatrixLookAtLH(s_Eye, s_At, s_Up);

		d3dContext->RSSetState(solidRasterState.Get());
		//d3dContext->RSSetState(wireframeRasterState);

		gameMapRenderComponent->Draw(d3dContext, viewTransform, g_projectionTransform);

		renderComponentManager.Update(d3dContext, viewTransform, g_projectionTransform, updateTimer);
	}

	// foreach RenderComponent -> Draw
	for (auto i = 0; i < uiComponents.size(); i++)
		uiComponents.at(i)->Draw();

	d2dContext->EndDraw();

	d3dContext->ResolveSubresource(deviceResources->GetBackBufferRenderTarget(), 0, deviceResources->GetOffscreenRenderTarget(), 0, DXGI_FORMAT_B8G8R8A8_UNORM);

	// Show the new frame.
	deviceResources->Present();
}

void Game::Clear()
{
	// Clear the views.
	auto context = deviceResources->GetD3DDeviceContext();
	auto renderTarget = deviceResources->GetOffscreenRenderTargetView();
	auto depthStencil = deviceResources->GetDepthStencilView();

	context->ClearRenderTargetView(renderTarget, Colors::CornflowerBlue);
	context->ClearDepthStencilView(depthStencil, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	context->OMSetRenderTargets(1, &renderTarget, depthStencil);

	// Set the viewport.
	const auto viewport = deviceResources->GetScreenViewport();
	context->RSSetViewports(1, &viewport);
}

ShaderBuffer Game::LoadShader(const std::wstring filename)
{
	// load precompiled shaders from .cso objects
	ShaderBuffer sb{ nullptr, 0 };
	byte* fileData{ nullptr };

	// open the file
	std::ifstream csoFile(filename, std::ios::in | std::ios::binary | std::ios::ate);

	if (csoFile.is_open())
	{
		// get shader size
		sb.size = (unsigned int)csoFile.tellg();

		// collect shader data
		fileData = new byte[sb.size];
		csoFile.seekg(0, std::ios::beg);
		csoFile.read(reinterpret_cast<char*>(fileData), sb.size);
		csoFile.close();
		sb.buffer = fileData;
	}
	else
		throw std::exception("Critical error: Unable to open the compiled shader object!");

	return sb;
}

UICharacterListing* Game::GetCurrentlySelectedCharacterListing()
{
	for (auto i = 0; i < characterList.size(); i++)
	{
		if (characterList.at(i)->IsSelected())
			return characterList.at(i).get();
	}

	return nullptr;
}

void Game::SetActiveLayer(const Layer layer)
{
	activeLayer = layer;

	std::unique_ptr<Event> e = std::make_unique<ChangeActiveLayerEvent>(layer);
	eventHandler.QueueEvent(e);
}

void Game::OnActivated()
{
	// TODO: Game is becoming active window.
}

void Game::OnDeactivated()
{
	// TODO: Game is becoming background window.
}

void Game::OnSuspending()
{
	// TODO: Game is being power-suspended (or minimized).
}

void Game::OnResuming()
{
	timer.Reset();

	// TODO: Game is being power-resumed (or returning from minimize).
}

void Game::OnWindowMoved()
{
	const auto r = deviceResources->GetOutputSize();
	deviceResources->WindowSizeChanged(r.right, r.bottom);
}

void Game::OnWindowSizeChanged(int width, int height)
{
	if (!deviceResources->WindowSizeChanged(width, height))
		return;

	g_clientWidth = static_cast<float>(width);
	g_clientHeight = static_cast<float>(height);

	CreateWindowSizeDependentResources();
}

void Game::OnDeviceLost()
{
	// TODO: Add Direct3D resource cleanup here.
	// may not be needed - classes should be designed with RAII in mind, and clean up
	// after themselves. not sure if there is an exception to this with D3D,
	// so i'm leaving this stubbed out for now.
}

void Game::OnDeviceRestored()
{
	CreateDeviceDependentResources();

	CreateWindowSizeDependentResources();

	SetActiveLayer(activeLayer);
}

void Game::OnPong(unsigned int pingId)
{
	const auto delta = timer.TotalTime() - pingStart;
	ping = static_cast<int>(std::round(delta * 1000));
	pingStart = 0.0f;
	this->pingId++;
}

const bool Game::HandleEvent(const Event* const event)
{
	const auto fun = eventHandlers[event->type];
	if (fun)
		fun(event);

	return false;
}

void Game::CreateEventHandlers()
{
	eventHandlers[EventType::RightMouseDown] = [this](const Event* const event)
	{
		if (activeLayer != Layer::InGame)
			return;

		const auto derivedEvent = (MouseEvent*)event;

		const auto dir = Utility::MousePosToDirection(g_clientWidth, g_clientHeight, derivedEvent->mousePosX, derivedEvent->mousePosY);
		std::vector<std::string> args{ std::to_string(dir.x), std::to_string(dir.y), std::to_string(dir.z) };
		socketManager.SendPacket(OpCode::PlayerRightMouseDown, args);

		rightMouseDownDir = dir;
	};

	eventHandlers[EventType::RightMouseUp] = [this](const Event* const event)
	{
		if (activeLayer != Layer::InGame)
			return;

		socketManager.SendPacket(OpCode::PlayerRightMouseUp);

		rightMouseDownDir = VEC_ZERO;
	};

	eventHandlers[EventType::MouseMove] = [this](const Event* const event)
	{
		const auto derivedEvent = (MouseEvent*)event;

		g_mousePosX = derivedEvent->mousePosX;
		g_mousePosY = derivedEvent->mousePosY;

		if (activeLayer == Layer::InGame && rightMouseDownDir != VEC_ZERO)
		{
			const auto dir = Utility::MousePosToDirection(g_clientWidth, g_clientHeight, derivedEvent->mousePosX, derivedEvent->mousePosY);
			if (dir != rightMouseDownDir)
			{
				std::vector<std::string> args{ std::to_string(dir.x), std::to_string(dir.y), std::to_string(dir.z) };
				socketManager.SendPacket(OpCode::PlayerRightMouseDirChange, args);
				rightMouseDownDir = dir;
			}
		}
	};

	eventHandlers[EventType::CreateAccountFailed] = [this](const Event* const event)
	{
		const auto derivedEvent = (CreateAccountFailedEvent*)event;

		createAccount_errorMessageLabel->SetText(("Failed to create account. Reason: " + derivedEvent->error).c_str());
	};

	eventHandlers[EventType::CreateAccountSuccess] = [this](const Event* const event)
	{
		createAccount_accountNameInput->ClearInput();
		createAccount_passwordInput->ClearInput();

		createAccount_errorMessageLabel->SetText("");
		loginScreen_successMessageLabel->SetText("Account created successfully.");
		SetActiveLayer(Login);
	};

	eventHandlers[EventType::LoginFailed] = [this](const Event* const event)
	{
		const auto derivedEvent = (LoginFailedEvent*)event;

		loginScreen_errorMessageLabel->SetText(("Login failed. Reason: " + derivedEvent->error).c_str());
		SetActiveLayer(Login);
	};

	eventHandlers[EventType::LoginSuccess] = [this](const Event* const event)
	{
		const auto derivedEvent = (LoginSuccessEvent*)event;

		loginScreen_accountNameInput->ClearInput();
		loginScreen_passwordInput->ClearInput();

		CreateCharacterListings(derivedEvent->characterList);
		InitializeCharacterListings();
		SetActiveLayer(CharacterSelect);
	};

	eventHandlers[EventType::CreateCharacterFailed] = [this](const Event* const event)
	{
		const auto derivedEvent = (CreateCharacterFailedEvent*)event;

		createCharacter_errorMessageLabel->SetText(("Character creation failed. Reason: " + derivedEvent->error).c_str());
	};

	eventHandlers[EventType::CreateCharacterSuccess] = [this](const Event* const event)
	{
		const auto derivedEvent = (CreateCharacterSuccessEvent*)event;

		createCharacter_characterNameInput->ClearInput();

		CreateCharacterListings(derivedEvent->characterList);
		InitializeCharacterListings();
		createCharacter_errorMessageLabel->SetText("");
		characterSelect_successMessageLabel->SetText("Character created successfully.");
		SetActiveLayer(CharacterSelect);
	};

	eventHandlers[EventType::DeleteCharacterSuccess] = [this](const Event* const event)
	{
		const auto derivedEvent = (DeleteCharacterSuccessEvent*)event;

		CreateCharacterListings(derivedEvent->characterList);
		InitializeCharacterListings();
		characterNamePendingDeletion = "";
		createCharacter_errorMessageLabel->SetText("");
		characterSelect_successMessageLabel->SetText("Character deleted successfully.");
		SetActiveLayer(CharacterSelect);
	};

	eventHandlers[EventType::EnterWorldSuccess] = [this](const Event* const event)
	{
		const auto derivedEvent = (EnterWorldSuccessEvent*)event;

		const auto agility = derivedEvent->agility;
		const auto strength = derivedEvent->strength;
		const auto wisdom = derivedEvent->wisdom;
		const auto intelligence = derivedEvent->intelligence;
		const auto charisma = derivedEvent->charisma;
		const auto luck = derivedEvent->luck;
		const auto endurance = derivedEvent->endurance;
		const auto health = derivedEvent->health;
		const auto maxHealth = derivedEvent->maxHealth;
		const auto mana = derivedEvent->mana;
		const auto maxMana = derivedEvent->maxMana;
		const auto stamina = derivedEvent->stamina;
		const auto maxStamina = derivedEvent->maxStamina;

		GameObject& player = objectManager.CreateGameObject(derivedEvent->position, XMFLOAT3{ 14.0f, 14.0f, 14.0f }, PLAYER_SPEED, GameObjectType::Player, derivedEvent->name, derivedEvent->accountId);
		const auto playerId = player.GetId();
		this->player = &player;

		clientSettingsManager = std::make_unique<ClientSettingsManager>(*this, player.name);
		clientSettingsManager->LoadClientSettings();

		RenderComponent& sphereRenderComponent = renderComponentManager.CreateRenderComponent(playerId, meshes[derivedEvent->modelId].get(), vertexShader.Get(), pixelShader.Get(), textures[derivedEvent->textureId].Get());
		player.renderComponentId = sphereRenderComponent.GetId();
		
		StatsComponent& statsComponent = statsComponentManager.CreateStatsComponent(playerId, agility, strength, wisdom, intelligence, charisma, luck, endurance, health, maxHealth, mana, maxMana, stamina, maxStamina);
		player.statsComponentId = statsComponent.GetId();

		InventoryComponent& inventoryComponent = inventoryComponentManager.CreateInventoryComponent(playerId);
		player.inventoryComponentId = inventoryComponent.GetId();

		gameMap.SetTileOccupied(player.localPosition, true);

		if (skills.size() > 0)
			skills.clear();
		skills = std::move(derivedEvent->skills);
		skillsContainer->SetSkills(&skills);
		
		if (abilities.size() > 0)
			abilities.clear();
		abilities = std::move(derivedEvent->abilities);
		abilitiesContainer->SetAbilities(&abilities);

		const std::vector<int>& savedUIAbilities = clientSettingsManager->GetUIAbilityIds();

		for (auto i = 0; i < 10; i++)
		{
			const auto abilityId = savedUIAbilities.at(i);
			if (abilityId >= 0)
				hotbar->CreateUIAbilityAtIndex(abilitiesContainer->GetUIAbilityById(abilityId), i);
		}

		inventory->playerId = player.GetId();

		const auto onClickCreateAccountCancelButton = [this]()
		{
			createAccount_errorMessageLabel->SetText("");
			SetActiveLayer(Login);
		};

		// init characterHUD
		characterHUD = std::make_unique<UICharacterHUD>(UIComponentArgs{ deviceResources.get(), uiComponents, [](const float, const float) { return XMFLOAT2{ 10.0f, 12.0f }; }, InGame, 0 }, statsComponent, derivedEvent->name.c_str());
		characterHUD->Initialize(textFormatSuccessMessage.Get(), healthBrush.Get(), manaBrush.Get(), staminaBrush.Get(), statBackgroundBrush.Get(), blackBrush.Get(), blackBrush.Get(), whiteBrush.Get());

		std::sort(uiComponents.begin(), uiComponents.end(), CompareUIComponents);

		textWindow->AddMessage("Welcome to Wren!");

		SetActiveLayer(InGame);
	};

	eventHandlers[EventType::NpcUpdate] = [this](const Event* const event)
	{
		const auto derivedEvent = (NpcUpdateEvent*)event;

		const auto gameObjectId = derivedEvent->gameObjectId;
		const auto type = derivedEvent->type;
		const auto pos = derivedEvent->pos;
		const auto mov = derivedEvent->mov;
		const auto agility = derivedEvent->agility;
		const auto strength = derivedEvent->strength;
		const auto wisdom = derivedEvent->wisdom;
		const auto intelligence = derivedEvent->intelligence;
		const auto charisma = derivedEvent->charisma;
		const auto luck = derivedEvent->luck;
		const auto endurance = derivedEvent->endurance;
		const auto health = derivedEvent->health;
		const auto maxHealth = derivedEvent->maxHealth;
		const auto mana = derivedEvent->mana;
		const auto maxMana = derivedEvent->maxMana;
		const auto stamina = derivedEvent->stamina;
		const auto maxStamina = derivedEvent->maxStamina;

		auto npc = find_if(npcs.begin(), npcs.end(), [&gameObjectId](std::unique_ptr<Npc>& npc) { return npc->GetId() == gameObjectId; });
		const auto modelId = npc->get()->GetModelId();
		const auto textureId = npc->get()->GetTextureId();
		const auto name = npc->get()->GetName();
		const auto speed = npc->get()->GetSpeed();

		if (!objectManager.GameObjectExists(gameObjectId))
		{
			GameObject& obj = objectManager.CreateGameObject(pos, XMFLOAT3{ 14.0f, 14.0f, 14.0f }, speed, GameObjectType::Npc, name, gameObjectId);
			obj.movementVector = mov;
			const RenderComponent& sphereRenderComponent = renderComponentManager.CreateRenderComponent(gameObjectId, meshes[modelId].get(), vertexShader.Get(), pixelShader.Get(), textures[textureId].Get());
			obj.renderComponentId = sphereRenderComponent.GetId();

			const StatsComponent& statsComponent = statsComponentManager.CreateStatsComponent(gameObjectId, agility, strength, wisdom, intelligence, charisma, luck, endurance, health, maxHealth, mana, maxMana, stamina, maxStamina);
			obj.statsComponentId = statsComponent.GetId();

			const InventoryComponent& inventoryComponent = inventoryComponentManager.CreateInventoryComponent(gameObjectId);
			obj.inventoryComponentId = inventoryComponent.GetId();

			gameMap.SetTileOccupied(obj.localPosition, true);
		}
		else
		{
			GameObject& gameObject = objectManager.GetGameObjectById(derivedEvent->gameObjectId);
			gameObject.localPosition = pos;
			gameObject.movementVector = mov;

			StatsComponent& statsComponent = statsComponentManager.GetComponentById(gameObject.statsComponentId);
			statsComponent.agility = agility;
			statsComponent.strength = strength;
			statsComponent.wisdom = wisdom;
			statsComponent.intelligence = intelligence;
			statsComponent.charisma = charisma;
			statsComponent.luck = luck;
			statsComponent.endurance = endurance;
			statsComponent.health = health;
			statsComponent.maxHealth = maxHealth;
			statsComponent.mana = mana;
			statsComponent.maxMana = maxMana;
			statsComponent.stamina = stamina;
			statsComponent.maxStamina = maxStamina;
		}
	};

	eventHandlers[EventType::PlayerUpdate] = [this](const Event* const event)
	{
		const auto derivedEvent = (PlayerUpdateEvent*)event;

		const auto gameObjectId = derivedEvent->accountId;
		const auto type = derivedEvent->type;
		const auto pos = derivedEvent->pos;
		const auto mov = derivedEvent->mov;
		const auto modelId = derivedEvent->modelId;
		const auto textureId = derivedEvent->textureId;
		const auto name = derivedEvent->name;
		const auto agility = derivedEvent->agility;
		const auto strength = derivedEvent->strength;
		const auto wisdom = derivedEvent->wisdom;
		const auto intelligence = derivedEvent->intelligence;
		const auto charisma = derivedEvent->charisma;
		const auto luck = derivedEvent->luck;
		const auto endurance = derivedEvent->endurance;
		const auto health = derivedEvent->health;
		const auto maxHealth = derivedEvent->maxHealth;
		const auto mana = derivedEvent->mana;
		const auto maxMana = derivedEvent->maxMana;
		const auto stamina = derivedEvent->stamina;
		const auto maxStamina = derivedEvent->maxStamina;

		if (!objectManager.GameObjectExists(gameObjectId))
		{
			GameObject& obj = objectManager.CreateGameObject(pos, XMFLOAT3{ 14.0f, 14.0f, 14.0f }, PLAYER_SPEED, GameObjectType::Player, name, gameObjectId);
			obj.movementVector = mov;
			const RenderComponent& sphereRenderComponent = renderComponentManager.CreateRenderComponent(gameObjectId, meshes.at(modelId).get(), vertexShader.Get(), pixelShader.Get(), textures.at(textureId).Get());
			obj.renderComponentId = sphereRenderComponent.GetId();

			const StatsComponent& statsComponent = statsComponentManager.CreateStatsComponent(gameObjectId, agility, strength, wisdom, intelligence, charisma, luck, endurance, health, maxHealth, mana, maxMana, stamina, maxStamina);
			obj.statsComponentId = statsComponent.GetId();

			const InventoryComponent& inventoryComponent = inventoryComponentManager.CreateInventoryComponent(gameObjectId);
			obj.inventoryComponentId = inventoryComponent.GetId();
		}
		else
		{
			GameObject& obj = objectManager.GetGameObjectById(derivedEvent->accountId);
			obj.localPosition = pos;
			obj.movementVector = mov;

			StatsComponent& statsComponent = statsComponentManager.GetComponentById(obj.statsComponentId);
			statsComponent.agility = agility;
			statsComponent.strength = strength;
			statsComponent.wisdom = wisdom;
			statsComponent.intelligence = intelligence;
			statsComponent.charisma = charisma;
			statsComponent.luck = luck;
			statsComponent.endurance = endurance;
			statsComponent.health = health;
			statsComponent.maxHealth = maxHealth;
			statsComponent.mana = mana;
			statsComponent.maxMana = maxMana;
			statsComponent.stamina = stamina;
			statsComponent.maxStamina = maxStamina;
		}
	};

	eventHandlers[EventType::ActivateAbility] = [this](const Event* const event)
	{
		const auto derivedEvent = (ActivateAbilityEvent*)event;

		std::vector<std::string> args{ std::to_string(derivedEvent->abilityId) };
		socketManager.SendPacket(OpCode::ActivateAbility, args);
	};

	eventHandlers[EventType::ReorderUIComponents] = [this](const Event* const event)
	{
		std::sort(uiComponents.begin(), uiComponents.end(), CompareUIComponents);
	};

	eventHandlers[EventType::LeftMouseDown] = [this](const Event* const event)
	{
		if (activeLayer != Layer::InGame)
			return;

		const auto derivedEvent = (MouseEvent*)event;

		GameObject* clickedGameObject{ nullptr };
		float smallestDist = FLT_MAX;

		auto gameObjects = objectManager.GetGameObjects();
		for (auto j = 0; j < objectManager.GetGameObjectIndex(); j++)
		{
			GameObject& gameObject = gameObjects[j];
			const auto pos = gameObject.GetWorldPosition();
			const auto scale = gameObject.scale;
			const auto worldTransform = XMMatrixScaling(scale.x, scale.y, scale.z) * XMMatrixTranslation(pos.x, pos.y, pos.z);

			XMVECTOR roScreen = XMVectorSet(derivedEvent->mousePosX, derivedEvent->mousePosY, 0.0f, 1.0f);
			XMVECTOR rdScreen = XMVectorSet(derivedEvent->mousePosX, derivedEvent->mousePosY, 1.0f, 1.0f);
			XMVECTOR ro = XMVector3Unproject(roScreen, 0.0f, 0.0f, g_clientWidth, g_clientHeight, 0.0f, 1000.0f, g_projectionTransform, viewTransform, worldTransform);
			XMVECTOR rd = XMVector3Unproject(rdScreen, 0.0f, 0.0f, g_clientWidth, g_clientHeight, 0.0f, 1000.0f, g_projectionTransform, viewTransform, worldTransform);
			rd = XMVector3Normalize(rd - ro);

			RenderComponent& renderComponent = renderComponentManager.GetComponentById(gameObject.renderComponentId);
			auto vertices = renderComponent.mesh->vertices;
			auto indices = renderComponent.mesh->indices;

			auto dist = 0.0f;
			unsigned int i = 0;
			while (i < indices.size())
			{
				auto ver1 = vertices[indices[i]];
				auto ver2 = vertices[indices[i + 1]];
				auto ver3 = vertices[indices[i + 2]];
				XMVECTOR v1 = XMVectorSet(ver1.Position.x, ver1.Position.y, ver1.Position.z, 1.0f);
				XMVECTOR v2 = XMVectorSet(ver2.Position.x, ver2.Position.y, ver2.Position.z, 1.0f);
				XMVECTOR v3 = XMVectorSet(ver3.Position.x, ver3.Position.y, ver3.Position.z, 1.0f);
				auto result = TriangleTests::Intersects(ro, rd, v1, v2, v3, dist);
				if (result)
				{
					if (dist < smallestDist)
					{
						smallestDist = dist;
						clickedGameObject = &gameObject;
					}
				}
				i += 3;
			}
		}
		if (clickedGameObject)
		{
			const auto objectId = clickedGameObject->GetId();
			StatsComponent& statsComponent = statsComponentManager.GetComponentById(clickedGameObject->statsComponentId);
			std::unique_ptr<Event> e = std::make_unique<SetTargetEvent>(objectId, clickedGameObject->name, &statsComponent);
			eventHandler.QueueEvent(e);
			std::vector<std::string> args{ std::to_string(objectId) };
			socketManager.SendPacket(OpCode::SetTarget, args);
		}
		else
		{
			std::unique_ptr<Event> e = std::make_unique<Event>(EventType::UnsetTarget);
			eventHandler.QueueEvent(e);
			socketManager.SendPacket(OpCode::UnsetTarget);
		}

		// check for double click
		if (doubleClickStart == 0.0f)
			doubleClickStart = timer.TotalTime();
		else if (timer.TotalTime() - doubleClickStart <= 0.5f)
		{
			std::unique_ptr<Event> e = std::make_unique<DoubleLeftMouseDownEvent>(g_mousePosX, g_mousePosY, clickedGameObject);
			eventHandler.QueueEvent(e);
			doubleClickStart = 0.0f;
		}
	};

	eventHandlers[EventType::SendChatMessage] = [this](const Event* const event)
	{
		const auto derivedEvent = (SendChatMessage*)event;

		std::vector<std::string> args{ player->name, derivedEvent->message };
		socketManager.SendPacket(OpCode::SendChatMessage, args);
	};

	eventHandlers[EventType::PropagateChatMessage] = [this](const Event* const event)
	{
		const auto derivedEvent = (PropagateChatMessageEvent*)event;
		
		textWindow->AddMessage("(" + derivedEvent->senderName + ") " + derivedEvent->message);
	};

	eventHandlers[EventType::ServerMessage] = [this](const Event* const event)
	{
		const auto derivedEvent = (ServerMessageEvent*)event;

		textWindow->AddMessage(derivedEvent->message);
	};

	eventHandlers[EventType::SkillIncrease] = [this](const Event* const event)
	{
		const auto derivedEvent = (SkillIncreaseEvent*)event;

		// TODO: refactors skills on ClientSide to be somewhere easier to access, use array, etc.
		for (auto i = 0; i < skills.size(); i++)
		{
			auto skill{ skills.at(i).get() };

			if (skill->skillId == derivedEvent->skillId)
			{
				skill->value = derivedEvent->newValue;
				textWindow->AddMessage("Your skill in " + skill->name + " has increased to " + std::to_string(derivedEvent->newValue));
			}
		}
	};

	eventHandlers[EventType::DoubleLeftMouseDown] = [this](const Event* const event)
	{
		const auto derivedEvent = (DoubleLeftMouseDownEvent*)event;
		
		if (derivedEvent->clickedObject && !lootPanel->isVisible)
		{
			const auto clickedObject = derivedEvent->clickedObject;
			const StatsComponent& statsComponent = statsComponentManager.GetComponentById(clickedObject->statsComponentId);

			if (!statsComponent.alive)
				lootPanel->ToggleVisibility();
		}
	};
	eventHandlers[EventType::StartDraggingUIAbility] = [this](const Event* const event)
	{
		g_mouseIsDragging = true;
	};
	eventHandlers[EventType::StartDraggingUIItem] = [this](const Event* const event)
	{
		g_mouseIsDragging = true;
	};
	eventHandlers[EventType::UIAbilityDropped] = [this](const Event* const event)
	{
		g_mouseIsDragging = false;
	};
	eventHandlers[EventType::UIItemDropped] = [this](const Event* const event)
	{
		g_mouseIsDragging = false;
	};
	eventHandlers[EventType::MoveItemSuccess] = [this](const Event* const event)
	{
		const auto derivedEvent = (MoveItemSuccessEvent*)event;

		InventoryComponent& inventoryComponent = inventoryComponentManager.GetComponentById(player->inventoryComponentId);
		inventoryComponent.MoveItem(derivedEvent->draggingSlot, derivedEvent->slot);
	};
	eventHandlers[EventType::SystemKeyDown] = [this](const Event* const event)
	{
		const auto keyDownEvent = (SystemKeyDownEvent*)event;
		const auto keyCode = keyDownEvent->keyCode;

		switch (keyCode)
		{
			case VK_LCONTROL:
			{
				g_leftCtrlHeld = true;
				break;
			}
			case VK_LMENU:
			{
				g_leftAltHeld = true;
				break;
			}
			case VK_LSHIFT:
			{
				g_leftShiftHeld = true;
				break;
			}
		}
	};
	eventHandlers[EventType::SystemKeyUp] = [this](const Event* const event)
	{
		const auto keyDownEvent = (SystemKeyUpEvent*)event;
		const auto keyCode = keyDownEvent->keyCode;

		switch (keyCode)
		{
			case VK_LCONTROL:
			{
				g_leftCtrlHeld = false;
				break;
			}
			case VK_LMENU:
			{
				g_leftAltHeld = false;
				break;
			}
			case VK_LSHIFT:
			{
				g_leftShiftHeld = false;
				break;
			}
		}
	};
}

void Game::QuitGame()
{
	DestroyWindow(deviceResources->GetWindow());
}

Game::~Game()
{
	eventHandler.Unsubscribe(*this);
}