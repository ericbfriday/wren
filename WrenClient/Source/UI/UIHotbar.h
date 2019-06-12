#pragma once

#include "UIComponent.h"
#include "UIAbility.h"
#include "EventHandling/Observer.h"
#include "EventHandling/Events/Event.h"

class UIHotbar : public UIComponent
{
	char draggingIndex{ -1 };
	ComPtr<ID2D1RectangleGeometry> geometry[10];
	UIAbility* uiAbilities[10] = { nullptr };
	ID2D1SolidColorBrush* brush;
	ID2D1DeviceContext1* d2dDeviceContext;
	ID2D1Factory2* d2dFactory;
	float clientHeight;
public:
	UIHotbar(
		std::vector<UIComponent*>& uiComponents,
		const XMFLOAT2 position,
		const Layer uiLayer,
		const unsigned int zIndex,
		ID2D1SolidColorBrush* brush,
		ID2D1DeviceContext1* d2dDeviceContext,
		ID2D1Factory2* d2dFactory,
		const float clientHeight);
	virtual void Draw();
	virtual const bool HandleEvent(const Event* const event);
	virtual const std::string GetUIAbilityDragBehavior() const;
	void DrawSprites();
};