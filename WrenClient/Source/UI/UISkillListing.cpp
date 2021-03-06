#include "stdafx.h"
#include "UISkillListing.h"

using namespace DX;

UISkillListing::UISkillListing(UIComponentArgs uiComponentArgs, WrenCommon::Skill* skill)
	: UIComponent(uiComponentArgs),
	  value{ skill->value },
	  name{ skill->name }
{
}

void UISkillListing::Initialize(ID2D1SolidColorBrush* textBrush, IDWriteTextFormat* textFormat)
{
	this->textBrush = textBrush;
	this->textFormat = textFormat;

	ThrowIfFailed(
		deviceResources->GetWriteFactory()->CreateTextLayout(
			Utility::s2ws(name).c_str(),
			static_cast<unsigned int>(name.size()),
			textFormat,
			140.0f,
			20.0f,
			nameTextLayout.ReleaseAndGetAddressOf()
		)
	);

	CreateValueTextLayout();
}

void UISkillListing::Draw()
{
	if (!isVisible) return;

	const auto d2dDeviceContext = deviceResources->GetD2DDeviceContext();
	const auto position = GetWorldPosition();

	// Draw Name Text
	d2dDeviceContext->DrawTextLayout(D2D1::Point2F(position.x, position.y), nameTextLayout.Get(), textBrush);

	// Draw Value Text
	d2dDeviceContext->DrawTextLayout(D2D1::Point2F(position.x + 160.0f, position.y), valueTextLayout.Get(), textBrush);
}

void UISkillListing::SetValue(const int value)
{
	this->value = value;

	CreateValueTextLayout();
}

void UISkillListing::CreateValueTextLayout()
{
	const auto valueString = std::to_string(value);

	ThrowIfFailed(
		deviceResources->GetWriteFactory()->CreateTextLayout(
			Utility::s2ws(valueString).c_str(),
			static_cast<unsigned int>(valueString.size()),
			textFormat,
			20.0f,
			30.0f,
			valueTextLayout.ReleaseAndGetAddressOf()
		)
	);
}

const bool UISkillListing::HandleEvent(const Event* const event)
{
	// first pass the event to UIComponent base so it can reset localPosition based on new client dimensions
	UIComponent::HandleEvent(event);

	const auto type = event->type;
	switch (type)
	{
		case EventType::ChangeActiveLayer:
		{
			isVisible = false;

			break;
		}
	}

	return false;
}