#include "audio_inspector_core.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <dlfcn.h>

#include <obs.h>
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/platform.h>
#include <set>
#include <map>
#include <algorithm>

namespace audio_inspector_core {

std::string get_obs_version()
{
	// OBSバージョンを取得
	const char *version = obs_get_version_string();
	if (version) {
		return std::string("OBS ") + version;
	}
	return "OBS (unknown version)";
}

AudioDriverInfo get_audio_info()
{
	AudioDriverInfo info;
	
	// OBSのオーディオ情報を取得
	audio_t *audio = obs_get_audio();
	if (audio) {
		const struct audio_output_info *aoi = audio_output_get_info(audio);
		if (aoi) {
			info.sample_rate = aoi->samples_per_sec;
			
			// チャンネル数を取得
			info.channels = get_audio_channels(aoi->speakers);
		}
	}
	
	// ドライバタイプはプラットフォームによる
#if defined(__APPLE__)
	info.driver_type = "CoreAudio";
#elif defined(_WIN32)
	info.driver_type = "WASAPI";
#elif defined(__linux__)
	info.driver_type = "PulseAudio";
#else
	info.driver_type = "Unknown";
#endif
	
	return info;
}

// Process-global simulated device storage (used until libobs integration)
static std::vector<DeviceInfo> g_devices_storage;
static bool g_devices_initialized = false;

std::string get_os_info()
{
	// 簡単なプレースホルダ。実環境では QSysInfo や uname を使う
	return qPrintable(QSysInfo::prettyProductName().toUtf8());
}


// scene内source列挙用static関数
// scene内source列挙用static関数（obs_scene_enum_itemsが無い場合）
static bool collect_scene_sources(obs_scene_t *scene, obs_sceneitem_t *item, void *param) {
	(void)scene; // unused parameter
	std::set<obs_source_t*> *scene_sources = static_cast<std::set<obs_source_t*>*>(param);
	obs_source_t *src = obs_sceneitem_get_source(item);
	if (src) scene_sources->insert(src);
	return true;
}

// 全scene列挙用static関数
static bool enum_scene_cb(void *param, obs_source_t *scene) {
	if (obs_source_get_type(scene) != OBS_SOURCE_TYPE_SCENE) return true;
	obs_scene_t *scn = obs_scene_from_source(scene);
	if (!scn) return true;
	obs_scene_enum_items(scn, collect_scene_sources, param);
	return true;
}

// 全source列挙用static関数
struct SourceEnumParam {
	std::set<obs_source_t*> *scene_sources;
	std::vector<DeviceInfo> *out;
};
static bool enum_source_cb(void *vparam, obs_source_t *source) {
	SourceEnumParam *param = static_cast<SourceEnumParam*>(vparam);
	if (!source) return true;
	uint32_t flags = obs_source_get_output_flags(source);
	if ((flags & OBS_SOURCE_AUDIO) == 0) return true;
	if (obs_source_get_type(source) == OBS_SOURCE_TYPE_SCENE) return true;
	if (param->scene_sources->count(source) > 0) return true; // sceneに属している
	DeviceInfo di;
	di.id = obs_source_get_id(source);
	di.name = obs_source_get_name(source);
	di.enabled = obs_source_active(source);
	param->out->push_back(di);
	return true;
}

std::vector<DeviceInfo> list_global_audio_devices()
{
	std::vector<DeviceInfo> out;

	// OBSから実際のソース名を取得し、フォールバックには英語名を使用
	const char* desktop_fallback[] = {"Desktop Audio", "Desktop Audio 2"};
	const char* mic_fallback[] = {"Mic/Aux", "Mic/Aux 2", "Mic/Aux 3", "Mic/Aux 4"};

	// Desktop Audio (channels 1, 2)
	for (int i = 0; i < 2; ++i) {
		obs_source_t* src = obs_get_output_source(i + 1);
		DeviceInfo di;
		
		if (!src) {
			// Source not configured
			di.name = desktop_fallback[i];
			di.id = "";
			di.enabled = false;
			di.status = "Disabled";
		} else {
			const char* source_name = obs_source_get_name(src);
			di.name = source_name ? source_name : desktop_fallback[i];
			di.id = "";
			di.enabled = true;
			di.status = "Active";
			
			// オーディオミキサーのバス情報を取得（1〜6）
			uint32_t mixers = obs_source_get_audio_mixers(src);
			for (int j = 0; j < 6; ++j) {
				if (mixers & (1 << j)) {
					di.output_buses.push_back(j + 1); // 1-indexed
				}
			}
			
			obs_source_release(src);
		}
		out.push_back(di);
	}
	
	// Mic/Aux Audio (channels 3, 4, 5, 6)
	for (int i = 0; i < 4; ++i) {
		obs_source_t* src = obs_get_output_source(i + 3);
		DeviceInfo di;
		
		if (!src) {
			// Source not configured
			di.name = mic_fallback[i];
			di.id = "";
			di.enabled = false;
			di.status = "Disabled";
		} else {
			const char* source_name = obs_source_get_name(src);
			di.name = source_name ? source_name : mic_fallback[i];
			di.id = "";
			di.enabled = true;
			di.status = "Active";
			
			// オーディオミキサーのバス情報を取得（1〜6）
			uint32_t mixers = obs_source_get_audio_mixers(src);
			for (int j = 0; j < 6; ++j) {
				if (mixers & (1 << j)) {
					di.output_buses.push_back(j + 1); // 1-indexed
				}
			}
			
			obs_source_release(src);
		}
		out.push_back(di);
	}
	
	return out;
}

std::vector<SourceInfo> list_audio_sources()
{
	// TODO: libobs でソースを列挙して情報を埋める
	std::vector<SourceInfo> out;
	// ダミー
	SourceInfo src1;
	src1.id = "src1";
	src1.name = "Mic 1";
	src1.type = "mic";
	src1.scene = "Scene 1";
	src1.is_shared = false;
	src1.is_active = true;
	src1.is_muted = false;
	src1.monitor_type = 1;
	src1.sync_offset_ms = 0;
	out.push_back(src1);
	
	SourceInfo src2;
	src2.id = "src2";
	src2.name = "Desktop Audio";
	src2.type = "desktop";
	src2.scene = "Scene 1";
	src2.is_shared = false;
	src2.is_active = true;
	src2.is_muted = false;
	src2.monitor_type = 2;
	src2.sync_offset_ms = 0;
	out.push_back(src2);
	return out;
}

// ========== シンプルなShared判定 ==========
// 同じソース（UUID）が複数回使われていたらShared

// 全シーンでのソース使用状況を収集する構造体
struct SourceUsageCollector {
	std::map<std::string, int> use_count;          // UUID -> 使用回数
	std::map<std::string, std::set<std::string>> used_in_scenes; // UUID -> シーン名セット
	std::string current_scene_name;
};

static bool count_source_usage(obs_scene_t *, obs_sceneitem_t *item, void *param) {
	SourceUsageCollector *collector = static_cast<SourceUsageCollector*>(param);
	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!source) return true;
	
