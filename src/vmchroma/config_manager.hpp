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

#pragma once

#include <string>
#include "utils.hpp"
#include "yaml-cpp/yaml.h"

class config_manager
{
    std::wstring BM_FILE_BG = L"bg.bmp";
    std::wstring BM_FILE_BG_SETTINGS = L"bg_settings.bmp";
    std::wstring BM_FILE_BG_CASSETTE = L"bg_cassette.bmp";
    std::wstring CONFIG_FILE_THEME = L"vmchroma.yaml";
    std::wstring CONFIG_FILE_COLORS = L"colors.yaml";
    std::wstring reg_sub_key_vmchroma = L"VB-Audio\\VMChroma";
    std::wstring reg_sub_key_default = L"VB-Audio\\VMChroma\\Default";
    std::wstring reg_sub_key_banana = L"VB-Audio\\VMChroma\\Banana";
    std::wstring reg_sub_key_potato = L"VB-Audio\\VMChroma\\Potato";
    std::wstring reg_val_wnd_size_width = L"window_size_width";
    std::wstring reg_val_wnd_size_height = L"window_size_height";
    flavor_id current_flavor_id = FLAVOR_NONE;
    flavor_info_t active_flavor = {};
    std::unordered_map<flavor_id, flavor_info_t> flavor_map =
    {
        {FLAVOR_DEFAULT, {"default", FLAVOR_DEFAULT, 1024, 552, 0, 235, 750}},
        {FLAVOR_BANANA, {"banana", FLAVOR_BANANA, 1024, 550, 800, 305, 744}},
        {FLAVOR_POTATO, {"potato", FLAVOR_POTATO, 1645, 835, 1050, 340, 1045}},
    };
    YAML::Node yaml_colors;
    YAML::Node yaml_config;
    std::vector<uint8_t> bg_main_bitmap_data;
    std::vector<uint8_t> bg_settings_bitmap_data;
    std::vector<uint8_t> bg_cassette_bitmap_data;
    bool theme_enabled = true;

    std::optional<uint32_t> font_quality;
    std::optional<float> fader_shift_scroll_step;
    std::optional<float> fader_scroll_step;
    std::optional<uint32_t> ui_update_interval;
    std::optional<bool> restore_size;
    std::optional<std::vector<std::string>> app_blacklist;
    std::optional<std::map<std::string, std::string>> app_aliases;
    std::optional<bool> always_use_appname;
    std::optional<bool> include_system_session;

private:
    template <YAML::NodeType::value v, typename T, typename Validator>
    std::optional<T> get_value(const char* category, const char* key, Validator validator, bool is_mandatory = true)
    {
        const auto node_val = yaml_config[category][key];

        if (!node_val.IsDefined() || node_val.IsNull())
        {
            if (is_mandatory)
                SPDLOG_ERROR("Missing value for key '{}'", key);
            return std::nullopt;
        }

        if (node_val.Type() != v)
        {
            SPDLOG_ERROR("Invalid node type for key '{}'", key);
            return std::nullopt;
        }

        try
        {
            auto val = node_val.as<T>();

            if (!validator(val))
            {
                SPDLOG_ERROR("Value for '{}' not in valid range", key);
                return std::nullopt;
            }

            return val;
        }
        catch (const YAML::TypedBadConversion<T>&)
        {
            SPDLOG_ERROR("Error converting value for key '{}'", key);
            return std::nullopt;
        }
    }

    template <YAML::NodeType::value v, typename T>
    std::optional<T> get_value(const char* category, const char* key, bool is_mandatory = true)
    {
        return get_value<v, T>(category, key, [](const T&) { return true; }, is_mandatory);
    }

public:
    bool get_theme_enabled();
    void reg_save_wnd_size(uint32_t width, uint32_t height);
    bool reg_get_wnd_size(uint32_t& width, uint32_t& height);
    std::optional<flavor_id> get_current_flavor_id();
    bool init_theme();
    bool load_config();
    const std::optional<uint32_t>& get_font_quality();
    const std::optional<float>& get_fader_shift_scroll_step();
    const std::optional<float>& get_fader_scroll_step();
    const std::optional<uint32_t>& get_ui_update_interval();
    const std::optional<bool>& get_restore_size();
    const std::optional<std::vector<std::string>>& get_app_blacklist();
    const std::optional<std::map<std::string, std::string>>& get_app_aliases();
    const std::optional<bool>& get_always_use_appname();
    std::optional<std::string> cfg_get_color(const std::string& arg_col, const color_category& category);
    const std::vector<uint8_t>& get_bm_data_main();
    const std::vector<uint8_t>& get_bm_data_settings();
    const std::vector<uint8_t>& get_bm_data_cassette();
    const flavor_info_t& get_active_flavor();
};
