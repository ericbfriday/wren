#include "stdafx.h"
#include "UILootContainer.h"
#include "EventHandling/Events/ChangeActiveLayerEvent.h"
#include "../Events/DoubleLeftMouseDownEvent.h"
#include "../Events/WindowResizeEvent.h"
#include "EventHandling/Events/LootItemSuccessEvent.h"

UILootContainer::UILootContainer(
	UIComponentArgs uiComponentArgs,
	EventHandler& eventHandler,
	ClientSocketManager& socketManager,
	StatsComponentManager& statsComponentManager,
	InventoryComponentManager& inventoryComponentManager,
	std::vector<std::unique_ptr<Item>>& allItems,
	std::vector<ComPtr<ID3D11ShaderResourceView>>& allTextures,
	const float clientWidth,
	const float clientHeight)
	: UIComponent(uiComponentArgs),
	  eventHandler{ eventHandler },
	  socketManager{ socketManager },
	  statsComponentManager{ statsComponentManager },
	  inventoryComponentManager{ inventoryComponentManager },
	  allItems{ allItems },
	  allTextures{ allTextures },
	  clientWidth{ clientWidth },
	  clientHeight{ clientHeight }
{
}

void UILootContainer::Initialize(
	ID2D1SolidColorBrush* brush,
	ID2D1SolidColorBrush* highlightBrush,
	ID3D11VertexShader* vertexShader,
	ID3D11PixelShader* pixelShader,
	const BYTE* vertexShaderBuffer,
	const int vertexShaderSize,
	const XMMATRIX projectionTransform,
	ID2D1SolidColorBrush* bodyBrush,
	ID2D1SolidColorBrush* borderBrush,
	ID2D1SolidColorBrush* textBrush,
	IDWriteTextFormat* textFormatTitle,
	IDWriteTextFormat* textFormatDescription
)
{
	this->brush = brush;
	this->highlightBrush = highlightBrush;
	this->vertexShader = vertexShader;
	this->pixelShader = pixelShader;
	this->vertexShaderBuffer = vertexShaderBuffer;
	this->vertexShaderSize = vertexShaderSize;
	this->projectionTransform = projectionTransform;
	this->bodyBrush = bodyBrush;
	this->borderBrush = borderBrush;
	this->textBrush = textBrush;
	this->textFormatTitle = textFormatTitle;
	this->textFormatDescription = textFormatDescription;
}

void UILootContainer::Draw()
{
	if (!isVisible) return;

	// FIXME: this should be optimized. see comment below in HandleEvent.
	ReinitializeGeometry();

	for (auto i = 0; i < 12; i++)
		deviceResources->GetD2DDeviceContext()->DrawGeometry(geometry[i].Get(), brush, 2.0f);
}

const bool UILootContainer::HandleEvent(const Event* const event)
{
	const auto type = event->type;
	switch (type)
	{
		case EventType::ChangeActiveLayer:
		{
			const auto derivedEvent = (ChangeActiveLayerEvent*)event;

			if (derivedEvent->layer == uiLayer && GetParent() == nullptr)
				isVisible = true;
			else
				isVisible = false;

			break;
		}
		case EventType::DoubleLeftMouseDown:
		{
			const auto derivedEvent = (DoubleLeftMouseDownEvent*)event;

			if (derivedEvent->clickedObject && !isVisible)
			{
				const auto clickedObject = derivedEvent->clickedObject;
				const auto clickedObjectId = clickedObject->GetId();
				const StatsComponent& statsComponent = statsComponentManager.GetComponentById(clickedObject->statsComponentId);
				
				if (!statsComponent.alive && clickedObjectId != currentGameObjectId)
				{
					currentGameObjectId = clickedObjectId;

					// reset state
					items.clear();
					uiItems.clear();
					ClearChildren();

					// initialize UIItems
					const InventoryComponent& inventoryComponent = inventoryComponentManager.GetComponentById(clickedObject->inventoryComponentId);
					for (auto i = 0; i < INVENTORY_SIZE; i++)
					{
						const auto itemId = inventoryComponent.itemIds.at(i);
						if (itemId >= 0)
						{
							const auto item = allItems.at(itemId - 1).get();
							items.push_back(item);

							const auto xOffset = 5.0f + ((i % 3) * 45.0f);
							const auto yOffset = 25.0f + ((i / 3) * 45.0f);
							const auto texture = allTextures.at(item->GetSpriteId()).Get();
							const auto grayTexture = allTextures.at(item->GetGraySpriteId()).Get();
							uiItems.push_back(std::make_unique<UIItem>(UIComponentArgs{ deviceResources, uiComponents, [xOffset, yOffset](const float, const float) { return XMFLOAT2{ xOffset + 2.0f, yOffset + 2.0f }; }, uiLayer, 3 }, eventHandler, socketManager, itemId, item->GetName(), item->GetDescription(), clientWidth, clientHeight));
							uiItems.at(i)->Initialize(vertexShader, pixelShader, texture, grayTexture, highlightBrush, vertexShaderBuffer, vertexShaderSize, projectionTransform, bodyBrush, borderBrush, textBrush, textFormatTitle, textFormatDescription);
							AddChildComponent(*uiItems.at(i).get());
						}
					}

					std::unique_ptr<Event> e = std::make_unique<Event>(EventType::ReorderUIComponents);
					eventHandler.QueueEvent(e);
				}
			}

			break;
		}
		case EventType::LootItemSuccess:
		{
			const auto derivedEvent = (LootItemSuccessEvent*)event;

			if (isVisible && derivedEvent->gameObjectId == currentGameObjectId)
			{
				const auto slot = derivedEvent->slot;

				RemoveChildComponent(uiItems.at(slot).get());
				items.at(slot) = nullptr;
				uiItems.at(slot) = nullptr;
			}
			
			break;
		}
		case EventType::WindowResize:
		{
			const auto derivedEvent = (WindowResizeEvent*)event;

			clientWidth = derivedEvent->width;
			clientHeight = derivedEvent->height;

			break;
		}
		// this is a decent idea to optimize, but it's not working correctly.
		// this component gets the MouseMove event BEFORE the UIPanel.
		// so it reinitializes its geometry based on the UIPanel's old position
		// then the UIPanel gets the MouseMove event and moves its position,
		// making the boxes inside the panel seem to jiggle around.
		/*case EventType::MouseMove:
		{
			const auto parent = static_cast<UIPanel*>(GetParent());
			
			if (parent->GetIsDragging())
				ReinitializeGeometry();

			break;
		}*/
	}

	return false;
}

void UILootContainer::ReinitializeGeometry()
{
	const auto position = GetWorldPosition();
	const auto width = 40.0f;
	auto xOffset = 5.0f;
	auto yOffset = 25.0f;
	for (auto i = 0; i < 4; i++)
	{
		for (auto j = 0; j < 3; j++)
		{
			deviceResources->GetD2DFactory()->CreateRectangleGeometry(D2D1::RectF(position.x + xOffset, position.y + yOffset, position.x + xOffset + width, position.y + yOffset + width), geometry[j + (3 * i)].ReleaseAndGetAddressOf());
			xOffset += 45.0f;
		}

		yOffset += 45.0f;
		xOffset = 5.0f;
	}
}

const std::string UILootContainer::GetUIItemRightClickBehavior() const
{
	return "LOOT";
}