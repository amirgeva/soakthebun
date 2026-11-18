#pragma once

#include <SDL.h>

#include <atomic>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "app/audio.h"
#include "app/save.h"
#include "core/level.h"

namespace soak {

class Canvas;

struct Options {
    int level = 0;           // > 0: preselect this level in the menu
    int width = 1024;
    int height = 768;
    std::string screenshot;  // render one frame to this .bmp and exit
    std::string scene = "game";  // screenshot scene: menu | game | won
    std::string moves;       // screenshot: moves to play first (U/D/L/R)
    std::string record;      // play the scripted intro, raw RGB24 frames to this file ("-" = stdout)
    int fps = 30;            // frame rate for --record
};

class Game {
public:
    ~Game();
    int run(const Options& opt);

private:
    enum class Scene { Menu, Loading, Playing, Won };

    struct Sprites {
        int tile = 0;
        SDL_Texture* floor[2] = {nullptr, nullptr};
        SDL_Texture* wall = nullptr;
        SDL_Texture* puddle = nullptr;
        SDL_Texture* bun = nullptr;
        SDL_Texture* soaked = nullptr;
        SDL_Texture* hero[4] = {nullptr, nullptr, nullptr, nullptr};
    };
    struct Anim {
        bool active = false;
        float t = 0.f, duration = 0.1f;
        int from = 0, to = 0;
        int box = -1, boxFrom = 0, boxTo = 0;
    };
    struct Particle {
        float x, y, vx, vy, life, maxLife, size;
    };
    struct Drop {
        float x, y, speed, len;
    };
    struct Job {
        int level = 0;
        std::shared_ptr<std::atomic<bool>> cancel;
        std::future<Level> result;
        bool valid() const { return result.valid(); }
    };
    struct Layout {
        int tile, ox, oy;
    };

    bool init(const Options& opt);
    void shutdown();
    void persist();
    int runScreenshot(const Options& opt);
    int runRecord(const Options& opt);

    // Input and game flow.
    void handleEvent(const SDL_Event& e);
    void onKeyDown(SDL_Keycode key, bool repeat);
    void onKeyUp(SDL_Keycode key);
    void requestLevel(int number);
    void pollJob();
    void beginLevel(Level&& level);
    void goToMenu(int level);
    void tryMove(int dir);
    void finishAnim();
    void undo();
    void restart();
    void onWin();
    void toggleMute();
    void setMenuLevel(int level);

    static Job startJob(int number);
    static void cancelJob(Job& job);

    // Simulation.
    void update(float dt);
    void splash(int cell, int count);
    float frand();

    // Rendering.
    void render();
    void renderBackground();
    void renderRain();
    void renderMenu();
    void renderLoading();
    void renderBoard();
    void renderHud();
    void renderWon();
    void renderCaption();
    Layout layout() const;
    int uiScale() const;
    int fitScale(const std::string& text, int maxWidth, int scale) const;

    // Assets.
    SDL_Texture* makeTexture(const Canvas& canvas);
    void createStaticAssets();
    void ensureSprites(int tile);
    void destroySprites();
    void ensureTitleArt(int size);
    void ensurePanel(int w, int h);
    void setWindowIcon();

    // Text.
    int textWidth(const std::string& s, int scale) const;
    void drawText(const std::string& s, int x, int y, int scale, SDL_Color color, int align = -1, bool shadow = true);
    void drawTextRaw(const std::string& s, int x, int y, int scale, SDL_Color color);

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    bool running_ = true;
    bool sdlReady_ = false;
    bool vsync_ = false;
    bool persist_ = true;  // false in screenshot mode
    Scene scene_ = Scene::Menu;
    float time_ = 0.f;
    int viewW_ = 0, viewH_ = 0;
    bool fixedView_ = false;
    uint32_t rngState_ = 0x9E3779B9u;

    Sprites sprites_;
    int wantedTile_ = 0;
    bool rebakeNow_ = false;
    float wantedSince_ = 0.f;
    SDL_Texture* font_ = nullptr;
    SDL_Texture* background_ = nullptr;
    SDL_Texture* dot_ = nullptr;
    SDL_Texture* ripple_ = nullptr;
    SDL_Texture* title_ = nullptr;
    int titleSize_ = 0;
    int wantedTitle_ = 0;
    float wantedTitleSince_ = 0.f;
    SDL_Texture* panel_ = nullptr;
    int panelW_ = 0, panelH_ = 0;

    Audio audio_;
    SaveData save_;

    // Menu.
    int menuLevel_ = 1;
    int menuSel_ = 0;
    std::string typed_;
    float typedAt_ = -10.f;
    std::vector<Drop> rain_;

    // Play.
    int levelNum_ = 1;
    Board board_;
    Anim anim_;
    int facing_ = DirDown;
    int heldDir_ = -1;
    float animSpeed_ = 1.f;  // > 1 speeds up move animations (recorded demo)
    float repeatTimer_ = 0.f;
    bool solvedPending_ = false;
    float winTimer_ = 0.f;
    std::vector<Particle> particles_;
    Job job_;
    Job prefetch_;

    // Subtitle shown in the recorded intro.
    std::string caption_;
    float captionStart_ = 0.f, captionEnd_ = 0.f;
};

}  // namespace soak
