#include "audio_inspector_widget.h"
#include "audio_inspector_core.h"

#include <QTabWidget>
#include <QFrame>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QClipboard>
#include <QApplication>
#include <QLabel>
#include <QScrollArea>
#include <QToolTip>

// OBS API
#include <obs.h>
#include <obs-frontend-api.h>

// デストラクタ呼び出しフラグ（スレッドセーフのためatomic）
#include <atomic>
static std::atomic<bool> g_widget_destroying{false};

// Static callback for OBS signal handler
static void on_channel_change(void *data, calldata_t *cd)
{
    (void)cd; // unused
    if (g_widget_destroying.load()) {
        return; // ウィジェットが破棄中の場合は無視
    }
    AudioInspectorWidget *widget = static_cast<AudioInspectorWidget*>(data);
    if (widget) {
        // Schedule UI update on the main thread
        QMetaObject::invokeMethod(widget, "populateGlobalTab", Qt::QueuedConnection);
    }
}

AudioInspectorWidget::AudioInspectorWidget(QWidget *parent)
    : QWidget(parent)
    , m_tabs(new QTabWidget(this))
    , m_activeScrollArea(new QScrollArea(this))
    , m_activeContainer(new QWidget(this))
    , m_activeLayout(new QVBoxLayout(m_activeContainer))
    , m_globalScrollArea(new QScrollArea(this))
    , m_globalContainer(new QWidget(this))
    , m_globalLayout(new QVBoxLayout(m_globalContainer))
    , m_mapScrollArea(new QScrollArea(this))
    , m_mapContainer(new QWidget(this))
    , m_mapLayout(new QVBoxLayout(m_mapContainer))
    , m_copyMapButton(new QPushButton(tr("Copy Map to Clipboard"), this))
    , m_audioInfoLabel(nullptr)
    , m_refreshTimer(new QTimer(this))
{
    setupUi();

    connect(m_refreshTimer, &QTimer::timeout, this, &AudioInspectorWidget::refresh);
    m_refreshTimer->start(2000); // 2秒ごとに更新（負荷軽減）
    connect(m_copyMapButton, &QPushButton::clicked, this, &AudioInspectorWidget::onCopyMapToClipboard);
    
    // タブ切り替え時にMapタブを更新
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 2) {
            populateMapTab();
        }
    });
    
    // OBS シグナルハンドラをセットアップ
    setupSignalHandlers();
}

AudioInspectorWidget::~AudioInspectorWidget()
{
    // デストラクタ開始をマーク（コールバックでのアクセス防止）
    g_widget_destroying.store(true);
    
    // タイマーを停止
    m_refreshTimer->stop();
    
    // シグナルハンドラを削除
    signal_handler_t *handler = obs_get_signal_handler();
    if (handler) {
        signal_handler_disconnect(handler, "channel_change", on_channel_change, this);
    }
}

void AudioInspectorWidget::setupSignalHandlers()
{
    // OBS の channel_change シグナルを監視
    signal_handler_t *handler = obs_get_signal_handler();
    if (handler) {
        signal_handler_connect(handler, "channel_change", on_channel_change, this);
        blog(LOG_INFO, "[AudioInspector] Connected to channel_change signal");
    } else {
        blog(LOG_WARNING, "[AudioInspector] Could not get OBS signal handler");
    }
}

