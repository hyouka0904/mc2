// SPDX-License-Identifier: GPL-3.0-only
#include "gos_mods.h"

#include "mod_system.h"

#include <filesystem>
#include <memory>
#include <stdio.h>

namespace {

struct ActiveMods {
    mc2mod::LoadOrder order;
    mc2mod::Overlay overlay;
    mc2mod::ModRegistry registry; // owns the manifests `order` points to
};

std::unique_ptr<ActiveMods> g_mods;

} // namespace

bool gos_ModsInit(const char* mods_dir, const char* modpack_file, std::string* report)
{
    g_mods.reset();
    std::unique_ptr<ActiveMods> a(new ActiveMods());
    std::string dir = mods_dir ? mods_dir : "";
    const bool have_pack = modpack_file && *modpack_file;
    std::error_code ec;

    if (dir.empty() && !have_pack) {
        if (!std::filesystem::is_directory("mods", ec)) {
            if (report) *report = "no mods";
            return true; // nothing requested
        }
        dir = "mods";
    }

    mc2mod::ModpackManifest pack;
    std::vector<std::string> errors;
    if (have_pack && !mc2mod::ModpackManifest::load(modpack_file, &pack, &errors)) {
        if (report) {
            *report = "";
            for (const std::string& e : errors) *report += e + "\n";
        }
        return false;
    }

    if (!dir.empty())
        a->registry.scan(dir);
    if (have_pack && std::filesystem::is_directory(pack.local_mods_dir, ec))
        a->registry.scan(pack.local_mods_dir); // the pack's own mods win

    const mc2mod::Selection sel = have_pack ? mc2mod::selectModpack(pack) : mc2mod::selectEnabled(a->registry);
    a->order = mc2mod::resolveLoadOrder(a->registry, sel);
    for (const std::string& e : a->registry.errors())
        a->order.errors.push_back(e);
    a->overlay.build(a->order);

    if (report) {
        *report = have_pack ? "Modpack " + pack.id + " " + pack.version.toString() + "\n" : std::string();
        *report += mc2mod::describe(a->order, a->overlay);
    }
    if (!a->order.ok())
        return false;
    if (!a->order.mods.empty())
        g_mods = std::move(a);
    return true;
}

void gos_ModsShutdown()
{
    g_mods.reset();
}

bool gos_ModsActive()
{
    return g_mods != nullptr;
}

const char* gos_ModResolvePath(const char* game_path)
{
    if (!g_mods || !game_path)
        return nullptr;
    const mc2mod::Overlay::Entry* e = g_mods->overlay.find(game_path);
    return e ? e->real_path.c_str() : nullptr;
}
