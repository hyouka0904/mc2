// mc2mod - create, check and inspect MechCommander 2 mods and modpacks.
//
//   mc2mod new <mods_dir> <id> [name...]        scaffold a new mod
//   mc2mod new-pack <modpack.ini> <id> <mod[=req]>...
//                                               write a modpack manifest
//   mc2mod validate <mod_dir>...                check mods (exit 1 on errors)
//   mc2mod list <mods_dir>                      installed mods
//   mc2mod resolve <mods_dir> [--pack <modpack.ini>]
//                                               load order, file overrides,
//                                               problems (exit 1 on errors)
//   mc2mod which <mods_dir> <game_path> [--pack <modpack.ini>]
//                                               which mod provides a file
//
// Exit status: 0 ok, 1 problems found, 2 usage error.

#include "mod_system.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace mc2mod;

static int usage()
{
    fprintf(stderr,
            "usage:\n"
            "  mc2mod new <mods_dir> <id> [name...]\n"
            "  mc2mod new-pack <modpack.ini> <id> <mod[=requirement]>...\n"
            "  mc2mod validate <mod_dir>...\n"
            "  mc2mod list <mods_dir>\n"
            "  mc2mod resolve <mods_dir> [--pack <modpack.ini>]\n"
            "  mc2mod which <mods_dir> <game_path> [--pack <modpack.ini>]\n");
    return 2;
}

static void printErrors(const std::vector<std::string>& errors)
{
    for (const std::string& e : errors)
        printf("error: %s\n", e.c_str());
}

// Builds registry + load order for resolve / which. Returns false on errors.
static bool load(const std::string& mods_dir, const char* pack_path, ModRegistry* reg, LoadOrder* order,
                 std::string* pack_label)
{
    ModpackManifest pack;
    std::vector<std::string> errors;
    if (pack_path && !ModpackManifest::load(pack_path, &pack, &errors)) {
        printErrors(errors);
        return false;
    }
    reg->scan(mods_dir);
    if (pack_path) {
        std::error_code ec;
        if (std::filesystem::is_directory(pack.local_mods_dir, ec))
            reg->scan(pack.local_mods_dir); // the pack's own mods win
        *pack_label = "modpack " + pack.id + " " + pack.version.toString();
    }
    *order = resolveLoadOrder(*reg, pack_path ? selectModpack(pack) : selectEnabled(*reg));
    for (const std::string& e : reg->errors())
        order->errors.push_back(e);
    return order->ok();
}

static const char* option(int argc, char** argv, const char* name)
{
    for (int i = 0; i + 1 < argc; ++i)
        if (!strcmp(argv[i], name))
            return argv[i + 1];
    return nullptr;
}

int main(int argc, char** argv)
{
    if (argc < 2)
        return usage();
    const std::string cmd = argv[1];

    if (cmd == "new" && argc >= 4) {
        std::string name;
        for (int i = 4; i < argc; ++i)
            name += (name.empty() ? "" : " ") + std::string(argv[i]);
        std::string err;
        if (!createModSkeleton(argv[2], argv[3], name, &err)) {
            printf("error: %s\n", err.c_str());
            return 1;
        }
        printf("created %s/%s (edit mod.ini, put files under content/)\n", argv[2], argv[3]);
        return 0;
    }

    if (cmd == "new-pack" && argc >= 5) {
        std::vector<std::string> mods(argv + 4, argv + argc);
        std::string err;
        if (!createModpack(argv[2], argv[3], "", mods, &err)) {
            printf("error: %s\n", err.c_str());
            return 1;
        }
        printf("created %s with %zu mods\n", argv[2], mods.size());
        return 0;
    }

    if (cmd == "validate" && argc >= 3) {
        int bad = 0;
        for (int i = 2; i < argc; ++i) {
            std::vector<std::string> errors = validateMod(argv[i]);
            if (errors.empty()) {
                ModManifest m;
                std::vector<std::string> ignore;
                ModManifest::load(argv[i], &m, &ignore);
                printf("ok: %s (%s %s)\n", argv[i], m.id.c_str(), m.version.toString().c_str());
            } else {
                printErrors(errors);
                ++bad;
            }
        }
        return bad ? 1 : 0;
    }

    if (cmd == "list" && argc == 3) {
        ModRegistry reg;
        reg.scan(argv[2]);
        for (const ModManifest* m : reg.all())
            printf("%-24s %-10s %s%s\n", m->id.c_str(), m->version.toString().c_str(), m->name.c_str(),
                   m->enabled ? "" : "  [disabled]");
        printErrors(reg.errors());
        return reg.errors().empty() ? 0 : 1;
    }

    if ((cmd == "resolve" && argc >= 3) || (cmd == "which" && argc >= 4)) {
        ModRegistry reg;
        LoadOrder order;
        std::string label;
        const bool ok = load(argv[2], option(argc, argv, "--pack"), &reg, &order, &label);
        Overlay overlay;
        overlay.build(order);
        if (cmd == "which") {
            const Overlay::Entry* e = overlay.find(argv[3]);
            if (!e) {
                printf("%s: not provided by any mod (game file)\n", argv[3]);
            } else {
                printf("%s: %s (%s)", argv[3], e->mod_id.c_str(), e->real_path.c_str());
                for (const std::string& o : e->overridden)
                    printf(", overrides %s", o.c_str());
                printf("\n");
            }
            return ok ? 0 : 1;
        }
        if (!label.empty())
            printf("%s\n", label.c_str());
        printf("%s", describe(order, overlay).c_str());
        return ok ? 0 : 1;
    }

    return usage();
}
