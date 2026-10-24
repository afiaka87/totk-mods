// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "BombPickup.hpp"
#include "BombWheelPickup.hpp"
#include "totk/engine/CodeCheck.hpp"
#include <lib.hpp>
#include <cmath>
#include <cstring>
namespace remote_bomb::pickup {
namespace {
using namespace totk::engine;
constexpr CodeSite sites[]{
    {0xAB92CC,0xA9BA7BFD,0xA9016FFC}, {0x15D1FC8,0x71002C3F,0x54000268},
    {0x1433D58,0xF9411408,0xB4000068}, {0xADF114,0xD10243FF,0xA9037BFD},
    {0x11BD100,0xF9411408,0xB4000068},
    {0x1152394,0xF9411408,0xB4000068}, {0xB7D3EC,0xD10103FF,0xA9017BFD},
};
struct CreateArgument {
    float position[3], rotation[9], scale[3];
    const char* name;
    unsigned char opaque48[0x10];
    void *blackboard, *opaque60, *parent, *dependent, *creator, *rtti, *heap;
    unsigned char flags, padding[7];
};
static_assert(sizeof(CreateArgument) == 0x98 && offsetof(CreateArgument,flags) == 0x90);
}
bool validate(std::uintptr_t base) { return codeMatches(base,sites,"remote-bomb","PICKUP_BIND_REFUSED"); }
bool ready(std::uintptr_t base, engine::Handle player) {
    const auto report = [](unsigned reason, unsigned ground = 0, unsigned movement = 0) {
        static unsigned previous = ~0u, reports = 0;
        if (reason != previous && reports < 48) {
            previous = reason; ++reports;
            Logging.Log("[remote-bomb] WHEEL_READY reason=%u grounded=%x movement=%x",reason,ground,movement);
        }
        return reason == 0;
    };
    if (!player.isCurrent(player.scene)) return report(1);
    const auto component = reinterpret_cast<std::uintptr_t(*)(std::uintptr_t)>(base+0x11BD100)(player.address);
    if (!isPlausibleAddress(component)) return report(2);
    const auto rider = reinterpret_cast<std::uintptr_t(*)(std::uintptr_t)>(base+0x1152394)(player.address);
    if (isPlausibleAddress(rider) && reinterpret_cast<bool(*)(std::uintptr_t)>(base+0xB7D3EC)(rider))
        return report(3);
    const auto velocity = readMemory<engine::Position>(player.address+layout::kActorLinearVelocity);
    // Native grounded state; no automatic pickup during swimming, climbing, riding or a fall.
    const auto movement = readMemory<std::uint16_t>(component+5204);
    const auto ground = readMemory<unsigned>(component+0xE50);
    const auto reason = !(ground&1) ? 4u : (movement&0x60f) ? 5u :
        !(std::isfinite(velocity.x) && std::isfinite(velocity.y) && std::isfinite(velocity.z)) ? 6u : 0u;
    return report(reason,ground,movement);
}
bool spawn(std::uintptr_t base, engine::Handle player, Shape shape, Upgrade upgrade) {
    if (!ready(base,player)) return false;
    const auto manager = readMemory<std::uintptr_t>(base+0x4722920);
    if (!isPlausibleAddress(manager)) return false;
    CreateArgument argument{};
    const auto position = engine::position(player);
    float rotation[9];
    std::memcpy(rotation,reinterpret_cast<const void*>(player.address+layout::kActorRotation),sizeof(rotation));
    const float length = std::sqrt(rotation[2]*rotation[2]+rotation[8]*rotation[8]);
    if (!std::isfinite(length) || length < .01f) return false;
    argument.position[0] = position.x+rotation[2]/length*1.2f;
    argument.position[1] = position.y+.4f;
    argument.position[2] = position.z+rotation[8]/length*1.2f;
    argument.rotation[0] = argument.rotation[4] = argument.rotation[8] = 1;
    for (auto& scale : argument.scale) scale = 1;
    argument.name = "";
    argument.flags = 7;
    const char* name = engine::actorName(shape,upgrade);
    unsigned result{};
    void* preactor{};
    using Create = bool(*)(std::uintptr_t,const char**,const CreateArgument*,void*,unsigned,
                          void*,void*,void*,bool,unsigned*,void**);
    const bool accepted = reinterpret_cast<Create>(base+0xAB92CC)(manager,&name,&argument,nullptr,1,
        nullptr,nullptr,nullptr,false,&result,&preactor);
    Logging.Log("[remote-bomb] WHEEL_SPAWN actor=%s accepted=%u result=%u",name,unsigned(accepted),result);
    return accepted;
}
bool focused(std::uintptr_t base, engine::Handle player, engine::Handle bomb) {
    if (!ready(base,player) || !bomb.isCurrent(player.scene)) return false;
    const auto receiver = reinterpret_cast<std::uintptr_t(*)(std::uintptr_t)>(base+0x1433D58)(player.address);
    if (!isPlausibleAddress(receiver)) return false;
    // Match the highest-priority native attention action, not merely any nearby lift candidate.
    const auto link = reinterpret_cast<std::uintptr_t(*)(std::uintptr_t,std::uint64_t,int,int)>(
        base+0xADF114)(receiver,0,-1,-1);
    if (!isPlausibleAddress(link)) return false;
    const auto client = readMemory<std::uintptr_t>(link);
    return isPlausibleAddress(client) && intendedLift(bomb.address,
        readMemory<std::uintptr_t>(client+8),readMemory<unsigned>(client+52));
}
}
