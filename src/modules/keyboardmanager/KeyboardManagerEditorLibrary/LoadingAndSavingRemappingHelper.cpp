#include "pch.h"
#include "LoadingAndSavingRemappingHelper.h"

#include <set>

#include <common/interop/shared_constants.h>
#include <ErrorTypes.h>
#include <KeyboardManagerState.h>

#include <keyboardmanager/KeyboardManagerEditorLibrary/trace.h>

namespace LoadingAndSavingRemappingHelper
{
    // Function to check if the set of remappings in the buffer are valid
    KeyboardManagerHelper::ErrorType CheckIfRemappingsAreValid(const RemapBuffer& remappings)
    {
        KeyboardManagerHelper::ErrorType isSuccess = KeyboardManagerHelper::ErrorType::NoError;
        std::map<std::wstring, std::set<KeyShortcutUnion>> ogKeys;
        for (int i = 0; i < remappings.size(); i++)
        {
            KeyShortcutUnion ogKey = remappings[i].first[0];
            KeyShortcutUnion newKey = remappings[i].first[1];
            std::wstring appName = remappings[i].second;

            bool ogKeyValidity = (ogKey.index() == 0 && std::get<DWORD>(ogKey) != NULL) || (ogKey.index() == 1 && std::get<Shortcut>(ogKey).IsValidShortcut());
            bool newKeyValidity = (newKey.index() == 0 && std::get<DWORD>(newKey) != NULL) || (newKey.index() == 1 && std::get<Shortcut>(newKey).IsValidShortcut());

            // Add new set for a new target app name
            if (ogKeys.find(appName) == ogKeys.end())
            {
                ogKeys[appName] = std::set<KeyShortcutUnion>();
            }

            if (ogKeyValidity && newKeyValidity && ogKeys[appName].find(ogKey) == ogKeys[appName].end())
            {
                ogKeys[appName].insert(ogKey);
            }
            else if (ogKeyValidity && newKeyValidity && ogKeys[appName].find(ogKey) != ogKeys[appName].end())
            {
                isSuccess = KeyboardManagerHelper::ErrorType::RemapUnsuccessful;
            }
            else
            {
                isSuccess = KeyboardManagerHelper::ErrorType::RemapUnsuccessful;
            }
        }

        return isSuccess;
    }

    // Function to return the set of keys that have been orphaned from the remap buffer
    std::vector<DWORD> GetOrphanedKeys(const RemapBuffer& remappings)
    {
        std::set<DWORD> ogKeys;
        std::set<DWORD> newKeys;

        for (int i = 0; i < remappings.size(); i++)
        {
            DWORD ogKey = std::get<DWORD>(remappings[i].first[0]);
            KeyShortcutUnion newKey = remappings[i].first[1];

            if (ogKey != NULL && ((newKey.index() == 0 && std::get<DWORD>(newKey) != 0) || (newKey.index() == 1 && std::get<Shortcut>(newKey).IsValidShortcut())))
            {
                ogKeys.insert(ogKey);

                // newKey should be added only if the target is a key
                if (remappings[i].first[1].index() == 0)
                {
                    newKeys.insert(std::get<DWORD>(newKey));
                }
            }
        }

        for (auto& k : newKeys)
        {
            ogKeys.erase(k);
        }

        return std::vector(ogKeys.begin(), ogKeys.end());
    }

    // Function to combine remappings if the L and R version of the modifier is mapped to the same key
    void CombineRemappings(SingleKeyRemapTable& table, DWORD leftKey, DWORD rightKey, DWORD combinedKey)
    {
        if (table.find(leftKey) != table.end() && table.find(rightKey) != table.end())
        {
            // If they are mapped to the same key, delete those entries and set the common version
            if (table[leftKey] == table[rightKey])
            {
                table[combinedKey] = table[leftKey];
                table.erase(leftKey);
                table.erase(rightKey);
            }
        }
    }

    // Function to pre process the remap table before loading it into the UI
    void PreProcessRemapTable(SingleKeyRemapTable& table)
    {
        // Pre process the table to combine L and R versions of Ctrl/Alt/Shift/Win that are mapped to the same key
        CombineRemappings(table, VK_LCONTROL, VK_RCONTROL, VK_CONTROL);
        CombineRemappings(table, VK_LMENU, VK_RMENU, VK_MENU);
        CombineRemappings(table, VK_LSHIFT, VK_RSHIFT, VK_SHIFT);
        CombineRemappings(table, VK_LWIN, VK_RWIN, CommonSharedConstants::VK_WIN_BOTH);
    }

