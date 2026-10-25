#include "CatalogScene.h"

#include "cocos/ui/UIButton.h"
#include "cocos/ui/UILayout.h"
#include "document.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <windows.h>

namespace {
constexpr int kPageSize = 10;
const cocos2d::Color4B kBackground(10, 12, 20, 255);
const cocos2d::Color4B kPanel(20, 24, 38, 255);
const cocos2d::Color4B kPanelRaised(31, 36, 52, 255);
const cocos2d::Color4B kAccent(92, 72, 155, 255);
const cocos2d::Color3B kText(245, 244, 250);
const cocos2d::Color3B kMuted(152, 170, 201);

std::string readCatalogFile() {
    wchar_t executablePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return {};
    std::wstring path(executablePath, length);
    const auto slash = path.find_last_of(L"\\/");
    path.resize(slash == std::wstring::npos ? 0 : slash + 1);
    path += L"features.json";

    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

std::string stringMember(const rapidjson::Value& object, const char* key) {
    const auto found = object.FindMember(key);
    return found != object.MemberEnd() && found->value.IsString()
        ? found->value.GetString() : std::string{};
}

cocos2d::Color4B withAlpha(cocos2d::Color3B color, GLubyte alpha = 255) {
    return {color.r, color.g, color.b, alpha};
}
}

cocos2d::Scene* CatalogScene::createScene() {
    return CatalogScene::create();
}

bool CatalogScene::init() {
    if (!Scene::init() || !loadCatalog()) return false;
    activeCategory_ = categories_.empty() ? std::string{} : categories_.front();
    if (!features_.empty()) selectedId_ = features_.front().id;
    rebuild();
    return true;
}

bool CatalogScene::loadCatalog() {
    const std::string source = readCatalogFile();
    rapidjson::Document document;
    document.Parse(source.c_str());
    if (!document.IsObject() || !document.HasMember("features") || !document["features"].IsArray()) {
        return false;
    }

    for (const auto& value : document["features"].GetArray()) {
        if (!value.IsObject()) continue;
        Feature feature;
        feature.id = stringMember(value, "id");
        feature.name = stringMember(value, "name");
        feature.category = stringMember(value, "category");
        feature.description = stringMember(value, "description");
        feature.status = stringMember(value, "implementation_status");
        feature.activation = stringMember(value, "activation_type");
        feature.risk = stringMember(value, "risk_level");
        if (value.HasMember("compatibility") && value["compatibility"].IsArray()) {
            for (const auto& item : value["compatibility"].GetArray()) {
                if (item.IsString()) feature.compatibility.emplace_back(item.GetString());
            }
        }
        if (feature.id.empty() || feature.name.empty() || feature.category.empty()) continue;
        if (std::find(categories_.begin(), categories_.end(), feature.category) == categories_.end()) {
            categories_.push_back(feature.category);
        }
        features_.push_back(std::move(feature));
    }
    return !features_.empty();
}

std::vector<const CatalogScene::Feature*> CatalogScene::visibleFeatures() const {
    std::vector<const Feature*> visible;
    for (const auto& feature : features_) {
        if (!activeCategory_.empty() && feature.category != activeCategory_) continue;
        if (!activeStatus_.empty() && feature.status != activeStatus_) continue;
        visible.push_back(&feature);
    }
    return visible;
}

const CatalogScene::Feature* CatalogScene::selectedFeature() const {
    const auto found = std::find_if(features_.begin(), features_.end(), [this](const Feature& feature) {
        return feature.id == selectedId_;
    });
    return found == features_.end() ? nullptr : &*found;
}

cocos2d::Color3B CatalogScene::statusColor(const std::string& status) const {
    if (status == "implemented") return cocos2d::Color3B(82, 215, 165);
    if (status == "experimental") return cocos2d::Color3B(187, 139, 255);
    return cocos2d::Color3B(238, 172, 94);
}

cocos2d::Label* CatalogScene::addLabel(const std::string& text, float x, float y, float width,
    int fontSize, cocos2d::Color3B color, bool bold) {
    auto* label = cocos2d::Label::createWithSystemFont(text, bold ? "Segoe UI Semibold" : "Segoe UI", fontSize,
        cocos2d::Size(width, 0), cocos2d::TextHAlignment::LEFT, cocos2d::TextVAlignment::CENTER);
    if (!label) return nullptr;
    label->setAnchorPoint(cocos2d::Vec2(0, 0.5f));
    label->setPosition(x, y);
    label->setColor(color);
    addChild(label);
    return label;
}

void CatalogScene::addPanel(float x, float y, float width, float height, cocos2d::Color4B color) {
    auto* panel = cocos2d::LayerColor::create(color, width, height);
    panel->setAnchorPoint(cocos2d::Vec2::ZERO);
    panel->setPosition(x, y);
    addChild(panel);
}

void CatalogScene::addButton(const std::string& text, float x, float y, float width, float height,
    cocos2d::Color4B fill, const std::function<void()>& onClick, int fontSize) {
    addPanel(x, y, width, height, fill);
    auto* button = cocos2d::ui::Button::create();
    button->setTitleText(text);
    button->setTitleFontName("Segoe UI");
    button->setTitleFontSize(fontSize);
    button->setTitleColor(kText);
    button->setContentSize(cocos2d::Size(width, height));
    button->setAnchorPoint(cocos2d::Vec2::ZERO);
    button->setPosition(cocos2d::Vec2(x, y));
    button->addTouchEventListener([onClick](cocos2d::Ref*, cocos2d::ui::Widget::TouchEventType event) {
        if (event == cocos2d::ui::Widget::TouchEventType::ENDED) onClick();
    });
    addChild(button, 2);
}

void CatalogScene::selectCategory(const std::string& category) {
    activeCategory_ = category;
    page_ = 0;
    const auto visible = visibleFeatures();
    selectedId_ = visible.empty() ? std::string{} : visible.front()->id;
    rebuild();
}

void CatalogScene::selectStatus(const std::string& status) {
    activeStatus_ = status;
    page_ = 0;
    const auto visible = visibleFeatures();
    selectedId_ = visible.empty() ? std::string{} : visible.front()->id;
    rebuild();
}

void CatalogScene::selectFeature(const std::string& id) {
    selectedId_ = id;
    rebuild();
}

void CatalogScene::changePage(int delta) {
    const auto visible = visibleFeatures();
    const int pageCount = std::max(1, static_cast<int>((visible.size() + kPageSize - 1) / kPageSize));
    page_ = std::clamp(page_ + delta, 0, pageCount - 1);
    const int first = page_ * kPageSize;
    if (!visible.empty()) selectedId_ = visible[static_cast<std::size_t>(first)]->id;
    rebuild();
}

void CatalogScene::rebuild() {
    removeAllChildren();
    const auto visibleSize = cocos2d::Director::getInstance()->getVisibleSize();
    const float width = visibleSize.width;
    const float height = visibleSize.height;
    const float margin = 26.0f;
    const float headerHeight = 76.0f;
    const float bodyY = 28.0f;
    const float bodyHeight = height - headerHeight - 44.0f;
    const float navWidth = 246.0f;
    const float detailWidth = 326.0f;
    const float gap = 14.0f;
    const float listX = margin + navWidth + gap;
    const float listWidth = width - listX - detailWidth - gap - margin;
    const float detailX = listX + listWidth + gap;

    addPanel(0, 0, width, height, kBackground);
    addPanel(0, height - headerHeight, width, headerHeight, cocos2d::Color4B(24, 20, 35, 255));
    addPanel(margin, bodyY, navWidth, bodyHeight, kPanel);
    addPanel(listX, bodyY, listWidth, bodyHeight, kPanel);
    addPanel(detailX, bodyY, detailWidth, bodyHeight, kPanel);

    auto* accent = cocos2d::LayerColor::create(cocos2d::Color4B(108, 90, 186, 255), width, 2);
    accent->setAnchorPoint(cocos2d::Vec2::ZERO);
    accent->setPosition(0, height - headerHeight);
    addChild(accent);

    addLabel("GD CLIENT  /  FEATURE CATALOG", margin, height - 32, 650, 22, kText, true);
    addLabel("STANDALONE COCOS2D-X APP     •     CATALOG VIEW ONLY", margin, height - 56, 650, 12, kMuted, true);
    addPanel(width - 188, height - 51, 160, 30, kAccent);
    addLabel("NO GAME CONNECTION", width - 178, height - 36, 145, 12,
        cocos2d::Color3B(235, 230, 255), true);

    addLabel("CATEGORIES", margin + 16, bodyY + bodyHeight - 24, navWidth - 32, 13, kMuted, true);
    for (std::size_t i = 0; i < categories_.size(); ++i) {
        const float buttonY = bodyY + bodyHeight - 65.0f - static_cast<float>(i) * 34.0f;
        if (buttonY < bodyY + 8) break;
        const bool selected = categories_[i] == activeCategory_;
        addButton(categories_[i], margin + 10, buttonY, navWidth - 20, 29,
            selected ? kAccent : cocos2d::Color4B(31, 35, 48, 255),
            [this, category = categories_[i]] { selectCategory(category); }, 12);
    }

    addLabel("FEATURES", listX + 17, bodyY + bodyHeight - 24, listWidth - 34, 15, kText, true);
    const std::vector<std::string> filters{"All", "Implemented", "Experimental", "Not wired"};
    const float filterY = bodyY + bodyHeight - 70.0f;
    const float filterGap = 6.0f;
    const float filterWidth = (listWidth - 28.0f - filterGap * 3.0f) / 4.0f;
    const std::vector<std::string> statuses{"", "implemented", "experimental", "not-wired"};
    for (std::size_t i = 0; i < filters.size(); ++i) {
        const bool selected = activeStatus_ == statuses[i];
        addButton(filters[i], listX + 14.0f + static_cast<float>(i) * (filterWidth + filterGap),
            filterY, filterWidth, 28,
            selected ? kAccent : cocos2d::Color4B(39, 45, 62, 255),
            [this, status = statuses[i]] { selectStatus(status); }, 10);
    }

    const auto visible = visibleFeatures();
    const int pageCount = std::max(1, static_cast<int>((visible.size() + kPageSize - 1) / kPageSize));
    const int first = page_ * kPageSize;
    for (int row = 0; row < kPageSize; ++row) {
        const int index = first + row;
        if (index >= static_cast<int>(visible.size())) break;
        const auto* feature = visible[static_cast<std::size_t>(index)];
        const float rowY = filterY - 51.0f - static_cast<float>(row) * 47.0f;
        const bool selected = feature->id == selectedId_;
        addButton(feature->name, listX + 14, rowY, listWidth - 28, 40,
            selected ? cocos2d::Color4B(59, 41, 74, 255) : cocos2d::Color4B(38, 28, 49, 255),
            [this, id = feature->id] { selectFeature(id); }, 11);
        addLabel(feature->status == "implemented" ? "IMPLEMENTED" :
            feature->status == "experimental" ? "EXPERIMENTAL" : "NOT WIRED",
            listX + listWidth - 134, rowY + 20, 108, 11, statusColor(feature->status), true);
    }

    const float pagerY = bodyY + 12.0f;
    addButton("Previous", listX + 14, pagerY, 92, 30, cocos2d::Color4B(55, 39, 70, 255),
        [this] { changePage(-1); }, 11);
    addLabel("Page " + std::to_string(page_ + 1) + " / " + std::to_string(pageCount),
        listX + 118, pagerY + 15, listWidth - 236, 12, kMuted, false);
    addButton("Next", listX + listWidth - 106, pagerY, 92, 30, cocos2d::Color4B(55, 39, 70, 255),
        [this] { changePage(1); }, 11);

    addLabel("DETAILS", detailX + 17, bodyY + bodyHeight - 24, detailWidth - 34, 14, kMuted, true);
    if (const auto* feature = selectedFeature()) {
        addLabel(feature->name, detailX + 17, bodyY + bodyHeight - 61, detailWidth - 34, 19, kText, true);
        addLabel(feature->category, detailX + 17, bodyY + bodyHeight - 88, detailWidth - 34, 12, kMuted, false);
        addPanel(detailX + 17, bodyY + bodyHeight - 127, 128, 25, withAlpha(statusColor(feature->status)));
        addLabel(feature->status == "implemented" ? "IMPLEMENTED" :
            feature->status == "experimental" ? "EXPERIMENTAL" : "NOT WIRED",
            detailX + 23, bodyY + bodyHeight - 115, 116, 11, kText, true);
        addLabel("ACTIVATION  " + feature->activation, detailX + 17,
            bodyY + bodyHeight - 158, detailWidth - 34, 11, kMuted, true);
        addLabel("RISK  " + feature->risk, detailX + 17,
            bodyY + bodyHeight - 182, detailWidth - 34, 11, kMuted, true);
        auto* description = cocos2d::Label::createWithSystemFont(feature->description, "Segoe UI", 13,
            cocos2d::Size(detailWidth - 34, 0), cocos2d::TextHAlignment::LEFT, cocos2d::TextVAlignment::TOP);
        if (description) {
            description->setAnchorPoint(cocos2d::Vec2(0, 1));
            description->setPosition(detailX + 17, bodyY + bodyHeight - 216);
            description->setColor(kText);
            addChild(description);
        }
        if (!feature->compatibility.empty()) {
            addLabel("COMPATIBILITY", detailX + 17, bodyY + 120, detailWidth - 34, 11, kMuted, true);
            std::string compatibility;
            for (const auto& item : feature->compatibility) {
                if (!compatibility.empty()) compatibility += "\n";
                compatibility += item;
            }
            auto* compatLabel = cocos2d::Label::createWithSystemFont(compatibility, "Segoe UI", 11,
                cocos2d::Size(detailWidth - 34, 0), cocos2d::TextHAlignment::LEFT, cocos2d::TextVAlignment::TOP);
            if (compatLabel) {
                compatLabel->setAnchorPoint(cocos2d::Vec2(0, 1));
                compatLabel->setPosition(detailX + 17, bodyY + 101);
                compatLabel->setColor(kMuted);
                addChild(compatLabel);
            }
        }
    } else {
        addLabel("No matching catalog entries.", detailX + 17, bodyY + bodyHeight - 64,
            detailWidth - 34, 13, kMuted, false);
    }

    addLabel("Catalog statuses describe source wiring, not proof of game-runtime behavior.",
        margin, 10, width - margin * 2, 10, kMuted, false);
}