void AudioInspectorWidget::setupUi()
{
    // Active tab
    {
        QWidget *w = new QWidget(this);
        QVBoxLayout *l = new QVBoxLayout(w);
        
        m_activeLayout->setContentsMargins(5, 5, 5, 5);
        m_activeLayout->setSpacing(4);
        m_activeLayout->addStretch();
        m_activeContainer->setLayout(m_activeLayout);
        
        m_activeScrollArea->setWidget(m_activeContainer);
        m_activeScrollArea->setWidgetResizable(true);
        m_activeScrollArea->setFrameShape(QFrame::StyledPanel);
        m_activeScrollArea->setFrameShadow(QFrame::Sunken);
        m_activeScrollArea->setStyleSheet("QScrollArea { background-color: #2d2d2d; }");
        m_activeContainer->setStyleSheet("QWidget { background-color: #2d2d2d; }");
        
        l->addWidget(m_activeScrollArea);
        w->setLayout(l);
        m_tabs->addTab(w, tr("Active"));
    }

    // Global tab
    {
        QWidget *w = new QWidget(this);
        QVBoxLayout *l = new QVBoxLayout(w);
        
        // QScrollArea内にQLabelを配置するシンプルな構成
        m_globalLayout->setContentsMargins(5, 5, 5, 5);
        m_globalLayout->setSpacing(4);
        m_globalLayout->addStretch();
        m_globalContainer->setLayout(m_globalLayout);
        
        m_globalScrollArea->setWidget(m_globalContainer);
        m_globalScrollArea->setWidgetResizable(true);
        m_globalScrollArea->setFrameShape(QFrame::StyledPanel);  // 枠を追加
        m_globalScrollArea->setFrameShadow(QFrame::Sunken);      // 枠のスタイル
        
        // 背景色を他のタブと合わせる
        m_globalScrollArea->setStyleSheet("QScrollArea { background-color: #2d2d2d; }");
        m_globalContainer->setStyleSheet("QWidget { background-color: #2d2d2d; }");
        
        l->addWidget(m_globalScrollArea);
        w->setLayout(l);
        m_tabs->addTab(w, tr("Global"));
    }

    // Map tab
    {
        QWidget *w = new QWidget(this);
        QVBoxLayout *l = new QVBoxLayout(w);
        
        m_mapLayout->setContentsMargins(5, 5, 5, 5);
        m_mapLayout->setSpacing(2);
        m_mapLayout->addStretch();
        m_mapContainer->setLayout(m_mapLayout);
        
        m_mapScrollArea->setWidget(m_mapContainer);
        m_mapScrollArea->setWidgetResizable(true);
        m_mapScrollArea->setFrameShape(QFrame::StyledPanel);
        m_mapScrollArea->setFrameShadow(QFrame::Sunken);
        m_mapScrollArea->setStyleSheet("QScrollArea { background-color: #2d2d2d; }");
        m_mapContainer->setStyleSheet("QWidget { background-color: #2d2d2d; }");

        l->addWidget(m_mapScrollArea);

        QHBoxLayout *hl = new QHBoxLayout();
        hl->addStretch();
        hl->addWidget(m_copyMapButton);
        l->addLayout(hl);

        w->setLayout(l);
        m_tabs->addTab(w, tr("Map"));
    }

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(0);  // タブとラベルの間隔を0に
    mainLayout->addWidget(m_tabs);
    
    // オーディオ情報ラベル（タブの下に表示）
    m_audioInfoLabel = new QLabel(this);
    m_audioInfoLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    m_audioInfoLabel->setStyleSheet("QLabel { color: rgb(120, 120, 120); font-size: 11px; padding-left: 10px; }");
    m_audioInfoLabel->setContentsMargins(0, 0, 0, 2);
    mainLayout->addWidget(m_audioInfoLabel);
    
    setLayout(mainLayout);
    
    // オーディオ情報を更新
    updateAudioInfoLabel();

    // 初回更新: Active タブのみ定期更新し、Global は初回に一度だけ取得して選択保持を可能にする
    populateGlobalTab();
    refresh();
}

void AudioInspectorWidget::refresh()
{
    // ウィジェットが表示されていない場合はスキップ（負荷軽減）
    if (!isVisible()) {
        return;
    }
    
    // Activeタブは常に更新
    populateActiveTab();
    
    // Globalタブは常に更新（軽量）
    populateGlobalTab();
    
    // Mapタブは選択されている時のみ更新（重い処理）
    if (m_tabs->currentIndex() == 2) {
        populateMapTab();
    }
}

