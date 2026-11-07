#pragma once
// QML view model for the ARRANGE tab (docs/daw-plan.md W6/W7): the section
// band, the clip lanes, the SONG-mode transport position, the loop window and
// the clip edit actions.
//
// The arrangement is the session `song` object (mode, lengthBeats, loop window,
// sections and clips). The live editor values are mirrored by SessionSettings
// and saved by project_io into project.mnkybox.
//
// Clips point at take keys, never at audio copies. Applying a lane resolves the
// key through RecorderViewModel and hands absolute paths to the engine, so a
// take can move between tracks without rewriting the arrangement.
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "app_state.hpp"
#include "arrange_logic.hpp"
#include "audio/peaks.hpp"
#include "audio_glue.hpp"
#include "link_client.hpp"
#include "mixer_model.hpp"
#include "recorder_view_model.hpp"
#include "session_settings.hpp"

namespace mnky::app {

class ArrangeViewModel final : public QObject {
  Q_OBJECT
  // SONG mode is the transport's governing switch: on = the clip lanes play,
  // off = the free Active-take path is untouched.
  Q_PROPERTY(bool songMode READ songMode WRITE setSongMode NOTIFY songModeChanged)
  Q_PROPERTY(QString songModeText READ songModeText NOTIFY songModeChanged)
  Q_PROPERTY(QVariantList sections READ sections NOTIFY arrangementChanged)
  Q_PROPERTY(QVariantList lanes READ lanes NOTIFY arrangementChanged)
  Q_PROPERTY(QVariantList clips READ clips NOTIFY arrangementChanged)
  Q_PROPERTY(int clipCount READ clipCount NOTIFY arrangementChanged)
  Q_PROPERTY(int sectionCount READ sectionCount NOTIFY arrangementChanged)
  Q_PROPERTY(int selectedSection READ selectedSection NOTIFY selectionChanged)
  Q_PROPERTY(QString selectedSectionName READ selectedSectionName NOTIFY selectionChanged)
  Q_PROPERTY(int selectedClip READ selectedClip NOTIFY selectionChanged)
  Q_PROPERTY(QString selectedClipName READ selectedClipName NOTIFY selectionChanged)
  Q_PROPERTY(bool hasClipSelection READ hasClipSelection NOTIFY selectionChanged)
  Q_PROPERTY(int grid READ grid WRITE setGrid NOTIFY gridChanged)
  Q_PROPERTY(QString gridText READ gridText NOTIFY gridChanged)
  Q_PROPERTY(double viewStartBeats READ viewStartBeats NOTIFY viewChanged)
  Q_PROPERTY(double viewBeats READ viewBeats NOTIFY viewChanged)
  Q_PROPERTY(double playheadBeats READ playheadBeats NOTIFY playheadChanged)
  Q_PROPERTY(QString playheadText READ playheadText NOTIFY playheadChanged)
  Q_PROPERTY(double lengthBeats READ lengthBeats NOTIFY arrangementChanged)
  Q_PROPERTY(QString lengthText READ lengthText NOTIFY arrangementChanged)
  Q_PROPERTY(bool loopOn READ loopOn WRITE setLoopOn NOTIFY loopChanged)
  Q_PROPERTY(double loopStartBeats READ loopStartBeats NOTIFY loopChanged)
  Q_PROPERTY(double loopEndBeats READ loopEndBeats NOTIFY loopChanged)
  Q_PROPERTY(QString loopText READ loopText NOTIFY loopChanged)
  // Bumped on every playhead wrap; QML flashes the loop band from this.
  Q_PROPERTY(int loopFlash READ loopFlash NOTIFY loopChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
  // The clip "..." sheet (modal): actions on one clip, then the GAIN/FADE pane.
  Q_PROPERTY(bool clipMenuOpen READ clipMenuOpen NOTIFY clipMenuChanged)
  Q_PROPERTY(QString clipMenuTitle READ clipMenuTitle NOTIFY clipMenuChanged)
  Q_PROPERTY(int clipMenuPane READ clipMenuPane NOTIFY clipMenuChanged)
  Q_PROPERTY(QStringList clipActions READ clipActions CONSTANT)
  Q_PROPERTY(bool clipMuted READ clipMuted NOTIFY clipMenuChanged)
  Q_PROPERTY(double clipGainDb READ clipGainDb NOTIFY clipMenuChanged)
  Q_PROPERTY(double clipFadeInBeats READ clipFadeInBeats NOTIFY clipMenuChanged)
  Q_PROPERTY(double clipFadeOutBeats READ clipFadeOutBeats NOTIFY clipMenuChanged)
  Q_PROPERTY(QString clipPositionText READ clipPositionText NOTIFY clipMenuChanged)
  Q_PROPERTY(QString clipLengthText READ clipLengthText NOTIFY clipMenuChanged)
  // LEN chip stepper popup (bars coarse, beats fine, grid-snapped).
  Q_PROPERTY(bool lengthPopupOpen READ lengthPopupOpen NOTIFY lengthPopupChanged)

 public:
  ArrangeViewModel(AppState& state, AudioGlue& audio, MixerModel& mixer,
                   RecorderViewModel& recorder, SessionSettings& settings,
                   QObject* parent = nullptr);

  bool songMode() const { return settings_.songMode; }
  void setSongMode(bool on);
  QString songModeText() const;
  QVariantList sections() const;
  QVariantList lanes() const;
  QVariantList clips() const;
  int clipCount() const { return static_cast<int>(settings_.song.clips.size()); }
  // Clips whose take key resolves to a real file (the render guard).
  int readableClipCount() const;
  int sectionCount() const {
    return static_cast<int>(settings_.song.sections.size());
  }
  int selectedSection() const { return selectedSection_; }
  QString selectedSectionName() const;
  int selectedClip() const { return selectedClip_; }
  QString selectedClipName() const;
  bool hasClipSelection() const;
  int grid() const { return settings_.song.grid; }
  Q_INVOKABLE void setGrid(int grid);
  QString gridText() const;
  double viewStartBeats() const { return viewStartBeats_; }
  double viewBeats() const { return viewBeats_; }
  double playheadBeats() const { return playheadBeats_; }
  QString playheadText() const;
  double lengthBeats() const;
  QString lengthText() const;
  bool loopOn() const { return settings_.loopOn; }
  Q_INVOKABLE void setLoopOn(bool on);
  double loopStartBeats() const;
  double loopEndBeats() const;
  QString loopText() const;
  int loopFlash() const { return loopFlash_; }
  QString statusText() const { return statusText_; }
  bool clipMenuOpen() const { return clipMenuOpen_; }
  QString clipMenuTitle() const;
  int clipMenuPane() const { return clipMenuPane_; }
  QStringList clipActions() const;
  bool clipMuted() const;
  double clipGainDb() const;
  double clipFadeInBeats() const;
  double clipFadeOutBeats() const;
  QString clipPositionText() const;
  QString clipLengthText() const;
  bool lengthPopupOpen() const { return lengthPopupOpen_; }

  // Section verbs.
  Q_INVOKABLE void addSection();
  Q_INVOKABLE void duplicateSection();
  Q_INVOKABLE void selectSection(int index);
  Q_INVOKABLE void loopSection();
  Q_INVOKABLE void renameSection(const QString& name);
  QString defaultSectionName() const;

  // Clip verbs.
  Q_INVOKABLE void selectClip(int index);
  Q_INVOKABLE void openClipMenu(int index);
  Q_INVOKABLE void closeClipMenu();
  Q_INVOKABLE void showClipMenuActions();
  Q_INVOKABLE void clipMenuAction(int action);
  Q_INVOKABLE void setClipGainDb(double gainDb);
  Q_INVOKABLE void setClipFadeIn(double beats);
  Q_INVOKABLE void setClipFadeOut(double beats);
  Q_INVOKABLE void nudgeClipPosition(int steps);
  Q_INVOKABLE void nudgeClipLength(int steps);
  Q_INVOKABLE QString clipNameAt(int index) const;
  Q_INVOKABLE void duplicateClipAt(int index);
  Q_INVOKABLE void deleteClipAt(int index);
  Q_INVOKABLE void setClipMuted(int index, bool muted);
  // Touch drag: no engine apply per move; the lane is re-applied on release.
  Q_INVOKABLE void beginClipDrag(int index, int edge);
  Q_INVOKABLE void updateClipDrag(double beat);
  Q_INVOKABLE void endClipDrag();
  Q_INVOKABLE void commitClipDrag(int index, int edge, double beat);

  // Transport/seek and view.
  Q_INVOKABLE void seekToBeat(double beats);
  Q_INVOKABLE double beatAtRatio(double ratio) const;
  Q_INVOKABLE double ratioOfBeat(double beats) const;
  Q_INVOKABLE QString beatTextAt(double beats) const;
  Q_INVOKABLE void zoomIn();
  Q_INVOKABLE void zoomOut();
  Q_INVOKABLE void zoomBy(double factor);
  Q_INVOKABLE void setViewStartBeats(double beats);
  Q_INVOKABLE void centerOnPlayhead();
  Q_INVOKABLE void openLengthPopup();
  Q_INVOKABLE void closeLengthPopup();
  Q_INVOKABLE void adjustLengthBars(int deltaBars);
  Q_INVOKABLE void adjustLengthBeats(int deltaBeats);
  Q_INVOKABLE int neighborClip(int index, int dx, int dy) const;
  Q_INVOKABLE void noteLoopWrap();
  Q_INVOKABLE void resetView();

  // App/controller seams.
  // Resolves every clip and applies the per-track lanes to the engine. With no
  // clips the engine is left in free-take mode even while the UI says SONG ON,
  // so an empty arrangement can never silence the W1 take path.
  void applySongToEngine();
  void refresh();  // playhead + tempo reapply, called by AppController::timerTick
  void reloadFromSettings();
  // Punch-into-song: place one written take as a clip at the punch in point.
  // Returns the new clip index, or -1 with a reason in `error`.
  int placePunchTake(int track, const std::string& takeKey,
                     double startBeats, std::string* error);
  // The loop window the transport wraps at.
  double effectiveLoopEndBeats() const;
  double effectiveLoopStartBeats() const;
  // Read-out for the transport strip's position text.
  static double beatsOfTick(std::uint32_t tick);

 signals:
  void songModeChanged();
  void arrangementChanged();
  void selectionChanged();
  void gridChanged();
  void viewChanged();
  void playheadChanged();
  void loopChanged();
  void statusChanged();
  void clipMenuChanged();
  void lengthPopupChanged();
  // AppController owns notices, the keypad, confirmations and the take edit
  // popup; the view model only reports what happened.
  void noticeRequested(const QString& kind, const QString& text);
  void editTakeRequested(int track, const QString& takeKey);
  void deleteClipRequested(int index);
  void songModeEditedByUser();

 private:
  struct ClipWaveCache {
    std::string path;
    std::uint64_t size = 0;
    std::uint64_t mtime = 0;
    audio::PeakSummary summary;
    bool valid = false;
  };
  session::SongClip* clipAt(int index);
  const session::SongClip* clipAt(int index) const;
  std::filesystem::path takePath(const session::SongClip& clip) const;
  double takeBeats(const session::SongClip& clip) const;
  arrange::TakeLengthFn lengthFn() const;
  QVariantList clipWave(const session::SongClip& clip) const;
  QString clipName(const session::SongClip& clip) const;
  QString trackName(int track) const;
  QString trackModeWord(int track) const;
  int selectedTrack() const;
  void markChanged();
  void setStatus(const QString& text);
  void notify(const QString& kind, const QString& text);
  void emitSelection();
  void clampSelection();
  void applyClipMenuValues();
  void applySingleClipToEngine(int clipIndex);
  void refreshTempo();
  int newSectionNumber() const;

  AppState& state_;
  AudioGlue& audio_;
  MixerModel& mixer_;
  RecorderViewModel& recorder_;
  SessionSettings& settings_;

  int selectedSection_ = 0;
  int selectedClip_ = -1;
  double viewStartBeats_ = 0.0;
  double viewBeats_ = 64.0;  // 16 bars across
  double playheadBeats_ = 0.0;
  int loopFlash_ = 0;
  int lastTransportTempoX100_ = 0;
  QString statusText_ = QStringLiteral("ARRANGE - tap a clip, tap empty to seek");
  // Clip drag scratch: the original clip is restored on a refused move/trim.
  bool dragging_ = false;
  int dragIndex_ = -1;
  int dragEdge_ = 0;  // 0 = move, 1 = left edge, 2 = right edge
  session::SongClip dragOriginal_;
  bool dragMoved_ = false;
  QString dragError_;
  // Clip "..." sheet.
  bool clipMenuOpen_ = false;
  int clipMenuIndex_ = -1;
  int clipMenuPane_ = 0;
  // LEN stepper popup.
  bool lengthPopupOpen_ = false;
  // Wave cache keyed by take key + length.
  mutable std::map<std::string, ClipWaveCache> waveCache_;
};

}  // namespace mnky::app
