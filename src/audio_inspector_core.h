#pragma once

#include <string>
#include <vector>

namespace audio_inspector_core {

/* 最低限の情報を持つ構造体群。必要なら拡張してください。 */
struct DeviceInfo {
	std::string id;     // 内部識別子
	std::string name;
	bool enabled = false;
	std::string status; // 追加: 既定/無効/デバイスID など
	std::vector<int> output_buses; // OBSの内部バス1〜6のどれに出力しているか
};

struct SourceInfo {
	std::string id;         // 内部識別子
	std::string name;
	std::string type;       // 例: "mic", "desktop-audio", "browser"
	std::string scene;      // 所属シーン
	bool is_shared = false; // 同じソースが複数回使われているか
	std::vector<std::string> used_in_scenes; // Shared時に使用されているシーン一覧
	bool is_active = false;
	bool is_muted = false;
	int monitor_type = 0;   // 0 = none, 1 = monitor off, 2 = monitor only など（libobs に合わせて定義）
	int sync_offset_ms = 0;
	std::vector<int> output_buses; // OBSの内部バス1～6のどれに出力しているか
};

// シーンごとの音声ソース情報
struct SceneAudioInfo {
	std::string scene_name;
	std::vector<SourceInfo> sources;
};

// 全体のオーディオマップ
struct AudioMap {
	std::vector<SceneAudioInfo> scenes;
	std::vector<DeviceInfo> global_devices; // 有効なもののみ
};

// オーディオドライバ情報
struct AudioDriverInfo {
	int sample_rate = 0;        // サンプルレート (Hz)
	std::string driver_type;    // CoreAudio, WASAPI, ALSA, etc.
	int channels = 0;           // チャンネル数
};

/* 実装（スタブ／ラッパー）
   - list_global_audio_devices() / list_audio_sources() は libobs の API を呼んで値を返すように実装してください。
   - generate_audio_map_json() は Audio Map を JSON 文字列で返します（実装済みの雛形あり）。
*/
std::string get_obs_version();
std::string get_os_info();
AudioDriverInfo get_audio_info();

std::vector<DeviceInfo> list_global_audio_devices();
std::vector<SourceInfo> list_audio_sources();
std::vector<SourceInfo> list_active_sources(); // アクティブな（音を出している）ソースのみ

// id 指定でグローバルデバイスを切り替える（実際の実装で libobs の設定を変更する）
bool switch_global_device(const std::string &device_id);

// 全シーンの音声マップを生成
AudioMap generate_audio_map();

// Audio Map を JSON 文字列として生成（JSON のフォーマットは設計書準拠）
std::string generate_audio_map_json();

} // namespace audio_inspector_core

