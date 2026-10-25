#include "CatalogScene.h"
#include "cocos2d.h"

#include <windows.h>

class CatalogApp final : public cocos2d::Application {
public:
    void initGLContextAttrs() override {
        GLContextAttrs attributes = {8, 8, 8, 8, 24, 8, 0};
        cocos2d::GLView::setGLContextAttrs(attributes);
    }

    bool applicationDidFinishLaunching() override {
        auto* director = cocos2d::Director::getInstance();
        auto* view = director->getOpenGLView();
        if (!view) {
            view = cocos2d::GLViewImpl::createWithRect(
                "GD Client Catalog", cocos2d::Rect(80, 80, 1440, 900));
            director->setOpenGLView(view);
        }
        view->setFrameSize(1440, 900);
        view->setDesignResolutionSize(1280, 800, ResolutionPolicy::FIXED_WIDTH);
        director->setAnimationInterval(1.0 / 60.0);
        director->setDisplayStats(false);
        director->runWithScene(CatalogScene::createScene());
        return true;
    }

    void applicationDidEnterBackground() override {
        cocos2d::Director::getInstance()->stopAnimation();
    }

    void applicationWillEnterForeground() override {
        cocos2d::Director::getInstance()->startAnimation();
    }
};

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    CatalogApp application;
    return cocos2d::Application::getInstance()->run();
}