void AudioInspectorWidget::populateActiveTab()
{
    // 既存のラベルをすべて削除
    QLayoutItem *child;
    while ((child = m_activeLayout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            delete child->widget();
        }
        delete child;
    }

    // シーンヘッダー
    obs_source_t *current_scene = obs_frontend_get_current_scene();
    QString sceneName = "Scene";
    if (current_scene) {
        const char *name = obs_source_get_name(current_scene);
        if (name) sceneName = QString::fromUtf8(name);
        obs_source_release(current_scene);
    }
    
    QLabel *sceneHeader = new QLabel(QString("#%1").arg(sceneName), m_activeContainer);
    sceneHeader->setTextInteractionFlags(Qt::NoTextInteraction);
    sceneHeader->setStyleSheet("QLabel { color: rgb(220, 220, 220); font-weight: bold; }");
    m_activeLayout->addWidget(sceneHeader);

    auto active = audio_inspector_core::list_active_sources();
    for (const auto &s : active) {
        // ソース名 : Active/Muted【Shared】（Sharedでない場合は表示しない）
        QString status = s.is_muted ? "Muted" : "Active";
        QString sourceText;
        QString tooltip;
        
        if (s.is_shared) {
            // 同じソースが複数回使われている → Shared
            sourceText = QString::fromUtf8("・%1 : %2【Shared】")
                             .arg(QString::fromStdString(s.name))
                             .arg(status);
            // 使用されているシーン一覧をツールチップに表示
            QStringList sceneList;
            for (const auto &scene : s.used_in_scenes) {
                sceneList.append(QString::fromStdString(scene));
            }
            tooltip = QString("Used in: %1").arg(sceneList.join(", "));
        } else {
            sourceText = QString("・%1 : %2")
                             .arg(QString::fromStdString(s.name))
                             .arg(status);
        }
        QLabel *sourceLabel = new QLabel(sourceText, m_activeContainer);
        sourceLabel->setTextInteractionFlags(Qt::NoTextInteraction);
        if (!tooltip.isEmpty()) {
            sourceLabel->setToolTip(tooltip);
            sourceLabel->setToolTipDuration(10000); // 10秒間表示
        }
        
        // 色分け: Muted=グレー, Shared=オレンジ, その他=白
        if (s.is_muted) {
            sourceLabel->setStyleSheet("QLabel { color: rgb(120, 120, 120); }"); // グレー
        } else if (s.is_shared) {
            sourceLabel->setStyleSheet("QLabel { color: #e6a854; }"); // オレンジ
        } else {
            sourceLabel->setStyleSheet("QLabel { color: rgb(220, 220, 220); }");
        }
        m_activeLayout->addWidget(sourceLabel);
    }
    
    // グローバル音声セクションを追加
    auto devices = audio_inspector_core::list_global_audio_devices();
    bool hasEnabledGlobal = false;
    for (const auto &d : devices) {
        if (d.enabled && d.status != "Disabled") {
            hasEnabledGlobal = true;
            break;
        }
    }
    
    if (hasEnabledGlobal) {
        // セパレーター
        QFrame *separator = new QFrame(m_activeContainer);
        separator->setFrameShape(QFrame::HLine);
        separator->setFrameShadow(QFrame::Sunken);
        separator->setStyleSheet("QFrame { color: rgb(100, 100, 100); }");
        m_activeLayout->addWidget(separator);
        
        // グローバルヘッダー
        QLabel *globalHeader = new QLabel("#Global", m_activeContainer);
        globalHeader->setTextInteractionFlags(Qt::NoTextInteraction);
        globalHeader->setStyleSheet("QLabel { color: rgb(220, 220, 220); font-weight: bold; }");
        m_activeLayout->addWidget(globalHeader);
        
        // 有効なグローバル音声デバイスを表示
        for (const auto &d : devices) {
            if (d.enabled && d.status != "Disabled") {
                // デバイス名（行頭に・を付ける）
                QString deviceText = QString("・%1 : %2")
                    .arg(QString::fromStdString(d.name))
                    .arg(QString::fromStdString(d.status));
                QLabel *deviceLabel = new QLabel(deviceText, m_activeContainer);
                deviceLabel->setTextInteractionFlags(Qt::NoTextInteraction);
                deviceLabel->setStyleSheet("QLabel { color: rgb(220, 220, 220); }");
                m_activeLayout->addWidget(deviceLabel);
            }
        }
    }
    
    // 下部にストレッチを追加
    m_activeLayout->addStretch();
}

void AudioInspectorWidget::populateGlobalTab()
{
    // 既存のラベルをすべて削除
    QLayoutItem *child;
    while ((child = m_globalLayout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            delete child->widget();
        }
        delete child;
    }

    auto devices = audio_inspector_core::list_global_audio_devices();
    for (const auto &d : devices) {
        // デバイス名（行頭に・を付ける）
        QString text = QString("・%1").arg(QString::fromStdString(d.name));
        if (!d.status.empty()) {
            text += QString(" : %1").arg(QString::fromStdString(d.status));
        }
        
        QLabel *label = new QLabel(text, m_globalContainer);
        label->setTextInteractionFlags(Qt::NoTextInteraction);
        
        // Show disabled devices in gray
        if (!d.enabled || d.status == "Disabled") {
            label->setStyleSheet("QLabel { color: rgb(100, 100, 100); }");
        } else {
            label->setStyleSheet("QLabel { color: rgb(220, 220, 220); }");
        }
        
        m_globalLayout->addWidget(label);
    }
    
    // 下部にストレッチを追加
    m_globalLayout->addStretch();
}

