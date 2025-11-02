/**
Copyright (C) 2025 Klaus Hahnenkamp

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "config_manager.hpp"

#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include "winapi_hook_defs.hpp"
#include "window_manager.hpp"
#include "yaml-cpp/yaml.h"


/**
 * Saves the current window dimensions to the windows registry
 * @param width Current Width
 * @param height Current Height
 */
void config_manager::reg_save_wnd_size(uint32_t width, uint32_t height)
{
    std::wstring sub_key;
    const auto cur_flavor = get_current_flavor_id();

    if (!cur_flavor)
    {
        SPDLOG_ERROR("error getting current flavor");
        return;
    }

    if (*cur_flavor == FLAVOR_POTATO)
        sub_key = reg_sub_key_potato;

    if (*cur_flavor == FLAVOR_BANANA)
        sub_key = reg_sub_key_banana;

    if (*cur_flavor == FLAVOR_DEFAULT)
        sub_key = reg_sub_key_default;

    HKEY hKey;
    auto result = RegCreateKeyExW(HKEY_CURRENT_USER, sub_key.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &hKey, nullptr);

    if (result != ERROR_SUCCESS)
    {
        SPDLOG_ERROR("error open registry key");
        return;
    }

    result = RegSetValueExW(hKey, reg_val_wnd_size_width.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&width), sizeof(DWORD));

    if (result != ERROR_SUCCESS)
    {
        SPDLOG_ERROR("error writing registry key");
        return;
    }

    result = RegSetValueExW(hKey, reg_val_wnd_size_height.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&height), sizeof(DWORD));

    if (result != ERROR_SUCCESS)
    {
        SPDLOG_ERROR("error writing registry key");
        return;
    }

    RegCloseKey(hKey);
}

/**
 * Queries the last window dimension from the windows registry
 * @param width Last width
 * @param height Last Height
 * @return
 */
bool config_manager::reg_get_wnd_size(uint32_t& width, uint32_t& height)
{
    std::wstring sub_key;
    const auto cur_flavor = get_current_flavor_id();

    if (cur_flavor == FLAVOR_NONE)
    {
        SPDLOG_ERROR("error getting current flavor");
        return false;
    }

    if (cur_flavor == FLAVOR_POTATO)
        sub_key = reg_sub_key_potato;

    if (cur_flavor == FLAVOR_BANANA)
        sub_key = reg_sub_key_banana;

    if (cur_flavor == FLAVOR_DEFAULT)
        sub_key = reg_sub_key_default;

    HKEY key;
    auto result = RegOpenKeyExW(HKEY_CURRENT_USER, sub_key.c_str(), 0, KEY_READ, &key);

    // key doesn't exist yet
    if (result == ERROR_FILE_NOT_FOUND)
        return false;

    if (result != ERROR_SUCCESS)
    {
        SPDLOG_ERROR("Error opening registry key: {}", result);
        return false;
    }

    DWORD data_size = sizeof(DWORD);
    DWORD type;

    result = RegQueryValueExW(key, reg_val_wnd_size_width.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(&width), &data_size);

    if (result != ERROR_SUCCESS || type != REG_DWORD || data_size != sizeof(DWORD))
    {
        SPDLOG_ERROR("Error reading width registry value: {}", result);
        RegCloseKey(key);
        return false;
    }

    result = RegQueryValueExW(key, reg_val_wnd_size_height.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(&height), &data_size);

    if (result != ERROR_SUCCESS || type != REG_DWORD || data_size != sizeof(DWORD))
    {
        SPDLOG_ERROR("Error reading height registry value: {}", result);
        RegCloseKey(key);
        return false;
    }

    RegCloseKey(key);
    return true;
}

/**
 * Queries the current Voicemeeter version by reading the version info of the executable
 * @return The current Voicemeeter flavor
 */
