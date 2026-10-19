// Dynamic Input Buffer – Geode mod
// Hooks:
//   GJBaseGameLayer::handleButton  -> every jump press/release goes through here
//   GJBaseGameLayer::processCommands -> runs once per physics step (our "tick" clock)
//   PlayLayer::init / onQuit        -> create / reset overlay and state
#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/loader/SettingV3.hpp>

using namespace geode::prelude;

namespace {
    constexpr int MODES[] = {0, 2, 4, 8, 16};
    constexpr int MODE_COUNT = 5;

    struct BufferState {
        int modeIdx = 0;          // index into MODES
        int tick = 0;             // physics-step counter
        bool pending = false;     // a press is stored and waiting
        bool pendingP1 = true;    // which player the press belongs to
        int pendingSince = 0;     // tick at which the press was recorded
        int pendingButton = 1;    // PlayerButton::Jump
        int lastSaved = 0;        // ticks "gained" on the last executed buffered input
        bool replaying = false;   // guard against re-entrancy

        int window() const { return MODES[modeIdx]; }
        void clear() { pending = false; }
    };

    BufferState g;
    CCLabelBMFont* g_label = nullptr;

    std::string modeText() {
        if (g.window() == 0) return "Off";
        return fmt::format("+{} Ticks", g.window());
    }

    void refreshOverlay() {
        if (!g_label) return;
        g_label->setString(fmt::format(
            "Buffer: {}\nSaved: {} ticks", modeText(), g.lastSaved
        ).c_str());
    }

    // Can the player perform a jump *right now*?  (ground-based check; orbs/pads are
    // handled by the game's own queued-input logic since we re-send the press each tick.)
    bool canAct(PlayerObject* p) {
        return p && (p->m_isOnGround || p->m_isOnGround2);
    }
}

class $modify(DBGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        if (g.replaying || g.window() == 0 || button != 1) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        if (down) {
            auto* p = isPlayer1 ? m_player1 : m_player2;
            // Always forward the press (normal behaviour, 0 extra ticks).
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            // If the player can't act yet, remember the press for up to N ticks.
            if (!canAct(p)) {
                g.pending = true;
                g.pendingP1 = isPlayer1;
                g.pendingSince = g.tick;
                g.pendingButton = button;
            } else {
                g.clear();
            }
        } else {
            // Release cancels a stored press that has not fired yet.
            g.clear();
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
        }
    }

    void processCommands(float dt) {
        GJBaseGameLayer::processCommands(dt);
        g.tick++;

        if (!g.pending || g.window() == 0) return;

        int age = g.tick - g.pendingSince;
        if (age > g.window()) { g.clear(); return; }   // window expired

        auto* p = g.pendingP1 ? m_player1 : m_player2;
        if (canAct(p)) {
            // Player can act now -> fire the stored press inside the extended window.
            g.replaying = true;
            GJBaseGameLayer::handleButton(false, g.pendingButton, g.pendingP1);
            GJBaseGameLayer::handleButton(true, g.pendingButton, g.pendingP1);
            g.replaying = false;
            g.lastSaved = age;
            g.clear();
            refreshOverlay();
        }
    }
};

class $modify(DBPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        g.tick = 0;
        g.lastSaved = 0;
        g.clear();

        g_label = CCLabelBMFont::create("", "chatFont.fnt");
        g_label->setAnchorPoint({0.f, 1.f});
        g_label->setScale(0.6f);
        g_label->setOpacity(180);
        g_label->setAlignment(kCCTextAlignmentLeft);
        auto win = CCDirector::get()->getWinSize();
        g_label->setPosition({8.f, win.height - 8.f});
        g_label->setZOrder(1000);
        this->getParent() ? this->getParent()->addChild(g_label) : this->addChild(g_label);
        refreshOverlay();
        return true;
    }

    void onQuit() {
        g_label = nullptr;   // node is removed together with its parent scene
        g.clear();
        PlayLayer::onQuit();
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        g.clear();
    }
};

// F6 (configurable) cycles the mode: Off -> +2 -> +4 -> +8 -> +16 -> Off
$execute {
    listenForKeybindSettingPresses("cycle-key", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat) return;
        g.modeIdx = (g.modeIdx + 1) % MODE_COUNT;
        g.clear();
        refreshOverlay();
    });
}