void AudioInspectorWidget::populateMapTab()
{
    // 既存のウィジェットをすべて削除
    QLayoutItem *child;
    while ((child = m_mapLayout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            delete child->widget();
        }
        delete child;
    }
    
    auto audioMap = audio_inspector_core::generate_audio_map();
    
    // Globalセクションを先に表示（有効なもののみ）
    if (!audioMap.global_devices.empty()) {
        // Globalヘッダー
        QLabel *globalHeader = new QLabel("Global", m_mapContainer);
        globalHeader->setTextInteractionFlags(Qt::NoTextInteraction);
        globalHeader->setStyleSheet("QLabel { color: rgb(220, 220, 220); font-weight: bold; }");
        m_mapLayout->addWidget(globalHeader);
        
        // グローバル音声デバイス
        for (const auto &device : audioMap.global_devices) {
            QString text = QString(" └ %1").arg(QString::fromStdString(device.name));
            if (!device.status.empty()) {
                text += QString(": %1").arg(QString::fromStdString(device.status));
            }
            
            QLabel *deviceLabel = new QLabel(text, m_mapContainer);
            deviceLabel->setTextInteractionFlags(Qt::NoTextInteraction);
            deviceLabel->setStyleSheet("QLabel { color: rgb(180, 180, 180); }");
            m_mapLayout->addWidget(deviceLabel);
        }
        
        // セパレーター
        QFrame *separator = new QFrame(m_mapContainer);
        separator->setFrameShape(QFrame::HLine);
        separator->setFrameShadow(QFrame::Sunken);
        separator->setStyleSheet("QFrame { color: rgb(100, 100, 100); }");
        m_mapLayout->addWidget(separator);
        
        // スペース
        QLabel *spacer = new QLabel("", m_mapContainer);
        spacer->setFixedHeight(4);
        m_mapLayout->addWidget(spacer);
    }
    
    // 各シーンを表示
    for (const auto &sceneInfo : audioMap.scenes) {
        // シーン名ラベル
        QLabel *sceneLabel = new QLabel(QString::fromStdString(sceneInfo.scene_name), m_mapContainer);
        sceneLabel->setTextInteractionFlags(Qt::NoTextInteraction);
        sceneLabel->setStyleSheet("QLabel { color: rgb(220, 220, 220); font-weight: bold; }");
        m_mapLayout->addWidget(sceneLabel);
        
        // シーン内のソース
        for (const auto &source : sceneInfo.sources) {
            QString statusText = source.is_muted ? "Muted" : "Active";
            QString sharedText;
            QString tooltip;
            
            if (source.is_shared) {
                // 同じソースが複数回使われている → Shared
                sharedText = "Shared";
                // 使用されているシーン一覧をツールチップに表示
                QStringList sceneList;
                for (const auto &scene : source.used_in_scenes) {
                    sceneList.append(QString::fromStdString(scene));
                }
                tooltip = QString("Used in: %1").arg(sceneList.join(", "));
            }
            
            // Out:を構築
            QString outText;
            if (source.output_buses.empty()) {
                outText = "none";
            } else {
                for (size_t i = 0; i < source.output_buses.size(); ++i) {
                    if (i > 0) outText += ",";
                    outText += QString::number(source.output_buses[i]);
                }
            }
            
            QString text;
            if (source.is_shared) {
                text = QString::fromUtf8(" └ %1: %2 [%3/Shared/Out:%4]")
                           .arg(QString::fromStdString(source.type))
                           .arg(QString::fromStdString(source.name))
                           .arg(statusText)
                           .arg(outText);
            } else {
                text = QString::fromUtf8(" └ %1: %2 [%3/Out:%4]")
                           .arg(QString::fromStdString(source.type))
                           .arg(QString::fromStdString(source.name))
                           .arg(statusText)
                           .arg(outText);
            }
            
            QLabel *sourceLabel = new QLabel(text, m_mapContainer);
            sourceLabel->setTextInteractionFlags(Qt::NoTextInteraction);
            if (!tooltip.isEmpty()) {
                sourceLabel->setToolTip(tooltip);
                sourceLabel->setToolTipDuration(10000); // 10秒間表示
            }
            
            // 色分け: Muted=グレー, Shared=オレンジ, その他=白
            if (source.is_muted) {
                sourceLabel->setStyleSheet("QLabel { color: rgb(120, 120, 120); }"); // グレー
            } else if (source.is_shared) {
                sourceLabel->setStyleSheet("QLabel { color: #e6a854; }"); // オレンジ
            } else {
                sourceLabel->setStyleSheet("QLabel { color: rgb(180, 180, 180); }");
            }
            
            m_mapLayout->addWidget(sourceLabel);
        }
        
        // シーン間にスペースを追加
        QLabel *spacer = new QLabel("", m_mapContainer);
        spacer->setFixedHeight(8);
        m_mapLayout->addWidget(spacer);
    }
    
    // 下部にストレッチを追加
    m_mapLayout->addStretch();
}

void AudioInspectorWidget::onCopyMapToClipboard()
{
    // JSONを生成してクリップボードにコピー
    QString json = QString::fromStdString(audio_inspector_core::generate_audio_map_json());
    QClipboard *cb = QApplication::clipboard();
    cb->setText(json);
}

void AudioInspectorWidget::updateAudioInfoLabel()
{
    auto audioInfo = audio_inspector_core::get_audio_info();
    
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
    } else if (audioInfo.sample_rate > 0) {
        sampleRateStr = QString::number(audioInfo.sample_rate) + " Hz";
    } else {
        sampleRateStr = "Unknown";
    }
    
    QString text = QString("%1 / %2")
                       .arg(sampleRateStr)
                       .arg(QString::fromStdString(audioInfo.driver_type));
    
    m_audioInfoLabel->setText(text);
}