	uint32_t flags = obs_source_get_output_flags(source);
	if ((flags & OBS_SOURCE_AUDIO) == 0) {
		// グループの場合は再帰的に処理
		if (obs_source_is_group(source)) {
			obs_scene_t *group_scene = obs_group_from_source(source);
			if (group_scene) {
				obs_scene_enum_items(group_scene, count_source_usage, param);
			}
		}
		return true;
	}
	
	const char *uuid = obs_source_get_uuid(source);
	if (uuid) {
		std::string uuid_str(uuid);
		collector->use_count[uuid_str]++;
		collector->used_in_scenes[uuid_str].insert(collector->current_scene_name);
	}
	return true;
}

static bool enum_scenes_for_usage(void *param, obs_source_t *scene_source) {
	SourceUsageCollector *collector = static_cast<SourceUsageCollector*>(param);
	
	if (obs_source_get_type(scene_source) != OBS_SOURCE_TYPE_SCENE) {
		return true;
	}
	
	const char *scene_name = obs_source_get_name(scene_source);
	collector->current_scene_name = scene_name ? scene_name : "";
	
	obs_scene_t *scene = obs_scene_from_source(scene_source);
	if (scene) {
		obs_scene_enum_items(scene, count_source_usage, collector);
	}
	return true;
}

