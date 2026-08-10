#include "settings/UserSettings.h"

#include <windows.h>

#include <cstdio>

int wmain(int argc, wchar_t* argv[])
{
    if (argc != 2 || argv[1] == nullptr || argv[1][0] == L'\0')
    {
        fwprintf(stderr, L"Usage: BFVRUserSettingsSeedWriter <output-file>\n");
        return 2;
    }
    if (GetFileAttributesW(argv[1]) != INVALID_FILE_ATTRIBUTES)
    {
        fwprintf(
            stderr,
            L"Refusing to overwrite an existing UserConfig file: %ls\n",
            argv[1]);
        return 2;
    }

    bfvr::settings::UserSettingsStore store;
    if (!store.Initialize(argv[1]))
    {
        fwprintf(stderr, L"Could not initialize the BFVR settings seed writer.\n");
        return 2;
    }
    const bfvr::settings::UserSettingsLoadResult result =
        store.LoadOrCreateDefaults();
    if (result.status !=
        bfvr::settings::UserSettingsLoadStatus::MissingCreatedDefaults)
    {
        fwprintf(
            stderr,
            L"Could not create the seeded BFVR UserConfig file (status=%u).\n",
            static_cast<unsigned int>(result.status));
        return 2;
    }

    wprintf(L"Created seeded BFVR settings: %ls\n", argv[1]);
    return 0;
}