    // Function to apply the single key remappings from the buffer to the KeyboardManagerState variable
    void ApplySingleKeyRemappings(KeyboardManagerState& keyboardManagerState, const RemapBuffer& remappings, bool isTelemetryRequired)
    {
        // Clear existing Key Remaps
        keyboardManagerState.ClearSingleKeyRemaps();
        DWORD successfulKeyToKeyRemapCount = 0;
        DWORD successfulKeyToShortcutRemapCount = 0;
        for (int i = 0; i < remappings.size(); i++)
        {
            DWORD originalKey = std::get<DWORD>(remappings[i].first[0]);
            KeyShortcutUnion newKey = remappings[i].first[1];

            if (originalKey != NULL && !(newKey.index() == 0 && std::get<DWORD>(newKey) == NULL) && !(newKey.index() == 1 && !std::get<Shortcut>(newKey).IsValidShortcut()))
            {
                // If Ctrl/Alt/Shift are added, add their L and R versions instead to the same key
                bool result = false;
                bool res1, res2;
                switch (originalKey)
                {
                case VK_CONTROL:
                    res1 = keyboardManagerState.AddSingleKeyRemap(VK_LCONTROL, newKey);
                    res2 = keyboardManagerState.AddSingleKeyRemap(VK_RCONTROL, newKey);
                    result = res1 && res2;
                    break;
                case VK_MENU:
                    res1 = keyboardManagerState.AddSingleKeyRemap(VK_LMENU, newKey);
                    res2 = keyboardManagerState.AddSingleKeyRemap(VK_RMENU, newKey);
                    result = res1 && res2;
                    break;
                case VK_SHIFT:
                    res1 = keyboardManagerState.AddSingleKeyRemap(VK_LSHIFT, newKey);
                    res2 = keyboardManagerState.AddSingleKeyRemap(VK_RSHIFT, newKey);
                    result = res1 && res2;
                    break;
                case CommonSharedConstants::VK_WIN_BOTH:
                    res1 = keyboardManagerState.AddSingleKeyRemap(VK_LWIN, newKey);
                    res2 = keyboardManagerState.AddSingleKeyRemap(VK_RWIN, newKey);
                    result = res1 && res2;
                    break;
                default:
                    result = keyboardManagerState.AddSingleKeyRemap(originalKey, newKey);
                }

                if (result)
                {
                    if (newKey.index() == 0)
                    {
                        successfulKeyToKeyRemapCount += 1;
                    }
                    else
                    {
                        successfulKeyToShortcutRemapCount += 1;
                    }
                }
            }
        }

        // If telemetry is to be logged, log the key remap counts
        if (isTelemetryRequired)
        {
            Trace::KeyRemapCount(successfulKeyToKeyRemapCount, successfulKeyToShortcutRemapCount);
        }
    }

    // Function to apply the shortcut remappings from the buffer to the KeyboardManagerState variable
    void ApplyShortcutRemappings(KeyboardManagerState& keyboardManagerState, const RemapBuffer& remappings, bool isTelemetryRequired)
    {
        // Clear existing shortcuts
        keyboardManagerState.ClearOSLevelShortcuts();
        keyboardManagerState.ClearAppSpecificShortcuts();
        DWORD successfulOSLevelShortcutToShortcutRemapCount = 0;
        DWORD successfulOSLevelShortcutToKeyRemapCount = 0;
        DWORD successfulAppSpecificShortcutToShortcutRemapCount = 0;
        DWORD successfulAppSpecificShortcutToKeyRemapCount = 0;
        
        // Save the shortcuts that are valid and report if any of them were invalid
        for (int i = 0; i < remappings.size(); i++)
        {
            Shortcut originalShortcut = std::get<Shortcut>(remappings[i].first[0]);
            KeyShortcutUnion newShortcut = remappings[i].first[1];

            if (originalShortcut.IsValidShortcut() && ((newShortcut.index() == 0 && std::get<DWORD>(newShortcut) != NULL) || (newShortcut.index() == 1 && std::get<Shortcut>(newShortcut).IsValidShortcut())))
            {
                if (remappings[i].second == L"")
                {
                    bool result = keyboardManagerState.AddOSLevelShortcut(originalShortcut, newShortcut);
                    if (result)
                    {
                        if (newShortcut.index() == 0)
                        {
                            successfulOSLevelShortcutToKeyRemapCount += 1;
                        }
                        else
                        {
                            successfulOSLevelShortcutToShortcutRemapCount += 1;
                        }
                    }
                }
                else
                {
                    bool result = keyboardManagerState.AddAppSpecificShortcut(remappings[i].second, originalShortcut, newShortcut);
                    if (result)
                    {
                        if (newShortcut.index() == 0)
                        {
                            successfulAppSpecificShortcutToKeyRemapCount += 1;
                        }
                        else
                        {
                            successfulAppSpecificShortcutToShortcutRemapCount += 1;
                        }
                    }
                }
            }
        }

        // If telemetry is to be logged, log the shortcut remap counts
        if (isTelemetryRequired)
        {
            Trace::OSLevelShortcutRemapCount(successfulOSLevelShortcutToShortcutRemapCount, successfulOSLevelShortcutToKeyRemapCount);
            Trace::AppSpecificShortcutRemapCount(successfulAppSpecificShortcutToShortcutRemapCount, successfulAppSpecificShortcutToKeyRemapCount);
        }
    }
}