// 全シーンのソース使用状況を取得
static SourceUsageCollector get_all_source_usage() {
	SourceUsageCollector collector;
	obs_enum_scenes(enum_scenes_for_usage, &collector);
	return collector;
}

// 現在のシーンのソースを収集するための構造体
struct ActiveSourceCollector {
	std::vector<SourceInfo> sources;
	std::string current_scene_name;
	SourceUsageCollector *usage; // 全シーンでの使用状況
	std::set<obs_source_t*> *global_audio_sources; // グローバル音声ソース
};

// シーン内のアイテムを列挙してソースを収集
static bool collect_active_scene_item(obs_scene_t *scene, obs_sceneitem_t *item, void *param) {
	(void)scene;
	ActiveSourceCollector *collector = static_cast<ActiveSourceCollector*>(param);
	
	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!source) return true;
	
	// 音声フラグをチェック
	uint32_t flags = obs_source_get_output_flags(source);
	if ((flags & OBS_SOURCE_AUDIO) == 0) {
		// グループの場合は再帰的に処理
		if (obs_source_is_group(source)) {
			obs_scene_t *group_scene = obs_group_from_source(source);
			if (group_scene) {
				obs_scene_enum_items(group_scene, collect_active_scene_item, param);
			}
		}
		return true;
	}
	
	// グローバル音声デバイスは除外
	if (collector->global_audio_sources->count(source) > 0) {
		return true;
	}
	
	// SourceInfo を作成
	SourceInfo info;
	const char *uuid = obs_source_get_uuid(source);
	info.id = uuid ? uuid : "";
	info.name = obs_source_get_name(source) ? obs_source_get_name(source) : "";
	info.type = obs_source_get_id(source) ? obs_source_get_id(source) : "";
	info.scene = collector->current_scene_name;
	
	// ミュート状態を取得
	bool mixer_muted = obs_source_muted(source);
	bool item_hidden = !obs_sceneitem_visible(item);
	info.is_muted = mixer_muted || item_hidden;
	
	// アクティブ状態を判定
	info.is_active = obs_source_active(source);
	
	// Shared判定: 同じUUIDのソースが複数回使われていたらShared
	if (uuid) {
		std::string uuid_str(uuid);
		auto it = collector->usage->use_count.find(uuid_str);
		if (it != collector->usage->use_count.end() && it->second > 1) {
			info.is_shared = true;
			// 使用されているシーン一覧を記録
			auto scenes_it = collector->usage->used_in_scenes.find(uuid_str);
			if (scenes_it != collector->usage->used_in_scenes.end()) {
				for (const auto &scene_name : scenes_it->second) {
					info.used_in_scenes.push_back(scene_name);
				}
			}
		}
	}
	
	// オーディオミキサーのバス情報を取得（1〜6）
	uint32_t mixers = obs_source_get_audio_mixers(source);
	for (int i = 0; i < 6; ++i) {
		if (mixers & (1 << i)) {
			info.output_buses.push_back(i + 1);
		}
	}
	
	collector->sources.push_back(info);
	
	return true;
}

std::vector<SourceInfo> list_active_sources()
{
	// 全シーンでのソース使用状況を取得
	SourceUsageCollector usage = get_all_source_usage();
	
	// グローバル音声ソースをキャッシュ
	std::set<obs_source_t*> global_audio_sources;
	std::vector<obs_source_t*> global_sources_to_release;
	for (int i = 1; i <= 6; ++i) {
		obs_source_t* global_src = obs_get_output_source(i);
		if (global_src) {
			global_audio_sources.insert(global_src);
			global_sources_to_release.push_back(global_src);
		}
	}
	
	ActiveSourceCollector collector;
	collector.usage = &usage;
	collector.global_audio_sources = &global_audio_sources;
	
	// 現在のシーンを取得
	obs_source_t *current_scene_source = obs_frontend_get_current_scene();
	if (!current_scene_source) {
		blog(LOG_WARNING, "[AudioInspector] No current scene");
		for (obs_source_t* src : global_sources_to_release) {
			obs_source_release(src);
		}
		return collector.sources;
	}
	
	const char *scene_name = obs_source_get_name(current_scene_source);
	collector.current_scene_name = scene_name ? scene_name : "";
	obs_scene_t *scene = obs_scene_from_source(current_scene_source);
	
	if (scene) {
		obs_scene_enum_items(scene, collect_active_scene_item, &collector);
	}
	
	obs_source_release(current_scene_source);
	
	// グローバル音声ソースを解放
	for (obs_source_t* src : global_sources_to_release) {
		obs_source_release(src);
	}
	
	// OBS UIの表示順に合わせるため逆順にする
	std::reverse(collector.sources.begin(), collector.sources.end());
	
	return collector.sources;
}

