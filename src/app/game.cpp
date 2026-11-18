#include "app/game.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "core/generator.h"
#include "core/solver.h"
#include "gfx/art.h"
#include "gfx/canvas.h"
#include "gfx/font.h"

namespace soak {
namespace {

constexpr float kMoveTime = 0.10f;
constexpr float kPushTime = 0.12f;
constexpr float kRepeatDelay = 0.22f;  // hold an arrow this long to start walking
constexpr float kWinDelay = 0.65f;     // let the last splash play before the overlay
constexpr float kRebakeDelay = 0.15f;  // wait for a window resize to settle
constexpr int kMenuItems = 3;          // Start, Level, Quit
constexpr float kPi = 3.14159265f;
constexpr int kHintStates = 2500000;   // search budget for one hint (~2 s worst case)

SDL_Color rgba(Uint8 r, Uint8 g, Uint8 b, Uint8 a = 255) { return SDL_Color{r, g, b, a}; }

int dirForKey(SDL_Keycode k) {
    switch (k) {
        case SDLK_UP: return DirUp;
        case SDLK_RIGHT: return DirRight;
        case SDLK_DOWN: return DirDown;
        case SDLK_LEFT: return DirLeft;
        default: return -1;
    }
}

SDL_Scancode scancodeForDir(int dir) {
    static const SDL_Scancode kCodes[4] = {SDL_SCANCODE_UP, SDL_SCANCODE_RIGHT, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT};
    return kCodes[dir];
}

int digitForKey(SDL_Keycode k) {
    if (k >= SDLK_0 && k <= SDLK_9) return int(k - SDLK_0);
    if (k >= SDLK_KP_1 && k <= SDLK_KP_9) return int(k - SDLK_KP_1) + 1;
    if (k == SDLK_KP_0) return 0;
    return -1;
}

bool isConfirm(SDL_Keycode k) { return k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE; }

float easeOut(float t) {
    t = clamp01(t);
    return 1.f - (1.f - t) * (1.f - t);
}

}  // namespace

Game::~Game() { shutdown(); }

// ---------------------------------------------------------------- lifecycle

int Game::run(const Options& opt) {
    if (!init(opt)) {
        shutdown();
        return 1;
    }
    if (!opt.screenshot.empty() || !opt.record.empty()) {
        const int rc = opt.record.empty() ? runScreenshot(opt) : runRecord(opt);
        shutdown();
        return rc;
    }

    const double freq = double(SDL_GetPerformanceFrequency());
    Uint64 last = SDL_GetPerformanceCounter();
    while (running_) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) handleEvent(e);
        const Uint64 now = SDL_GetPerformanceCounter();
        const float dt = std::min(0.05f, float(double(now - last) / freq));
        last = now;
        update(dt);
        render();
        SDL_RenderPresent(renderer_);
        if (!vsync_) {
            const double spent = double(SDL_GetPerformanceCounter() - now) / freq;
            if (spent < 1.0 / 120.0) SDL_Delay(Uint32((1.0 / 120.0 - spent) * 1000.0));
        }
    }
    shutdown();
    return 0;
}

bool Game::init(const Options& opt) {
    const bool screenshot = !opt.screenshot.empty() || !opt.record.empty();  // offscreen modes
    persist_ = !screenshot;
    // Crisp rendering on high-DPI Windows displays, with the window sized in
    // logical units (SDL 2.24+; ignored by older versions).
    SDL_SetHint("SDL_WINDOWS_DPI_AWARENESS", "permonitorv2");
    SDL_SetHint("SDL_WINDOWS_DPI_SCALING", "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    sdlReady_ = true;

    Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (screenshot) flags |= SDL_WINDOW_HIDDEN;
    window_ = SDL_CreateWindow("Soak the Bun", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, opt.width, opt.height,
                               flags);
    if (!window_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create window: %s", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(window_, 480, 360);

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    renderer_ = SDL_CreateRenderer(window_, -1,
                                   SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!renderer_) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
    if (!renderer_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create renderer: %s", SDL_GetError());
        return false;
    }
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(renderer_, &info) == 0) vsync_ = (info.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    setWindowIcon();
    createStaticAssets();

    save_ = loadSave();
    if (!screenshot) audio_.init();
    audio_.setMuted(save_.muted);
    setMenuLevel(opt.level > 0 ? opt.level : save_.lastLevel);
    return true;
}

void Game::shutdown() {
    cancelHint();
    cancelJob(job_);
    cancelJob(prefetch_);
    audio_.shutdown();
    destroySprites();
    for (SDL_Texture** t : {&font_, &background_, &dot_, &ripple_, &title_, &panel_}) {
        if (*t) SDL_DestroyTexture(*t);
        *t = nullptr;
    }
    if (renderer_) SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
    if (window_) SDL_DestroyWindow(window_);
    window_ = nullptr;
    if (sdlReady_) SDL_Quit();
    sdlReady_ = false;
}

void Game::persist() {
    if (persist_) storeSave(save_);
}

// Renders one frame of a scene into a .bmp (used for README screenshots and
// for checking the art without playing).
int Game::runScreenshot(const Options& opt) {
    fixedView_ = true;
    viewW_ = opt.width;
    viewH_ = opt.height;
    SDL_Texture* target =
        SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, viewW_, viewH_);
    if (!target) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Render target unavailable: %s", SDL_GetError());
        return 1;
    }
    // Drive the real input path: menu -> Enter -> background generation ->
    // arrow keys -> (win). Doubles as an end-to-end smoke test.
    const float step = 1.f / 60.f;
    auto press = [&](SDL_Keycode key) {
        onKeyDown(key, false);
        onKeyUp(key);
    };
    auto waitWhileLoading = [&] {
        const Uint32 start = SDL_GetTicks();
        while (scene_ == Scene::Loading && SDL_GetTicks() - start < 30000) {
            update(step);
            SDL_Delay(5);
        }
    };
    scene_ = Scene::Menu;
    for (int i = 0; i < 90; ++i) update(step);
    if (opt.scene != "menu") {
        press(SDLK_RETURN);
        waitWhileLoading();
        for (char ch : opt.moves) {
            const char u = char(std::toupper(static_cast<unsigned char>(ch)));
            const SDL_Keycode key = u == 'U'   ? SDLK_UP
                                    : u == 'D' ? SDLK_DOWN
                                    : u == 'L' ? SDLK_LEFT
                                    : u == 'R' ? SDLK_RIGHT
                                               : SDLK_UNKNOWN;
            if (key == SDLK_UNKNOWN) continue;
            press(key);
            for (int i = 0; i < 10 && anim_.active; ++i) update(step);
        }
        for (int i = 0; i < 50 && scene_ == Scene::Playing && solvedPending_; ++i) update(step);
        for (int i = 0; i < 6; ++i) update(step);
        if (opt.scene == "hint") {
            press(SDLK_h);
            const Uint32 start = SDL_GetTicks();
            while (hintJob_.valid() && SDL_GetTicks() - start < 30000) {
                update(step);
                SDL_Delay(2);
            }
            for (int i = 0; i < 20; ++i) update(step);
        }
        if (opt.scene == "won" && scene_ != Scene::Won) onWin();
        if (opt.scene == "next" && scene_ == Scene::Won) {
            press(SDLK_RETURN);  // should pick up the prefetched level
            waitWhileLoading();
        }
    }
    SDL_Log("screenshot: scene=%d level=%d moves=%d pushes=%d solved=%d", int(scene_), levelNum_, board_.moves(),
            board_.pushes(), board_.solved() ? 1 : 0);

    SDL_SetRenderTarget(renderer_, target);
    render();
    std::vector<uint8_t> px(size_t(viewW_) * size_t(viewH_) * 4);
    const int read = SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_ARGB8888, px.data(), viewW_ * 4);
    SDL_SetRenderTarget(renderer_, nullptr);
    SDL_DestroyTexture(target);
    if (read != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not read pixels: %s", SDL_GetError());
        return 1;
    }
    SDL_Surface* surf =
        SDL_CreateRGBSurfaceWithFormatFrom(px.data(), viewW_, viewH_, 32, viewW_ * 4, SDL_PIXELFORMAT_ARGB8888);
    const int saved = surf ? SDL_SaveBMP(surf, opt.screenshot.c_str()) : -1;
    if (surf) SDL_FreeSurface(surf);
    if (saved != 0) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not save screenshot: %s", SDL_GetError());
    return saved == 0 ? 0 : 1;
}

