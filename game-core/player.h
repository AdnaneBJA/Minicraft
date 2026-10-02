#pragma once

#include "geometry.h"
#include "items.h"

#include <optional>
#include <span>

class Events;
class TileMap;

// What the player asks for during one tick: the client fills it from the keyboard; later, the server receives it
// from each client.
struct PlayerInput {
    int moveX = 0;               // -1, 0, 1
    int moveY = 0;
    bool attack = false;         // Space held
    bool attackPressed = false;  // Space went down since the last tick
};

class Player {
public:
    enum class Direction { Down, Up, Left, Right };

    static constexpr float kSize = 16.0f;
    static constexpr float kHitboxX = 4.0f;
    static constexpr float kHitboxY = 8.0f;
    static constexpr float kHitboxWidth = 8.0f;
    static constexpr float kHitboxHeight = 6.0f;
    static constexpr int kMaxHealth = 10;
    static constexpr int kMaxEnergy = 10;
    static constexpr int kMaxHunger = 10;
    static constexpr int kMaxArmor = 100;
    static constexpr float kLightRadius = 40.0f;  // Player.getLightRadius: 5 (x 8 px)

    void setPosition(float x, float y);
    // One 60 Hz tick: stats (energy, hunger, drowning, lava), knockback, then a step in the input's direction.
    // `obstacles` (furniture) block movement like solid tiles. Health lost here shows up in `events`.
    void tick(const PlayerInput& input, const TileMap& map, std::span<const Rect> obstacles, Events& events);

    // Starts a punch in the facing direction if there is energy left, spending 1. Returns false when exhausted.
    bool tryPunch();
    // Minicraft's payStamina: fails if out of energy, otherwise takes up to `cost` (never below 0).
    bool payEnergy(int cost);
    // Shows the slash animation (a punch that didn't hit anything).
    void showSlash();
    // In water or lava (the tile under the player's centre): half speed, and only the head is drawn.
    bool isSwimming() const { return swimming_; }
    // The tile a punch would hit: 12 px in front of the player's centre (Minicraft's INTERACT_DIST).
    Point interactionTile() const;
    // The area a punch reaches for mobs (Minicraft's interaction box with ATTACK_DIST = 20 px).
    Rect attackBox() const;
    // Unit vector of the facing direction (e.g. right = {1, 0}).
    Point facing() const;

    // Hit by a mob, an arrow or a blast: worn armour soaks it up first (Player.doHurt), the rest costs health,
    // and the player is knocked back along (directionX, directionY), unless still in the hurt cooldown. Records the
    // damage numbers and the hurt sound. Returns true if the hit landed.
    bool takeHit(int damage, int directionX, int directionY, Events& events);

    // Eats food that restores `value` hunger (FoodItem.interactOn: 2 energy). False if not hungry or exhausted.
    bool eat(int value);
    // Puts on armour (ArmorItem.interactOn: 9 energy). False if already wearing some or exhausted.
    bool wearArmor(ItemType armor);
    // The armour being worn and the points it has left (0-100).
    const std::optional<ItemType>& armor() const { return armor_; }
    int armorPoints() const { return armorPoints_; }
    int hunger() const { return hunger_; }
    // Light around the player in the dark: 40 px, or a held lantern's.
    float lightRadius() const;

    // The item in the player's hand (Minicraft's activeItem), taken out of the inventory.
    const std::optional<Inventory::Stack>& heldItem() const { return heldItem_; }
    void setHeldItem(std::optional<Inventory::Stack> item) { heldItem_ = item; }
    // Holding furniture: the player walks with it raised over the head (Minicraft's carrySprites).
    bool isCarryingFurniture() const { return heldItem_ && isFurniture(heldItem_->type); }
    // Top-left of the carried furniture's 16x16 sprite: 12 px above the player sprite (Minicraft: furniture.y =
    // yo - 4), sinking with the player in water.
    Vec2 carriedFurniturePosition() const { return {x_, y_ - 12.0f + (swimming_ ? 4.0f : 0.0f)}; }