std::optional<flavor_id> config_manager::get_current_flavor_id()
{
    if (current_flavor_id != FLAVOR_NONE)
        return current_flavor_id;

    std::wstring executable_name(MAX_PATH, '\0');

    if (!GetModuleFileName(nullptr, executable_name.data(), MAX_PATH))
    {
        SPDLOG_ERROR("GetModuleFileName failed");
        return std::nullopt;
    }

    DWORD dummy;
    DWORD version_info_size = GetFileVersionInfoSize(executable_name.c_str(), &dummy);

    if (version_info_size == 0)
    {
        SPDLOG_ERROR("GetFileVersionInfoSize returned 0");
        return std::nullopt;
    }

    std::vector<char> version_info(version_info_size);

    if (!o_GetFileVersionInfoW(executable_name.c_str(), 0, version_info_size, version_info.data()))
    {
        SPDLOG_ERROR("GetFileVersionInfo failed");
        return std::nullopt;
    }

    LPVOID value = nullptr;
    UINT valueLen = 0;
    const std::wstring query = L"\\StringFileInfo\\000004b0\\ProductName";

    if (!o_VerQueryValueW(version_info.data(), query.data(), &value, &valueLen) || valueLen <= 0)
    {
        SPDLOG_ERROR("VerQueryValue failed");
        return std::nullopt;
    }

    const std::wstring product_name = static_cast<wchar_t*>(value);

    if (product_name == L"VoiceMeeter")
    {
        current_flavor_id = FLAVOR_DEFAULT;
        return FLAVOR_DEFAULT;
    }

    if (product_name == L"VoiceMeeter Banana")
    {
        current_flavor_id = FLAVOR_BANANA;
        return FLAVOR_BANANA;
    }

    if (product_name == L"VoiceMeeter Potato")
    {
        current_flavor_id = FLAVOR_POTATO;
        return FLAVOR_POTATO;
    }

    SPDLOG_ERROR("no product name matched");
    return std::nullopt;
}

/**
 * Loads the theme bitmap data from the theme directory
 * @return True if loading was successful
 */
bool config_manager::init_theme()
{
    const auto flavor_id = get_current_flavor_id();

    if (!flavor_id)
    {
        SPDLOG_ERROR("can't get Voicemeeter flavor from version info");
        return false;
    }

    active_flavor = flavor_map[*flavor_id];

    // no theme specified
    const auto active_theme_name = get_value<YAML::NodeType::Scalar, std::string>("theme", active_flavor.name.c_str(), false);

    if (!active_theme_name)
    {
        theme_enabled = false;
        return true;
    }

    auto active_theme_name_wstr = utils::str_to_wstr(*active_theme_name);

    if (!active_theme_name_wstr)
    {
        SPDLOG_ERROR("active_theme_name_str conversion error");
        return false;
    }

    auto active_flavor_name = utils::str_to_wstr(active_flavor.name);

    if (!active_flavor_name)
    {
        SPDLOG_ERROR("active_flavor.name conversion error");
        return false;
    }

    auto userprofile_path = utils::get_userprofile_path();

    std::wstring theme_path = (std::filesystem::path(*userprofile_path) / L"themes" / *active_theme_name_wstr / *active_flavor_name);

    if (!std::filesystem::exists(std::filesystem::path(theme_path)))
    {
        SPDLOG_ERROR("can't find themes folder {}", utils::wstr_to_str_or_default(theme_path));
        return false;
    }

    if (!std::filesystem::exists(std::filesystem::path(theme_path) / BM_FILE_BG))
    {
        SPDLOG_ERROR("can't find {} in themes folder", utils::wstr_to_str_or_default(BM_FILE_BG));
        return false;
    }

    if (!utils::load_bitmap(std::filesystem::path(theme_path) / BM_FILE_BG, bg_main_bitmap_data))
    {
        SPDLOG_ERROR("error loading {}", utils::wstr_to_str_or_default(BM_FILE_BG));
        return false;
    }

    if (!std::filesystem::exists(std::filesystem::path(theme_path) / BM_FILE_BG_SETTINGS))
    {
        SPDLOG_ERROR("can't find {} in themes folder", utils::wstr_to_str_or_default(BM_FILE_BG_SETTINGS));
        return false;
    }

    if (!utils::load_bitmap(std::filesystem::path(theme_path) / BM_FILE_BG_SETTINGS, bg_settings_bitmap_data))
    {
        SPDLOG_ERROR("error loading {}", utils::wstr_to_str_or_default(BM_FILE_BG_SETTINGS));
        return false;
    }

    if (!std::filesystem::exists(std::filesystem::path(theme_path) / BM_FILE_BG_CASSETTE))
    {
        SPDLOG_ERROR("can't find {} in themes folder", utils::wstr_to_str_or_default(BM_FILE_BG_CASSETTE));
        return false;
    }

    if (!utils::load_bitmap(std::filesystem::path(theme_path) / BM_FILE_BG_CASSETTE, bg_cassette_bitmap_data))
    {
        SPDLOG_ERROR("error loading {}", utils::wstr_to_str_or_default(BM_FILE_BG_CASSETTE));
        return false;
    }

    if (!std::filesystem::exists(std::filesystem::path(*userprofile_path) / L"themes" / *active_theme_name_wstr / CONFIG_FILE_COLORS))
    {
        SPDLOG_ERROR("can't find {}", utils::wstr_to_str_or_default(CONFIG_FILE_COLORS));
        return false;
    }

    std::ifstream colors_file(std::filesystem::path(*userprofile_path) / L"themes" / *active_theme_name_wstr / CONFIG_FILE_COLORS);

    if (!colors_file.is_open())
    {
        SPDLOG_ERROR("can't open {}", utils::wstr_to_str_or_default(CONFIG_FILE_COLORS));
        return false;
    }

    try
    {
        yaml_colors = YAML::Load(colors_file);
    }
    catch (YAML::ParserException&)
    {
        SPDLOG_ERROR("failed to parse {}", utils::wstr_to_str_or_default(CONFIG_FILE_COLORS));
        return false;
    }

    return true;
}

