#pragma once

#include "cocos2d.h"

#include <string>
#include <vector>

class CatalogScene final : public cocos2d::Scene {
public:
    static cocos2d::Scene* createScene();
    bool init() override;
    CREATE_FUNC(CatalogScene);

private:
    struct Feature {
        std::string id;
        std::string name;
        std::string category;
        std::string description;
        std::string status;
        std::string activation;
        std::string risk;
        std::vector<std::string> compatibility;
    };

    bool loadCatalog();
    void rebuild();
    void selectCategory(const std::string& category);
    void selectStatus(const std::string& status);
    void selectFeature(const std::string& id);
    void changePage(int delta);
    std::vector<const Feature*> visibleFeatures() const;
    const Feature* selectedFeature() const;
    cocos2d::Color3B statusColor(const std::string& status) const;
    cocos2d::Label* addLabel(const std::string& text, float x, float y, float width,
        int fontSize, cocos2d::Color3B color, bool bold = false);
    void addPanel(float x, float y, float width, float height, cocos2d::Color4B color);
    void addButton(const std::string& text, float x, float y, float width, float height,
        cocos2d::Color4B fill, const std::function<void()>& onClick, int fontSize = 13);

    std::vector<Feature> features_;
    std::vector<std::string> categories_;
    std::string activeCategory_;
    std::string activeStatus_;
    std::string selectedId_;
    int page_ = 0;
};