// Plays a scripted gameplay intro at a fixed frame rate and writes every
// frame as raw RGB24 (pipe it into ffmpeg; see tools/make_intro.sh). Levels
// are solved with the optimal-push solver, through the normal key handling.
int Game::runRecord(const Options& opt) {
    fixedView_ = true;
    viewW_ = opt.width;
    viewH_ = opt.height;
    save_ = SaveData{};  // a fresh player, whatever this machine has saved
    setMenuLevel(1);

    FILE* out = nullptr;
    if (opt.record == "-") {
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        out = stdout;
    } else {
        out = std::fopen(opt.record.c_str(), "wb");
    }
    if (!out) {
        std::fprintf(stderr, "cannot open %s for writing\n", opt.record.c_str());
        return 1;
    }
    SDL_Texture* target =
        SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, viewW_, viewH_);
    if (!target) {
        std::fprintf(stderr, "render target unavailable: %s\n", SDL_GetError());
        if (out != stdout) std::fclose(out);
        return 1;
    }

    const int fps = std::clamp(opt.fps, 10, 60);
    const float dt = 1.f / float(fps);
    std::vector<uint8_t> px(size_t(viewW_) * size_t(viewH_) * 3);
    int frames = 0;
    bool ok = true;

    auto frame = [&] {
        update(dt);
        SDL_SetRenderTarget(renderer_, target);
        render();
        if (SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_RGB24, px.data(), viewW_ * 3) != 0) ok = false;
        SDL_SetRenderTarget(renderer_, nullptr);
        if (std::fwrite(px.data(), 1, px.size(), out) != px.size()) ok = false;
        ++frames;
    };
    auto hold = [&](float seconds) {
        const int n = int(std::lround(seconds * float(fps)));
        for (int i = 0; i < n && ok; ++i) frame();
    };
    auto press = [&](SDL_Keycode key, float after) {
        onKeyDown(key, false);
        onKeyUp(key);
        hold(after);
    };
    // Generation runs in real time; the video simply cuts to the level.
    auto waitForLevel = [&] {
        const Uint32 start = SDL_GetTicks();
        while (scene_ == Scene::Loading && SDL_GetTicks() - start < 30000) {
            update(0.f);
            SDL_Delay(2);
        }
    };
    auto caption = [&](const char* text, float seconds) {
        caption_ = text;
        captionStart_ = time_;
        captionEnd_ = time_ + seconds;
    };
    auto solve = [&](float walk, float push, float speed) {
        static const SDL_Keycode kKeys[4] = {SDLK_UP, SDLK_RIGHT, SDLK_DOWN, SDLK_LEFT};
        animSpeed_ = speed;
        const std::string moves = solutionMoves(board_.level(), 3000000);
        Board sim;
        sim.load(board_.level());
        for (char m : moves) {
            const int dir = m == 'U' ? DirUp : m == 'R' ? DirRight : m == 'D' ? DirDown : DirLeft;
            const bool pushed = sim.move(dir) == Board::Result::Pushed;
            press(kKeys[dir], pushed ? push : walk);
        }
        animSpeed_ = 1.f;
    };

    // ---- Storyboard ----
    scene_ = Scene::Menu;
    for (int i = 0; i < 90; ++i) update(dt);  // let the rain fill the screen first
    hold(1.2f);
    caption("PICK ANY LEVEL - YOUR PROGRESS IS SAVED", 3.6f);
    press(SDLK_DOWN, 0.45f);
    press(SDLK_RIGHT, 0.3f);
    press(SDLK_RIGHT, 0.3f);
    press(SDLK_RIGHT, 0.5f);
    press(SDLK_LEFT, 0.25f);
    press(SDLK_LEFT, 0.25f);
    press(SDLK_LEFT, 0.5f);
    press(SDLK_UP, 0.6f);

    press(SDLK_RETURN, 0.f);
    waitForLevel();
    caption("PUSH EVERY BUN INTO A PUDDLE", 3.4f);
    hold(0.7f);
    solve(0.16f, 0.22f, 1.f);
    hold(3.0f);

    press(SDLK_RETURN, 0.f);
    waitForLevel();
    caption("EVERY LEVEL IS GENERATED - AND ALWAYS SOLVABLE", 3.8f);
    hold(0.5f);
    solve(0.13f, 0.18f, 1.f);
    hold(2.8f);

    press(SDLK_ESCAPE, 0.7f);
    caption("THE PUZZLES GET HARDER AS YOU GO", 4.0f);
    press(SDLK_1, 0.3f);
    press(SDLK_2, 0.8f);
    press(SDLK_RETURN, 0.f);
    waitForLevel();
    hold(0.6f);
    solve(0.085f, 0.11f, 1.35f);
    hold(3.3f);

    press(SDLK_ESCAPE, 2.8f);

    SDL_DestroyTexture(target);
    std::fflush(out);
    if (out != stdout) std::fclose(out);
    std::fprintf(stderr, "recorded %d frames (%.1f s at %d fps, %dx%d)%s\n", frames, double(frames) / fps, fps,
                 viewW_, viewH_, ok ? "" : " - WRITE ERRORS");
    return ok ? 0 : 1;
}