// 全シーンの音声マップを生成するためのコールバック
struct MapSourceCollector {
	std::vector<SourceInfo> *sources;
	std::string scene_name;
	SourceUsageCollector *usage; // 全シーンでの使用状況
	std::set<obs_source_t*> *global_audio_sources;
};

static bool collect_map_scene_item(obs_scene_t *, obs_sceneitem_t *item, void *param) {
	MapSourceCollector *collector = static_cast<MapSourceCollector*>(param);
	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!source) return true;
	
	// 音声フラグをチェック
	uint32_t flags = obs_source_get_output_flags(source);
	if ((flags & OBS_SOURCE_AUDIO) == 0) {
		// グループの場合は再帰的に処理
		if (obs_source_is_group(source)) {
			obs_scene_t *group_scene = obs_group_from_source(source);
			if (group_scene) {
				obs_scene_enum_items(group_scene, collect_map_scene_item, param);
			}
		}
		return true;
	}
	
	// グローバル音声デバイスは除外
	if (collector->global_audio_sources->count(source) > 0) {
		return true;
	}
	
	// SourceInfo を作成
	SourceInfo info;
	const char *uuid = obs_source_get_uuid(source);
	info.id = uuid ? uuid : "";
	info.name = obs_source_get_name(source) ? obs_source_get_name(source) : "";
	
	// ソースタイプを取得し、読みやすい形式に変換
	const char *source_id = obs_source_get_id(source);
	std::string type_str = source_id ? source_id : "";
	
	// ソースIDを読みやすい表示名に変換
	if (type_str == "coreaudio_input_capture") {
		info.type = "Audio Input";
	} else if (type_str == "coreaudio_output_capture") {
		info.type = "Audio Output";
	} else if (type_str == "browser_source") {
		info.type = "Browser";
	} else if (type_str == "ffmpeg_source") {
		info.type = "Media Source";
	} else if (type_str == "vlc_source") {
		info.type = "VLC Source";
	} else if (type_str == "ndi_source") {
		info.type = "NDI";
	} else if (type_str == "syphon-input") {
		info.type = "Syphon";
	} else if (type_str == "text_ft2_source" || type_str == "text_gdiplus") {
		info.type = "Text";
	} else if (type_str == "wasapi_input_capture") {
		info.type = "Audio Input";
	} else if (type_str == "wasapi_output_capture") {
		info.type = "Audio Output";
	} else {
		info.type = type_str;
	}
	
	info.scene = collector->scene_name;
	
	// ミュート状態を取得
	bool mixer_muted = obs_source_muted(source);
	bool item_hidden = !obs_sceneitem_visible(item);
	info.is_muted = mixer_muted || item_hidden;
	
	// Shared判定: 同じUUIDのソースが複数回使われていたらShared
	if (uuid) {
		std::string uuid_str(uuid);
		auto it = collector->usage->use_count.find(uuid_str);
		if (it != collector->usage->use_count.end() && it->second > 1) {
			info.is_shared = true;
			// 使用されているシーン一覧を記録
			auto scenes_it = collector->usage->used_in_scenes.find(uuid_str);
			if (scenes_it != collector->usage->used_in_scenes.end()) {
				for (const auto &scene_name : scenes_it->second) {
					info.used_in_scenes.push_back(scene_name);
				}
			}
		}
	}
	
	// オーディオミキサーのバス情報を取得（1〜6）
	uint32_t mixers = obs_source_get_audio_mixers(source);
	for (int i = 0; i < 6; ++i) {
		if (mixers & (1 << i)) {
			info.output_buses.push_back(i + 1);
		}
	}
	
	collector->sources->push_back(info);
	return true;
}