    int health() const { return health_; }
    int energy() const { return energy_; }
    // Ticks left in the pause after running out of energy (bolts blink meanwhile); 0 when not exhausted.
    int energyRechargeDelay() const { return energyRechargeDelay_; }
    void refillStats();
    // Sets the saved stats (a loaded save); clears any hurt cooldown, knockback or exhaustion pause.
    void restoreStats(int health, int energy, int hunger, std::optional<ItemType> armor, int armorPoints);
    // Takes off the armour (it goes into the death chest when the player dies).
    void removeArmor();
    bool isDead() const { return health_ <= 0; }
    // Minicraft's entity centre (8, 11 inside the sprite), used for effects attached to the player.
    Vec2 center() const { return {x_ + 8.0f, y_ + 11.0f}; }

    // Sprite rectangle (what is drawn).
    Rect bounds() const { return {x_, y_, kSize, kSize}; }
    // Collision box: a small rectangle at the feet, like Minicraft (8x6), so the head can overlap trees.
    Rect hitbox() const { return {x_ + kHitboxX, y_ + kHitboxY, kHitboxWidth, kHitboxHeight}; }

    // What the client needs to draw the player.
    Direction direction() const { return direction_; }
    float walkDistance() const { return walkDistance_; }
    bool isAttacking() const { return attackTicks_ > 0; }        // the slash shows
    Direction attackDirection() const { return attackDirection_; }
    bool isPunching() const { return punchPoseTicks_ > 0; }      // the punching hand shows
    int punchHand() const { return punchHand_; }                 // 0/1, alternating every punch
    bool isHurtFlashing() const;                                 // white just after being hurt
    int ticks() const { return ticks_; }                         // drives the swimming ripple

private:
    // Moves along one axis, stopping flush against the first solid tile in the way.
    void moveX(float delta, const TileMap& map, std::span<const Rect> obstacles);
    void moveY(float delta, const TileMap& map, std::span<const Rect> obstacles);
    // One 60 Hz tick of energy recharge (Minicraft's stamina rules).
    void tickEnergy();
    // One 60 Hz tick of Minicraft+'s hunger: time, walking and low energy wear it down; a full stomach heals and an
    // empty one starves.
    void tickHunger();
    // Loses health unless still in the hurt cooldown; starts the cooldown and the white flash.
    void hurt(int damage);
    void tickKnockback(const TileMap& map, std::span<const Rect> obstacles);

    float x_ = 0.0f;
    float y_ = 0.0f;
    float walkDistance_ = 0.0f;  // pixels walked; drives the 2-frame walk animation
    Direction direction_ = Direction::Down;
    int attackTicks_ = 0;     // ticks left showing the slash
    int punchPoseTicks_ = 0;  // ticks left showing the punching hand (hit or miss)
    int punchHand_ = 0;       // 0/1: which hand the last punch used; alternates every punch
    Direction attackDirection_ = Direction::Down;

    int health_ = kMaxHealth;
    int energy_ = kMaxEnergy;
    int energyRecharge_ = 0;       // ticks accumulated towards the next bolt
    int energyRechargeDelay_ = 0;  // exhaustion pause in ticks
    bool swimming_ = false;
    int hurtTime_ = 0;     // ticks of hurt cooldown left (no more damage meanwhile)
    int knockbackX_ = 0;   // remaining knockback "steps" (Minicraft's xKnockback / yKnockback)
    int knockbackY_ = 0;
    int damageTaken_ = 0;  // health lost during the current tick (drowning, lava, starving)
    bool inLava_ = false;
    int hunger_ = kMaxHunger;
    int hungerStamCount_ = 7;     // "bites" left before losing a hunger point (Minicraft+'s hungerStamCnt)
    int stamHungerTicks_ = 400;   // points left before losing a bite
    int stepCount_ = 0;           // pixels walked since the last walking hunger penalty
    float stepAccumulator_ = 0.0f;
    int hungerChargeDelay_ = 0;   // ticks towards the next heart healed by a full stomach
    int hungerStarveDelay_ = 0;   // ticks towards the next heart lost to starving
    std::optional<ItemType> armor_;
    int armorPoints_ = 0;
    int armorDamageBuffer_ = 0;
    int ticks_ = 0;  // 60 Hz ticks since start; drives the swimming ripple animation
    std::optional<Inventory::Stack> heldItem_;
};