// ---------------------------------------------------------------- level jobs

Game::Job Game::startJob(int number) {
    Job job;
    job.level = number;
    job.cancel = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<std::atomic<bool>> flag = job.cancel;
    job.result = std::async(std::launch::async, [number, flag] { return generateLevel(number, flag.get()); });
    return job;
}

void Game::cancelJob(Job& job) {
    if (!job.valid()) return;
    job.cancel->store(true);
    job.result.wait();
    job = Job{};
}

void Game::requestLevel(int number) {
    number = std::clamp(number, 1, kMaxLevel);
    levelNum_ = number;
    save_.lastLevel = number;
    persist();
    if (!(job_.valid() && job_.level == number)) {
        cancelJob(job_);
        if (prefetch_.valid() && prefetch_.level == number) job_ = std::move(prefetch_);
        else job_ = startJob(number);
    }
    scene_ = Scene::Loading;
    heldDir_ = -1;
    pollJob();
}

void Game::pollJob() {
    if (!job_.valid()) return;
    if (job_.result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    Level level = job_.result.get();
    job_ = Job{};
    if (!level.valid()) {
        goToMenu(levelNum_);
        return;
    }
    beginLevel(std::move(level));
}

void Game::beginLevel(Level&& level) {
    levelNum_ = level.number;
    board_.load(level);
    rebakeNow_ = true;  // new room size: no need to wait for a resize to settle
    cancelHint();
    hint_.active = false;
    caption_.clear();
    scene_ = Scene::Playing;
    anim_ = Anim{};
    particles_.clear();
    solvedPending_ = false;
    facing_ = DirDown;
    heldDir_ = -1;
    // Generate the next level in the background while this one is played.
    if (levelNum_ < kMaxLevel && !(prefetch_.valid() && prefetch_.level == levelNum_ + 1)) {
        cancelJob(prefetch_);
        prefetch_ = startJob(levelNum_ + 1);
    }
}

void Game::goToMenu(int level) {
    cancelJob(job_);
    cancelHint();
    hint_.active = false;
    caption_.clear();
    setMenuLevel(level);
    menuSel_ = 0;
    scene_ = Scene::Menu;
    heldDir_ = -1;
    anim_.active = false;
}

void Game::setMenuLevel(int level) { menuLevel_ = std::clamp(level, 1, kMaxLevel); }

// ---------------------------------------------------------------- input

void Game::handleEvent(const SDL_Event& e) {
    switch (e.type) {
        case SDL_QUIT:
            running_ = false;
            break;
        case SDL_KEYDOWN:
            onKeyDown(e.key.keysym.sym, e.key.repeat != 0);
            break;
        case SDL_KEYUP:
            onKeyUp(e.key.keysym.sym);
            break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) heldDir_ = -1;
            break;
        default:
            break;
    }
}

