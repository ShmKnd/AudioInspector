#pragma once

#include <QWidget>
#include <QScrollArea>

class QTabWidget;
class QPushButton;
class QTimer;
class QVBoxLayout;
class QLabel;
class QFrame;

namespace audio_inspector_core {
	struct DeviceInfo;
	struct SourceInfo;
}

class AudioInspectorWidget : public QWidget
{
	Q_OBJECT
public:
	explicit AudioInspectorWidget(QWidget *parent = nullptr);
	~AudioInspectorWidget() override;
	
	void setupSignalHandlers();

public slots:
	void refresh();               // 定期更新・再取得
	void onCopyMapToClipboard();  // Audio Map を JSON でクリップボードへコピー
	void populateGlobalTab();     // グローバル音声デバイス一覧を更新

private:
	void setupUi();
	void populateActiveTab();
	void populateMapTab();
	void updateAudioInfoLabel();

	QTabWidget *m_tabs;

	// Active tab
	QScrollArea *m_activeScrollArea;
	QWidget *m_activeContainer;
	QVBoxLayout *m_activeLayout;

	// Global tab
	QScrollArea *m_globalScrollArea;
	QWidget *m_globalContainer;
	QVBoxLayout *m_globalLayout;

	// Map tab
	QScrollArea *m_mapScrollArea;
	QWidget *m_mapContainer;
	QVBoxLayout *m_mapLayout;
	QPushButton *m_copyMapButton;

	// Audio info label
	QLabel *m_audioInfoLabel;

	QTimer *m_refreshTimer;
};