struct SceneEnumData {
	std::vector<SceneAudioInfo> *scenes;
	SourceUsageCollector *usage;
	std::set<obs_source_t*> *global_audio_sources;
};

static bool enum_scene_for_map(void *param, obs_source_t *scene_source) {
	SceneEnumData *data = static_cast<SceneEnumData*>(param);
	
	if (obs_source_get_type(scene_source) != OBS_SOURCE_TYPE_SCENE) {
		return true;
	}
	
	const char *scene_name = obs_source_get_name(scene_source);
	obs_scene_t *scene = obs_scene_from_source(scene_source);
	
	if (!scene) return true;
	
	SceneAudioInfo sceneInfo;
	sceneInfo.scene_name = scene_name ? scene_name : "";
	
	MapSourceCollector collector;
	collector.sources = &sceneInfo.sources;
	collector.scene_name = sceneInfo.scene_name;
	collector.usage = data->usage;
	collector.global_audio_sources = data->global_audio_sources;
	
	obs_scene_enum_items(scene, collect_map_scene_item, &collector);
	
	// ソースがあるシーンのみ追加
	if (!sceneInfo.sources.empty()) {
		// OBS UIの表示順に合わせるため逆順にする
		std::reverse(sceneInfo.sources.begin(), sceneInfo.sources.end());
		data->scenes->push_back(sceneInfo);
	}
	
	return true;
}

AudioMap generate_audio_map()
{
	// 全シーンでのソース使用状況を取得
	SourceUsageCollector usage = get_all_source_usage();
	
	AudioMap map;
	
	// グローバル音声ソースを事前にキャッシュ（チャンネル1-6）
	std::set<obs_source_t*> global_audio_sources;
	std::vector<obs_source_t*> global_sources_to_release;
	for (int i = 1; i <= 6; ++i) {
		obs_source_t* global_src = obs_get_output_source(i);
		if (global_src) {
			global_audio_sources.insert(global_src);
			global_sources_to_release.push_back(global_src);
		}
	}
	
	// 全シーンを列挙してマップを作成
	SceneEnumData enumData;
	enumData.scenes = &map.scenes;
	enumData.usage = &usage;
	enumData.global_audio_sources = &global_audio_sources;
	obs_enum_scenes(enum_scene_for_map, &enumData);
	
	// キャッシュしたグローバル音声ソースを解放
	for (obs_source_t* src : global_sources_to_release) {
		obs_source_release(src);
	}
	
	// 有効なグローバル音声デバイスを取得
	auto allDevices = list_global_audio_devices();
	for (const auto &d : allDevices) {
		if (d.enabled && d.status != "Disabled") {
			map.global_devices.push_back(d);
		}
	}
	
	return map;
}

bool switch_global_device(const std::string &device_id)
{
	// First attempt: if libobs is available at runtime, try to update OBS private settings
	void *handle = RTLD_DEFAULT;

	using get_private_data_t = void *(*)(void);
	using data_set_string_t = void (*)(void *, const char *, const char *);
	using apply_private_data_t = void (*)(void *);

	get_private_data_t get_private_data = (get_private_data_t)dlsym(handle, "obs_get_private_data");
	data_set_string_t obs_data_set_string = (data_set_string_t)dlsym(handle, "obs_data_set_string");
	apply_private_data_t obs_apply_private_data = (apply_private_data_t)dlsym(handle, "obs_apply_private_data");

	if (get_private_data && obs_data_set_string && obs_apply_private_data) {
		void *settings = get_private_data();
		if (settings) {
			// Candidate setting keys (best-effort). OBS implementation may use different keys; adjust if needed.
			const std::vector<std::string> keys = {
				"desktop_audio_device_id",
				"desktop_audio_device",
				"desktop-audio-device",
				"desktop_audio",
				"mic_device",
				"mic_audio_device",
				"mic_aux_device",
				"mic_device_id",
				"mic1_device",
			};

			for (const auto &k : keys) {
				obs_data_set_string(settings, k.c_str(), device_id.c_str());
			}

			// Apply the modified private settings; this will persist settings and may cause OBS to react
			obs_apply_private_data(settings);
			return true;
		}
	}

	// Fallback: update the in-process simulated storage
	if (!g_devices_initialized)
		list_global_audio_devices(); // 初期化

	std::string group;
	if (device_id.rfind("desktop", 0) == 0) group = "desktop";
	else if (device_id.rfind("mic", 0) == 0) group = "mic";
	else return false;

	bool found = false;
	for (auto &d : g_devices_storage) {
		if (d.id == device_id) {
			d.enabled = true;
			found = true;
		} else {
			if (group == "desktop" && d.id.rfind("desktop", 0) == 0)
				d.enabled = false;
			if (group == "mic" && d.id.rfind("mic", 0) == 0)
				d.enabled = false;
		}
	}
	return found;
}