void Game::onKeyDown(SDL_Keycode key, bool repeat) {
    if (repeat == false && key == SDLK_m) {
        toggleMute();
        return;
    }
    if (!repeat && (key == SDLK_F11 || (key == SDLK_RETURN && (SDL_GetModState() & KMOD_ALT)))) {
        const bool full = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
        SDL_SetWindowFullscreen(window_, full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
        return;
    }

    switch (scene_) {
        case Scene::Menu: {
            const int dir = dirForKey(key);
            const int digit = digitForKey(key);
            if (dir == DirUp || dir == DirDown) {
                menuSel_ = (menuSel_ + (dir == DirDown ? 1 : kMenuItems - 1)) % kMenuItems;
                audio_.play(Sfx::Tick);
            } else if (dir == DirLeft || dir == DirRight) {
                setMenuLevel(menuLevel_ + (dir == DirRight ? 1 : -1));
                typed_.clear();
                audio_.play(Sfx::Tick);
            } else if (key == SDLK_PAGEUP || key == SDLK_PAGEDOWN) {
                setMenuLevel(menuLevel_ + (key == SDLK_PAGEUP ? 10 : -10));
                typed_.clear();
                audio_.play(Sfx::Tick);
            } else if (key == SDLK_HOME) {
                setMenuLevel(1);
            } else if (key == SDLK_END) {
                setMenuLevel(save_.bestLevel + 1);
            } else if (digit >= 0 && !repeat) {
                // Type a level number directly.
                if (time_ - typedAt_ > 1.5f || typed_.size() >= 3) typed_.clear();
                typed_ += char('0' + digit);
                typedAt_ = time_;
                const int value = std::atoi(typed_.c_str());
                if (value >= 1) setMenuLevel(value);
                menuSel_ = 1;
            } else if (key == SDLK_BACKSPACE) {
                setMenuLevel(menuLevel_ / 10);
                typed_.clear();
            } else if (isConfirm(key) && !repeat) {
                if (menuSel_ == 2) running_ = false;
                else requestLevel(menuLevel_);
            } else if (key == SDLK_ESCAPE && !repeat) {
                running_ = false;
            }
            break;
        }
        case Scene::Loading:
            if (key == SDLK_ESCAPE && !repeat) goToMenu(levelNum_);
            break;
        case Scene::Playing: {
            const int dir = dirForKey(key);
            if (dir >= 0) {
                // Holding an arrow walks; OS key repeat is ignored in favour
                // of our own timing so movement is smooth and consistent.
                if (!repeat) {
                    heldDir_ = dir;
                    repeatTimer_ = kRepeatDelay;
                    tryMove(dir);
                }
            } else if (key == SDLK_u || key == SDLK_z || key == SDLK_BACKSPACE) {
                undo();
            } else if (key == SDLK_h && !repeat) {
                requestHint();
            } else if (key == SDLK_r && !repeat) {
                restart();
            } else if (key == SDLK_ESCAPE && !repeat) {
                goToMenu(levelNum_);
            }
            break;
        }
        case Scene::Won:
            if (repeat) break;
            if (isConfirm(key)) requestLevel(levelNum_ + 1);
            else if (key == SDLK_r) restart();
            else if (key == SDLK_ESCAPE) goToMenu(levelNum_ + 1);
            break;
    }
}

void Game::onKeyUp(SDL_Keycode key) {
    const int dir = dirForKey(key);
    if (dir < 0 || dir != heldDir_) return;
    heldDir_ = -1;
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    for (int d = 0; d < 4; ++d) {
        if (keys[scancodeForDir(d)]) {
            heldDir_ = d;
            repeatTimer_ = kRepeatDelay;
            break;
        }
    }
}

void Game::tryMove(int dir) {
    if (scene_ != Scene::Playing || solvedPending_) return;
    finishAnim();
    facing_ = dir;
    const int from = board_.player();
    const Board::Result result = board_.move(dir);
    if (result == Board::Result::Blocked) return;

    anim_ = Anim{};
    anim_.active = true;
    anim_.from = from;
    anim_.to = board_.player();
    anim_.duration = kMoveTime / animSpeed_;
    if (result == Board::Result::Pushed) {
        anim_.box = board_.lastPushedBox();
        anim_.boxTo = board_.boxes()[size_t(anim_.box)];
        anim_.boxFrom = anim_.boxTo - board_.level().offset(dir);
        anim_.duration = kPushTime / animSpeed_;
        audio_.play(Sfx::Push);
    }
    if (board_.solved()) {
        solvedPending_ = true;
        winTimer_ = kWinDelay;
    }
}

void Game::finishAnim() {
    if (!anim_.active) return;
    anim_.active = false;
    if (anim_.box >= 0 && board_.level().isGoal(anim_.boxTo)) {
        splash(anim_.boxTo, 16);
        audio_.play(Sfx::Splash);
    }
}

void Game::undo() {
    if (scene_ != Scene::Playing) return;
    anim_.active = false;
    if (board_.undo()) {
        audio_.play(Sfx::Undo);
        solvedPending_ = board_.solved();
        winTimer_ = kWinDelay;
    }
}

void Game::restart() {
    board_.restart();
    cancelHint();
    hint_.active = false;
    anim_ = Anim{};
    particles_.clear();
    solvedPending_ = false;
    facing_ = DirDown;
    heldDir_ = -1;
    scene_ = Scene::Playing;
}

void Game::onWin() {
    scene_ = Scene::Won;
    heldDir_ = -1;
    hint_.active = false;
    caption_.clear();
    audio_.play(Sfx::Win);
    // The menu now offers the next level by default.
    save_.lastLevel = std::min(levelNum_ + 1, kMaxLevel);
    save_.bestLevel = std::max(save_.bestLevel, levelNum_);
    persist();
    for (int b : board_.boxes()) splash(b, 10);
}

void Game::toggleMute() {
    save_.muted = !save_.muted;
    audio_.setMuted(save_.muted);
    persist();
}

void Game::showMessage(const std::string& text, float seconds) {
    caption_ = text;
    captionStart_ = time_;
    captionEnd_ = time_ + seconds;
}

// Solves from the current position in the background; pollHint() picks up
// the first push of the best (fewest pushes) solution.
void Game::requestHint() {
    if (scene_ != Scene::Playing || solvedPending_ || hintJob_.valid()) return;
    finishAnim();
    if (hint_.active) return;  // the current hint still applies
    Level pos = board_.level();
    pos.boxes = board_.boxes();
    pos.player = board_.player();
    hintJob_.boxes = pos.boxes;
    hintJob_.player = pos.player;
    hintJob_.cancel = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<std::atomic<bool>> flag = hintJob_.cancel;
    hintJob_.result = std::async(std::launch::async, [pos = std::move(pos), flag] {
        return solveWithMoves(pos, kHintStates, flag.get());
    });
    showMessage("THINKING...", 120.f);
}

void Game::pollHint() {
    if (!hintJob_.valid()) return;
    if (board_.boxes() != hintJob_.boxes) {  // a push or undo made it stale
        cancelHint();
        caption_.clear();
        return;
    }
    if (hintJob_.result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    const SolveResult r = hintJob_.result.get();
    Level pos = board_.level();
    pos.boxes = hintJob_.boxes;
    pos.player = hintJob_.player;
    hintJob_ = HintJob{};

    if (r.solved && !r.moves.empty()) {
        Board sim;
        sim.load(pos);
        for (char m : r.moves) {
            const int dir = m == 'U' ? DirUp : m == 'R' ? DirRight : m == 'D' ? DirDown : DirLeft;
            if (sim.move(dir) == Board::Result::Pushed) {
                hint_.active = true;
                hint_.dir = dir;
                hint_.boxCell = sim.boxes()[size_t(sim.lastPushedBox())] - pos.offset(dir);
                hint_.boxes = pos.boxes;
                break;
            }
        }
        char msg[96];
        if (r.pushes == 1) std::snprintf(msg, sizeof msg, "HINT: ONE LAST PUSH!");
        else std::snprintf(msg, sizeof msg, "HINT: PUSH THE MARKED BUN - %d PUSHES LEFT AT BEST", r.pushes);
        showMessage(msg, 4.5f);
    } else if (r.exhausted) {
        showMessage("STUCK - NO SOLUTION FROM HERE. TRY UNDO (U)", 4.5f);
    } else if (!r.cancelled) {
        showMessage("TOO TRICKY TO HINT FROM HERE - TRY UNDO (U)", 4.5f);
    }
}

void Game::cancelHint() {
    if (!hintJob_.valid()) return;
    hintJob_.cancel->store(true);
    hintJob_.result.wait();
    hintJob_ = HintJob{};
}

// ---------------------------------------------------------------- simulation

float Game::frand() {
    rngState_ ^= rngState_ << 13;
    rngState_ ^= rngState_ >> 17;
    rngState_ ^= rngState_ << 5;
    return float(rngState_ >> 8) / float(1u << 24);
}

void Game::splash(int cell, int count) {
    const Level& lv = board_.level();
    const float cx = float(cell % lv.width) + 0.5f;
    const float cy = float(cell / lv.width) + 0.7f;
    for (int i = 0; i < count; ++i) {
        const float a = kPi * (0.08f + 0.84f * frand());
        const float speed = 1.6f + frand() * 2.6f;
        Particle p;
        p.x = cx + (frand() - 0.5f) * 0.4f;
        p.y = cy;
        p.vx = std::cos(a) * speed;
        p.vy = -std::sin(a) * speed * 1.3f;
        p.maxLife = p.life = 0.45f + frand() * 0.35f;
        p.size = 0.06f + frand() * 0.07f;
        particles_.push_back(p);
    }
}

void Game::update(float dt) {
    time_ += dt;

    if (scene_ == Scene::Menu || scene_ == Scene::Loading) {
        while (rain_.size() < 110) rain_.push_back(Drop{frand(), frand(), 0.45f + frand() * 0.5f, 0.015f + frand() * 0.03f});
        for (Drop& d : rain_) {
            d.y += d.speed * dt;
            if (d.y - d.len > 1.f) {
                d.y = -frand() * 0.1f;
                d.x = frand();
            }
        }
    }

    for (Particle& p : particles_) {
        p.vy += 9.f * dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.life -= dt;
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(), [](const Particle& p) { return p.life <= 0.f; }),
                     particles_.end());

    switch (scene_) {
        case Scene::Loading:
            pollJob();
            break;
        case Scene::Playing:
            if (anim_.active) {
                anim_.t += dt;
                if (anim_.t >= anim_.duration) finishAnim();
            }
            if (heldDir_ >= 0 && !solvedPending_) {
                repeatTimer_ -= dt;
                if (repeatTimer_ <= 0.f && !anim_.active) tryMove(heldDir_);
            }
            if (solvedPending_ && !anim_.active) {
                winTimer_ -= dt;
                if (winTimer_ <= 0.f) onWin();
            }
            // A hint is only valid for the bun layout it was computed for.
            if (hint_.active && board_.boxes() != hint_.boxes) hint_.active = false;
            pollHint();
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------- assets

SDL_Texture* Game::makeTexture(const Canvas& canvas) {
    const std::vector<uint8_t> px = canvas.toRGBA8();
    SDL_Texture* t =
        SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, canvas.width(), canvas.height());
    if (!t) return nullptr;
    SDL_UpdateTexture(t, nullptr, px.data(), canvas.width() * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

void Game::setWindowIcon() {
    Canvas c(64, 64);
    art::icon(Pen(c), true);
    std::vector<uint8_t> px = c.toRGBA8();
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(px.data(), 64, 64, 32, 64 * 4, SDL_PIXELFORMAT_RGBA32);
    if (s) {
        SDL_SetWindowIcon(window_, s);
        SDL_FreeSurface(s);
    }
}

void Game::createStaticAssets() {
    {  // Font atlas: one 6x7 cell per ASCII code, white pixels, nearest filtering.
        const int w = 128 * font::kAdvance, h = font::kGlyphH;
        std::vector<uint32_t> px(size_t(w) * size_t(h), 0);
        for (int ch = 0; ch < 128; ++ch) {
            const char* const* rows = font::glyph(char(ch));
            if (!rows) continue;
            for (int y = 0; y < font::kGlyphH; ++y)
                for (int x = 0; x < font::kGlyphW; ++x)
                    if (rows[y][x] == '#') px[size_t(y * w + ch * font::kAdvance + x)] = 0xFFFFFFFFu;
        }
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        font_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
        if (font_) {
            SDL_UpdateTexture(font_, nullptr, px.data(), w * 4);
            SDL_SetTextureBlendMode(font_, SDL_BLENDMODE_BLEND);
        }
    }
    {  // Night-blue backdrop.
        Canvas c(4, 128);
        Pen(c).rect(0.f, 0.f, 1.f, 32.f, VGrad{0.f, 32.f, rgb(0x0A1C30), rgb(0x1B4D68)});
        background_ = makeTexture(c);
    }
    {
        Canvas c(32, 32);
        art::dot(Pen(c));
        dot_ = makeTexture(c);
    }
    {
        Canvas c(96, 96);
        art::ripple(Pen(c));
        ripple_ = makeTexture(c);
    }
}

void Game::destroySprites() {
    SDL_Texture** all[] = {&sprites_.floor[0], &sprites_.floor[1], &sprites_.wall,    &sprites_.puddle,
                           &sprites_.bun,      &sprites_.soaked,   &sprites_.hero[0], &sprites_.hero[1],
                           &sprites_.hero[2],  &sprites_.hero[3], &sprites_.arrow};
    for (SDL_Texture** t : all) {
        if (*t) SDL_DestroyTexture(*t);
        *t = nullptr;
    }
    sprites_.tile = 0;
}

// Sprites are drawn at exactly the on-screen tile size so they stay crisp.
// While a window is being resized the old ones are scaled until it settles.
void Game::ensureSprites(int tile) {
    if (sprites_.tile == tile) return;
    if (tile != wantedTile_) {
        wantedTile_ = tile;
        wantedSince_ = time_;
    }
    if (sprites_.tile != 0 && !fixedView_ && !rebakeNow_ && time_ - wantedSince_ < kRebakeDelay) return;
    rebakeNow_ = false;

    destroySprites();
    sprites_.tile = tile;
    auto bake = [&](auto draw) {
        Canvas c(tile, tile);
        draw(Pen(c));
        return makeTexture(c);
    };
    sprites_.floor[0] = bake([](const Pen& p) { art::floorTile(p, 0); });
    sprites_.floor[1] = bake([](const Pen& p) { art::floorTile(p, 1); });
    sprites_.wall = bake([](const Pen& p) { art::wallTile(p); });
    sprites_.puddle = bake([](const Pen& p) { art::puddle(p); });
    sprites_.bun = bake([](const Pen& p) { art::bun(p, false); });
    sprites_.soaked = bake([](const Pen& p) { art::bun(p, true); });
    for (int d = 0; d < 4; ++d) sprites_.hero[d] = bake([d](const Pen& p) { art::hero(p, d); });
    sprites_.arrow = bake([](const Pen& p) { art::arrow(p); });
}

void Game::ensureTitleArt(int size) {
    if (titleSize_ == size) return;
    if (size != wantedTitle_) {
        wantedTitle_ = size;
        wantedTitleSince_ = time_;
    }
    if (title_ && !fixedView_ && time_ - wantedTitleSince_ < kRebakeDelay) return;
    if (title_) SDL_DestroyTexture(title_);
    Canvas c(size, size);
    art::icon(Pen(c), false);
    title_ = makeTexture(c);
    titleSize_ = size;
}

void Game::ensurePanel(int w, int h) {
    if (panel_ && panelW_ == w && panelH_ == h) return;
    if (panel_) SDL_DestroyTexture(panel_);
    Canvas c(w, h);
    const Pen p(c, 0.f, 0.f, 1.f);  // pixel units
    const float fw = float(w), fh = float(h), r = std::min(fw, fh) * 0.12f, b = 3.f;
    p.roundRect(0.f, 0.f, fw, fh, r, solid(rgb(0x7CC8F2, 0.9f)));
    p.roundRect(b, b, fw - b, fh - b, r - b, VGrad{0.f, fh, rgb(0x1B4C80, 0.96f), rgb(0x0E2B47, 0.96f)});
    panel_ = makeTexture(c);
    panelW_ = w;
    panelH_ = h;
}

// ---------------------------------------------------------------- text

int Game::textWidth(const std::string& s, int scale) const {
    return s.empty() ? 0 : int(s.size()) * font::kAdvance * scale - scale;
}

int Game::fitScale(const std::string& text, int maxWidth, int scale) const {
    while (scale > 1 && textWidth(text, scale) > maxWidth) --scale;
    return std::max(1, scale);
}

void Game::drawText(const std::string& s, int x, int y, int scale, SDL_Color color, int align, bool shadow) {
    const int w = textWidth(s, scale);
    if (align == 0) x -= w / 2;
    else if (align > 0) x -= w;
    if (shadow) {
        const int o = std::max(1, scale / 2);
        drawTextRaw(s, x + o, y + o, scale, rgba(0, 8, 20, Uint8(color.a * 0.6f)));
    }
    drawTextRaw(s, x, y, scale, color);
}

void Game::drawTextRaw(const std::string& s, int x, int y, int scale, SDL_Color color) {
    if (!font_) return;
    SDL_SetTextureColorMod(font_, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(font_, color.a);
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(s[i]);
        if (ch >= 'a' && ch <= 'z') ch = static_cast<unsigned char>(ch - 'a' + 'A');
        if (ch >= 128 || ch == ' ') continue;
        const SDL_Rect src{int(ch) * font::kAdvance, 0, font::kGlyphW, font::kGlyphH};
        const SDL_Rect dst{x + int(i) * font::kAdvance * scale, y, font::kGlyphW * scale, font::kGlyphH * scale};
        SDL_RenderCopy(renderer_, font_, &src, &dst);
    }
}

// ---------------------------------------------------------------- rendering

int Game::uiScale() const { return std::max(1, std::min(viewW_ / 340, viewH_ / 250)); }

Game::Layout Game::layout() const {
    const Level& lv = board_.level();
    const int s = uiScale();
    const int top = 15 * s, bottom = 13 * s, margin = 4 * s;
    const int aw = viewW_ - 2 * margin;
    const int ah = viewH_ - top - bottom - 2 * margin;
    int tile = std::min(aw / std::max(1, lv.width), ah / std::max(1, lv.height));
    tile = std::clamp(tile, 8, 160);
    Layout l;
    l.tile = tile;
    l.ox = (viewW_ - tile * lv.width) / 2;
    l.oy = top + margin + (ah - tile * lv.height) / 2;
    return l;
}

void Game::render() {
    if (!fixedView_) SDL_GetRendererOutputSize(renderer_, &viewW_, &viewH_);
    renderBackground();
    switch (scene_) {
        case Scene::Menu:
            renderRain();
            renderMenu();
            break;
        case Scene::Loading:
            renderRain();
            renderLoading();
            break;
        case Scene::Playing:
            renderBoard();
            renderHud();
            break;
        case Scene::Won:
            renderBoard();
            renderHud();
            renderWon();
            break;
    }
    renderCaption();
}

void Game::renderBackground() {
    SDL_SetRenderDrawColor(renderer_, 10, 28, 48, 255);
    SDL_RenderClear(renderer_);
    if (background_) SDL_RenderCopy(renderer_, background_, nullptr, nullptr);
}

void Game::renderRain() {
    const int s = uiScale();
    SDL_SetRenderDrawColor(renderer_, 150, 205, 255, 70);
    for (const Drop& d : rain_) {
        const int len = std::max(4, int(d.len * float(viewH_)));
        SDL_Rect r{int(d.x * float(viewW_)), int(d.y * float(viewH_)) - len, std::max(1, s / 2), len};
        SDL_RenderFillRect(renderer_, &r);
    }
}

void Game::renderMenu() {
    const int s = uiScale();
    const int artSize = std::max(64, std::min(viewH_ * 34 / 100, viewW_ * 45 / 100));
    ensureTitleArt(artSize);
    int y = viewH_ * 5 / 100;
    if (title_) {
        const int bob = int(std::sin(time_ * 1.6f) * float(s) * 2.f);
        SDL_Rect r{(viewW_ - artSize) / 2, y + bob, artSize, artSize};
        SDL_RenderCopy(renderer_, title_, nullptr, &r);
    }
    y += artSize + 2 * s;

    const std::string title = "SOAK THE BUN";
    const int ts = fitScale(title, viewW_ * 9 / 10, s * 3);
    drawText(title, viewW_ / 2, y, ts, rgba(255, 205, 105), 0);
    y += 7 * ts + 4 * s;
    drawText("PUSH EVERY BUN INTO A PUDDLE", viewW_ / 2, y, s, rgba(165, 220, 255), 0);
    y += 7 * s + 10 * s;

    const int is = std::max(2, s * 3 / 2);
    char level[48];
    std::snprintf(level, sizeof level, "LEVEL  " SOAK_ARROW_LEFT " %d " SOAK_ARROW_RIGHT, menuLevel_);
    const std::string items[kMenuItems] = {"START", level, "QUIT"};
    const float pulse = 0.5f + 0.5f * std::sin(time_ * 5.f);
    for (int i = 0; i < kMenuItems; ++i) {
        const bool sel = i == menuSel_;
        const int w = textWidth(items[i], is);
        if (sel) {
            SDL_SetRenderDrawColor(renderer_, 120, 200, 255, Uint8(28 + 24 * pulse));
            SDL_Rect bar{viewW_ / 2 - w / 2 - 8 * s, y - 2 * s, w + 16 * s, 7 * is + 4 * s};
            SDL_RenderFillRect(renderer_, &bar);
        }
        drawText(items[i], viewW_ / 2, y, is, sel ? rgba(255, 220, 120) : rgba(220, 232, 245, 210), 0);
        y += 7 * is + 6 * s;
    }
    y += 3 * s;

    char info[96];
    const int buns = paramsForLevel(menuLevel_).boxes;
    if (save_.bestLevel > 0)
        std::snprintf(info, sizeof info, "%d BUN%s   HIGHEST LEVEL SOAKED: %d", buns, buns == 1 ? "" : "S",
                      save_.bestLevel);
    else
        std::snprintf(info, sizeof info, "%d BUN%s", buns, buns == 1 ? "" : "S");
    drawText(info, viewW_ / 2, y, s, rgba(165, 220, 255, 220), 0);

    const std::string help = SOAK_ARROW_UP SOAK_ARROW_DOWN " CHOOSE   " SOAK_ARROW_LEFT SOAK_ARROW_RIGHT
                             " LEVEL   0-9 TYPE LEVEL   ENTER PLAY   ESC QUIT";
    const int hs = fitScale(help, viewW_ - 4 * s, std::max(1, s - 1));
    drawText(help, viewW_ / 2, viewH_ - 7 * hs - 4 * s, hs, rgba(190, 215, 240, 200), 0);
}

void Game::renderLoading() {
    const int s = uiScale();
    if (sprites_.tile == 0) ensureSprites(64);
    const int size = 32 * s;
    const float bounce = std::fabs(std::sin(time_ * 5.f));
    if (sprites_.hero[DirDown]) {
        SDL_Rect r{(viewW_ - size) / 2, viewH_ / 2 - size - int(bounce * 12.f * float(s)), size, size};
        SDL_RenderCopy(renderer_, sprites_.hero[DirDown], nullptr, &r);
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "SOAKING LEVEL %d", levelNum_);
    const int ls = s * 2;
    std::string text = buf;
    const int x = viewW_ / 2 - textWidth(text, ls) / 2;
    text.append(size_t(int(time_ * 3.f) % 4), '.');
    drawText(text, x, viewH_ / 2 + 4 * s, ls, rgba(255, 220, 120));
    drawText("ESC CANCEL", viewW_ / 2, viewH_ / 2 + 7 * ls + 10 * s, s, rgba(190, 215, 240, 200), 0);
}

void Game::renderBoard() {
    const Level& lv = board_.level();
    if (!lv.valid()) return;
    const Layout l = layout();
    ensureSprites(l.tile);
    const int t = l.tile;
    const int w = lv.width;
    auto at = [&](float x, float y) {
        return SDL_Rect{l.ox + int(std::lround(x * float(t))), l.oy + int(std::lround(y * float(t))), t, t};
    };

    // Soft drop shadow under the whole room.
    const int so = std::max(2, t / 10);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 80);
    for (int y = 0; y < lv.height; ++y)
        for (int x = 0; x < w; ++x)
            if (lv.tiles[size_t(y * w + x)] != Tile::Void) {
                SDL_Rect r{l.ox + x * t + so, l.oy + y * t + so, t, t};
                SDL_RenderFillRect(renderer_, &r);
            }

    for (int y = 0; y < lv.height; ++y) {
        for (int x = 0; x < w; ++x) {
            const int c = y * w + x;
            const SDL_Rect r = at(float(x), float(y));
            const Tile tile = lv.tiles[size_t(c)];
            if (tile == Tile::Wall) {
                SDL_RenderCopy(renderer_, sprites_.wall, nullptr, &r);
            } else if (tile == Tile::Floor) {
                SDL_RenderCopy(renderer_, sprites_.floor[(x + y) & 1], nullptr, &r);
                if (lv.isGoal(c)) {
                    SDL_RenderCopy(renderer_, sprites_.puddle, nullptr, &r);
                    if (board_.boxAt(c) < 0 && ripple_) {
                        const float phase = std::fmod(time_ * 0.55f + float((c * 37) % 100) / 100.f, 1.f);
                        const float size = float(t) * (0.25f + 0.55f * phase);
                        SDL_Rect rr{r.x + int((float(t) - size) * 0.5f), r.y + int(float(t) * 0.57f - size * 0.5f),
                                    int(size), int(size)};
                        SDL_SetTextureAlphaMod(ripple_, Uint8(150.f * (1.f - phase)));
                        SDL_RenderCopy(renderer_, ripple_, nullptr, &rr);
                    }
                }
            }
        }
    }

    renderHint(l, false);

    const float k = anim_.active ? easeOut(anim_.t / anim_.duration) : 1.f;
    const std::vector<int>& boxes = board_.boxes();
    for (size_t i = 0; i < boxes.size(); ++i) {
        float bx = float(boxes[i] % w), by = float(boxes[i] / w);
        const bool moving = anim_.active && anim_.box == int(i);
        if (moving) {
            const float fx = float(anim_.boxFrom % w), fy = float(anim_.boxFrom / w);
            bx = fx + (bx - fx) * k;
            by = fy + (by - fy) * k;
        }
        const bool soaked = !moving && lv.isGoal(boxes[i]);
        const SDL_Rect r = at(bx, by);
        SDL_RenderCopy(renderer_, soaked ? sprites_.soaked : sprites_.bun, nullptr, &r);
    }

    float px = float(board_.player() % w), py = float(board_.player() / w);
    float lift = std::sin(time_ * 4.f) * 0.02f;
    if (anim_.active) {
        const float fx = float(anim_.from % w), fy = float(anim_.from / w);
        px = fx + (px - fx) * k;
        py = fy + (py - fy) * k;
        lift = -std::sin(kPi * k) * 0.07f;
    }
    const SDL_Rect hr = at(px, py + lift);
    SDL_RenderCopy(renderer_, sprites_.hero[facing_], nullptr, &hr);
    renderHint(l, true);

    if (dot_) {
        SDL_SetTextureColorMod(dot_, 140, 215, 255);
        for (const Particle& p : particles_) {
            const float alpha = clamp01(p.life / p.maxLife);
            const int size = std::max(2, int(p.size * float(t)));
            SDL_Rect r{l.ox + int(p.x * float(t)) - size / 2, l.oy + int(p.y * float(t)) - size / 2, size, size};
            SDL_SetTextureAlphaMod(dot_, Uint8(255.f * alpha));
            SDL_RenderCopy(renderer_, dot_, nullptr, &r);
        }
    }
}

void Game::renderHud() {
    const int s = uiScale();
    const int bar = 15 * s;
    SDL_SetRenderDrawColor(renderer_, 6, 16, 30, 200);
    SDL_Rect r{0, 0, viewW_, bar};
    SDL_RenderFillRect(renderer_, &r);

    const int y = (bar - 7 * s) / 2;
    const int pad = 5 * s;
    char buf[96];
    std::snprintf(buf, sizeof buf, "LEVEL %d", levelNum_);
    drawText(buf, pad, y, s, rgba(255, 210, 110));
    std::snprintf(buf, sizeof buf, "MOVES %d   PUSHES %d", board_.moves(), board_.pushes());
    drawText(buf, viewW_ / 2, y, s, rgba(235, 240, 250), 0);
    std::snprintf(buf, sizeof buf, "SOAKED %d/%d", board_.boxesOnGoals(), int(board_.boxes().size()));
    drawText(buf, viewW_ - pad, y, s, rgba(140, 215, 255), 1);

    const std::string help = std::string(SOAK_ARROW_LEFT SOAK_ARROW_UP SOAK_ARROW_DOWN SOAK_ARROW_RIGHT
                                         " MOVE   U UNDO   R RESTART   H HINT   M ") +
                             (audio_.muted() ? "UNMUTE" : "MUTE") + "   ESC MENU";
    const int hs = fitScale(help, viewW_ - 4 * s, s >= 3 ? s - 1 : s);
    drawText(help, viewW_ / 2, viewH_ - 7 * hs - 3 * s, hs, rgba(190, 215, 240, 200), 0);
}

void Game::renderWon() {
    SDL_SetRenderDrawColor(renderer_, 0, 10, 25, 120);
    SDL_Rect full{0, 0, viewW_, viewH_};
    SDL_RenderFillRect(renderer_, &full);

    const int s = uiScale();
    const Level& lv = board_.level();
    const std::string title = "SOAKED!";
    char l1[64], l2[64], l3[64];
    std::snprintf(l1, sizeof l1, "LEVEL %d COMPLETE", levelNum_);
    std::snprintf(l2, sizeof l2, "MOVES %d   PUSHES %d", board_.moves(), board_.pushes());
    const bool perfect = board_.pushes() <= lv.optimalPushes;
    if (perfect) std::snprintf(l3, sizeof l3, "PERFECT! FEWEST PUSHES POSSIBLE");
    else std::snprintf(l3, sizeof l3, "FEWEST POSSIBLE: %d PUSHES", lv.optimalPushes);
    const std::string keys = "ENTER NEXT LEVEL   R REPLAY   ESC MENU";

    const int ts = fitScale(title, viewW_ * 8 / 10, s * 3);
    const int bs = fitScale(keys, viewW_ * 85 / 100 - 16 * s, s);
    int pw = std::max({textWidth(title, ts), textWidth(l1, bs), textWidth(l2, bs), textWidth(l3, bs),
                       textWidth(keys, bs)}) +
             16 * s;
    pw = std::min(pw, viewW_ - 2 * s);
    const int ph = 8 * s + 7 * ts + 6 * s + 7 * bs + 4 * s + 7 * bs + 4 * s + 7 * bs + 9 * s + 7 * bs + 8 * s;
    ensurePanel(pw, ph);
    const int px = (viewW_ - pw) / 2, py = (viewH_ - ph) / 2;
    if (panel_) {
        SDL_Rect r{px, py, pw, ph};
        SDL_RenderCopy(renderer_, panel_, nullptr, &r);
    }
    int y = py + 8 * s;
    const int cx = viewW_ / 2;
    drawText(title, cx, y, ts, rgba(255, 210, 110), 0);
    y += 7 * ts + 6 * s;
    drawText(l1, cx, y, bs, rgba(235, 240, 250), 0);
    y += 7 * bs + 4 * s;
    drawText(l2, cx, y, bs, rgba(235, 240, 250), 0);
    y += 7 * bs + 4 * s;
    drawText(l3, cx, y, bs, perfect ? rgba(140, 255, 170) : rgba(165, 220, 255), 0);
    y += 7 * bs + 9 * s;
    drawText(keys, cx, y, bs, rgba(255, 220, 120), 0);
}

// Under the buns: footsteps to where the push starts and a gold halo around
// the bun. Over them: an arrow showing the push direction.
void Game::renderHint(const Layout& l, bool overBuns) {
    if (!hint_.active) return;
    const Level& lv = board_.level();
    const int t = l.tile, w = lv.width;
    const float pulse = 0.5f + 0.5f * std::sin(time_ * 6.f);
    const float bx = float(hint_.boxCell % w), by = float(hint_.boxCell / w);

    if (overBuns) {
        if (!sprites_.arrow) return;
        const float reach = 0.62f + 0.08f * pulse;
        const float cx = (bx + 0.5f + float(kDirX[hint_.dir]) * reach) * float(t);
        const float cy = (by + 0.5f + float(kDirY[hint_.dir]) * reach) * float(t);
        const int size = std::max(8, int(float(t) * 0.62f));
        SDL_Rect r{l.ox + int(cx) - size / 2, l.oy + int(cy) - size / 2, size, size};
        SDL_RenderCopyEx(renderer_, sprites_.arrow, nullptr, &r, double(hint_.dir - 1) * 90.0, nullptr,
                         SDL_FLIP_NONE);
        return;
    }

    // Shortest walk from the player to the cell behind the bun.
    const int stand = hint_.boxCell - lv.offset(hint_.dir);
    std::vector<int> via(size_t(lv.cellCount()), -1);
    std::vector<int> queue{board_.player()};
    via[size_t(board_.player())] = board_.player();
    for (size_t qi = 0; qi < queue.size() && via[size_t(stand)] < 0; ++qi) {
        for (int d = 0; d < 4; ++d) {
            const int c = queue[qi] + lv.offset(d);
            if (!lv.isFloor(c) || board_.boxAt(c) >= 0 || via[size_t(c)] >= 0) continue;
            via[size_t(c)] = queue[qi];
            queue.push_back(c);
        }
    }
    if (dot_ && via[size_t(stand)] >= 0) {
        SDL_SetTextureColorMod(dot_, 255, 214, 90);
        SDL_SetTextureAlphaMod(dot_, Uint8(170 + 60 * pulse));
        const int size = std::max(3, t / 6);
        for (int c = stand; c != board_.player(); c = via[size_t(c)]) {
            SDL_Rect r{l.ox + (c % w) * t + (t - size) / 2, l.oy + (c / w) * t + (t - size) / 2, size, size};
            SDL_RenderCopy(renderer_, dot_, nullptr, &r);
        }
    }
    SDL_SetRenderDrawColor(renderer_, 255, 200, 60, Uint8(70 + 50 * pulse));
    SDL_Rect glow{l.ox + int(bx) * t, l.oy + int(by) * t, t, t};
    SDL_RenderFillRect(renderer_, &glow);
    if (ripple_) {
        const float size = float(t) * (1.2f + 0.12f * pulse);
        SDL_Rect r{l.ox + int((bx + 0.5f) * float(t) - size * 0.5f), l.oy + int((by + 0.66f) * float(t) - size * 0.5f),
                   int(size), int(size)};
        SDL_SetTextureColorMod(ripple_, 255, 205, 70);
        SDL_SetTextureAlphaMod(ripple_, Uint8(190 + 65 * pulse));
        SDL_RenderCopy(renderer_, ripple_, nullptr, &r);
        SDL_SetTextureColorMod(ripple_, 255, 255, 255);
    }
}

void Game::renderCaption() {
    if (caption_.empty() || time_ < captionStart_ || time_ >= captionEnd_) return;
    const float alpha = clamp01(std::min((time_ - captionStart_) / 0.25f, (captionEnd_ - time_) / 0.35f));
    const int s = uiScale();
    const int cs = fitScale(caption_, viewW_ - 20 * s, s + (s + 1) / 2);
    const int w = textWidth(caption_, cs);
    const int y = viewH_ - 13 * s - 7 * cs - 8 * s;
    SDL_SetRenderDrawColor(renderer_, 5, 15, 30, Uint8(185.f * alpha));
    SDL_Rect pill{viewW_ / 2 - w / 2 - 6 * s, y - 3 * s, w + 12 * s, 7 * cs + 6 * s};
    SDL_RenderFillRect(renderer_, &pill);
    drawText(caption_, viewW_ / 2, y, cs, rgba(255, 228, 150, Uint8(255.f * alpha)), 0);
}

}  // namespace soak
