#!/usr/bin/env python3
"""Exercise the actual NPC attachment methods with lightweight native stubs.

The extracted production methods own visibility decisions. Stubs replace only
inventory, settings and scene attachment services; this is not rendered evidence.
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
METHODS = (
    "void NpcAnimation::showWeapons(bool showWeapon)",
    "bool NpcAnimation::updateCarriedLeftVisible(ESM::RefId weaptype) const",
    "void NpcAnimation::showCarriedLeft(bool show)",
    "void NpcAnimation::equipmentChanged()",
)


def extract_method(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        if end == len(source):
            raise ValueError(f"unterminated method: {signature}")
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


STUBS = r'''
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace ESM
{
    using RefId = int;
    enum Part { PRT_Weapon, PRT_Shield, PRT_Count };
    struct WeaponType
    {
        static constexpr int TwoHanded = 1;
        static constexpr RefId MarksmanCrossbow = 3;
        int mFlags = 0;
        RefId mAmmoType = 4;
    };
    struct Weapon
    {
        static constexpr int sRecordId = 1;
        struct { RefId mType = 1; } mData;
    };
    struct Armor { static constexpr int sRecordId = 2; };
    struct Light { static constexpr int sRecordId = 3; };
    struct NPC { bool isMale() const { return true; } };
}
namespace VFS::Path { using Normalized = std::string; }
namespace osg
{
    struct Vec4f {};
    struct Group
    {
        bool shieldBone = false;
        Group* asGroup() { return this; }
        template<class Visitor> void accept(Visitor& visitor)
        {
            visitor.mFoundNode = shieldBone ? this : nullptr;
        }
    };
}
namespace SceneUtil
{
    struct FindByNameVisitor
    {
        explicit FindByNameVisitor(const char*) {}
        osg::Group* mFoundNode = nullptr;
    };
    struct LightCommon { explicit LightCommon(const ESM::Light&) {} };
}
namespace Settings
{
    struct Game { bool mShieldSheathing = false; };
    Game& game() { static Game instance; return instance; }
}
namespace MWMechanics
{
    enum class DrawState { Nothing, Weapon, Spell };
    struct Input { bool mTwoHanded = false; bool mTwoHandedLeft = false; };
    struct CreatureStats
    {
        DrawState draw = DrawState::Nothing;
        Input input;
        bool attacking = false;
        DrawState getDrawState() const { return draw; }
        const Input& getEldenCombatInput() const { return input; }
        void setAttackingOrSpell(bool value) { attacking = value; }
    };
}
namespace MWWorld
{
    struct InventoryStore;
    struct Class;
    template<class T> struct LiveCellRef
    {
        const T* mBase;
        const LiveCellRef* operator->() const { return this; }
    };
    struct Ptr
    {
        int type = 0;
        int identity = 0;
        InventoryStore* inventory = nullptr;
        MWMechanics::CreatureStats* stats = nullptr;
        ESM::Weapon weapon;
        ESM::Light light;
        int getType() const { return type; }
        const Class& getClass() const;
        bool operator==(const Ptr& other) const { return identity == other.identity; }
        template<class T> LiveCellRef<T> get() const
        {
            if constexpr (std::is_same_v<T, ESM::Weapon>) return { &weapon };
            else return { &light };
        }
    };
    using ConstContainerStoreIterator = const Ptr*;
    struct InventoryStore
    {
        enum { Slot_CarriedRight, Slot_CarriedLeft, Slot_Ammunition };
        std::array<Ptr, 3> slots;
        ConstContainerStoreIterator getSlot(int slot) const
        {
            return slots[slot].type ? &slots[slot] : nullptr;
        }
        ConstContainerStoreIterator end() const { return nullptr; }
    };
    struct Class
    {
        InventoryStore& getInventoryStore(const Ptr& ptr) const { return *ptr.inventory; }
        MWMechanics::CreatureStats& getCreatureStats(const Ptr& ptr) const { return *ptr.stats; }
        osg::Vec4f getEnchantmentColor(const Ptr&) const { return {}; }
        VFS::Path::Normalized getCorrectedModel(const Ptr&) const { return "item.nif"; }
        std::string getEnchantment(const Ptr&) const { return {}; }
    };
    const Class& Ptr::getClass() const { static Class instance; return instance; }
}
namespace MWMechanics
{
    bool enabled = true;
    MWWorld::Ptr player;
    bool useEldenCombat(const MWWorld::Ptr&) { return enabled; }
    bool useEldenMelee(const MWWorld::Ptr& ptr)
    {
        return enabled && ptr.stats->draw == DrawState::Weapon;
    }
    const MWWorld::Ptr& getPlayer() { return player; }
    void getActiveWeapon(const MWWorld::Ptr& ptr, ESM::RefId* type)
    {
        *type = 0;
        if (ptr.stats->draw == DrawState::Weapon)
            *type = ptr.inventory->slots[MWWorld::InventoryStore::Slot_CarriedRight].weapon.mData.mType;
        else if (ptr.stats->draw == DrawState::Spell)
            *type = 2;
    }
    const ESM::WeaponType* getWeaponType(ESM::RefId type)
    {
        static const ESM::WeaponType oneHanded;
        static const ESM::WeaponType twoHanded { ESM::WeaponType::TwoHanded };
        return type == 2 ? &twoHanded : &oneHanded;
    }
}
namespace MWRender
{
    struct PartHolder
    {
        osg::Group node;
        osg::Group* getNode() { return &node; }
    };
    struct Ammunition { void reset() {} };
    class NpcAnimation
    {
    public:
        enum ViewMode { VM_Normal, VM_FirstPerson };
        MWWorld::InventoryStore inventory;
        MWMechanics::CreatureStats stats;
        MWWorld::Ptr mPtr;
        bool mShowWeapons = false;
        bool mShowCarriedLeft = true;
        bool mEldenInventoryPreview = false;
        std::array<std::unique_ptr<PartHolder>, ESM::PRT_Count> mObjectParts;
        Ammunition mAmmunition;
        ESM::NPC npc;
        const ESM::NPC* mNpc = &npc;
        osg::Group root;
        osg::Group* mObjectRoot = &root;
        ViewMode mViewMode = VM_Normal;
        bool sheathedShieldMesh = false;
        bool holsteredWeapon = false;
        bool holsteredShield = false;
        int extraLights = 0;
        NpcAnimation()
        {
            mPtr.identity = 1;
            mPtr.inventory = &inventory;
            mPtr.stats = &stats;
            MWMechanics::player = mPtr;
            inventory.slots[MWWorld::InventoryStore::Slot_CarriedRight].type = ESM::Weapon::sRecordId;
        }
        void showWeapons(bool showWeapon);
        bool updateCarriedLeftVisible(ESM::RefId weaptype) const;
        void showCarriedLeft(bool show);
        void equipmentChanged();
        void removeIndividualPart(ESM::Part part) { mObjectParts[part].reset(); }
        bool addOrReplaceIndividualPart(ESM::Part part, int, int, const VFS::Path::Normalized&,
            bool, osg::Vec4f*, bool = false)
        {
            mObjectParts[part] = std::make_unique<PartHolder>();
            return true;
        }
        void reserveIndividualPart(ESM::Part, int, int) {}
        void addExtraLight(osg::Group*, const SceneUtil::LightCommon&) { ++extraLights; }
        void attachArrow() {}
        void updateQuiver() {}
        void updateHolsteredWeapon(bool show) { holsteredWeapon = show; }
        void updateHolsteredShield(bool carried)
        {
            holsteredShield = Settings::game().mShieldSheathing && !carried
                && inventory.slots[MWWorld::InventoryStore::Slot_CarriedLeft].type == ESM::Armor::sRecordId;
        }
        VFS::Path::Normalized getShieldMesh(const MWWorld::Ptr&, bool) const { return "shield.nif"; }
        std::string getSheathedShieldMesh(const MWWorld::Ptr&) const
        {
            return sheathedShieldMesh ? "sheathed-shield.nif" : "";
        }
        // Scene rebuilding clears meshes, then calls these methods in production order.
        void updateParts()
        {
            mObjectParts = {};
            showWeapons(mShowWeapons);
            showCarriedLeft(mShowCarriedLeft);
        }
        bool leftVisible() const { return bool(mObjectParts[ESM::PRT_Shield]); }
        bool rightVisible() const { return bool(mObjectParts[ESM::PRT_Weapon]); }
        void leftItem(int type) { inventory.slots[MWWorld::InventoryStore::Slot_CarriedLeft].type = type; }
    };
'''


CHECKS = r'''
}
int assertions = 0;
void expect(bool condition, const char* message)
{
    ++assertions;
    if (!condition) throw std::runtime_error(message);
}
int main()
{
    try
    {
        using MWMechanics::DrawState;
        using MWRender::NpcAnimation;
        for (auto view : { NpcAnimation::VM_Normal, NpcAnimation::VM_FirstPerson })
        {
            MWMechanics::enabled = true;
            Settings::game().mShieldSheathing = false;
            NpcAnimation npc;
            npc.mViewMode = view;
            npc.leftItem(ESM::Weapon::sRecordId);
            npc.equipmentChanged();
            expect(!npc.leftVisible() && !npc.mShowCarriedLeft, "equipping a sheathed left weapon shows it");
            npc.stats.draw = DrawState::Weapon;
            npc.showWeapons(false);
            expect(!npc.leftVisible(), "left weapon appears before equip-attach");
            npc.showWeapons(true);
            expect(npc.rightVisible() && npc.leftVisible(), "equip-attach did not show both weapons");
            npc.updateParts();
            expect(npc.rightVisible() && npc.leftVisible(), "drawn rebuild lost a weapon");
            // Draw state changes before native unequip-detach; retain both until that key.
            npc.stats.draw = DrawState::Nothing;
            npc.showCarriedLeft(npc.updateCarriedLeftVisible(0));
            expect(npc.rightVisible() && npc.leftVisible(), "draw state alone hid the left weapon");
            npc.showWeapons(false);
            expect(!npc.rightVisible() && !npc.leftVisible(), "unequip-detach left a weapon visible");
            npc.showCarriedLeft(true);
            expect(!npc.leftVisible() && !npc.mShowCarriedLeft, "stale left-show bypassed weapon sheathing");
            npc.updateParts();
            expect(!npc.leftVisible(), "sheathed rebuild resurrected left weapon");
            npc.equipmentChanged();
            expect(!npc.leftVisible(), "sheathed inventory change resurrected left weapon");
            npc.stats.draw = DrawState::Weapon;
            npc.showWeapons(true);
            expect(npc.rightVisible() && npc.leftVisible(), "redraw did not restore both weapons");
            npc.stats.input = { true, false };
            npc.showWeapons(true);
            expect(npc.rightVisible() && !npc.leftVisible(), "right two-hand grip shows left weapon");
            npc.stats.input = { true, true };
            npc.showWeapons(true);
            expect(!npc.rightVisible() && npc.leftVisible(), "left two-hand grip incorrectly uses right part visibility");
            npc.updateParts();
            expect(!npc.rightVisible() && npc.leftVisible(), "rebuild lost selected-left two-hand weapon");
            npc.stats.draw = DrawState::Nothing;
            npc.showWeapons(false);
            expect(!npc.rightVisible() && !npc.leftVisible(), "left two-hand grip did not sheathe");
            npc.stats.draw = DrawState::Weapon;
            npc.showWeapons(true);
            expect(!npc.rightVisible() && npc.leftVisible(), "left two-hand grip did not redraw");
            npc.stats.draw = DrawState::Spell;
            npc.showWeapons(false);
            npc.showCarriedLeft(true);
            expect(!npc.leftVisible(), "spell stance resurrected left weapon");
            npc.stats.input = {};
            npc.stats.draw = DrawState::Nothing;
            MWMechanics::enabled = false;
            npc.leftItem(ESM::Armor::sRecordId);
            npc.equipmentChanged();
            expect(npc.leftVisible(), "hidden weapon to shield swap kept shield hidden with combat disabled");
            npc.showWeapons(false);
            expect(npc.leftVisible(), "weapon sheathing hid an ordinary shield");
            npc.showCarriedLeft(false);
            npc.showWeapons(true);
            expect(!npc.leftVisible(), "weapon attach overrode a shield visibility event");
            npc.showCarriedLeft(true);
            npc.showWeapons(false);
            expect(npc.leftVisible(), "weapon detach overrode a shield visibility event");
            npc.leftItem(ESM::Weapon::sRecordId);
            npc.equipmentChanged();
            expect(!npc.leftVisible(), "combat-disabled sheathed weapon became visible");
            npc.leftItem(ESM::Light::sRecordId);
            npc.equipmentChanged();
            expect(npc.leftVisible() && npc.extraLights > 0, "hidden weapon to torch swap lost torch or light");
            npc.showWeapons(false);
            expect(npc.leftVisible(), "sheathing hid torch");
            npc.showCarriedLeft(false);
            npc.showWeapons(true);
            expect(!npc.leftVisible(), "weapon attach overrode hidden torch");
            // Existing optional shield sheathing retains its own decision and attachment.
            npc.stats.draw = DrawState::Nothing;
            Settings::game().mShieldSheathing = true;
            npc.root.shieldBone = true;
            npc.sheathedShieldMesh = true;
            npc.leftItem(ESM::Armor::sRecordId);
            npc.equipmentChanged();
            expect(!npc.leftVisible() && npc.holsteredShield, "native shield sheathing changed");
            // Inventory preview intentionally draws weapons even when the actor is sheathed.
            Settings::game().mShieldSheathing = false;
            npc.leftItem(ESM::Weapon::sRecordId);
            npc.showWeapons(true);
            npc.updateParts();
            npc.showCarriedLeft(true);
            expect(npc.leftVisible() && npc.rightVisible(), "inventory preview cannot show equipped weapons");
            npc.mEldenInventoryPreview = true;
            for (const auto draw : { DrawState::Nothing, DrawState::Weapon })
            {
                npc.stats.draw = draw;
                npc.stats.input = { true, false };
                npc.showWeapons(true);
                npc.updateParts();
                expect(npc.rightVisible() && !npc.leftVisible(), "preview lost selected-right two-hand visibility");
                npc.stats.input = { true, true };
                npc.showWeapons(true);
                npc.updateParts();
                expect(!npc.rightVisible() && npc.leftVisible(), "preview lost selected-left two-hand visibility");
                npc.stats.input = {};
                npc.showWeapons(true);
                npc.updateParts();
                expect(npc.rightVisible() && npc.leftVisible(), "preview lost ordinary dual-wield visibility");
            }
            NpcAnimation leftOnly;
            leftOnly.mViewMode = view;
            leftOnly.inventory.slots[MWWorld::InventoryStore::Slot_CarriedRight].type = 0;
            leftOnly.leftItem(ESM::Weapon::sRecordId);
            leftOnly.stats.draw = DrawState::Weapon;
            // Character initialization establishes drawn visibility before the left policy.
            leftOnly.showWeapons(true);
            leftOnly.showCarriedLeft(leftOnly.updateCarriedLeftVisible(0));
            expect(leftOnly.leftVisible() && !leftOnly.rightVisible(), "drawn offhand-only initialization lost weapon");
            leftOnly.showWeapons(false);
            expect(!leftOnly.leftVisible(), "offhand-only weapon did not sheathe");
        }
        std::cout << "Passed " << assertions << " NPC attachment assertions using production method bodies\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "NPC attachment regression after " << assertions << " assertions: " << error.what() << '\n';
        return 1;
    }
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "apps/openmw/mwrender/npcanimation.cpp")
    parser.add_argument("--compiler", default=os.environ.get("CXX") or shutil.which("c++"), help="C++20 compiler")
    args = parser.parse_args()
    if not args.compiler:
        parser.error("a C++20 compiler is required")
    source = args.source.read_text(encoding="utf-8")
    methods = "\n\n".join(extract_method(source, method) for method in METHODS)
    with tempfile.TemporaryDirectory(prefix="elden-weapon-visibility-") as directory:
        fixture = Path(directory) / "visibility.cpp"
        binary = Path(directory) / "visibility"
        fixture.write_text(STUBS + methods + CHECKS, encoding="utf-8")
        subprocess.run([args.compiler, "-std=c++20", "-O0", "-Wall", "-Wextra", "-Werror",
                        str(fixture), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