std::string generate_audio_map_json()
{
	// 新しいgenerate_audio_map()を使用
	AudioMap map = generate_audio_map();
	AudioDriverInfo audioInfo = get_audio_info();
	
	QJsonObject root;
	root["obs_version"] = QString::fromStdString(get_obs_version());
	root["os"] = QString::fromStdString(get_os_info());
	
	// オーディオドライバ情報
	QJsonObject audioObj;
	audioObj["driver"] = QString::fromStdString(audioInfo.driver_type);
	audioObj["sample_rate"] = audioInfo.sample_rate;
	audioObj["channels"] = audioInfo.channels;
	
	// サンプルレートを読みやすい形式に
	QString sampleRateStr;
	if (audioInfo.sample_rate == 44100) {
		sampleRateStr = "44.1 kHz";
	} else if (audioInfo.sample_rate == 48000) {
		sampleRateStr = "48 kHz";
	} else if (audioInfo.sample_rate == 88200) {
		sampleRateStr = "88.2 kHz";
	} else if (audioInfo.sample_rate == 96000) {
		sampleRateStr = "96 kHz";
	} else if (audioInfo.sample_rate == 176400) {
		sampleRateStr = "176.4 kHz";
	} else if (audioInfo.sample_rate == 192000) {
		sampleRateStr = "192 kHz";
	} else {
		sampleRateStr = QString::number(audioInfo.sample_rate) + " Hz";
	}
	audioObj["sample_rate_display"] = sampleRateStr;
	root["audio_settings"] = audioObj;

	// シーンごとのソース情報
	QJsonArray scenesArray;
	for (const auto &sceneInfo : map.scenes) {
		QJsonObject sceneObj;
		sceneObj["scene_name"] = QString::fromStdString(sceneInfo.scene_name);
		
		QJsonArray sourcesArray;
		for (const auto &s : sceneInfo.sources) {
			QJsonObject so;
			so["id"] = QString::fromStdString(s.id);
			so["name"] = QString::fromStdString(s.name);
			so["type"] = QString::fromStdString(s.type);
			so["is_muted"] = s.is_muted;
			so["is_shared"] = s.is_shared;
			
			// output_busesをコンパクトな文字列として追加 (例: "[1,2,3]")
			QString busStr = "[";
			for (size_t i = 0; i < s.output_buses.size(); ++i) {
				if (i > 0) busStr += ",";
				busStr += QString::number(s.output_buses[i]);
			}
			busStr += "]";
			so["output_buses"] = busStr;
			
			sourcesArray.append(so);
		}
		sceneObj["sources"] = sourcesArray;
		scenesArray.append(sceneObj);
	}
	root["scenes"] = scenesArray;

	// 有効なグローバル音声デバイスのみ
	QJsonArray globals;
	for (const auto &d : map.global_devices) {
		QJsonObject o;
		o["name"] = QString::fromStdString(d.name);
		o["status"] = QString::fromStdString(d.status);
		globals.append(o);
	}
	root["global_audio_devices"] = globals;

	QJsonDocument doc(root);
	return doc.toJson(QJsonDocument::Indented).toStdString();
}

} // namespace audio_inspector_core