/**
 * Loads the config file vmchroma.yaml
 * @return True if loading was successful
 */
bool config_manager::load_config()
{
    const auto userprofile_path = utils::get_userprofile_path();

    if (!userprofile_path)
    {
        SPDLOG_ERROR("can't get userprofile path");
        return false;
    }

    if (!std::filesystem::exists(std::filesystem::path(*userprofile_path) / CONFIG_FILE_THEME))
    {
        SPDLOG_ERROR("config file not found");
        return false;
    }

    std::ifstream cfg_file(std::filesystem::path(*userprofile_path) / CONFIG_FILE_THEME);

    if (!cfg_file.is_open())
    {
        SPDLOG_ERROR("can't open config file");
        return false;
    }    

    try
    {
        yaml_config = YAML::Load(cfg_file);
    }
    catch (YAML::ParserException&)
    {
        SPDLOG_ERROR("failed to parse config file");
        return false;
    }

    font_quality = get_value<YAML::NodeType::Scalar, uint32_t>("misc", "fontQuality", [](const uint32_t x) { return x <= 6; });
    fader_shift_scroll_step = get_value<YAML::NodeType::Scalar, float>("misc", "faderShiftScrollStep");
    fader_scroll_step = get_value<YAML::NodeType::Scalar, float>("misc", "faderScrollStep");
    ui_update_interval = get_value<YAML::NodeType::Scalar, uint32_t>("misc", "updateIntervalUI", [](const uint32_t x) { return x >= 16; });
    restore_size = get_value<YAML::NodeType::Scalar, bool>("misc", "restoreSize");
    app_blacklist = get_value<YAML::NodeType::Sequence, std::vector<std::string>>("potato", "appBlacklist", false);
    app_aliases = get_value<YAML::NodeType::Map, std::map<std::string, std::string>>("potato", "appAliasMap", false);
    always_use_appname = get_value<YAML::NodeType::Scalar, bool>("potato", "alwaysUseAppName");
    include_system_session = get_value<YAML::NodeType::Scalar, bool>("potato", "includeSystemSoundSession");

    return true;
}

/**
 * Gets a color value from the yaml file in a case-insensitive way
 * @param arg_col The color value in upper case
 * @param category Can either be "shapes" or "text"
 * @return The mapped color value for the current theme
 */
std::optional<std::string> config_manager::cfg_get_color(const std::string& arg_col, const color_category& category)
{
    YAML::Node category_node;

    if (category == CATEGORY_SHAPES)
        category_node = yaml_colors["shapes"];

    if (category == CATEGORY_TEXT)
        category_node = yaml_colors["text"];

    for (auto it = category_node.begin(); it != category_node.end(); ++it)
    {
        auto current_color = it->first.as<std::string>();

        if (lstrcmpiA(current_color.c_str(), arg_col.c_str()) == 0)
        {
            auto ret = it->second.as<std::string>();

            if (ret.empty())
                return std::nullopt;

            return ret;
        }
    }

    return std::nullopt;
}

const std::vector<uint8_t>& config_manager::get_bm_data_main()
{
    return bg_main_bitmap_data;
}

const std::vector<uint8_t>& config_manager::get_bm_data_settings()
{
    return bg_settings_bitmap_data;
}

const std::vector<uint8_t>& config_manager::get_bm_data_cassette()
{
    return bg_cassette_bitmap_data;
}

const flavor_info_t& config_manager::get_active_flavor()
{
    return active_flavor;
}

const std::optional<uint32_t>& config_manager::get_font_quality()
{
    return font_quality;
}

const std::optional<float>& config_manager::get_fader_shift_scroll_step()
{
    return fader_shift_scroll_step;
}

const std::optional<float>& config_manager::get_fader_scroll_step()
{
    return fader_scroll_step;
}

const std::optional<uint32_t>& config_manager::get_ui_update_interval()
{
    return ui_update_interval;
}

const std::optional<bool>& config_manager::get_restore_size()
{
    return restore_size;
}

const std::optional<std::vector<std::string>>& config_manager::get_app_blacklist()
{
    return app_blacklist;
}

const std::optional<std::map<std::string, std::string>>& config_manager::get_app_aliases()
{
    return app_aliases;
}

const std::optional<bool>& config_manager::get_always_use_appname()
{
    return always_use_appname;
}

bool config_manager::get_theme_enabled()
{
    return theme_enabled;
}
